-- SPDX-License-Identifier: GPL-3.0-or-later
-- Behavioral checks for the visual pressure adapter; these are not an aircraft
-- certification model or a replacement for viewing the installed aircraft.
local core_path = assert(arg[1], "pass a380x_hydraulic_surfaces.lua")
local M = assert(loadfile(core_path))()
local function near(a, b, tolerance, label)
    assert(math.abs(a - b) <= (tolerance or 1e-9),
        (label or "values differ") .. ": " .. tostring(a) .. " vs " .. tostring(b))
end
local function input()
    return {grounded = true, paused = false, pressures = {0, 0, 0},
        ailerons = {0, 0, 0, 0, 0, 0}, spoilers = {0, 0, 0, 0, 0, 0, 0, 0},
        pitch = 0, rudder = 0, heading = 0, wind_direction = 0,
        wind_speed = 0, airspeed = 0}
end
local function advance(state, current, seconds, fps)
    fps = fps or 60
    for _ = 1, math.floor(seconds * fps + 0.5) do M.update(state, current, 1 / fps) end
    return state.outputs
end
local function snapshot(out)
    local copy = {}; for _, key in ipairs(M.KEYS) do copy[key] = out[key] end; return copy
end
local function same(out, before, label)
    for _, key in ipairs(M.KEYS) do near(out[key], before[key], 1e-12, (label or "hold") .. " " .. key) end
end
local function neutral(state)
    for _, key in ipairs(M.KEYS) do near(state.outputs[key], 0) end
