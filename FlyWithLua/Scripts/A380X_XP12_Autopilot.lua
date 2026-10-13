-- SPDX-License-Identifier: GPL-3.0-or-later
-- Airbus-style A380 FCU interaction layer for the X-Plane 12 development port.
-- Selected/managed actions are mapped to the closest native X-Plane guidance
-- modes; this is not a port of the FlyByWire flight-guidance computers.

local function target_aircraft()
    local path = (AIRCRAFT_PATH or ""):gsub("\\", "/")
    return PLANE_ICAO == "A388" and (path .. "/"):find("/A380X%-XP12/") ~= nil
end

local function message(value)
    if type(logMsg) == "function" then logMsg("A380X FCU: " .. value) end
end

if not target_aircraft() then return end
local required_api = {
    "dataref", "XPLMFindDataRef", "command_once", "float_wnd_create", "float_wnd_destroy",
    "float_wnd_set_title", "float_wnd_set_ondraw", "float_wnd_set_onclick", "float_wnd_set_onclose",
    "draw_string_Helvetica_12", "create_command", "add_macro", "do_every_frame",
}
if not SUPPORTS_FLOATING_WINDOWS then
    message("FlyWithLua NG floating windows are required; module disabled")
    return
end
for _, name in ipairs(required_api) do
    if type(_G[name]) ~= "function" then
        message("missing API " .. name .. "; module disabled")
        return
    end
end
local graphics_ok = pcall(require, "graphics")
local gfx = graphics
if not graphics_ok or type(gfx) ~= "table" then
    message("graphics module unavailable; module disabled")
    return
end
for _, name in ipairs({"set_color", "set_width", "draw_line", "draw_rectangle"}) do
    if type(gfx[name]) ~= "function" then
        message("graphics API unavailable: " .. name .. "; module disabled")
        return
    end
end

local bindings = {
    {"power", "sim/cockpit2/autopilot/autopilot_has_power"},
    {"speed", "sim/cockpit2/autopilot/airspeed_dial_kts_mach"},
    {"mach", "sim/cockpit2/autopilot/airspeed_is_mach"},
    {"heading", "sim/cockpit2/autopilot/heading_dial_deg_mag_pilot"},
    {"mag_var", "sim/flightmodel/position/magnetic_variation"},
    {"vs", "sim/cockpit2/autopilot/vvi_dial_fpm"},
    {"fpa", "sim/cockpit2/autopilot/fpa"},
    {"altitude", "sim/cockpit2/autopilot/altitude_dial_ft"},
    {"alt_step", "sim/aircraft/autopilot/alt_step_ft", "writable"},
    {"ap1", "sim/cockpit2/autopilot/autopilot_on"},
    {"ap2", "sim/cockpit2/autopilot/autopilot2_on"},
    {"fd", "sim/cockpit2/autopilot/flight_director_mode"},
    {"at_mode", "sim/cockpit2/autopilot/autothrottle_enabled"},
    {"at_arm", "sim/cockpit2/autopilot/autothrottle_arm"},
    {"hdg_status", "sim/cockpit2/autopilot/heading_status"},
    {"trk_status", "sim/cockpit2/autopilot/track_status"},
    {"nav_status", "sim/cockpit2/autopilot/nav_status"},
    {"gpss_status", "sim/cockpit2/autopilot/gpss_status"},
    {"appr_status", "sim/cockpit2/autopilot/approach_status"},
    {"gs_status", "sim/cockpit2/autopilot/glideslope_status"},
    {"alt_status", "sim/cockpit2/autopilot/altitude_hold_status"},
    {"vs_status", "sim/cockpit2/autopilot/vvi_status"},
    {"fpa_status", "sim/cockpit2/autopilot/fpa_status"},
    {"spd_status", "sim/cockpit2/autopilot/speed_status"},
    {"vnav_status", "sim/cockpit2/autopilot/vnav_status"},
    {"vnav_speed_status", "sim/cockpit2/autopilot/vnav_speed_status"},
    {"trk_fpa", "sim/cockpit2/autopilot/trk_fpa"},
}
for _, binding in ipairs(bindings) do
    if not XPLMFindDataRef(binding[2]) then
        message("missing dataref " .. binding[2] .. "; module disabled")
        return
    end
end
for _, binding in ipairs(bindings) do
    dataref("a380x_ap_" .. binding[1], binding[2], binding[3] or "readonly")
