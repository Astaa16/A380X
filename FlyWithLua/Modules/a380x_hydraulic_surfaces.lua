-- SPDX-License-Identifier: GPL-3.0-or-later
-- Pressure-dependent exterior animation, not a flight-model/control-law override.
-- Native pressure is in this ACF's PSI units (nominal engine pumps: 3000 PSI),
-- NOT 0..1 despite the old simulator hydraulic_pressure_ratio naming.
-- FBW hydraulic/mod.rs supplies the -20-degree relaxed elevator/aileron pose;
-- its MSFS aspect supplies these clip signs. Settling rates below are a bounded
-- visual approximation, not the upstream force/actuator solver.
local M = {}
M.KEYS = {
    "aileron_l_inboard_ratio", "aileron_l_midboard_ratio", "aileron_l_outboard_ratio",
    "aileron_r_inboard_ratio", "aileron_r_midboard_ratio", "aileron_r_outboard_ratio",
    "elevator_ratio", "rudder_ratio",
    "spoiler_l_inboard_deg", "spoiler_r_inboard_deg",
    "spoiler_l_inner_deg", "spoiler_r_inner_deg",
    "spoiler_l_middle_deg", "spoiler_r_middle_deg",
    "spoiler_l_outer_deg", "spoiler_r_outer_deg"
}
M.PRESSURE_RELEASE_PSI = 450
M.PRESSURE_RESTORE_PSI = 900
M.DROOP_RATIO = 20 / 30

local function clamp(x, lo, hi) return math.max(lo, math.min(hi, x)) end
local function finite(x)
    return type(x) == "number" and x == x and math.abs(x) < math.huge
end
local function valid_vector(x, n)
    if type(x) ~= "table" then return false end
    for i = 1, n do if not finite(x[i]) then return false end end
    return true
end
function M.valid(input)
    return type(input) == "table" and type(input.grounded) == "boolean"
        and type(input.paused) == "boolean"
        and valid_vector(input.ailerons, 6) and valid_vector(input.spoilers, 8)
        and valid_vector(input.pressures, 3)
        and finite(input.pitch) and finite(input.rudder) and finite(input.heading)
        and finite(input.wind_direction) and finite(input.wind_speed)
        and finite(input.airspeed)
end
local function pressure(input)
    return math.max(0, input.pressures[1], input.pressures[2], input.pressures[3])
end
local function slew(previous, target, rate, dt)
    return previous + clamp(target - previous, -rate * dt, rate * dt)
end
local function damp(previous, target, tau, rate, dt)
    return slew(previous, previous + (target - previous) * (1 - math.exp(-dt / tau)), rate, dt)
end

function M.new(input)
    assert(M.valid(input), "invalid hydraulic surface input")
    local state = {outputs = {}, powered = pressure(input) >= M.PRESSURE_RESTORE_PSI}
    local follows_controls = state.powered or not input.grounded
    for i, key in ipairs(M.KEYS) do
        local value = 0
        if follows_controls then
            if i <= 6 then value = clamp(input.ailerons[i], -1, 1)
            elseif i == 7 then value = clamp(input.pitch, -1, 1)
            elseif i == 8 then value = clamp(input.rudder, -1, 1)
            elseif state.powered then value = clamp(input.spoilers[i - 8], 0, 65) end
        end
        state.outputs[key] = value
    end
    -- A cold load begins at neutral and visibly settles; an unpowered joystick
    -- or speedbrake handle cannot seed a raised spoiler at initialization.
    return state
end

function M.update(state, input, dt)
    -- Invalid input, pauses and reload-sized time gaps hold the previous pose.
    -- Never fast-forward passive motion across a pause or reseed from the stick.
    if not M.valid(input) or input.paused or not finite(dt) or dt <= 0 or dt > 1 then
        return state.outputs
    end
    local p = pressure(input)
    if p >= M.PRESSURE_RESTORE_PSI then state.powered = true
    elseif p <= M.PRESSURE_RELEASE_PSI then state.powered = false end

    local out = state.outputs
    -- Total flameout is not total control-power loss on an A380: RAT-powered
    -- EHA/EBHA and aerodynamic loads prevent a parked gravity pose in flight.
    -- This visual adapter retains native commands airborne. It does not model
    -- individual EHA/EBHA failures or grant simulator flight-control authority.
    if state.powered or not input.grounded then
        for i = 1, 6 do
            local key = M.KEYS[i]
            out[key] = slew(out[key], clamp(input.ailerons[i], -1, 1), 1, dt)
        end
        out.elevator_ratio = slew(out.elevator_ratio, clamp(input.pitch, -1, 1), 1, dt)
        out.rudder_ratio = slew(out.rudder_ratio, clamp(input.rudder, -1, 1), 1, dt)
        if state.powered then
            for i = 1, 8 do
                local key = M.KEYS[i + 8]
                out[key] = slew(out[key], clamp(input.spoilers[i], 0, 65), 45, dt)
            end
        end
        return out
    end

    -- Airflow progressively unloads parked droop during a fast ground roll.
    -- Weight-on-wheels alone must not slam a moving aircraft into the rest pose.
    local airflow = clamp((math.max(0, input.airspeed) - 10) / 25, 0, 1)
    local relaxed = M.DROOP_RATIO * (1 - airflow)
    for i = 1, 6 do
        local key = M.KEYS[i]
        local sign = i <= 3 and 1 or -1
        out[key] = damp(out[key], sign * relaxed, 8, 0.045, dt)
    end
    out.elevator_ratio = damp(out.elevator_ratio, -relaxed, 10, 0.04, dt)

    -- Wind is FROM true north. East wind on a north-facing aircraft pushes the
    -- rudder trailing edge left (negative). Calm air holds the last free pose.
    local wind = math.max(0, input.wind_speed)
    if wind >= 0.25 then
        local relative = math.rad((input.wind_direction - input.heading) % 360)
        local target = -math.sin(relative) * wind * wind / (wind * wind + 25)
        out.rudder_ratio = damp(out.rudder_ratio, target, 8, 0.05, dt)
    end
    -- Spoilers stay at the last supported position, ignoring stick/lever/wind.
    return out
end
return M
