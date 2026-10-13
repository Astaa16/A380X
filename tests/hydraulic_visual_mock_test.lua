-- SPDX-License-Identifier: GPL-3.0-or-later
-- Strict FlyWithLua host contract, run with the real pressure core. Native
-- writes and writes to the upstream aileron provider are always forbidden.
local script = assert(arg[1], "pass A380X_XP12_Hydraulics.lua")
local core_path = assert(arg[2], "pass a380x_hydraulic_surfaces.lua")
local real_core = assert(loadfile(core_path))()
local keys = {
    "aileron_l_inboard_ratio", "aileron_l_midboard_ratio", "aileron_l_outboard_ratio",
    "aileron_r_inboard_ratio", "aileron_r_midboard_ratio", "aileron_r_outboard_ratio",
    "elevator_ratio", "rudder_ratio", "spoiler_l_inboard_deg", "spoiler_r_inboard_deg",
    "spoiler_l_inner_deg", "spoiler_r_inner_deg", "spoiler_l_middle_deg", "spoiler_r_middle_deg",
    "spoiler_l_outer_deg", "spoiler_r_outer_deg"
}
local prefix, dance_prefix = "a380x/xp12/hydraulics/", "a380x/xp12/visual/"
local sources = {
    grounded = "sim/flightmodel/failures/onground_any", paused = "sim/time/paused",
    dt = "sim/operation/misc/frame_rate_period", pitch = "sim/flightmodel2/controls/pitch_ratio",
    rudder = "sim/flightmodel2/controls/heading_ratio", heading = "sim/flightmodel/position/psi",
    direction = "sim/weather/aircraft/wind_now_direction_degt", wind = "sim/weather/aircraft/wind_now_speed_msc",
    airspeed = "sim/flightmodel/position/true_airspeed", speedbrake = "sim/flightmodel2/wing/speedbrake1_deg",
    spoiler1 = "sim/flightmodel2/wing/spoiler1_deg", spoiler2 = "sim/flightmodel2/wing/spoiler2_deg"
}
for i = 1, 3 do sources["pressure" .. i] = "sim/cockpit2/hydraulics/indicators/hydraulic_pressure_" .. i end
local function finite(value) return type(value) == "number" and value == value and math.abs(value) < math.huge end
local function execute(code, env)
    local chunk
    if _VERSION == "Lua 5.1" then chunk = assert(loadstring(code)); setfenv(chunk, env)
    else chunk = assert(load(code, "host callback", "t", env)) end
    return chunk()