end
assert(#M.KEYS == 16)

-- A cold aircraft visibly settles instead of snapping to its final pose or
-- following a joystick/speedbrake that happens to be displaced at load.
local current = input()
current.pitch, current.rudder = 1, 1
current.ailerons, current.spoilers = {1, 1, 1, 1, 1, 1}, {60, 60, 60, 60, 60, 60, 60, 60}
local state = M.new(current)
neutral(state)
local out = advance(state, current, 1)
assert(out.aileron_l_inboard_ratio > 0 and out.aileron_l_inboard_ratio < 0.1, "aileron does not settle slowly")
assert(out.elevator_ratio < 0 and out.elevator_ratio > -0.1, "elevator does not settle slowly")
out = advance(state, current, 100)
for i = 1, 6 do near(out[M.KEYS[i]], i <= 3 and 2 / 3 or -2 / 3, 0.0001, "parked aileron sign") end
near(out.elevator_ratio, -2 / 3, 0.0001, "parked elevator sign")
near(out.rudder_ratio, 0, 0, "calm must not follow pedals")
for i = 9, 16 do near(out[M.KEYS[i]], 0, 0, "cold spoilers moved") end

-- Pressure alone permits movement. There is deliberately no engine-running
-- prerequisite: electric pumps and residual supported pressure also work.
for circuit = 1, 3 do
    current = input(); current.pressures[circuit] = 3000
    current.ailerons = {-0.7, 0.2, 0.5, 0.3, -0.4, 0.6}
    current.pitch, current.rudder = 0.6, -0.4
    current.spoilers = {10, 20, 30, 40, 50, 60, 15, 25}
    state = M.new(current)
    assert(state.powered, "live pressure source ignored")
    out = advance(state, current, 20)
    for i = 1, 6 do near(out[M.KEYS[i]], current.ailerons[i]) end
    near(out.elevator_ratio, 0.6); near(out.rudder_ratio, -0.4)
    for i = 1, 8 do near(out[M.KEYS[i + 8]], current.spoilers[i]) end
end

-- Losing pressure freezes every spoiler, including already-raised panels.
local supported = snapshot(state.outputs)
current.pressures = {0, 0, 0}; current.spoilers = {0, 65, 0, 65, 0, 65, 0, 65}
out = advance(state, current, 20)
for i = 9, 16 do near(out[M.KEYS[i]], supported[M.KEYS[i]], 0, "unsupported spoiler changed") end
current.pressures[1] = 3000
local before_recovery = snapshot(out)
M.update(state, current, 1 / 60)
assert(math.abs(out.elevator_ratio - before_recovery.elevator_ratio) <= 1 / 60 + 1e-9,
    "restoration snapped elevator")
assert(math.abs(out.spoiler_l_inboard_deg - before_recovery.spoiler_l_inboard_deg) <= 0.75 + 1e-9,
    "restoration snapped spoiler")
out = advance(state, current, 5)
near(out.elevator_ratio, 0.6)
for i = 1, 8 do near(out[M.KEYS[i + 8]], current.spoilers[i]) end

-- The deadband preserves state in either direction and eliminates chatter.
current = input(); current.pressures[1] = 3000; state = M.new(current)
for _, pressure in ipairs({700, 500, M.PRESSURE_RELEASE_PSI + 1}) do
    current.pressures[1] = pressure; M.update(state, current, 0.1)
    assert(state.powered, "pressure released inside deadband")
end
current.pressures[1] = M.PRESSURE_RELEASE_PSI; M.update(state, current, 0.1)
assert(not state.powered)
for _, pressure in ipairs({500, 700, M.PRESSURE_RESTORE_PSI - 1}) do
    current.pressures[1] = pressure; M.update(state, current, 0.1)
    assert(not state.powered, "pressure restored inside deadband")
end
current.pressures[1] = M.PRESSURE_RESTORE_PSI; M.update(state, current, 0.1)
assert(state.powered)

-- Wind is FROM true north, and the rudder trailing edge moves downstream.
current = input(); current.wind_direction, current.wind_speed = 90, 10
state = M.new(current); out = advance(state, current, 1)
assert(out.rudder_ratio < 0 and out.rudder_ratio >= -0.051, "east wind direction/rate incorrect")
out = advance(state, current, 100); assert(out.rudder_ratio < -0.7)
current.wind_speed = 0
local calm = out.rudder_ratio; advance(state, current, 30); near(out.rudder_ratio, calm, 0, "calm rudder pose")
current.wind_speed, current.wind_direction = 10, 270
local prior_rudder = out.rudder_ratio
M.update(state, current, 1 / 60)
assert(out.rudder_ratio > prior_rudder and out.rudder_ratio - prior_rudder < 0.001, "wind reversal snapped")
out = advance(state, current, 100); assert(out.rudder_ratio > 0.7)
current.heading, current.wind_direction = 90, 180
out = advance(state, current, 100); assert(out.rudder_ratio < -0.7, "wind did not rotate with true heading")
current.heading, current.wind_direction = 360, 450
out = advance(state, current, 100); assert(out.rudder_ratio < -0.7, "angle wrap changed wind side")

-- All engines out / zero hydraulic pressure in flight must not invoke the
-- parked gravity mode. Flight-control authority still belongs to X-Plane.
current = input(); current.grounded = false; current.airspeed = 85
current.ailerons = {0.2, 0.3, 0.4, -0.2, -0.3, -0.4}
current.pitch, current.rudder = 0.35, -0.25
current.spoilers = {1, 2, 3, 4, 5, 6, 7, 8}
state = M.new(current); out = advance(state, current, 120)
for i = 1, 6 do near(out[M.KEYS[i]], current.ailerons[i]) end
near(out.elevator_ratio, 0.35); near(out.rudder_ratio, -0.25)
for i = 9, 16 do near(out[M.KEYS[i]], 0, 0, "airborne cold spoilers moved") end
current.ailerons, current.pitch = {-0.3, -0.2, -0.1, 0.3, 0.2, 0.1}, -0.4
out = advance(state, current, 2); near(out.elevator_ratio, -0.4)
for i = 1, 6 do near(out[M.KEYS[i]], current.ailerons[i]) end
-- Losing pressure in flight must freeze raised panels as well as stowed ones,
-- while the commanded pitch/roll surfaces continue to follow native controls.
current.pressures[1] = 3000
advance(state, current, 2)
local flying_supported = snapshot(out)
current.pressures[1] = 0
current.spoilers = {65, 0, 65, 0, 65, 0, 65, 0}
current.pitch, current.rudder = 0.5, 0.6
advance(state, current, 120)
near(out.elevator_ratio, 0.5); near(out.rudder_ratio, 0.6)
for i = 9, 16 do near(out[M.KEYS[i]], flying_supported[M.KEYS[i]], 0, "airborne unsupported spoiler changed") end
current = input(); state = M.new(current); advance(state, current, 80)
current.grounded, current.pitch = false, 0.3
local parked = snapshot(state.outputs); M.update(state, current, 1 / 60)
assert(math.abs(state.outputs.elevator_ratio - parked.elevator_ratio) <= 1 / 60 + 1e-9,
    "ground-air transition snapped")
advance(state, current, 2); near(state.outputs.elevator_ratio, 0.3)

-- A moving aircraft on its wheels should not settle to the parked pose.
current = input(); current.airspeed = 40; state = M.new(current)
out = advance(state, current, 60)
for i = 1, 7 do near(out[M.KEYS[i]], 0, 1e-9, "high ground airflow drooped") end
current.airspeed = 22.5; out = advance(state, current, 100)
near(out.aileron_l_inboard_ratio, 1 / 3, 0.0001)
near(out.elevator_ratio, -1 / 3, 0.0001)

-- Bad source values/time discontinuities and pause hold the last valid pose;
-- they must not reset state to joystick position or consume a pressure change.
local saved = snapshot(out)
current.paused = true; current.pressures[1] = 3000
advance(state, current, 3); same(out, saved, "pause"); assert(not state.powered)
current.paused = false
for _, dt in ipairs({0, -1, 2, math.huge, -math.huge, 0 / 0, "bad"}) do
    M.update(state, current, dt); same(out, saved, "invalid dt"); assert(not state.powered)
end
M.update(state, current, nil); same(out, saved, "nil dt")
for _, bad in ipairs({false, "bad", math.huge, -math.huge, 0 / 0}) do
    local good = current.pitch; current.pitch = bad
    M.update(state, current, 1 / 60); same(out, saved, "invalid pitch"); current.pitch = good
end
for _, name in ipairs({"ailerons", "spoilers", "pressures"}) do
    local good = current[name][1]; current[name][1] = nil
    M.update(state, current, 1 / 60); same(out, saved, "incomplete input"); current[name][1] = good
end
M.update(state, nil, 1 / 60); same(out, saved, "nil input")

-- Bounded outputs even when native commands exceed their usual limits.
current = input(); current.pressures[1] = 3000
current.ailerons, current.spoilers = {50, -50, 50, -50, 50, -50}, {-10, 100, -10, 100, -10, 100, -10, 100}
current.pitch, current.rudder = 40, -40
state = M.new(current); out = advance(state, current, 2)
for i, key in ipairs(M.KEYS) do
    assert(out[key] >= (i <= 8 and -1 or 0) and out[key] <= (i <= 8 and 1 or 65), "unbounded " .. key)
end

-- Compare equal physical time at low and high rendering rates through a
-- full loss/recovery/wind reversal scenario, including rate-limit transitions.
local function scenario(fps)
    local command = input(); local model = M.new(command)
    command.wind_direction, command.wind_speed = 90, 12
    advance(model, command, 13, fps)
    local samples = {snapshot(model.outputs)}
    command.wind_direction = 270; advance(model, command, 11, fps)
    samples[2] = snapshot(model.outputs)
    command.pressures[2], command.pitch = 3000, 0.7
    command.ailerons = {-0.5, -0.3, -0.1, 0.5, 0.3, 0.1}
    advance(model, command, 4, fps); samples[3] = snapshot(model.outputs)
    command.pressures[2] = 0; advance(model, command, 19, fps)
    samples[4] = snapshot(model.outputs); return samples
end
local low, high = scenario(20), scenario(144)
for i = 1, #low do
    for _, key in ipairs(M.KEYS) do near(low[i][key], high[i][key], 0.0015, "frame rate " .. key) end
end
print("Hydraulic visual core checks passed (" .. _VERSION .. "): settling, pressure, wind, spoilers, flight, continuity, frame rates")