end

local window = nil
local WIDTH, HEIGHT = 900, 320
local speed_managed = true
local heading_managed = true
local altitude_managed = true
local vertical_managed = true
local true_reference = false
local metric_altitude = false

local function finite(value)
    return type(value) == "number" and value == value and math.abs(value) < math.huge
end

local function reading(key)
    local value = _G["a380x_ap_" .. key]
    return finite(value) and value or nil
end

local function native(command)
    if target_aircraft() then command_once(command) end
end

-- FCU push/pull semantics from the pinned FlyByWire A380 source. The native
-- commands below are the nearest X-Plane equivalents, not full Airbus laws.
function a380x_fcu_speed_push()
    speed_managed = true
    native("sim/autopilot/vnav_spd")
end

function a380x_fcu_speed_pull()
    speed_managed = false
    native("sim/autopilot/autothrottle_on")
end

function a380x_fcu_heading_push()
    heading_managed = true
    native("sim/autopilot/gpss")
end

function a380x_fcu_heading_pull()
    heading_managed = false
    native("sim/autopilot/heading")
end

function a380x_fcu_altitude_push()
    altitude_managed = true
    native("sim/autopilot/vnav")
end

function a380x_fcu_altitude_pull()
    altitude_managed = false
    native("sim/autopilot/level_change")
end

function a380x_fcu_vertical_push()
    vertical_managed = true
    native("sim/autopilot/altitude_hold")
end

function a380x_fcu_vertical_pull()
    vertical_managed = false
    native((reading("trk_fpa") or 0) > 0 and "sim/autopilot/fpa" or "sim/autopilot/vertical_speed_pre_sel")
end

function a380x_fcu_true_mag_toggle()
    true_reference = not true_reference
end

function a380x_fcu_metric_altitude_toggle()
    metric_altitude = not metric_altitude
end

function a380x_fcu_altitude_step_toggle()
    local step = reading("alt_step") or 100
    a380x_ap_alt_step = step >= 550 and 100 or 1000
end

local controls = {
    -- Top-row presentation controls.
    {x=32,  y=280, w=92, h=26, label="MACH / SPD", command="sim/autopilot/knots_mach_toggle", state="mach"},
    {x=250, y=280, w=92, h=26, label="TRUE / MAG", callback="a380x_fcu_true_mag_toggle", state="true_ref"},
    {x=402, y=280, w=116, h=26, label="HDG/VS TRK/FPA", command="sim/autopilot/trkfpa", state="trk_fpa"},
    {x=770, y=280, w=92, h=26, label="METER", callback="a380x_fcu_metric_altitude_toggle", state="metric"},

    -- Rotary-selector approximations. Rotation changes the selected value even
    -- while the FCU display remains managed, matching the push/pull workflow.
    {x=45,  y=132, w=42, h=30, label="-", command="sim/autopilot/airspeed_down"},
    {x=143, y=132, w=42, h=30, label="+", command="sim/autopilot/airspeed_up"},
    {x=250, y=132, w=42, h=30, label="-", command="sim/autopilot/heading_down"},
    {x=348, y=132, w=42, h=30, label="+", command="sim/autopilot/heading_up"},
    {x=530, y=132, w=42, h=30, label="-", command="sim/autopilot/altitude_down"},
    {x=638, y=132, w=42, h=30, label="+", command="sim/autopilot/altitude_up"},
    {x=735, y=132, w=42, h=30, label="-", command="sim/autopilot/vertical_speed_down"},
    {x=833, y=132, w=42, h=30, label="+", command="sim/autopilot/vertical_speed_up"},

    -- Explicit push/pull hit areas provide the selector behavior that X-Plane's
    -- stock command-knob manipulator cannot express on a single 3-D mesh.
    {x=42,  y=94, w=66, h=25, label="PUSH", callback="a380x_fcu_speed_push", state="spd_managed"},
    {x=122, y=94, w=66, h=25, label="PULL", callback="a380x_fcu_speed_pull", state="spd_selected"},
    {x=247, y=94, w=66, h=25, label="PUSH", callback="a380x_fcu_heading_push", state="hdg_managed"},
    {x=327, y=94, w=66, h=25, label="PULL", callback="a380x_fcu_heading_pull", state="hdg_selected"},
    {x=527, y=94, w=66, h=25, label="PUSH", callback="a380x_fcu_altitude_push", state="alt_managed"},
    {x=607, y=94, w=66, h=25, label="PULL", callback="a380x_fcu_altitude_pull", state="alt_selected"},
    {x=732, y=94, w=66, h=25, label="PUSH", callback="a380x_fcu_vertical_push", state="vs_managed"},
    {x=812, y=94, w=66, h=25, label="PULL", callback="a380x_fcu_vertical_pull", state="vs_selected"},

    -- FCU mode buttons, arranged to echo the physical unit.
    {x=205, y=45, w=72, h=34, label="LOC", command="sim/autopilot/NAV", state="nav"},
    {x=353, y=53, w=62, h=34, label="AP1", command="sim/autopilot/servos_toggle", state="ap1"},
    {x=425, y=53, w=62, h=34, label="AP2", command="sim/autopilot/servos2_toggle", state="ap2"},
    {x=497, y=53, w=76, h=34, label="A/THR", command="sim/autopilot/autothrottle_toggle", state="athr"},
    {x=620, y=45, w=72, h=34, label="ALT", command="sim/autopilot/altitude_hold", state="alt"},
    {x=786, y=45, w=78, h=34, label="APPR", command="sim/autopilot/approach", state="appr"},
    {x=420, y=132, w=72, h=34, label="FD", command="sim/autopilot/fdir_toggle", state="fd"},
    {x=25,  y=45, w=96, h=34, label="AP DISC", command="sim/autopilot/servos_off_any", state="disconnect"},
    {x=585, y=280, w=112, h=26, label="ALT 100 / 1000", callback="a380x_fcu_altitude_step_toggle", state="alt_step"},
}

