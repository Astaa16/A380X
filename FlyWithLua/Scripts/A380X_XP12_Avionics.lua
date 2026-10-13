-- SPDX-License-Identifier: GPL-3.0-or-later
-- Optional native X-Plane development instruments. The flight-director bar
-- geometry and FMA presentation are adapted from the pinned FBW A380 PFD, but
-- their inputs are X-Plane guidance datarefs rather than FBW/MSFS systems.
-- This is a floating test window and is not rendered on the 3D cockpit screens.

local function target_aircraft()
    local path = (AIRCRAFT_PATH or ""):gsub("\\", "/")
    return PLANE_ICAO == "A388" and (path .. "/"):find("/A380X%-XP12/") ~= nil
end

local function message(text)
    if type(logMsg) == "function" then logMsg("A380X instruments: " .. text) end
end

if not target_aircraft() then return end
local required_api = {
    "dataref", "XPLMFindDataRef", "float_wnd_create", "float_wnd_destroy",
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
for _, name in ipairs({"set_color", "set_width", "draw_line", "draw_rectangle", "draw_triangle"}) do
    if type(gfx[name]) ~= "function" then
        message("graphics API unavailable: " .. name .. "; module disabled")
        return
    end
end

local bindings = {
    {"ias", "sim/cockpit2/gauges/indicators/airspeed_kts_pilot"},
    {"alt", "sim/cockpit2/gauges/indicators/altitude_ft_pilot"},
    {"vs", "sim/cockpit2/gauges/indicators/vvi_fpm_pilot"},
    {"pitch", "sim/cockpit2/gauges/indicators/pitch_electric_deg_pilot"},
    {"roll", "sim/cockpit2/gauges/indicators/roll_electric_deg_pilot"},
    {"heading", "sim/cockpit2/gauges/indicators/heading_AHARS_deg_mag_pilot"},
    {"ra", "sim/cockpit2/gauges/indicators/radio_altimeter_height_ft_pilot"},
    {"fuel", "sim/flightmodel/weight/m_fuel_total"},
    {"flaps", "sim/cockpit2/controls/flap_handle_request_ratio"},
    {"ap_power", "sim/cockpit2/autopilot/autopilot_has_power"},
    {"fd_mode", "sim/cockpit2/autopilot/flight_director_mode"},
    {"fd_bars", "sim/cockpit2/autopilot/flight_director_command_bars_pilot"},
    {"fd_pitch", "sim/cockpit2/autopilot/flight_director_pitch_deg"},
    {"fd_roll", "sim/cockpit2/autopilot/flight_director_roll_deg"},
    {"ap1", "sim/cockpit2/autopilot/autopilot_on"},
    {"ap2", "sim/cockpit2/autopilot/autopilot2_on"},
    {"at_on", "sim/cockpit2/autopilot/autothrottle_on"},
    {"at_arm", "sim/cockpit2/autopilot/autothrottle_arm"},
    {"roll_status", "sim/cockpit2/autopilot/roll_status"},
    {"heading_status", "sim/cockpit2/autopilot/heading_status"},
    {"track_status", "sim/cockpit2/autopilot/track_status"},
    {"nav_status", "sim/cockpit2/autopilot/nav_status"},
    {"gpss_status", "sim/cockpit2/autopilot/gpss_status"},
    {"toga_lateral_status", "sim/cockpit2/autopilot/TOGA_lateral_status"},
    {"pitch_status", "sim/cockpit2/autopilot/pitch_status"},
    {"vs_status", "sim/cockpit2/autopilot/vvi_status"},
    {"fpa_status", "sim/cockpit2/autopilot/fpa_status"},
    {"speed_status", "sim/cockpit2/autopilot/speed_status"},
    {"altitude_status", "sim/cockpit2/autopilot/altitude_hold_status"},
    {"glideslope_status", "sim/cockpit2/autopilot/glideslope_status"},
    {"vnav_status", "sim/cockpit2/autopilot/vnav_status"},
    {"vnav_speed_status", "sim/cockpit2/autopilot/vnav_speed_status"},
    {"toga_vertical_status", "sim/cockpit2/autopilot/TOGA_status"},
}
for index = 0, 3 do
    bindings[#bindings + 1] = {"n1_" .. index, "sim/cockpit2/engine/indicators/N1_percent", index}
    bindings[#bindings + 1] = {"n2_" .. index, "sim/cockpit2/engine/indicators/N2_percent", index}
    bindings[#bindings + 1] = {"ff_" .. index, "sim/cockpit2/engine/indicators/fuel_flow_kg_sec", index}
end
for index = 0, 4 do
    bindings[#bindings + 1] = {"gear_" .. index, "sim/flightmodel2/gear/deploy_ratio", index}
end
-- Validate every name before binding: a missing binding can quarantine an
-- entire FlyWithLua script. Resolve once, outside rendering callbacks.
for _, binding in ipairs(bindings) do
    if not XPLMFindDataRef(binding[2]) then
        message("missing dataref " .. binding[2] .. "; module disabled")
        return
    end
end
for _, binding in ipairs(bindings) do
    if binding[3] ~= nil then
        dataref("a380x_av_" .. binding[1], binding[2], "readonly", binding[3])
    else
        dataref("a380x_av_" .. binding[1], binding[2], "readonly")
    end
end

-- These native systems are an optional development view. Unlike the core PFD
-- inputs above, a missing system dataref must not quarantine the whole script:
-- older X-Plane/FlyWithLua combinations render DATA for just that item.
local optional_bindings = {
    {"apu_gen", "sim/cockpit2/electrical/APU_generator_on"},
    {"apu_running", "sim/cockpit2/electrical/APU_running"},
    {"apu_n1", "sim/cockpit2/electrical/APU_N1_percent"},
    {"gpu_gen", "sim/cockpit2/electrical/GPU_generator_on"},
    {"pack_left", "sim/cockpit2/bleedair/actuators/pack_left"},
    {"pack_center", "sim/cockpit2/bleedair/actuators/pack_center"},
    {"pack_right", "sim/cockpit2/bleedair/actuators/pack_right"},
}
for index = 0, 1 do
    optional_bindings[#optional_bindings + 1] = {"battery_" .. index,
        "sim/cockpit2/electrical/battery_on", index}
end
for index = 0, 3 do
    optional_bindings[#optional_bindings + 1] = {"generator_" .. index,
        "sim/cockpit2/electrical/generator_on", index}
    optional_bindings[#optional_bindings + 1] = {"bleed_" .. index,
        "sim/cockpit2/bleedair/actuators/engine_bleed_sov", index}
    optional_bindings[#optional_bindings + 1] = {"anti_ice_" .. index,
        "sim/cockpit2/ice/ice_inlet_heat_on_per_engine", index}
end
for index = 0, 8 do
    optional_bindings[#optional_bindings + 1] = {"fuel_pump_" .. index,
        "sim/cockpit2/fuel/fuel_tank_pump_on", index}
end
for _, binding in ipairs(optional_bindings) do
    if XPLMFindDataRef(binding[2]) then
        if binding[3] ~= nil then
            dataref("a380x_av_" .. binding[1], binding[2], "readonly", binding[3])
        else
            dataref("a380x_av_" .. binding[1], binding[2], "readonly")
        end
    else
        message("optional dataref unavailable " .. binding[2])
    end
end

local window = nil -- Closed by default, including after reloading scripts.
local page = 1
local page_names = {"PFD", "SYS"}
local function finite(value)
    return type(value) == "number" and value == value and math.abs(value) < math.huge
end
local function reading(key)
    local value = _G["a380x_av_" .. key]
    return finite(value) and value or nil
end
local function number(key, format, scale)
    local value = reading(key)
    if value == nil then return "---" end
    return string.format(format or "%.0f", value * (scale or 1))
end
local function text(x, y, label, r, g, b)
    gfx.set_color(r or 0.88, g or 0.93, b or 1)
    draw_string_Helvetica_12(x, y, label)
end
local function line(x1, y1, x2, y2, r, g, b)
    gfx.set_color(r or 1, g or 1, b or 1)
    gfx.draw_line(x1, y1, x2, y2)
end

local function system_state(key)
    local value = reading(key)
    if value == nil then return "DATA", 1, 0.65, 0 end
    if value > 0.5 then return "ON", 0.24, 0.95, 0.36 end
    return "OFF", 0.58, 0.64, 0.68
end

local function draw_system_state(x, y, label, key)
    local value, r, g, b = system_state(key)
    text(x, y, label .. " " .. value, r, g, b)
end

local function status(key)
    local value = reading(key)
    if value == nil then return 0 end
    return math.max(0, math.min(2, math.floor(value + 0.5)))
end

-- FBW's FMA definitions select one active mode in a strict priority order.
-- X-Plane exposes mode status directly (0 off, 1 armed, 2 captured), so this
-- adapter keeps the same presentation rule without claiming FBW FG behavior.
local function first_mode(candidates, wanted_status)
    for _, candidate in ipairs(candidates) do
        if status(candidate[1]) == wanted_status then return candidate[2] end
    end
    return ""
end

local lateral_modes = {
    {"toga_lateral_status", "GA TRK"},
    {"nav_status", "LOC"},
    {"gpss_status", "NAV"},
    {"heading_status", "HDG"},
    {"track_status", "TRACK"},
    {"roll_status", "ROLL"},
}
local vertical_modes = {
    {"glideslope_status", "G/S"},
    {"toga_vertical_status", "SRS"},
    {"vnav_status", "VNAV"},
    {"altitude_status", "ALT"},
    {"fpa_status", "FPA"},
    {"vs_status", "V/S"},
    {"vnav_speed_status", "VNAV SPD"},
    {"speed_status", "SPEED"},
    {"pitch_status", "PITCH"},
}

local function draw_fma(x, y)
    local left, bottom, right, top = x + 18, y + 360, x + 702, y + 423
    gfx.set_color(0.025, 0.035, 0.045)
    gfx.draw_rectangle(left, bottom, right, top)
    for _, separator in ipairs({150, 310, 470, 570}) do
        line(x + separator, bottom, x + separator, top, 0.28, 0.32, 0.35)
    end
    text(x + 24, y + 408, "A/THR", 0.58, 0.64, 0.68)
    text(x + 170, y + 408, "VERTICAL", 0.58, 0.64, 0.68)
    text(x + 330, y + 408, "LATERAL", 0.58, 0.64, 0.68)
    text(x + 488, y + 408, "ARMED", 0.58, 0.64, 0.68)
    text(x + 590, y + 408, "AP / FD", 0.58, 0.64, 0.68)

    if (reading("ap_power") or 0) <= 0 then
        text(x + 170, y + 382, "AP POWER", 1, 0.65, 0)
        return
    end

    if (reading("at_on") or 0) > 0 then
        text(x + 24, y + 384, "A/THR", 0.24, 0.95, 0.36)
    elseif (reading("at_arm") or 0) > 0 then
        text(x + 24, y + 384, "A/THR", 0.25, 0.82, 1)
    end

    text(x + 170, y + 384, first_mode(vertical_modes, 2), 0.24, 0.95, 0.36)
    text(x + 330, y + 384, first_mode(lateral_modes, 2), 0.24, 0.95, 0.36)
    text(x + 488, y + 384, first_mode(vertical_modes, 1), 0.25, 0.82, 1)
    text(x + 488, y + 368, first_mode(lateral_modes, 1), 0.25, 0.82, 1)

    local ap1, ap2 = (reading("ap1") or 0) > 0, (reading("ap2") or 0) > 0
    local ap = ap1 and ap2 and "AP1+2" or (ap1 and "AP1" or (ap2 and "AP2" or ""))
    local fd = (reading("fd_mode") or 0) > 0 and "FD1" or ""
    text(x + 590, y + 384, ap, 1, 1, 1)
    text(x + 590, y + 368, fd, 1, 1, 1)
end

local function clamp(value, minimum, maximum)
    return math.max(minimum, math.min(maximum, value))
end

local function draw_flight_director(cx, cy)
    if (reading("ap_power") or 0) <= 0 or (reading("fd_mode") or 0) <= 0
        or (reading("fd_bars") or 0) <= 0 then return end
    local pitch, roll = reading("fd_pitch"), reading("fd_roll")
    if pitch == nil or roll == nil then
        text(cx - 23, cy + 32, "FD DATA", 1, 0.65, 0)
        return
    end

    -- FBW saturates roll at +/-45 degrees and pitch at +/-22.5 degrees.
    -- Scale its 158-unit PFD geometry to this window's 250-pixel horizon.
    local roll_offset = clamp(roll, -45, 45) * 0.70
    local pitch_offset = clamp(pitch, -22.5, 22.5) * 1.40
    gfx.set_width(5)
    line(cx + roll_offset, cy - 30, cx + roll_offset, cy + 30, 0.01, 0.01, 0.01)
    line(cx - 32, cy + pitch_offset, cx + 32, cy + pitch_offset, 0.01, 0.01, 0.01)
    gfx.set_width(3)
    line(cx + roll_offset, cy - 30, cx + roll_offset, cy + 30, 0.24, 0.95, 0.36)
    line(cx - 32, cy + pitch_offset, cx + 32, cy + pitch_offset, 0.24, 0.95, 0.36)
    gfx.set_width(1)
end

-- Split the horizon rectangle by a half-plane. This clips every attitude,
-- including vertical/inverted flight, without tan(roll) singularities.
local function half_plane(polygon, nx, ny, offset, keep_positive)
    local result = {}
    local previous = polygon[#polygon]
    local previous_d = nx * previous[1] + ny * previous[2] + offset
    for _, point in ipairs(polygon) do
        local distance = nx * point[1] + ny * point[2] + offset
        local inside = keep_positive and distance >= 0 or (not keep_positive and distance <= 0)
        local previous_inside = keep_positive and previous_d >= 0 or (not keep_positive and previous_d <= 0)
        if inside ~= previous_inside then
            local t = previous_d / (previous_d - distance)
            result[#result + 1] = {
                previous[1] + t * (point[1] - previous[1]),
                previous[2] + t * (point[2] - previous[2]),
            }
        end
        if inside then result[#result + 1] = point end
        previous, previous_d = point, distance
    end
    return result
end
local function polygon(points, cx, cy, r, g, b)
    gfx.set_color(r, g, b)
    for index = 2, #points - 1 do
        gfx.draw_triangle(cx + points[1][1], cy + points[1][2],
            cx + points[index][1], cy + points[index][2],
            cx + points[index + 1][1], cy + points[index + 1][2])
    end
end
local function horizon(cx, cy, pitch, roll)
    local box = {{-125, -92}, {125, -92}, {125, 92}, {-125, 92}}
    if not finite(pitch) or not finite(roll) then
        text(cx - 45, cy, "ATT DATA", 1, 0.65, 0)
        return
    end
    local angle = math.rad(roll)
    local nx, ny = -math.sin(angle), math.cos(angle)
    -- 2 pixels/degree, with the pitch offset rotating with the horizon.
    local offset = math.max(-90, math.min(90, pitch)) * 2
    polygon(half_plane(box, nx, ny, offset, true), cx, cy, 0.05, 0.29, 0.53)
    polygon(half_plane(box, nx, ny, offset, false), cx, cy, 0.37, 0.20, 0.10)
    -- Draw the bounded horizon by finding intersections with the rectangle.
    local intersections = {}
    for index, a in ipairs(box) do
        local b = box[index % #box + 1]
        local da, db = nx * a[1] + ny * a[2] + offset, nx * b[1] + ny * b[2] + offset
        if da * db < 0 then
            local t = da / (da - db)
            intersections[#intersections + 1] = {a[1] + t * (b[1] - a[1]), a[2] + t * (b[2] - a[2])}
        elseif da == 0 then
            intersections[#intersections + 1] = a
        end
    end
    if #intersections >= 2 then
        line(cx + intersections[1][1], cy + intersections[1][2],
            cx + intersections[2][1], cy + intersections[2][2])
    end
    -- Fixed aircraft symbol, yellow. Numeric pitch/roll remain available.
    line(cx - 45, cy, cx - 12, cy, 1, 0.85, 0)
    line(cx + 12, cy, cx + 45, cy, 1, 0.85, 0)
    line(cx - 12, cy, cx, cy - 7, 1, 0.85, 0)
    line(cx, cy - 7, cx + 12, cy, 1, 0.85, 0)
end

local function draw_pfd_page(x, y)
    draw_fma(x, y)
    text(x + 18, y + 327, "A380X DEVELOPMENT INSTRUMENTS  /  NATIVE X-PLANE", 0.35, 0.85, 1)
    text(x + 18, y + 307, "IAS " .. number("ias") .. " kt")
    text(x + 176, y + 307, "ALT " .. number("alt") .. " ft")
    horizon(x + 153, y + 198, reading("pitch"), reading("roll"))
    draw_flight_director(x + 153, y + 198)
    text(x + 28, y + 88, "PITCH " .. number("pitch", "%+.1f") .. "   BANK " .. number("roll", "%+.1f"))
    local heading = reading("heading")
    text(x + 28, y + 66, "HDG " .. (heading and string.format("%03.0f", math.floor(heading + 0.5) % 360) or "---")
        .. "   VS " .. number("vs", "%+.0f") .. " ft/min")
    text(x + 28, y + 44, "RA " .. number("ra") .. " ft")
    line(x + 325, y + 40, x + 325, y + 306, 0.2, 0.3, 0.4)
    text(x + 345, y + 288, "ENGINE")
    text(x + 345, y + 250, "N1 %")
    text(x + 345, y + 212, "N2 %")
    text(x + 345, y + 174, "FF kg/h")
    for index = 0, 3 do
        local ex = x + 432 + index * 65
        text(ex, y + 288, tostring(index + 1), 0.35, 0.85, 1)
        text(ex, y + 250, number("n1_" .. index, "%.1f"), 0.35, 1, 0.6)
        text(ex, y + 212, number("n2_" .. index, "%.1f"), 0.35, 1, 0.6)
        text(ex, y + 174, number("ff_" .. index, "%.0f", 3600), 0.35, 1, 0.6)
    end
    text(x + 345, y + 128, "FUEL " .. number("fuel", "%.1f", 0.001) .. " t")
    text(x + 345, y + 105, "FLAP HANDLE " .. number("flaps", "%.0f", 100) .. " %")
    local all_up, all_down, valid = true, true, true
    for index = 0, 4 do
        local ratio = reading("gear_" .. index)
        valid = valid and ratio ~= nil
        all_up = all_up and ratio ~= nil and ratio <= 0.01
        all_down = all_down and ratio ~= nil and ratio >= 0.99
    end
    local gear = not valid and "DATA" or (all_down and "DOWN" or (all_up and "UP" or "TRANSIT"))
    text(x + 345, y + 82, "GEAR " .. gear)
    text(x + 18, y + 17, "FBW-style presentation / native X-Plane guidance; no FBW FMS or FWS simulation.", 0.7, 0.75, 0.8)
end


local function draw_systems_page(x, y)
    text(x + 18, y + 400, "A380X NATIVE SYSTEMS MONITOR", 0.35, 0.85, 1)
    text(x + 18, y + 378, "Read-only X-Plane switch state; DATA means the optional source is unavailable.", 0.7, 0.75, 0.8)

    text(x + 18, y + 342, "ELECTRICAL / APU", 0.35, 0.85, 1)
    line(x + 18, y + 334, x + 702, y + 334, 0.2, 0.3, 0.4)
    draw_system_state(x + 28, y + 304, "BAT 1", "battery_0")
    draw_system_state(x + 128, y + 304, "BAT 2", "battery_1")
    draw_system_state(x + 228, y + 304, "GPU", "gpu_gen")
    draw_system_state(x + 328, y + 304, "APU", "apu_running")
    draw_system_state(x + 428, y + 304, "APU GEN", "apu_gen")
    text(x + 566, y + 304, "APU N1 " .. number("apu_n1", "%.0f") .. " %")
    for index = 0, 3 do
        draw_system_state(x + 28 + index * 165, y + 267, "GEN " .. tostring(index + 1), "generator_" .. index)
    end

    text(x + 18, y + 224, "PNEUMATIC / ANTI-ICE", 0.35, 0.85, 1)
    line(x + 18, y + 216, x + 702, y + 216, 0.2, 0.3, 0.4)
    for index = 0, 3 do
        local ex = x + 28 + index * 165
        text(ex, y + 190, "ENGINE " .. tostring(index + 1), 0.88, 0.93, 1)
        draw_system_state(ex, y + 168, "BLEED", "bleed_" .. index)
        draw_system_state(ex, y + 146, "ANTI-ICE", "anti_ice_" .. index)
    end
    draw_system_state(x + 28, y + 111, "PACK L", "pack_left")
    draw_system_state(x + 260, y + 111, "PACK C", "pack_center")
    draw_system_state(x + 492, y + 111, "PACK R", "pack_right")

    text(x + 18, y + 78, "FUEL PUMPS", 0.35, 0.85, 1)
    line(x + 18, y + 70, x + 702, y + 70, 0.2, 0.3, 0.4)
    for index = 0, 8 do
        draw_system_state(x + 24 + index * 75, y + 44, "P" .. tostring(index + 1), "fuel_pump_" .. index)
    end
    text(x + 18, y + 17, "Native switch-state monitor only; it does not model Airbus electrical, pneumatic or fuel computers.", 0.7, 0.75, 0.8)
end

local function draw_page_button(x, y)
    gfx.set_color(0.06, 0.12, 0.18)
    gfx.draw_rectangle(x + 622, y + 6, x + 704, y + 32)
    text(x + 632, y + 15, page_names[page % #page_names + 1] .. " >", 0.35, 0.85, 1)
end

function a380x_avionics_draw(_, x, y)
    if not window or not target_aircraft() then return end
    gfx.set_width(1)
    gfx.set_color(0.015, 0.025, 0.04)
    gfx.draw_rectangle(x, y, x + 720, y + 430)
    if page == 1 then draw_pfd_page(x, y) else draw_systems_page(x, y) end
    draw_page_button(x, y)
    gfx.set_width(1)
    gfx.set_color(1, 1, 1)
end

function a380x_avionics_cycle_page()
    page = page % #page_names + 1
    if window then float_wnd_set_title(window, "A380X - development instruments - " .. page_names[page]) end
end

function a380x_avionics_click(_, x, y, state)
    if state == 1 and x >= 622 and x <= 704 and y >= 6 and y <= 32 then
        a380x_avionics_cycle_page()
    end
end

function a380x_avionics_close()
    local old = window
    window = nil
    if old then float_wnd_destroy(old) end
end
function a380x_avionics_onclose(closed)
    if window == closed then window = nil end
end
function a380x_avionics_open()
    if not target_aircraft() or window then return end
    page = 1
    window = float_wnd_create(720, 430, 1, false)
    if not window then message("window creation failed"); return end
    float_wnd_set_title(window, "A380X - development instruments - " .. page_names[page])
    float_wnd_set_ondraw(window, "a380x_avionics_draw")
    float_wnd_set_onclick(window, "a380x_avionics_click")
    float_wnd_set_onclose(window, "a380x_avionics_onclose")
end
function a380x_avionics_toggle()
    if window then a380x_avionics_close() else a380x_avionics_open() end
end
function a380x_avionics_guard()
    if window and not target_aircraft() then a380x_avionics_close() end
end

create_command("a380x/xp12/avionics/toggle", "A380X: toggle development instruments", "a380x_avionics_toggle()", "", "")
create_command("a380x/xp12/avionics/page", "A380X: cycle development instrument page", "a380x_avionics_cycle_page()", "", "")
add_macro("A380X: development instruments", "a380x_avionics_open()", "a380x_avionics_close()", "deactivate")
do_every_frame("a380x_avionics_guard()")
-- FlyWithLua owns and destroys floating windows on script reload/exit.
message("ready; toggle a380x/xp12/avionics/toggle to open (closed by default)")
