-- SPDX-License-Identifier: GPL-3.0-or-later
-- Run with Lua 5.1+; tests window ownership, aircraft guards, units, horizon,
-- flight-director and FMA behavior. Rendering still needs an in-sim smoke test.
local script = assert(arg[1], "pass A380X_XP12_Avionics.lua")

local function host(options)
    options = options or {}
    local state = {
        bindings = {}, texts = {}, triangles = {}, lines = {}, windows = {},
        commands = {}, created = 0, destroyed = 0, messages = {}, width = 1, color = {1, 1, 1},
    }
    local env = setmetatable({}, {__index = _G})
    env._G = env
    env.PLANE_ICAO = options.icao or "A388"
    env.AIRCRAFT_PATH = options.path or "/Aircraft/A380X-XP12/"
    env.SUPPORTS_FLOATING_WINDOWS = options.support ~= false
    env.graphics = {}
    env.graphics.set_color = function(...) state.color = {...} end
    env.graphics.set_width = function(width) state.width = width end
    env.graphics.draw_line = function(...)
        state.lines[#state.lines + 1] = {coordinates = {...}, width = state.width, color = state.color}
    end
    env.graphics.draw_rectangle = function(...) end
    env.graphics.draw_triangle = function(...)
        local values = {...}
        for _, value in ipairs(values) do
            assert(type(value) == "number" and value == value and math.abs(value) < math.huge, "invalid render coordinate")
        end
        state.triangles[#state.triangles + 1] = values
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
    env.dataref = function(variable, name, mode, index)
        assert(mode == "readonly", "instrument module attempted a writable binding")
        state.bindings[#state.bindings + 1] = {name, mode, index}
        env[variable] = 0
    end
    env.float_wnd_create = function(w, h, style, imgui)
        assert(w == 720 and h == 430 and style == 1 and imgui == false)
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
        env.a380x_avionics_onclose(window)
    end
    env.float_wnd_set_title = function(window, title)
        assert(state.windows[window]); state.title = title
    end
    env.float_wnd_set_ondraw = function(window, callback)
        assert(state.windows[window]); state.draw = callback
    end
    env.float_wnd_set_onclick = function(window, callback)
        assert(state.windows[window]); state.click = callback
    end
    env.float_wnd_set_onclose = function(window, callback)
        assert(state.windows[window]); state.close = callback
    end
    env.draw_string_Helvetica_12 = function(_, _, label) state.texts[#state.texts + 1] = label end
    env.create_command = function(name, _, start, hold, finish)
        assert(hold == "" and finish == "")
        state.commands[name] = start
    end
    env.add_macro = function(_, _, _, initial) assert(initial == "deactivate") end
    env.do_every_frame = function(callback) state.guard = callback end
    -- A reference to any writable native setter or custom dataref is a test failure.
    env.XPLMSetDataf = function() error("unexpected simulator mutation") end
    env.create_dataref_table = function() error("unexpected custom dataref") end
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
assert(#state.bindings == 80, "unexpected native data bindings")
assert(state.commands["a380x/xp12/avionics/toggle"] and state.commands["a380x/xp12/avionics/page"]
    and state.guard, "missing host callbacks")
env.a380x_avionics_toggle()
env.a380x_avionics_open()
assert(state.created == 1, "duplicate window created")
assert(state.click and state.title:find("PFD", 1, true), "page button callback/title missing")
env.a380x_av_ias, env.a380x_av_alt, env.a380x_av_heading = 152.4, 12500, 359.8
env.a380x_av_ff_0, env.a380x_av_fuel = 1.5, 98000
env.a380x_av_ap_power = 1
for index = 0, 4 do env["a380x_av_gear_" .. index] = 1 end
env[state.draw](state.current, 0, 0)
local output = table.concat(state.texts, "\n")
assert(output:find("IAS 152 kt", 1, true))
assert(output:find("ALT 12500 ft", 1, true))
assert(output:find("HDG 000", 1, true), "rounded heading must wrap at 360")
assert(output:find("5400", 1, true), "kg/s to kg/h conversion failed")
assert(output:find("FUEL 98.0 t", 1, true), "kg to tonne conversion failed")
assert(output:find("GEAR DOWN", 1, true))
assert(output:find("SYS >", 1, true), "PFD page did not offer the SYS page")

-- The SYS page is a read-only view over optional native systems datarefs.
env.a380x_av_battery_0, env.a380x_av_battery_1 = 1, 0
env.a380x_av_gpu_gen, env.a380x_av_apu_running, env.a380x_av_apu_gen, env.a380x_av_apu_n1 = 1, 1, 1, 98.4
for index = 0, 3 do
    env["a380x_av_generator_" .. index] = index ~= 2 and 1 or 0
    env["a380x_av_bleed_" .. index] = index % 2
    env["a380x_av_anti_ice_" .. index] = (index + 1) % 2
end
env.a380x_av_pack_left, env.a380x_av_pack_center, env.a380x_av_pack_right = 1, 0, 1
for index = 0, 8 do env["a380x_av_fuel_pump_" .. index] = index % 2 end
env.a380x_avionics_cycle_page()
assert(state.title:find("SYS", 1, true), "page command did not update title")
state.texts = {}
env[state.draw](state.current, 0, 0)
output = table.concat(state.texts, "\n")
for _, expected in ipairs({"NATIVE SYSTEMS MONITOR", "BAT 1 ON", "BAT 2 OFF", "APU ON", "APU GEN ON",
    "APU N1 98 %", "GEN 3 OFF", "ENGINE 4", "BLEED ON", "ANTI-ICE ON", "PACK C OFF", "P9 OFF", "PFD >"}) do
    assert(output:find(expected, 1, true), "missing systems-page indication " .. expected)
end
env[state.click](state.current, 630, 20, 1)
assert(state.title:find("PFD", 1, true), "page button did not cycle to PFD")

-- The FBW-style FMA must preserve active-mode priority while presenting only
-- native X-Plane guidance status. Armed modes are shown independently.
env.a380x_av_at_on, env.a380x_av_ap1, env.a380x_av_fd_mode = 1, 1, 1
env.a380x_av_glideslope_status, env.a380x_av_vnav_status = 2, 2
env.a380x_av_nav_status, env.a380x_av_gpss_status = 2, 2
env.a380x_av_altitude_status, env.a380x_av_heading_status = 1, 1
state.texts = {}
env[state.draw](state.current, 0, 0)
output = table.concat(state.texts, "\n")
assert(output:find("G/S", 1, true) and not output:find("VNAV", 1, true), "vertical FMA priority failed")
assert(output:find("LOC", 1, true) and not output:find("NAV", 1, true), "lateral FMA priority failed")
for _, expected in ipairs({"A/THR", "ALT", "HDG", "AP1", "FD1"}) do
    assert(output:find(expected, 1, true), "missing FMA indication " .. expected)
end

-- Native X-Plane FD deflections drive the translated FBW command-bar shape.
env.a380x_av_fd_bars, env.a380x_av_fd_pitch, env.a380x_av_fd_roll = 1, 10, -20
state.lines = {}
env[state.draw](state.current, 0, 0)
local fd_lines = {}
for _, rendered in ipairs(state.lines) do
    if rendered.width == 3 then fd_lines[#fd_lines + 1] = rendered.coordinates end
end
assert(#fd_lines == 2, "flight director did not draw two command bars")
assert(math.abs(fd_lines[1][1] - 139) < 0.001 and math.abs(fd_lines[1][3] - 139) < 0.001,
    "roll command bar scale/sign is wrong")
assert(math.abs(fd_lines[2][2] - 212) < 0.001 and math.abs(fd_lines[2][4] - 212) < 0.001,
    "pitch command bar scale/sign is wrong")

-- Every horizon must partition the same rectangle and stay inside its bounds,
-- including 90-degree bank, inverted flight, and extreme pitch.
for _, attitude in ipairs({{0, 0}, {10, 0}, {-10, 30}, {0, 90}, {0, -90}, {0, 180}, {89, 80}, {-89, -170}}) do
    env.a380x_av_pitch, env.a380x_av_roll = attitude[1], attitude[2]
    state.triangles = {}
    env[state.draw](state.current, 0, 0)
    local area = 0
    for _, t in ipairs(state.triangles) do
        for i = 1, 5, 2 do
            assert(t[i] >= 28 - 1e-6 and t[i] <= 278 + 1e-6, "horizon escaped horizontal clip")
            assert(t[i + 1] >= 106 - 1e-6 and t[i + 1] <= 290 + 1e-6, "horizon escaped vertical clip")
        end
        area = area + math.abs((t[3] - t[1]) * (t[6] - t[2]) - (t[5] - t[1]) * (t[4] - t[2])) / 2
    end
    assert(math.abs(area - 250 * 184) < 0.001, "sky/ground did not cover the horizon")
end
env.a380x_av_pitch, env.a380x_av_ias = 0 / 0, math.huge
env.a380x_av_gear_2 = nil
env.a380x_av_fd_pitch = 0 / 0
state.texts, state.triangles, state.lines = {}, {}, {}
env[state.draw](state.current, 0, 0)
output = table.concat(state.texts, "\n")
assert(output:find("ATT DATA", 1, true) and output:find("IAS --- kt", 1, true))
assert(output:find("GEAR DATA", 1, true), "missing gear data must not show UP/DOWN")
assert(output:find("FD DATA", 1, true), "invalid flight-director input must be annunciated")
assert(#state.triangles == 0, "invalid attitude should not draw a false horizon")

env.a380x_av_pitch, env.a380x_av_fd_pitch = 0, 0
env.a380x_av_ap_power = 0
state.texts, state.lines = {}, {}
env[state.draw](state.current, 0, 0)
output = table.concat(state.texts, "\n")
assert(output:find("AP POWER", 1, true), "unpowered native guidance was not flagged")
for _, forbidden in ipairs({"G/S", "LOC", "AP1", "FD1"}) do
    assert(not output:find(forbidden, 1, true), "unpowered FMA showed " .. forbidden)
end
for _, rendered in ipairs(state.lines) do
    assert(rendered.width ~= 3, "unpowered flight director remained visible")
end

env.a380x_avionics_close()
env.a380x_avionics_close()
assert(state.destroyed == 1)
env.a380x_avionics_open()
local closed = state.current
state.windows[closed] = nil -- Window manager destroyed it before on-close callback.
env[state.close](closed)
env.a380x_avionics_close()
assert(state.destroyed == 1, "user-closed window was destroyed twice")
env.a380x_avionics_open()
env.PLANE_ICAO = "C172"
env.a380x_avionics_guard()
assert(state.destroyed == 2, "window leaked into another aircraft")
env.a380x_avionics_open()
assert(state.created == 3)

for _, opts in ipairs({{icao = "C172"}, {path = "/Aircraft/A380X-XP12-other/"}, {support = false}, {graphics_missing = true}, {missing_ref = "sim/cockpit2/engine/indicators/N1_percent"}}) do
    local _, rejected = host(opts)
    assert(#rejected.bindings == 0 and rejected.created == 0 and next(rejected.commands) == nil,
        "failed initialization left partial registration")
end

local optional, optional_state = host({missing_ref = "sim/cockpit2/bleedair/actuators/pack_center"})
assert(#optional_state.bindings == 79 and optional_state.commands["a380x/xp12/avionics/page"],
    "missing optional dataref disabled the instrument")
optional.a380x_avionics_open()
optional.a380x_avionics_cycle_page()
optional_state.texts = {}
optional[optional_state.draw](optional_state.current, 0, 0)
assert(table.concat(optional_state.texts, "\n"):find("PACK C DATA", 1, true),
    "missing optional dataref was not isolated")
local windows, windows_state = host({path = "D:\\X-Plane 12\\Aircraft\\A380X-XP12\\"})
windows.a380x_avionics_open()
assert(windows_state.created == 1, "Windows path guard failed")
print("Avionics mock checks passed: read-only PFD/SYS pages, optional refs, FD/FMA, units, clipping, power and guards")
