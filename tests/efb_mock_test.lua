-- SPDX-License-Identifier: GPL-3.0-or-later
-- Run with Lua 5.1+; simulator rendering and command effects still require a
-- native X-Plane smoke test.
local script = assert(arg[1], "pass A380X_XP12_EFB.lua")

local function host(options)
    options = options or {}
    local state = {
        bindings = {}, commands = {}, native = {}, windows = {}, messages = {},
        created = 0, destroyed = 0,
    }
    local env = setmetatable({}, {__index = _G})
    env._G = env
    env.PLANE_ICAO = options.icao or "A388"
    env.AIRCRAFT_PATH = options.path or "/Aircraft/A380X-XP12/"
    env.SUPPORTS_FLOATING_WINDOWS = options.support ~= false
    env.imgui = {}
    env.logMsg = function(value) state.messages[#state.messages + 1] = value end
    env.XPLMFindDataRef = function(name)
        if name == options.missing_ref then return nil end
        return name
    end
    env.dataref = function(variable, name, mode, index)
        assert(mode == "readonly" or mode == "writable")
        state.bindings[#state.bindings + 1] = {name, mode, index}
        env[variable] = 0
    end
    env.command_once = function(command) state.native[#state.native + 1] = command end
    env.float_wnd_create = function(width, height, style, use_imgui)
        assert(width == 1180 and height == 760 and style == 1 and use_imgui == true)
        state.created = state.created + 1
        local window = {id = state.created}
        state.windows[window] = true
        state.current = window
        return window
    end
    env.float_wnd_destroy = function(window)
        assert(state.windows[window], "stale or double-destroyed EFB window")
        state.windows[window] = nil
        state.destroyed = state.destroyed + 1
        if state.close then env[state.close](window) end
    end
    env.float_wnd_set_title = function(window, title)
        assert(state.windows[window]); state.title = title
    end
    env.float_wnd_set_imgui_builder = function(window, callback)
        assert(state.windows[window]); state.builder = callback
    end
    env.float_wnd_set_onclose = function(window, callback)
        assert(state.windows[window]); state.close = callback
    end
    env.create_command = function(name, _description, start, hold, finish)
        assert(hold == "" and finish == "")
        state.commands[name] = start
    end
    env.add_macro = function(_name, _activate, _deactivate, initial)
        assert(initial == "deactivate")
    end
    env.do_every_frame = function(callback) state.frame = callback end

    local chunk
    if _VERSION == "Lua 5.1" then
        chunk = assert(loadfile(script)); setfenv(chunk, env)
    else
        chunk = assert(loadfile(script, "t", env))
    end
    chunk()
    return env, state
end

local env, state = host()
assert(#state.bindings > 75, "expected complete native EFB binding set")
for _, command in ipairs({"window_toggle", "window_open", "window_close", "window_next_page", "window_power"}) do
    assert(state.commands["a380x/xp12/efb/" .. command], "missing EFB companion command " .. command)
end
assert(state.frame == "a380x_efb_update_core()", "missing EFB update callback")

env.a380x_efb_open()
env.a380x_efb_open()
assert(state.created == 1 and state.builder == "a380x_efb_on_build", "EFB window ownership failed")
assert(state.title:find("Dashboard", 1, true), "EFB did not start on dashboard")
env.a380x_efb_next_page()
assert(state.title:find("Dispatch", 1, true), "next-page command failed")
assert(env.a380x_efb_select_page("Ground") and not env.a380x_efb_select_page("NOT_A_PAGE"))

env.a380x_efb_on_ground = 1
for index = 0, 3 do env["a380x_efb_engine_" .. index] = 0 end
assert(env.a380x_efb_apply_payload(100, 5000), "valid payload was rejected")
assert(math.abs(env.a380x_efb_payload - 15400) < 0.001, "FBW passenger/bag payload math failed")
env.a380x_efb_on_ground = 0
assert(not env.a380x_efb_apply_payload(200, 6000), "in-flight payload mutation was allowed")
assert(math.abs(env.a380x_efb_payload - 15400) < 0.001, "rejected payload changed native mass")
env.a380x_efb_on_ground = 1
env.a380x_efb_engine_2 = 1
assert(not env.a380x_efb_apply_payload(200, 6000), "running-engine payload mutation was allowed")
env.a380x_efb_engine_2 = 0

local ratios = {0.30, 0.078, 0.22, 0.22, 0.078, 0.023, 0.023, 0.058, 0}
for index = 0, 8 do env["a380x_efb_tank_ratio_" .. index] = ratios[index + 1] end
assert(env.a380x_efb_set_fuel_total(100000), "fuel distribution failed")
local total = 0
for index = 0, 8 do total = total + env["a380x_efb_fuel_" .. index] end
assert(math.abs(total - 100000) < 0.01, "nine-tank distribution did not preserve total fuel")
assert(env.a380x_efb_fuel_8 == 0, "disabled ninth tank received fuel")

env.a380x_efb_fuel_total = 0
env.a380x_efb_time = 100
assert(env.a380x_efb_begin_refuel(200000, "REAL"), "valid real refuel was rejected")
env.a380x_efb_time = 101
env.a380x_efb_update_core()
total = 0
for index = 0, 8 do total = total + env["a380x_efb_fuel_" .. index] end
assert(total > 48 and total < 49, "FlyByWire real refuel rate translation is wrong")
env.a380x_efb_fuel_total = total
env.a380x_efb_time = 102
env.a380x_efb_update_core()
total = 0
for index = 0, 8 do total = total + env["a380x_efb_fuel_" .. index] end
assert(total > 97 and total < 98, "refuel target was lost after the first update")
env.a380x_efb_stop_refuel()
env.a380x_efb_on_ground = 0
assert(not env.a380x_efb_begin_refuel(100000, "FAST"), "in-flight fast refuel was allowed")
assert(env.a380x_efb_begin_refuel(100000, "INSTANT"), "instant fuel load was rejected")

assert(env.a380x_efb_failure_engine_1 == 0)
assert(env.a380x_efb_toggle_failure("failure_engine_1"))
assert(env.a380x_efb_failure_engine_1 == 6, "failure injection value is not X-Plane failed state")
env.a380x_efb_toggle_failure("failure_engine_1")
assert(env.a380x_efb_failure_engine_1 == 0, "failure repair failed")
assert(not env.a380x_efb_toggle_failure("missing_failure"))

env.a380x_efb_on_ground = 1
assert(env.a380x_efb_apply_preset("TURNAROUND"))
local native_text = table.concat(state.native, "\n")
assert(native_text:find("sim/electrical/battery_1_on", 1, true))
assert(native_text:find("sim/ground_ops/toggle_gpu_request", 1, true))
env.a380x_efb_on_ground = 0
assert(not env.a380x_efb_apply_preset("COLD_AND_DARK"), "airborne aircraft preset was allowed")

for index = 0, 16 do env["a380x_efb_door_" .. index] = 0 end
local command_count = #state.native
assert(env.a380x_efb_toggle_all_doors(), "all-door open command was rejected")
assert(#state.native == command_count + 17, "all-door command did not cover every modeled door")
for index = 0, 16 do
    assert(
        state.native[command_count + index + 1] == "sim/flight_controls/door_toggle_" .. (index + 1),
        "all-door command used an incorrect native channel"
    )
    env["a380x_efb_door_" .. index] = 1
end
command_count = #state.native
assert(env.a380x_efb_toggle_all_doors(), "all-door close command was rejected")
assert(#state.native == command_count + 17, "all-door close omitted a modeled door")

env.a380x_efb_close()
env.a380x_efb_close()
assert(state.destroyed == 1, "EFB window was destroyed twice")
env.a380x_efb_open()
env.PLANE_ICAO = "C172"
env.a380x_efb_update_core()
assert(state.destroyed == 2, "EFB window leaked into another aircraft")

for _, options in ipairs({
    {icao="C172"}, {path="/Aircraft/A380X-XP12-other/"}, {support=false},
}) do
    local _, rejected = host(options)
    assert(#rejected.bindings == 0 and next(rejected.commands) == nil and rejected.created == 0,
        "failed initialization left partial EFB registration")
end

local windows, windows_state = host({path="D:\\X-Plane 12\\Aircraft\\A380X-XP12\\"})
windows.a380x_efb_open()
assert(windows_state.created == 1, "Windows aircraft path guard failed")

local optional, optional_state = host({missing_ref="sim/weather/aircraft/qnh_pas"})
assert(#optional_state.bindings == #state.bindings - 1, "missing optional dataref was not isolated")
optional.a380x_efb_open()
assert(optional_state.created == 1, "missing optional dataref disabled the EFB")

print("EFB mock checks passed: guards, window, payload, fuel, failures, presets and cleanup")
