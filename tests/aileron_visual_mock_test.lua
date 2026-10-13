-- SPDX-License-Identifier: GPL-3.0-or-later
-- Lua 5.1+ host-contract tests. Execute registered callback strings exactly as
-- FlyWithLua does; these checks do not replace a native simulator smoke test.
local script = assert(arg[1], "pass A380X_XP12_Ailerons.lua")
local keys = {"l_inboard_ratio", "l_midboard_ratio", "l_outboard_ratio",
              "r_inboard_ratio", "r_midboard_ratio", "r_outboard_ratio"}
local prefix = "a380x/xp12/visual/aileron_"
local sources = {"sim/flightmodel2/controls/roll_ratio",
                 "sim/flightmodel/failures/onground_any", "sim/time/paused",
                 "sim/operation/misc/frame_rate_period"}
local allowed = {}
for _, key in ipairs(keys) do allowed[prefix .. key] = true end
local function finite(value)
    return type(value) == "number" and value == value and math.abs(value) < math.huge
end
local function execute(code, env)
    local chunk
    if _VERSION == "Lua 5.1" then
        chunk = assert(loadstring(code)); setfenv(chunk, env)
    else
        chunk = assert(load(code, "host callback", "t", env))
    end
    return chunk()
end
local function host(options)
    options = options or {}
    local log = {bindings = {}, created = {}, values = {}, writes = {}, messages = {},
                 frames = {}, exits = {}, resets = 0, updates = 0, native_writes = 0,
                 custom_reads = 0,
                 native = {[sources[1]] = options.roll or 0.4, [sources[2]] = 1, [sources[3]] = 0,
                           [sources[4]] = 1 / 60}}
    log.values, log.types = options.shared_values or {}, options.shared_types or {}
    -- Model the aircraft-local plugin's earlier load phase, before Lua starts.
    -- An explicitly supplied empty/partial table models a broken bootstrap.
    if options.shared_values == nil then
        for _, key in ipairs(keys) do log.values[prefix .. key] = 0 end
    end
    if options.shared_types == nil then
        for _, key in ipairs(keys) do log.types[prefix .. key] = 2 end
    end
    local references, reference_names = {}, {}
    local function reference(name)
        if not references[name] then
            -- Actual FlyWithLua returns an opaque lightuserdata handle, not
            -- the ref's name. Use userdata proxies to detect string assumptions.
            local handle
            if type(newproxy) == "function" then handle = newproxy()
            else handle = assert(io.open(script, "r")); handle:close() end
            references[name], reference_names[handle] = handle, name
        end
        return references[name]
    end
    local env = setmetatable({}, {
        __index = function(_, key)
            assert(key ~= "SIM_PERIOD", "FlyWithLua does not define SIM_PERIOD; bind the real timing dataref")
            return _G[key]
        end,
        __newindex = function(target, key, value)
            assert(key ~= "SIM_PERIOD", "test host must not invent SIM_PERIOD")
            rawset(target, key, value)
        end,
    })
    env._G = env
    env.PLANE_ICAO = options.icao or "A388"
    env.AIRCRAFT_PATH = options.path or "/Aircraft/A380X-XP12/"
    env.logMsg = function(text) log.messages[#log.messages + 1] = text end
    env.XPLMFindDataRef = function(name)
        assert(not name:find("override", 1, true), "override lookup")
        if options.lookup_error == name then error("mock lookup failed") end
        if name == options.missing_ref then return options.null_pointer and 0 or nil end
        if log.native[name] ~= nil or log.values[name] ~= nil then return reference(name) end
        return nil
    end
    env.XPLMGetDataRefTypes = function(handle)
        local name = reference_names[handle]
        assert(type(handle) == "userdata" and name, "type lookup needs the opaque dataref handle")
        if options.type_error == name then error("mock type lookup failed") end
        return log.types[name] or 2
    end
    local function custom_handle(name)
        return setmetatable({}, {
            __index = function(_, index)
                assert(index == 0)
                log.custom_reads = log.custom_reads + 1
                return log.values[name]
            end,
            __newindex = function(_, index, value)
                assert(index == 0 and finite(value) and math.abs(value) <= 1, "invalid custom publication")
                log.values[name] = value
                log.writes[#log.writes + 1] = {name, value}
            end,
        })
    end
    env.dataref_table = function(name)
        if allowed[name] and log.values[name] ~= nil then
            log.bindings[#log.bindings + 1] = name
            return custom_handle(name)
        end
        local known = false
        for _, source in ipairs(sources) do if source == name then known = true end end
        assert(known, "unexpected native binding: " .. name)
        log.bindings[#log.bindings + 1] = name
        if options.bad_binding == name then return nil end
        return setmetatable({}, {
            __index = function(_, index) assert(index == 0); return log.native[name] end,
            __newindex = function() log.native_writes = log.native_writes + 1; error("native write") end,
        })
    end
    env.create_dataref_table = function(name, kind)
        log.created[#log.created + 1] = name
        error("Lua must not create late animation datarefs: " .. tostring(name))
    end
    local function result(input)
        assert(type(input) == "table" and finite(input.roll) and math.abs(input.roll) <= 1)
        assert(type(input.grounded) == "boolean" and type(input.paused) == "boolean")
        local values = {}
        -- Deliberately distinct mock sides detect accidental wrapper sign
        -- changes; real-core output sign/distribution is tested separately.
        for index, key in ipairs(keys) do values[key] = input.roll * (index <= 3 and 1 or -1) end
        if log.bad_output then values.l_inboard_ratio = 0 / 0 end
        return values
    end
    local core = {
        new = function() if options.bad_new then error("mock constructor error") end; return {} end,
        reset = function(state, input)
            log.resets = log.resets + 1
            if options.bad_reset then return {} end
            state.outputs = result(input); return state.outputs
        end,
        update = function(state, input, dt)
            log.updates = log.updates + 1
            log.last_input, log.last_dt = input, dt
            if log.update_error then error("mock update error") end
            if finite(dt) and dt == 0 then return state.outputs end
            state.outputs = result(input); return state.outputs
        end,
    }
    if options.bad_core_api then core.update = false end
    env.require = function(name)
        assert(name == "a380x_aileron_dance", "unexpected dependency")
        if options.missing_core then error("module missing") end
        return options.core or core
    end
    env.do_every_frame = function(code) log.frames[#log.frames + 1] = code end
    env.do_on_exit = function(code) log.exits[#log.exits + 1] = code end
    -- Any use of an alternative writable native API is forbidden, not merely
    -- an unrecorded no-op in this test host.
    for _, name in ipairs({"dataref", "XPLMSetDataf", "XPLMSetDatai", "XPLMSetDatavf", "command_once"}) do
        env[name] = function() error("forbidden native control API: " .. name) end
    end
    if options.missing_api then env[options.missing_api] = false end
    if options.namespace_collision then env.A380X_AILERON_VISUALS = options.namespace_collision end
    local chunk
    if _VERSION == "Lua 5.1" then chunk = assert(loadfile(script)); setfenv(chunk, env)
    else chunk = assert(loadfile(script, "t", env)) end
    chunk()
    log.frame = function()
        for _, code in ipairs(log.frames) do execute(code, env) end
    end
    log.exit = function()
        for _, code in ipairs(log.exits) do execute(code, env) end
    end
    return env, log
end
local function all_zero(log)
    for _, key in ipairs(keys) do assert(log.values[prefix .. key] == 0, "non-neutral reset") end
end
local function message_count(log, needle)
    local count = 0
    for _, text in ipairs(log.messages) do
        if text:find(needle, 1, true) then count = count + 1 end
    end
    return count
end

local env, log = host()
assert(#log.bindings == 10 and #log.created == 0)
assert(#log.frames == 1 and log.frames[1] == "A380X_AILERON_VISUALS.update()")
assert(#log.exits == 1 and log.exits[1] == "A380X_AILERON_VISUALS.shutdown()")
assert(log.values[prefix .. keys[1]] == 0.4, "load should seed current input without a transient")
assert(rawget(env, "SIM_PERIOD") == nil, "host invented a timing global")
assert(message_count(log, "first nonzero publication") == 1 and log.custom_reads == 6,
       "initial nonzero diagnostic must read back all six SDK-backed outputs once")
log.native[sources[1]], log.native[sources[2]] = -0.7, 0
log.frame()
assert(log.updates == 1 and log.last_input.roll == -0.7 and not log.last_input.grounded)
assert(log.last_dt == 1 / 60 and log.values[prefix .. keys[1]] == -0.7)
local updates, writes = log.updates, #log.writes
log.native[sources[3]], log.native[sources[1]] = 1, 0.8
log.frame(); log.frame()
assert(log.updates == updates and #log.writes == writes, "pause advanced animation")
log.native[sources[3]] = 0
log.native[sources[4]] = 0
log.frame()
assert(log.values[prefix .. keys[1]] == -0.7, "zero dt must hold the core pose")
log.native[sources[4]] = 1 / 60
log.frame()
assert(log.values[prefix .. keys[1]] == 0.8)

-- The path AND ICAO guard must work after registration as well as on load.
env.PLANE_ICAO = "C172"
local before_reset = log.resets
log.frame(); all_zero(log)
assert(log.resets == before_reset + 1)
writes, updates = #log.writes, log.updates
log.frame()
assert(#log.writes == writes and log.updates == updates, "other aircraft received per-frame activity")
env.PLANE_ICAO, env.AIRCRAFT_PATH = "A388", "/Aircraft/A380X-XP12-other/"
log.frame(); assert(#log.writes == writes)
env.AIRCRAFT_PATH = "D:\\X-Plane 12\\Aircraft\\A380X-XP12\\"
log.native[sources[1]] = -0.2
log.frame()
assert(log.values[prefix .. keys[1]] == -0.2 and log.updates == updates, "reentry did not reset")
log.native[sources[1]] = 99
log.frame(); assert(log.last_input.roll == 1, "native roll was not bounded")

-- Invalid input neutralizes once; valid input reseeds. Invalid periods are
-- deliberately passed to the core, which owns timing/reset policy.
for _, invalid in ipairs({0 / 0, math.huge, -math.huge, "bad"}) do
    log.native[sources[1]] = invalid
    log.frame(); all_zero(log)
    log.native[sources[1]] = 0.25
    log.frame(); assert(log.values[prefix .. keys[1]] == 0.25)
end
for _, source in ipairs({sources[1], sources[2], sources[3]}) do
    local prior = log.native[source]
    log.native[source] = nil
    log.frame(); all_zero(log)
    log.native[source] = prior
    log.frame()
end
for _, source in ipairs({sources[2], sources[3]}) do
    local prior = log.native[source]
    log.native[source] = 2
    log.frame(); all_zero(log)
    log.native[source] = prior
    log.frame()
end
for _, dt in ipairs({-1, 0 / 0, math.huge, "bad", 5}) do
    log.native[sources[4]] = dt
    log.frame()
    assert(log.last_dt == dt or (type(dt) == "number" and dt ~= dt and log.last_dt ~= log.last_dt))
end
log.native[sources[4]] = nil
log.frame(); assert(log.last_dt == nil)
log.exit(); all_zero(log)
writes = #log.writes
log.exit(); log.frame()
assert(#log.writes == writes and not env.A380X_AILERON_VISUALS.enabled, "shutdown was not inert")
assert(log.native_writes == 0)
assert(message_count(log, "first frame callback") == 1, "first-frame diagnostic missing or repeated")
assert(message_count(log, "first nonzero publication") == 1 and log.custom_reads == 6,
       "nonzero readback diagnostic must not spam each frame")

-- All predictable startup failures occur before any publication/callback/global
-- registration, including a missing LAST member of the early-plugin contract.
for _, opts in ipairs({
    {icao = "C172"}, {path = "/Aircraft/A380X-XP12-other/"},
    {path = "/Aircraft/not-A380X-XP12/"}, {path = "/Aircraft/A380X-XP120/"},
    {missing_core = true}, {bad_core_api = true}, {bad_new = true}, {bad_reset = true},
    {missing_ref = prefix .. keys[6]}, {lookup_error = prefix .. keys[6]},
    {shared_values = {}},
    {missing_ref = sources[3]}, {missing_ref = sources[1], null_pointer = true},
    {bad_binding = sources[3]}, {missing_ref = sources[4]}, {bad_binding = sources[4]},
}) do
    local rejected, audit = host(opts)
    assert(#audit.created == 0 and #audit.frames == 0 and #audit.exits == 0,
           "failed initialization left partial registration")
    assert(#audit.writes == 0, "failed startup changed early-plugin values")
    assert(rawget(rejected, "A380X_AILERON_VISUALS") == nil)
end
for _, name in ipairs({"XPLMFindDataRef", "XPLMGetDataRefTypes", "dataref_table",
                       "do_every_frame", "do_on_exit", "require"}) do
    local _, audit = host({missing_api = name})
    assert(#audit.bindings == 0 and #audit.created == 0 and #audit.frames == 0, "missing API partially initialized")
end
local owned = {}
local rejected, audit = host({namespace_collision = owned})
assert(rejected.A380X_AILERON_VISUALS == owned and #audit.created == 0)
local windows, winlog = host({path = "D:\\X-Plane 12\\Aircraft\\A380X-XP12\\"})
winlog.frame(); assert(windows.A380X_AILERON_VISUALS.enabled and #winlog.created == 0)
local no_creator, no_creator_log = host({missing_api = "create_dataref_table"})
no_creator_log.frame()
assert(no_creator.A380X_AILERON_VISUALS.enabled and #no_creator_log.created == 0,
       "early-plugin binding must not depend on Lua dataref creation API")
local absent, absent_log = host({shared_values = {}})
assert(not absent.A380X_AILERON_VISUALS and #absent_log.created == 0 and #absent_log.bindings == 0)
assert(message_count(absent_log, "early-registration plugin missing; fully restart X-Plane") == 1,
       "missing early-plugin diagnostic must explain the required full restart")
local invalid_start, invalid_log = host({roll = 0 / 0})
all_zero(invalid_log)
invalid_log.native[sources[1]] = 0.1
invalid_log.frame()
assert(invalid_start.A380X_AILERON_VISUALS.enabled and invalid_log.values[prefix .. keys[1]] == 0.1)

-- FlyWithLua engine reload destroys its globals/callbacks, not the aircraft
-- plugin's datarefs. Rebind six scalar Floats without any Lua registration.
local old_env, old = host()
old.exit(); all_zero(old)
local other_env, other = host({icao = "C172", shared_values = old.values, shared_types = old.types})
assert(not other_env.A380X_AILERON_VISUALS and #other.created == 0 and #other.frames == 0)
local fresh, reloaded = host({roll = -0.35, shared_values = old.values, shared_types = old.types})
assert(fresh ~= old_env and fresh.A380X_AILERON_VISUALS ~= old_env.A380X_AILERON_VISUALS)
assert(#reloaded.created == 0 and #reloaded.bindings == 10 and #reloaded.frames == 1)
assert(reloaded.values == old.values and reloaded.values[prefix .. keys[1]] == -0.35)
reloaded.frame(); assert(fresh.A380X_AILERON_VISUALS.enabled and reloaded.native_writes == 0)
for _, bad_type in ipairs({0, 1, 3, 8}) do
    local kinds = {}
    for _, key in ipairs(keys) do kinds[prefix .. key] = 2 end
    kinds[prefix .. keys[6]] = bad_type
    local rejected, audit = host({shared_values = old.values, shared_types = kinds})
    assert(not rejected.A380X_AILERON_VISUALS and #audit.created == 0 and #audit.bindings == 0)
end
local _, bad_type_read = host({shared_values = old.values, shared_types = old.types,
                              type_error = prefix .. keys[6]})
assert(#bad_type_read.created == 0 and #bad_type_read.bindings == 0)
for missing = 1, #keys do
    local partial = {}
    for index, key in ipairs(keys) do if index ~= missing then partial[prefix .. key] = 0 end end
    local rejected, audit = host({shared_values = partial, shared_types = old.types})
    assert(not rejected.A380X_AILERON_VISUALS and #audit.created == 0 and #audit.bindings == 0)
    assert(message_count(audit, "partial early-registration dataref set") == 1)
end

-- Core failures are contained in the globally registered callback, do not
-- quarantine other scripts, and can only clear this provider's six refs.
for _, fault in ipairs({"bad_output", "update_error"}) do
    local failed, audit = host()
    audit[fault] = true
    audit.frame(); all_zero(audit)
    assert(not failed.A380X_AILERON_VISUALS.enabled and audit.native_writes == 0)
    local count = #audit.writes
    audit.frame(); assert(#audit.writes == count)
end
if arg[2] then
    local real_core = assert(loadfile(arg[2]))()
    local live, audit = host({core = real_core, roll = 0.4})
    for _, key in ipairs(keys) do
        assert(math.abs(audit.values[prefix .. key] - 0.4) < 1e-9, "real core ground passthrough")
    end
    live.PLANE_ICAO = "C172"; audit.frame(); all_zero(audit)
    live.PLANE_ICAO = "A388"
    audit.native[sources[2]] = 0
    audit.frame()
    for index, key in ipairs(keys) do
        local expected = index % 3 == 0 and 0.2 or 0.5
        assert(math.abs(audit.values[prefix .. key] - expected) < 1e-9, "real core airborne steady pose")
    end
    audit.native[sources[1]] = 1
    for _ = 1, 120 do audit.frame() end
    for index, key in ipairs(keys) do
        local expected = index <= 3 and 2 / 3 or 1
        assert(math.abs(audit.values[prefix .. key] - expected) < 1e-9, "real core signed full-roll limit")
    end
    local old_writes = #audit.writes
    audit.native[sources[3]], audit.native[sources[1]] = 1, -1
    for _ = 1, 30 do audit.frame() end
    assert(#audit.writes == old_writes, "real-core pause advanced")
    audit.native[sources[3]] = 0
    for _, dt in ipairs({-1, 0 / 0, math.huge, "bad", 5}) do
        audit.native[sources[4]] = dt; audit.frame()
        assert(live.A380X_AILERON_VISUALS.enabled, "real core rejected invalid-period reset")
    end
    audit.native[sources[4]] = nil; audit.frame()
    audit.exit(); all_zero(audit)
    assert(audit.native_writes == 0)

    -- This fails with the former nonexistent SIM_PERIOD dependency: the core
    -- would either be disabled by the strict host or reset to steady pose on
    -- every frame, eliminating the measured 350 ms lag entirely.
    local timed, timing = host({core = real_core, roll = 0})
    timing.native[sources[1]], timing.native[sources[2]] = 0.2, 0
    for _ = 1, 21 do timing.frame() end
    assert(timed.A380X_AILERON_VISUALS.enabled, "real timing-dataref callback was disabled")
    assert(math.abs(timing.values[prefix .. "l_midboard_ratio"]) < 1e-9,
           "midboard moved at/before350ms: driver is not advancing the core with the real frame period")
    assert(math.abs(timing.values[prefix .. "l_inboard_ratio"] - 0.25) < 1e-9)
    timing.frame()
    assert(timing.values[prefix .. "l_midboard_ratio"] > 0.01, "midboard did not start after350ms")
    for _ = 1, 120 do timing.frame() end
    assert(message_count(timing, "first frame callback") == 1)
    assert(message_count(timing, "first nonzero publication") == 1 and timing.custom_reads == 6)
    assert(timing.native_writes == 0)
    print("Real aileron core + wrapper integration passed (" .. _VERSION .. ")")
end
print("Aileron visual wrapper checks passed (" .. _VERSION .. "): six custom-only writes, read-only native handles, callback replay, guards, pause, resets, failures")
