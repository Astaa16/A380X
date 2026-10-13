-- SPDX-License-Identifier: GPL-3.0-or-later
-- Run with Lua 5.1+; no FlyWithLua, simulator, ffi or native libraries needed.
-- lua tests/aileron_dance_core_test.lua FlyWithLua/Modules/a380x_aileron_dance.lua
local path = arg[1] or "FlyWithLua/Modules/a380x_aileron_dance.lua"
local M = assert(loadfile(path))()
local tests = 0

local function near(actual, expected, tolerance, message)
    assert(math.abs(actual - expected) <= (tolerance or 1e-9),
        (message or "values differ") .. ": " .. tostring(actual) .. " vs " .. tostring(expected))
end

local function check(name, fn)
    fn()
    tests = tests + 1
    print("PASS " .. name)
end

local function advance(state, input, seconds, fps)
    local remaining = seconds
    while remaining > 1e-10 do
        local dt = math.min(remaining, 1 / (fps or 100))
        M.update(state, input, dt)
        remaining = remaining - dt
    end
    return state.outputs
end

local function zero(outputs)
    for _, value in pairs(outputs) do near(value, 0) end
end

check("neutral input never invents oscillation", function()
    local state = M.new()
    zero(advance(state, { roll = 0 }, 12, 60))
    assert(#state.history < 40, "delay history was not bounded")
end)

check("midboard begins after true 350 ms delay", function()
    local state = M.new()
    local input = { roll = 0.2 }
    local output = advance(state, input, 0.35, 60)
    near(output.l_midboard_ratio, 0)
    near(output.l_inboard_ratio, 0.25)
    near(output.l_outboard_ratio, 0.10)
    output = advance(state, input, 0.01, 100)
    near(output.l_midboard_ratio, 0.01, 1e-8)
end)

check("settled inboard and midboard are 2.5 times outboard", function()
    local output = advance(M.new(), { roll = 0.4 }, 3, 60)
    near(output.l_inboard_ratio, 0.5)
    near(output.l_midboard_ratio, 0.5)
    near(output.l_outboard_ratio, 0.2)
    for _, part in ipairs({ "inboard", "midboard", "outboard" }) do
        near(output["l_" .. part .. "_ratio"], output["r_" .. part .. "_ratio"])
        assert(output["l_" .. part .. "_ratio"] > 0, "positive native roll inverted")
    end
end)

check("reversal preserves old midboard direction while inboard reverses", function()
    local state = M.new()
    M.reset(state, { roll = 0.2 })
    local output = advance(state, { roll = -0.2 }, 0.3, 60)
    assert(output.l_inboard_ratio < 0 and output.l_midboard_ratio > 0)
    output = advance(state, { roll = -0.2 }, 2, 60)
    near(output.l_inboard_ratio, -0.25)
    near(output.l_midboard_ratio, -0.25)
    near(output.l_outboard_ratio, -0.10)
end)

check("ground bypass keeps all three surfaces together", function()
    local state = M.new()
    local output = advance(state, { roll = 0.2, grounded = true }, 0.1, 60)
    near(output.l_inboard_ratio, 0.1)
    near(output.l_midboard_ratio, 0.1)
    near(output.l_outboard_ratio, 0.1)
    output = advance(state, { roll = 0.2, grounded = true }, 0.3, 60)
    near(output.l_inboard_ratio, 0.2)
    near(output.l_midboard_ratio, 0.2)
    near(output.l_outboard_ratio, 0.2)
end)

check("ground to air retains ground delay history and has bounded movement", function()
    local state = M.new()
    M.reset(state, { roll = 0.2, grounded = true })
    local output = M.update(state, { roll = 0.2, grounded = false }, 0.01)
    near(output.l_midboard_ratio, 0.2)
    assert(math.abs(output.l_inboard_ratio - 0.2) <= 0.010000001)
    assert(math.abs(output.l_outboard_ratio - 0.2) <= 0.010000001)
    output = advance(state, { roll = 0.2 }, 1, 60)
    near(output.l_inboard_ratio, 0.25)
    near(output.l_midboard_ratio, 0.25)
    near(output.l_outboard_ratio, 0.1)
end)

check("pause and zero dt hold outputs and simulation clock", function()
    local state = M.new()
    M.reset(state, { roll = 0.2 })
    local expected, old_time = state.outputs.l_inboard_ratio, state.time
    for _ = 1, 30 do M.update(state, { roll = -1, paused = true }, 10) end
    near(state.outputs.l_inboard_ratio, expected)
    near(state.time, old_time)
    M.update(state, { roll = -1 }, 0)
    near(state.outputs.l_inboard_ratio, expected)
    near(state.time, old_time)
end)

check("explicit reset seeds steady command with no stale delay", function()
    local state = M.new()
    advance(state, { roll = 1 }, 2, 60)
    local output = M.reset(state, { roll = -0.2 })
    near(output.l_inboard_ratio, -0.25)
    near(output.l_midboard_ratio, -0.25)
    near(output.l_outboard_ratio, -0.1)
    output = M.update(state, { roll = -0.2 }, 0.02)
    near(output.l_midboard_ratio, -0.25)
end)

check("invalid dt and long time jumps reset deterministically", function()
    for _, dt in ipairs({ -1, 0/0, math.huge, -math.huge, 1.001 }) do
        local state = M.new()
        local output = M.update(state, { roll = 0.2 }, dt)
        near(output.l_inboard_ratio, 0.25)
        near(output.l_midboard_ratio, 0.25)
        near(output.l_outboard_ratio, 0.1)
        near(state.time, 0)
    end
    local state = M.new()
    M.update(state, { roll = 0.2 }, nil)
    near(state.outputs.l_inboard_ratio, 0.25)
end)

check("nonfinite roll fails neutral and excessive roll clamps", function()
    for _, roll in ipairs({ 0/0, math.huge, -math.huge, "bad" }) do
        zero(M.reset(M.new(), { roll = roll }))
    end
    local a, b = M.reset(M.new(), { roll = 100 }), M.reset(M.new(), { roll = 1 })
    for key, value in pairs(a) do near(value, b[key]) end
end)

check("source physical bounds map correctly on both animation sides", function()
    local positive = advance(M.new(), { roll = 1 }, 5, 60)
    local negative = advance(M.new(), { roll = -1 }, 5, 60)
    for _, part in ipairs({ "inboard", "midboard", "outboard" }) do
        near(positive["l_" .. part .. "_ratio"], 2/3)
        near(positive["r_" .. part .. "_ratio"], 1)
        near(negative["l_" .. part .. "_ratio"], -1)
        near(negative["r_" .. part .. "_ratio"], -2/3)
    end
end)

check("each surface respects the source final 30 degree per second limit", function()
    local state = M.new()
    for step = 1, 500 do
        local old = {}
        for key, value in pairs(state.outputs) do old[key] = value end
        local dt = (step % 3 + 1) / 120
        local output = M.update(state, { roll = step % 100 < 50 and 1 or -1 }, dt)
        for key, value in pairs(output) do
            assert(math.abs(value - old[key]) <= dt + 1e-9, "surface rate exceeded")
        end
    end
end)

check("20 60 and 144 fps reproduce the same timed command sequence", function()
    local states = { M.new(), M.new(), M.new() }
    local rates = { 20, 60, 144 }
    for _, segment in ipairs({ {0.4, 0.2}, {0.2, -0.4}, {0.7, 1}, {0.6, -1}, {1, 0} }) do
        for index, state in ipairs(states) do
            advance(state, { roll = segment[2] }, segment[1], rates[index])
        end
        for key, value in pairs(states[1].outputs) do
            near(states[2].outputs[key], value, 0.005, "20 vs 60 fps " .. key)
            near(states[3].outputs[key], value, 0.005, "20 vs 144 fps " .. key)
        end
    end
end)

check("tiny positive dt cannot grow delay queue indefinitely", function()
    local state = M.new()
    for step = 1, 12000 do
        M.update(state, { roll = step % 200 < 100 and 0.2 or -0.2 }, 0.0001)
    end
    assert(#state.history < 40)
    near(state.time, 1.2, 1e-8)
end)

print("Aileron dance core: " .. tests .. " checks passed")