local function text(x, y, label, r, g, b)
    gfx.set_color(r or 0.86, g or 0.90, b or 0.96)
    draw_string_Helvetica_12(x, y, label)
end

local function border(x, y, width, height, r, g, b)
    gfx.set_color(r, g, b)
    gfx.draw_line(x, y, x + width, y)
    gfx.draw_line(x + width, y, x + width, y + height)
    gfx.draw_line(x + width, y + height, x, y + height)
    gfx.draw_line(x, y + height, x, y)
end

local function status(key)
    local value = reading(key) or 0
    if value >= 2 then return 2 end
    if value >= 1 then return 1 end
    return 0
end

local function control_state(key)
    if key == nil then return 0 end
    if key == "disconnect" then return -1 end
    if key == "spd_managed" then return speed_managed and 2 or 0 end
    if key == "spd_selected" then return speed_managed and 0 or 2 end
    if key == "hdg_managed" then return heading_managed and 2 or 0 end
    if key == "hdg_selected" then return heading_managed and 0 or 2 end
    if key == "alt_managed" then return altitude_managed and 2 or 0 end
    if key == "alt_selected" then return altitude_managed and 0 or 2 end
    if key == "vs_managed" then return vertical_managed and 2 or 0 end
    if key == "vs_selected" then return vertical_managed and 0 or 2 end
    if key == "true_ref" then return true_reference and 2 or 0 end
    if key == "metric" then return metric_altitude and 2 or 0 end
    if key == "alt_step" then return (reading("alt_step") or 100) >= 550 and 2 or 0 end
    if key == "alt" then return status("alt_status") end
    if key == "nav" then return math.max(status("nav_status"), status("gpss_status")) end
    if key == "appr" then return status("appr_status") end
    if key == "trk_fpa" or key == "mach" or key == "ap1" or key == "ap2" then
        return (reading(key) or 0) > 0 and 2 or 0
    end
    if key == "fd" then return (reading("fd") or 0) > 0 and 2 or 0 end
    if key == "athr" then
        if (reading("at_mode") or 0) > 0 then return 2 end
        return (reading("at_arm") or 0) > 0 and 1 or 0
    end
    return 0
end

