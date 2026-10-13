-- SPDX-License-Identifier: GPL-3.0-or-later
-- Run with Lua 5.1+; tests the optional native-autopilot floating panel.
local script = assert(arg[1], "pass A380X_XP12_Autopilot.lua")

local function host(options)
    options = options or {}
    local state = {
        bindings = {}, binding_modes = {}, texts = {}, commands = {}, custom_commands = {},
        windows = {}, created = 0, destroyed = 0, messages = {},
    }
    local env = setmetatable({}, {__index = _G})
    env._G = env
    env.PLANE_ICAO = options.icao or "A388"
    env.AIRCRAFT_PATH = options.path or "/Aircraft/A380X-XP12/"
    env.SUPPORTS_FLOATING_WINDOWS = options.support ~= false
    env.graphics = {}
    for _, name in ipairs({"set_color", "set_width", "draw_line", "draw_rectangle"}) do
        env.graphics[name] = function(...) end
    end
    env.require = function(name)
        assert(name == "graphics", "unexpected external dependency")
        if options.graphics_missing then error("missing graphics") end
        return env.graphics
    end
    env.logMsg = function(msg) state.messages[#state.messages + 1] = msg end
    env.XPLMFindDataRef = function(name)
        if name == options.missing_ref then return nil end
        return name
    end
    env.dataref = function(variable, name, mode)
        local expected = name == "sim/aircraft/autopilot/alt_step_ft" and "writable" or "readonly"
        assert(mode == expected, "unexpected autopilot dataref access mode for " .. name)
        state.bindings[#state.bindings + 1] = name
        state.binding_modes[name] = mode
        env[variable] = 0
    end
    env.command_once = function(name) state.commands[#state.commands + 1] = name end
    env.float_wnd_create = function(width, height, style, imgui)
        assert(width == 900 and height == 320 and style == 1 and imgui == false)
        state.created = state.created + 1
        local window = {id = state.created}
        state.windows[window] = true
        state.current = window
        return window
    end
    env.float_wnd_destroy = function(window)
        assert(state.windows[window], "stale or double-destroyed window handle")
        state.windows[window] = nil
        state.destroyed = state.destroyed + 1
        env.a380x_autopilot_onclose(window)
    end
    env.float_wnd_set_title = function(window, _) assert(state.windows[window]) end
    env.float_wnd_set_ondraw = function(window, callback) assert(state.windows[window]); state.draw = callback end
    env.float_wnd_set_onclick = function(window, callback) assert(state.windows[window]); state.click = callback end
    env.float_wnd_set_onclose = function(window, callback) assert(state.windows[window]); state.close = callback end
    env.draw_string_Helvetica_12 = function(_, _, label) state.texts[#state.texts + 1] = label end
    env.create_command = function(name, _, start, hold, finish)
        assert(name:find("a380x/xp12/", 1, true) == 1 and hold == "" and finish == "")
        assert(not state.custom_commands[name], "duplicate custom command " .. name)
        state.custom_commands[name] = start
    end
    env.add_macro = function(_, _, _, initial) assert(initial == "deactivate") end
    env.do_every_frame = function(callback) state.guard = callback end
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
assert(state.created == 0, "window must be closed by default")
assert(#state.bindings == 27, "unexpected native autopilot bindings")
assert(state.binding_modes["sim/aircraft/autopilot/alt_step_ft"] == "writable")
local custom_count = 0
for _ in pairs(state.custom_commands) do custom_count = custom_count + 1 end
assert(custom_count == 12 and state.custom_commands["a380x/xp12/autopilot_panel/toggle"] and state.guard,
    "missing FCU custom commands or host callbacks")
env.a380x_autopilot_open()
env.a380x_autopilot_open()
assert(state.created == 1, "duplicate window created")

env.a380x_ap_power = 1
env.a380x_ap_speed = 250
env.a380x_ap_heading = 359.8
env.a380x_ap_altitude = 38000
env.a380x_ap_vs = -1200
env.a380x_ap_ap1 = 1
env.a380x_ap_hdg_status = 2
env.a380x_ap_alt_status = 1
env[state.draw](state.current, 0, 0)
local output = table.concat(state.texts, "\n")
assert(output:find("---", 1, true), "managed speed and heading must show dashes")
assert(output:find("----", 1, true), "managed V/S must show dashes")
assert(output:find("38000", 1, true), "altitude must remain visible in managed mode")
assert(output:find("LAT HDG", 1, true))
assert(output:find("VERT ALT ARM", 1, true))

env.a380x_fcu_speed_pull()
env.a380x_fcu_heading_pull()
env.a380x_fcu_altitude_pull()
env.a380x_fcu_vertical_pull()
assert(state.commands[#state.commands - 3] == "sim/autopilot/autothrottle_on")
assert(state.commands[#state.commands - 2] == "sim/autopilot/heading")
assert(state.commands[#state.commands - 1] == "sim/autopilot/level_change")
assert(state.commands[#state.commands] == "sim/autopilot/vertical_speed_pre_sel")
state.texts = {}
env[state.draw](state.current, 0, 0)
output = table.concat(state.texts, "\n")
assert(output:find("250", 1, true) and output:find("KT", 1, true))
assert(output:find("000", 1, true), "rounded heading must wrap at 360")
assert(output:find("-1200", 1, true))

env[state.click](state.current, 155, 145, 1)
assert(state.commands[#state.commands] == "sim/autopilot/airspeed_up", "speed selector click failed")
env[state.click](state.current, 365, 65, 1)
assert(state.commands[#state.commands] == "sim/autopilot/servos_toggle", "AP1 click failed")
local count = #state.commands
env[state.click](state.current, 365, 65, 2)
env[state.click](state.current, 895, 315, 1)
assert(#state.commands == count, "drag or outside click issued a command")

env.a380x_ap_mach = 1
env.a380x_ap_speed = 0.84
env.a380x_ap_trk_fpa = 1
env.a380x_ap_fpa = -3.2
state.texts = {}
env[state.draw](state.current, 0, 0)
output = table.concat(state.texts, "\n")
assert(output:find(".84", 1, true) and output:find("-3.2", 1, true))

env.a380x_fcu_speed_push()
env.a380x_fcu_heading_push()
env.a380x_fcu_altitude_push()
env.a380x_fcu_vertical_push()
assert(state.commands[#state.commands - 3] == "sim/autopilot/vnav_spd")
assert(state.commands[#state.commands - 2] == "sim/autopilot/gpss")
assert(state.commands[#state.commands - 1] == "sim/autopilot/vnav")
assert(state.commands[#state.commands] == "sim/autopilot/altitude_hold")

env.a380x_ap_alt_step = 100
env.a380x_fcu_altitude_step_toggle()
assert(env.a380x_ap_alt_step == 1000, "altitude increment did not switch to 1000 feet")
env.a380x_fcu_altitude_step_toggle()
assert(env.a380x_ap_alt_step == 100, "altitude increment did not switch back to 100 feet")

env.a380x_fcu_heading_pull()
env.a380x_ap_heading = 350
env.a380x_ap_mag_var = 15
env.a380x_ap_trk_fpa = 0
env.a380x_fcu_true_mag_toggle()
state.texts = {}
env[state.draw](state.current, 0, 0)
output = table.concat(state.texts, "\n")
assert(output:find("HDG TRUE", 1, true) and output:find("005", 1, true),
    "TRUE display did not apply local magnetic variation")

env.a380x_autopilot_close()
env.a380x_autopilot_close()
assert(state.destroyed == 1)
env.a380x_autopilot_open()
local closed = state.current
state.windows[closed] = nil
env[state.close](closed)
env.a380x_autopilot_close()
assert(state.destroyed == 1, "user-closed window was destroyed twice")
env.a380x_autopilot_open()
env.PLANE_ICAO = "C172"
env.a380x_autopilot_guard()
assert(state.destroyed == 2, "window leaked into another aircraft")

for _, opts in ipairs({
    {icao = "C172"}, {path = "/Aircraft/A380X-XP12-other/"}, {support = false},
    {graphics_missing = true}, {missing_ref = "sim/cockpit2/autopilot/altitude_dial_ft"},
}) do
    local _, rejected = host(opts)
    local registered = 0
    for _ in pairs(rejected.custom_commands) do registered = registered + 1 end
    assert(#rejected.bindings == 0 and rejected.created == 0 and registered == 0,
        "failed initialization left partial registration")
end
local windows, windows_state = host({path = "D:\\X-Plane 12\\Aircraft\\A380X-XP12\\"})
windows.a380x_autopilot_open()
assert(windows_state.created == 1, "Windows path guard failed")
print("Autopilot mock checks passed: A380 FCU push/pull, displays, modes, selectors, lifetime and guards")