end
local function host(options)
    options = options or {}
    local audit = {values = {}, types = {}, native = {}, bindings = {}, writes = {},
        messages = {}, frames = {}, exits = {}, reads = {}, created = 0, native_writes = 0,
        new_calls = 0, updates = 0}
    for _, name in pairs(sources) do audit.native[name] = {[0] = 0} end
    audit.native[sources.grounded][0], audit.native[sources.dt][0] = 1, 1 / 60
    audit.native[sources.pitch][0], audit.native[sources.rudder][0] = 0.3, -0.4
    audit.native[sources.pressure1][0] = options.cold and 0 or 3000
    audit.native[sources.speedbrake] = {[0] = 11, [1] = 12}
    audit.native[sources.spoiler1] = {[0] = 21, [1] = 22, [2] = 23, [3] = 24}
    audit.native[sources.spoiler2] = {[4] = 35, [5] = 36}
    -- This is the complete aircraft plugin bootstrap: six upstream visual
    -- inputs plus sixteen final hydraulic outputs. Lua creates none of them.
    for i, key in ipairs(keys) do
        audit.values[prefix .. key], audit.types[prefix .. key] = 0, 2
        if i <= 6 then audit.values[dance_prefix .. key], audit.types[dance_prefix .. key] = i / 10, 2 end
    end
    if options.shared_values then audit.values = options.shared_values end
    if options.wrong_type then audit.types[options.wrong_type] = options.kind or 8 end
    if options.invalid_start then audit.native[sources.pitch][0] = 0 / 0 end
    local references, names = {}, {}
    local function reference(name)
        if not references[name] then
            local handle
            if type(newproxy) == "function" then handle = newproxy()
            else handle = assert(io.open(script, "r")); handle:close() end
            references[name], names[handle] = handle, name
        end
        return references[name]
    end
    local env = setmetatable({}, {__index = function(_, key)
        assert(key ~= "SIM_PERIOD", "bind the simulator time dataref, not an invented global")
        return _G[key]
    end})
    env._G = env
    env.PLANE_ICAO, env.AIRCRAFT_PATH = options.icao or "A388", options.path or "/Aircraft/A380X-XP12/"
    env.logMsg = function(text) audit.messages[#audit.messages + 1] = text end
    env.XPLMFindDataRef = function(name)
        assert(not name:find("override", 1, true), "flight-control override lookup")
        if name == options.lookup_error then error("lookup failed") end
        if name == options.missing_ref then return options.null_pointer and 0 or nil end
        if audit.native[name] or audit.values[name] ~= nil then return reference(name) end
        return nil
    end
    env.XPLMGetDataRefTypes = function(handle)
        assert(type(handle) == "userdata" and names[handle], "type API must receive opaque SDK handle")
        if names[handle] == options.type_error then error("type lookup failed") end
        return audit.types[names[handle]] or 2
    end
    env.dataref_table = function(name)
        assert(audit.native[name] or audit.values[name] ~= nil, "unexpected binding " .. tostring(name))
        audit.bindings[#audit.bindings + 1] = name
        if name == options.bad_binding then return nil end
        return setmetatable({}, {
            __index = function(_, index)
                audit.reads[name] = audit.reads[name] or {}; audit.reads[name][index] = true
                if audit.native[name] then
                    assert(audit.native[name][index] ~= nil or index == 0,
                        "wrong array index " .. name .. "[" .. index .. "]")
                    return audit.native[name][index]
                end
                assert(index == 0, "custom float indexed as array")
                return audit.values[name]
            end,
            __newindex = function(_, index, value)
                if name:sub(1, #prefix) ~= prefix then
                    audit.native_writes = audit.native_writes + 1
                    error("write to native/upstream provider " .. name)
                end
                assert(index == 0 and finite(value), "invalid publication")
                audit.values[name] = value; audit.writes[#audit.writes + 1] = {name, value}
            end,
        })
    end
    env.create_dataref_table = function()
        audit.created = audit.created + 1; error("late animation dataref creation")
    end
    local core = {KEYS = keys, valid = real_core.valid}
    core.new = function(current)
        audit.new_calls, audit.last_input = audit.new_calls + 1, current
        if options.constructor_error then error("constructor failed") end
        return real_core.new(current)
    end
    core.update = function(state, current, dt)
        audit.updates, audit.last_input, audit.last_dt = audit.updates + 1, current, dt
        if audit.update_error then error("core update failed") end
        local values = real_core.update(state, current, dt)
        if audit.bad_output then
            local invalid = {}; for key, value in pairs(values) do invalid[key] = value end
            invalid[keys[16]] = audit.bad_output
            return invalid
        end
        return values
    end
    if options.bad_core_api then core.update = false end
    env.require = function(name)
        assert(name == "a380x_hydraulic_surfaces", "unexpected dependency")
        if options.missing_core then error("missing module") end
        return core
    end
    env.do_every_frame = function(code) audit.frames[#audit.frames + 1] = code end
    env.do_on_exit = function(code) audit.exits[#audit.exits + 1] = code end
    for _, name in ipairs({"dataref", "XPLMSetDataf", "XPLMSetDatai", "XPLMSetDatavf", "command_once"}) do
        env[name] = function() error("forbidden writable API: " .. name) end
    end
    if options.missing_api then env[options.missing_api] = false end
    if options.namespace_collision then env.A380X_HYDRAULIC_SURFACES = options.namespace_collision end
    local chunk
    if _VERSION == "Lua 5.1" then chunk = assert(loadfile(script)); setfenv(chunk, env)
    else chunk = assert(loadfile(script, "t", env)) end
    chunk()
    audit.frame = function() for _, code in ipairs(audit.frames) do execute(code, env) end end
    audit.exit = function() for _, code in ipairs(audit.exits) do execute(code, env) end end
    return env, audit
end
local function near(actual, expected, label)
    assert(math.abs(actual - expected) < 1e-9, (label or "unexpected value") .. ": " .. tostring(actual))
end
local function snapshot(audit)
    local result = {}; for _, key in ipairs(keys) do result[key] = audit.values[prefix .. key] end; return result
end
local function same(audit, before)
    for _, key in ipairs(keys) do near(audit.values[prefix .. key], before[key], "hold " .. key) end
end
local function messages(audit, needle)
    local count = 0
    for _, text in ipairs(audit.messages) do if text:find(needle, 1, true) then count = count + 1 end end
    return count
end
local env, audit = host()
local count = 0; for _ in pairs(audit.values) do count = count + 1 end
assert(count == 22 and #audit.bindings == 37 and audit.created == 0, "bootstrap contract differs")
assert(#audit.frames == 1 and audit.frames[1] == "A380X_HYDRAULIC_SURFACES.update()")
assert(#audit.exits == 1 and audit.exits[1] == "A380X_HYDRAULIC_SURFACES.shutdown()")
assert(#audit.writes == 16 and audit.new_calls == 1)
for i = 1, 6 do near(audit.values[prefix .. keys[i]], i / 10, "aileron adapter sign") end
near(audit.values[prefix .. "elevator_ratio"], 0.3); near(audit.values[prefix .. "rudder_ratio"], -0.4)
local expected_spoilers = {11, 12, 21, 22, 23, 24, 35, 36}
for i, value in ipairs(expected_spoilers) do near(audit.values[prefix .. keys[i + 8]], value, "spoiler array routing") end
audit.frame(); assert(audit.updates == 1 and audit.last_dt == 1 / 60)
assert(audit.last_input.grounded and not audit.last_input.paused)
assert(audit.reads[sources.spoiler2][4] and audit.reads[sources.spoiler2][5] and not audit.reads[sources.spoiler2][0])

-- With pressure off, handle/pedal/stick changes cannot move held spoilers.
local before = snapshot(audit)
audit.native[sources.pressure1][0], audit.native[sources.pitch][0] = 0, -1
audit.native[sources.speedbrake][0] = 60
for _ = 1, 300 do audit.frame() end
assert(audit.values[prefix .. "elevator_ratio"] < before.elevator_ratio)
for i = 9, 16 do near(audit.values[prefix .. keys[i]], before[keys[i]], "unsupported spoiler moved") end
-- Pauses and invalid time must hold existing values even with a pump restored.
before = snapshot(audit); audit.native[sources.paused][0] = 1
audit.native[sources.pressure1][0], audit.native[sources.pitch][0] = 3000, 1
audit.frame(); audit.frame(); same(audit, before)
audit.native[sources.paused][0] = 0
for _, dt in ipairs({0, -1, 2, math.huge, 0 / 0, "bad"}) do
    audit.native[sources.dt][0] = dt; audit.frame(); same(audit, before)
end
audit.native[sources.dt][0] = nil; audit.frame(); same(audit, before)
audit.native[sources.dt][0] = 1 / 60
-- Invalid source input is contained once, then resumes without a reset.
local constructions = audit.new_calls
audit.native[sources.pitch][0] = 0 / 0
audit.frame(); audit.frame(); same(audit, before)
assert(messages(audit, "invalid source input") == 1)
audit.native[sources.pitch][0] = 0.5; audit.frame()
assert(audit.new_calls == constructions and env.A380X_HYDRAULIC_SURFACES.enabled)
for _, name in ipairs({sources.grounded, sources.paused}) do
    local good = audit.native[name][0]; before = snapshot(audit)
    audit.native[name][0] = 2; audit.frame(); same(audit, before)
    audit.native[name][0] = good; audit.frame()
end

-- Foreign aircraft receive no callback publications; return to this exact
-- folder is a new aircraft session and seeds its actual powered controls.
before = snapshot(audit); local writes = #audit.writes
env.PLANE_ICAO = "C172"; audit.frame(); audit.frame(); same(audit, before)
assert(#audit.writes == writes)
env.PLANE_ICAO, env.AIRCRAFT_PATH = "A388", "/Aircraft/A380X-XP12-other/"
audit.frame(); assert(#audit.writes == writes)
env.AIRCRAFT_PATH = "D:\\X-Plane 12\\Aircraft\\A380X-XP12\\"
audit.frame(); assert(audit.new_calls == constructions + 1)
near(audit.values[prefix .. "elevator_ratio"], 0.5)
before = snapshot(audit); audit.exit(); same(audit, before)
writes = #audit.writes; audit.exit(); audit.frame()
assert(#audit.writes == writes and not env.A380X_HYDRAULIC_SURFACES.enabled)
assert(audit.native_writes == 0 and audit.created == 0)

-- Pressure loss while airborne leaves native controls in charge of pitch and
-- roll, but the whole wrapper must keep every spoiler at its supported pose.
local flying, flying_log = host()
flying_log.native[sources.grounded][0] = 0
flying_log.native[sources.airspeed][0] = 90
local flying_before = snapshot(flying_log)
flying_log.native[sources.pressure1][0] = 0
flying_log.native[sources.pitch][0], flying_log.native[sources.rudder][0] = -0.6, 0.7
for _, name in ipairs({sources.speedbrake, sources.spoiler1, sources.spoiler2}) do
    for index in pairs(flying_log.native[name]) do flying_log.native[name][index] = 60 end
end
for i = 1, 6 do flying_log.values[dance_prefix .. keys[i]] = -i / 10 end
for _ = 1, 300 do flying_log.frame() end
near(flying_log.values[prefix .. "elevator_ratio"], -0.6)
near(flying_log.values[prefix .. "rudder_ratio"], 0.7)
for i = 1, 6 do near(flying_log.values[prefix .. keys[i]], -i / 10) end
for i = 9, 16 do near(flying_log.values[prefix .. keys[i]], flying_before[keys[i]], "airborne unsupported spoiler moved") end
assert(flying.A380X_HYDRAULIC_SURFACES.enabled and flying_log.native_writes == 0)

-- Startup binding failures never register callbacks or publish any subset.
local failures = {
    {icao = "C172"}, {path = "/Aircraft/A380X-XP12-other/"}, {path = "/Aircraft/not-A380X-XP12/"},
    {path = "/Aircraft/A380X-XP120/"}, {missing_core = true}, {bad_core_api = true},
    {missing_ref = sources.pitch}, {missing_ref = sources.dt, null_pointer = true},
    {bad_binding = sources.spoiler1}, {lookup_error = sources.wind},
    {missing_ref = prefix .. keys[16]}, {missing_ref = dance_prefix .. keys[6]},
    {type_error = prefix .. keys[16]}, {shared_values = {}}
}
for _, key in ipairs(keys) do failures[#failures + 1] = {missing_ref = prefix .. key} end
for i = 1, 6 do failures[#failures + 1] = {missing_ref = dance_prefix .. keys[i]} end
for _, kind in ipairs({0, 1, 3, 8}) do
    failures[#failures + 1] = {wrong_type = prefix .. keys[16], kind = kind}
    failures[#failures + 1] = {wrong_type = dance_prefix .. keys[6], kind = kind}
end
for _, options in ipairs(failures) do
    local rejected, log = host(options)
    assert(#log.frames == 0 and #log.exits == 0 and #log.writes == 0 and log.created == 0,
        "partial publication or registration after startup failure")
    assert(rawget(rejected, "A380X_HYDRAULIC_SURFACES") == nil)
end
for _, name in ipairs({"XPLMFindDataRef", "XPLMGetDataRefTypes", "dataref_table", "do_every_frame", "do_on_exit", "require"}) do
    local _, log = host({missing_api = name})
    assert(#log.bindings == 0 and #log.frames == 0 and #log.writes == 0)
end
local owner = {}; local collision, collision_log = host({namespace_collision = owner})
assert(collision.A380X_HYDRAULIC_SURFACES == owner and #collision_log.bindings == 0)
local no_creator, creator_log = host({missing_api = "create_dataref_table"})
assert(no_creator.A380X_HYDRAULIC_SURFACES.enabled and creator_log.created == 0)
local cold, cold_log = host({cold = true})
for _, key in ipairs(keys) do near(cold_log.values[prefix .. key], 0, "cold seed") end
for _ = 1, 60 do cold_log.frame() end
assert(cold.A380X_HYDRAULIC_SURFACES.enabled and cold_log.values[prefix .. keys[1]] > 0)
local invalid, invalid_log = host({invalid_start = true})
assert(#invalid_log.writes == 0 and invalid.A380X_HYDRAULIC_SURFACES.enabled)
invalid_log.native[sources.pitch][0] = 0.2; invalid_log.frame()
near(invalid_log.values[prefix .. "elevator_ratio"], 0.2)

-- Corrupted core outputs fail atomically, preserve the last valid pose, and
-- disable this callback without escaping into the FlyWithLua host.
for _, failure in ipairs({"update_error", "nan", "range"}) do
    local failed, log = host(); before = snapshot(log); writes = #log.writes
    if failure == "update_error" then log.update_error = true
    else log.bad_output = failure == "nan" and 0 / 0 or 100 end
    log.frame(); same(log, before)
    assert(not failed.A380X_HYDRAULIC_SURFACES.enabled and #log.writes == writes)
    log.frame(); log.exit(); assert(#log.writes == writes and log.native_writes == 0)
end
local broken, broken_log = host({constructor_error = true})
assert(not broken.A380X_HYDRAULIC_SURFACES.enabled and #broken_log.writes == 0)
print("Hydraulic wrapper checks passed (" .. _VERSION .. "): 22 bootstrap refs, 16 custom-only outputs, native indices, guards, holds, atomic errors")
