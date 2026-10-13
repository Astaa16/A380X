-- SPDX-License-Identifier: GPL-3.0-or-later
-- Derived from FlyByWire Simulations' GPL-3.0 aircraft source, revision
-- 2baa2b35eadaf4c78e172ce41bbe6b40b4aeafb2:
-- fbw-a380x/src/wasm/fbw_a380/src/model/A380PrimComputerFctl.cpp:2435-2473
-- and :2495-2513, :2625-2713; constants in A380PrimComputerFctl_data.cpp.
--
-- VISUAL SIMULATION ONLY. This module neither reads nor writes simulator state
-- and does not implement the complete normal law, VDA, or hydraulic system.
-- Input roll is X-Plane's effective roll ratio, positive = right roll. The
-- adapter uses c = -30 * roll, matching the source direct-law input sign/gain;
-- it then applies the source airborne three-surface distribution, NOT the
-- source direct law. This choice is a visual adaptation, not a full FBW port.
--
-- Source command degrees are positive trailing-edge DOWN. The hydraulic
-- controller negates both command sides (hydraulic/mod.rs:5514-5599), then the
-- MSFS aspect negates only LEFT feedback and divides by 30 (ailerons.rs:16-87,
-- :151-161). Therefore source animation ratios are LEFT command/30 and RIGHT
-- -command/30. Anims_Exterior.xml:8 maps ratio to percent as ratio*50+50.
-- Do not divide by the authored clip's geometric angle or invert a side again.
--
-- Deliberate adaptations: the source's sampled 35-slot delay is replaced by
-- timestamp interpolation at a true 0.350 s delay; a <=10 ms integration grid
-- limits frame-rate sensitivity. Explicit reset seeds a steady pose (aircraft
-- load/time discontinuity), rather than simulating computer engagement. This
-- minimal module omits droop, antidroop, IAS scheduling and hydraulic failures.
-- Paused/zero dt holds; negative/nonfinite dt or >1 s resets at current input.

local M = {}
M.SOURCE_REVISION = "2baa2b35eadaf4c78e172ce41bbe6b40b4aeafb2"
M.DELAY_SECONDS = 0.350
M.MAX_STEP_SECONDS = 0.010
M.MAX_DT_SECONDS = 1.0

local DELAY, STEP, EPSILON = M.DELAY_SECONDS, M.MAX_STEP_SECONDS, 1e-10
local KEYS = {
    "l_inboard_ratio", "l_midboard_ratio", "l_outboard_ratio",
    "r_inboard_ratio", "r_midboard_ratio", "r_outboard_ratio"
}

local function finite(value)
    return type(value) == "number" and value == value
        and value > -math.huge and value < math.huge
end

local function clamp(value, low, high)
    return math.max(low, math.min(high, value))
end

local function command(input)
    local roll = input and input.roll or 0
    if not finite(roll) then roll = 0 end
    return -30 * clamp(roll, -1, 1)
end

local function move_toward(previous, target, rate, dt)
    return previous + clamp(target - previous, -rate * dt, rate * dt)
end

local function distributed(q, u, delayed)
    local inboard = clamp(u, -30, 30)
    local midboard = clamp(delayed, -30, 30)
    local outboard = clamp((u - inboard) + (delayed - midboard) + q, -30, 30)
    return inboard, midboard, outboard
end

local function targets(inboard, midboard, outboard)
    return {
        clamp(-inboard, -30, 20), clamp(-midboard, -30, 20), clamp(-outboard, -30, 20),
        clamp(inboard, -30, 20), clamp(midboard, -30, 20), clamp(outboard, -30, 20)
    }
end

local function publish(state)
    for index, key in ipairs(KEYS) do
        local side = index <= 3 and 1 or -1
        state.outputs[key] = side * state.degrees[index] / 30
    end
    return state.outputs
end

local function reset_delay(state, value)
    state.history = {
        { time = state.time - DELAY, value = value },
        { time = state.time, value = value }
    }
    state.next_sample = state.time + STEP
end

local function delayed_value(state)
    local wanted = state.time - DELAY
    local history = state.history
    -- Keep the lower interpolation endpoint, dropping expired history. At
    -- most about 38 entries survive, even with arbitrarily tiny positive dt.
    while #history > 2 and history[2].time <= wanted do
        table.remove(history, 1)
    end
    local low, high = history[1], history[2]
    if wanted <= low.time then return low.value end
    if wanted >= high.time then return high.value end
    local fraction = (wanted - low.time) / (high.time - low.time)
    return low.value + (high.value - low.value) * fraction
end

-- All public functions return the same six-key state.outputs table; callers
-- should treat it as read-only. new() itself returns the state, not outputs.
function M.reset(state, input)
    input = input or {}
    local c = command(input)
    state.time, state.q = 0, 0.5 * c
    local grounded = input.grounded == true
    local q = grounded and c or state.q
    local u = grounded and c or 2.5 * q
    local inboard, midboard, outboard = distributed(q, u, u)
    state.degrees = targets(inboard, midboard, outboard)
    state.outputs = state.outputs or {}
    reset_delay(state, u)
    state.initialized = true
    return publish(state)
end

function M.new()
    local state = {}
    M.reset(state, {})
    return state
end

function M.update(state, input, dt)
    input = input or {}
    if not state.initialized then M.reset(state, input) end
    if input.paused == true then return state.outputs end
    if not finite(dt) or dt < 0 or dt > M.MAX_DT_SECONDS then
        return M.reset(state, input)
    end
    if dt == 0 then return state.outputs end

    local c, grounded = command(input), input.grounded == true
    local remaining = dt
    while remaining > EPSILON do
        local to_sample = state.next_sample - state.time
        local step = math.min(remaining, STEP, to_sample)
        -- Keep the hidden splitter state active while the ground bypass is
        -- selected, as in the source: only its visible output is overridden.
        state.q = move_toward(state.q, 0.5 * c, 12, step)
        state.time = state.time + step
        local q = grounded and c or state.q
        local u = grounded and c or 2.5 * q
        local delayed
        if grounded then
            reset_delay(state, u)
            delayed = u
        else
            if state.time >= state.next_sample - EPSILON then
                state.history[#state.history + 1] = { time = state.time, value = u }
                state.next_sample = state.next_sample + STEP
            end
            delayed = delayed_value(state)
        end

        local inboard, midboard, outboard = distributed(q, u, delayed)
        local target = targets(inboard, midboard, outboard)
        for index = 1, 6 do
            state.degrees[index] = move_toward(state.degrees[index], target[index], 30, step)
        end
        remaining = remaining - step
    end
    return publish(state)
end

return M