local function draw_button(origin_x, origin_y, control)
    local state = control_state(control.state)
    if state == 2 then gfx.set_color(0.025, 0.16, 0.055)
    elseif state == 1 then gfx.set_color(0.025, 0.10, 0.18)
    elseif state == -1 then gfx.set_color(0.25, 0.035, 0.025)
    else gfx.set_color(0.035, 0.045, 0.055) end
    gfx.draw_rectangle(origin_x + control.x, origin_y + control.y,
        origin_x + control.x + control.w, origin_y + control.y + control.h)
    local br, bg, bb = 0.29, 0.35, 0.39
    if state == 2 then br, bg, bb = 0.20, 0.95, 0.32
    elseif state == 1 then br, bg, bb = 0.22, 0.70, 1
    elseif state == -1 then br, bg, bb = 1, 0.28, 0.20 end
    border(origin_x + control.x, origin_y + control.y, control.w, control.h, br, bg, bb)
    text(origin_x + control.x + math.max(4, (control.w - #control.label * 7) / 2),
        origin_y + control.y + math.floor((control.h - 10) / 2), control.label, br, bg, bb)
end

local function display_panel(origin_x, origin_y, x, width)
    gfx.set_color(0.035, 0.025, 0.45)
    gfx.draw_rectangle(origin_x + x, origin_y + 190, origin_x + x + width, origin_y + 268)
    border(origin_x + x, origin_y + 190, width, 78, 0.18, 0.28, 0.78)
end

local function display_value(origin_x, origin_y, x, title, value, unit)
    text(origin_x + x, origin_y + 244, title, 0.72, 0.76, 1)
    text(origin_x + x, origin_y + 214, value, 0.93, 0.95, 1)
    if unit and unit ~= "" then text(origin_x + x + 92, origin_y + 214, unit, 0.60, 0.64, 0.95) end
end

local function selected_speed()
    if speed_managed then return "---", "" end
    local speed = reading("speed")
    if speed == nil then return "---", "" end
    if (reading("mach") or 0) > 0 then return string.format(".%02.0f", speed * 100), "" end
    return string.format("%03.0f", speed), "KT"
end

local function selected_heading()
    if heading_managed then return "---" end
    local heading = reading("heading")
    if heading == nil then return "---" end
    if true_reference then heading = heading + (reading("mag_var") or 0) end
    return string.format("%03.0f", math.floor(heading + 0.5) % 360)
end

local function selected_altitude()
    local altitude = reading("altitude")
    return altitude and string.format("%05.0f", altitude) or "-----"
end

local function selected_vertical()
    if vertical_managed then return "----" end
    if (reading("trk_fpa") or 0) > 0 then
        local fpa = reading("fpa")
        return fpa and string.format("%+.1f", fpa) or "----"
    end
    local vs = reading("vs")
    return vs and string.format("%+05.0f", vs) or "----"
end

local function mode_name(candidates)
    for _, candidate in ipairs(candidates) do
        local mode_status = status(candidate[1])
        if mode_status > 0 then return candidate[2] .. (mode_status == 1 and " ARM" or "") end
    end
    return "---"
end

function a380x_autopilot_draw(_, x, y)
    if not window or not target_aircraft() then return end
    gfx.set_width(1)
    gfx.set_color(0.055, 0.070, 0.080)
    gfx.draw_rectangle(x, y, x + WIDTH, y + HEIGHT)
    display_panel(x, y, 32, 370)
    display_panel(x, y, 515, 350)
    local speed, speed_unit = selected_speed()
    display_value(x, y, 48, (reading("mach") or 0) > 0 and "MACH" or "SPD", speed, speed_unit)
    display_value(x, y, 248, ((reading("trk_fpa") or 0) > 0 and "TRK" or "HDG") .. (true_reference and " TRUE" or ""), selected_heading(), "")
    display_value(x, y, 532, "ALT", selected_altitude(), "")
    display_value(x, y, 730, (reading("trk_fpa") or 0) > 0 and "FPA" or "V/S", selected_vertical(), "")
    for _, control in ipairs(controls) do draw_button(x, y, control) end

    text(x + 100, y + 171, "SPD SELECTOR", 0.62, 0.68, 0.71)
    text(x + 302, y + 171, "HDG SELECTOR", 0.62, 0.68, 0.71)
    text(x + 582, y + 171, "ALT SELECTOR", 0.62, 0.68, 0.71)
    text(x + 787, y + 171, (reading("trk_fpa") or 0) > 0 and "FPA SELECTOR" or "V/S SELECTOR", 0.62, 0.68, 0.71)

    local lateral = mode_name({
        {"appr_status", "APPR"}, {"gpss_status", "NAV"}, {"nav_status", "LOC"},
        {"trk_status", "TRK"}, {"hdg_status", "HDG"},
    })
    local vertical = mode_name({
        {"gs_status", "G/S"}, {"vnav_status", "VNAV"}, {"alt_status", "ALT"},
        {"fpa_status", "FPA"}, {"vs_status", "V/S"}, {"vnav_speed_status", "VNAV SPD"},
    })
    text(x + 138, y + 68, "LAT " .. lateral, 0.24, 0.95, 0.36)
    text(x + 704, y + 68, "VERT " .. vertical, 0.24, 0.95, 0.36)
    local power = reading("power")
    text(x + 18, y + 12, power == 0 and "AP POWER OFF" or "A380 FCU / NATIVE GUIDANCE ADAPTER",
        power == 0 and 1 or 0.55, power == 0 and 0.32 or 0.66, power == 0 and 0.20 or 0.70)
    text(x + 650, y + 12, "Airbus interaction; X-Plane guidance", 0.54, 0.59, 0.63)
    gfx.set_color(1, 1, 1)
end

function a380x_autopilot_click(_, x, y, state)
    if state ~= 1 or not window or not target_aircraft() then return end
    for _, control in ipairs(controls) do
        if x >= control.x and x <= control.x + control.w
            and y >= control.y and y <= control.y + control.h then
            if control.command then native(control.command)
            elseif control.callback and type(_G[control.callback]) == "function" then _G[control.callback]() end
            return
        end
    end
end

function a380x_autopilot_close()
    local old = window
    window = nil
    if old then float_wnd_destroy(old) end
end

function a380x_autopilot_onclose(closed)
    if window == closed then window = nil end
end

function a380x_autopilot_open()
    if not target_aircraft() or window then return end
    window = float_wnd_create(WIDTH, HEIGHT, 1, false)
    if not window then message("window creation failed"); return end
    float_wnd_set_title(window, "A380X - Airbus-style FCU")
    float_wnd_set_ondraw(window, "a380x_autopilot_draw")
    float_wnd_set_onclick(window, "a380x_autopilot_click")
    float_wnd_set_onclose(window, "a380x_autopilot_onclose")
end

function a380x_autopilot_toggle()
    if window then a380x_autopilot_close() else a380x_autopilot_open() end
end

function a380x_autopilot_guard()
    if window and not target_aircraft() then a380x_autopilot_close() end
end

local custom_commands = {
    {"a380x/xp12/fcu/speed_push", "A380X FCU: push speed for managed guidance", "a380x_fcu_speed_push"},
    {"a380x/xp12/fcu/speed_pull", "A380X FCU: pull speed for selected guidance", "a380x_fcu_speed_pull"},
    {"a380x/xp12/fcu/heading_push", "A380X FCU: push heading for managed NAV", "a380x_fcu_heading_push"},
    {"a380x/xp12/fcu/heading_pull", "A380X FCU: pull heading for selected guidance", "a380x_fcu_heading_pull"},
    {"a380x/xp12/fcu/altitude_push", "A380X FCU: push altitude for managed VNAV", "a380x_fcu_altitude_push"},
    {"a380x/xp12/fcu/altitude_pull", "A380X FCU: pull altitude for selected open climb/descent", "a380x_fcu_altitude_pull"},
    {"a380x/xp12/fcu/vertical_push", "A380X FCU: push vertical selector to level off", "a380x_fcu_vertical_push"},
    {"a380x/xp12/fcu/vertical_pull", "A380X FCU: pull vertical selector for selected V/S or FPA", "a380x_fcu_vertical_pull"},
    {"a380x/xp12/fcu/true_mag_toggle", "A380X FCU: toggle true or magnetic heading display", "a380x_fcu_true_mag_toggle"},
    {"a380x/xp12/fcu/metric_altitude_toggle", "A380X FCU: toggle metric altitude indication", "a380x_fcu_metric_altitude_toggle"},
    {"a380x/xp12/fcu/altitude_step_toggle", "A380X FCU: toggle 100 or 1000 foot altitude step", "a380x_fcu_altitude_step_toggle"},
    {"a380x/xp12/autopilot_panel/toggle", "A380X: toggle Airbus-style FCU panel", "a380x_autopilot_toggle"},
}
for _, command in ipairs(custom_commands) do
    create_command(command[1], command[2], command[3] .. "()", "", "")
end
add_macro("A380X: Airbus-style FCU panel", "a380x_autopilot_open()", "a380x_autopilot_close()", "deactivate")
do_every_frame("a380x_autopilot_guard()")
message("ready; bind a380x/xp12/autopilot_panel/toggle to open (closed by default)")
