-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (c) 2023-2026 FlyByWire Simulations and A380X-XP12 contributors
--
-- X-Plane adapter for the pinned FlyByWire A380 flyPad. The page structure,
-- A380 limits, cabin data, checklist content, battery timing and TOD formulas
-- are translated from the GPL-3.0 FlyByWire sources. MSFS SimVars, Coherent
-- React rendering and K-events are replaced with native X-Plane 12 datarefs,
-- commands and a FlyWithLua ImGui window.

local function target_aircraft()
    local path = (AIRCRAFT_PATH or ""):gsub("\\", "/")
    return PLANE_ICAO == "A388" and (path .. "/"):find("/A380X%-XP12/") ~= nil
end

local function message(value)
    if type(logMsg) == "function" then logMsg("A380X EFB: " .. value) end
end

if not target_aircraft() then return end

local required_api = {
    "dataref", "XPLMFindDataRef", "command_once", "float_wnd_create", "float_wnd_destroy",
    "float_wnd_set_title", "float_wnd_set_imgui_builder", "float_wnd_set_onclose",
    "create_command", "add_macro", "do_every_frame",
}
if not SUPPORTS_FLOATING_WINDOWS or type(imgui) ~= "table" then
    message("FlyWithLua NG with ImGui floating windows is required; module disabled")
    return
end
for _, name in ipairs(required_api) do
    if type(_G[name]) ~= "function" then
        message("missing API " .. name .. "; module disabled")
        return
    end
end

local MAX_PAX = 484
local MAX_CARGO_KG = 51400
local MAX_FUEL_KG = 323546
local MAX_GW_KG = 510000
local MAX_ZFW_KG = 373000
local DEFAULT_PAX_WEIGHT_KG = 84
local DEFAULT_BAG_WEIGHT_KG = 20
local KG_PER_GALLON = 3.039075693483925
local REFUEL_KG_PER_SECOND = 16 * KG_PER_GALLON
local WINDOW_WIDTH, WINDOW_HEIGHT = 1180, 760

local bindings = {
    {"time", "sim/time/total_running_time_sec", "readonly"},
    {"zulu", "sim/time/zulu_time_sec", "readonly"},
    {"on_ground", "sim/flightmodel/failures/onground_any", "readonly"},
    {"latitude", "sim/flightmodel/position/latitude", "readonly"},
    {"longitude", "sim/flightmodel/position/longitude", "readonly"},
    {"altitude", "sim/cockpit2/gauges/indicators/altitude_ft_pilot", "readonly"},
    {"vertical_speed", "sim/cockpit2/gauges/indicators/vvi_fpm_pilot", "readonly"},
    {"ground_speed", "sim/cockpit2/gauges/indicators/ground_speed_kt", "readonly"},
    {"gps_distance", "sim/cockpit2/radios/indicators/gps_dme_distance_nm", "readonly"},
    {"gps_time", "sim/cockpit2/radios/indicators/gps_dme_time_min", "readonly"},
    {"fpa", "sim/cockpit2/autopilot/fpa", "readonly"},
    {"trk_fpa", "sim/cockpit2/autopilot/trk_fpa", "readonly"},
    {"payload", "sim/flightmodel/weight/m_fixed", "writable"},
    {"gross_weight", "sim/flightmodel/weight/m_total", "readonly"},
    {"fuel_total", "sim/flightmodel/weight/m_fuel_total", "readonly"},
    {"max_fuel_lb", "sim/aircraft/weight/acf_m_fuel_tot", "readonly"},
    {"battery_1", "sim/cockpit2/electrical/battery_on", "readonly", 0},
    {"battery_2", "sim/cockpit2/electrical/battery_on", "readonly", 1},
    {"gpu", "sim/cockpit2/electrical/GPU_generator_on", "readonly"},
    {"gpu_available", "sim/cockpit2/electrical/GPU_can_be_called_for", "readonly"},
    {"apu", "sim/cockpit2/electrical/APU_running", "readonly"},
    {"apu_n1", "sim/cockpit2/electrical/APU_N1_percent", "readonly"},
    {"avionics", "sim/cockpit2/switches/avionics_power_on", "readonly"},
    {"seatbelts", "sim/cockpit2/switches/fasten_seat_belts", "readonly"},
    {"no_smoking", "sim/cockpit2/switches/no_smoking", "readonly"},
    {"beacon", "sim/cockpit2/switches/beacon_on", "readonly"},
    {"parking_brake", "sim/cockpit2/controls/parking_brake_ratio", "readonly"},
    {"flaps", "sim/cockpit2/controls/flap_handle_request_ratio", "readonly"},
    {"speedbrake", "sim/cockpit2/controls/speedbrake_ratio", "readonly"},
    {"gear_handle", "sim/cockpit2/controls/gear_handle_down", "readonly"},
    {"oat", "sim/weather/aircraft/temperature_ambient_deg_c", "readonly"},
    {"qnh", "sim/weather/aircraft/qnh_pas", "readonly"},
    {"wind_direction", "sim/weather/aircraft/wind_direction_degt", "readonly", 0},
    {"wind_speed", "sim/weather/aircraft/wind_speed_kts", "readonly", 0},
    {"com1", "sim/cockpit2/radios/actuators/com1_frequency_hz_833", "readonly"},
    {"com1_standby", "sim/cockpit2/radios/actuators/com1_standby_frequency_hz_833", "writable"},
    {"transponder", "sim/cockpit2/radios/actuators/transponder_code", "writable"},
    {"transponder_mode", "sim/cockpit2/radios/actuators/transponder_mode", "writable"},
}
for index = 0, 3 do
    bindings[#bindings + 1] = {"engine_" .. index, "sim/flightmodel2/engines/engine_is_burning_fuel", "readonly", index}
    bindings[#bindings + 1] = {"generator_" .. index, "sim/cockpit2/electrical/generator_on", "readonly", index}
end
for index = 0, 4 do
    bindings[#bindings + 1] = {"gear_" .. index, "sim/flightmodel2/gear/deploy_ratio", "readonly", index}
end
for index = 0, 8 do
    bindings[#bindings + 1] = {"fuel_" .. index, "sim/flightmodel/weight/m_fuel", "writable", index}
    bindings[#bindings + 1] = {"tank_ratio_" .. index, "sim/aircraft/overflow/acf_tank_rat", "readonly", index}
end
for index = 0, 16 do
    bindings[#bindings + 1] = {"door_" .. index, "sim/flightmodel2/misc/door_open_ratio", "readonly", index}
end

local failure_definitions = {
    {"Engine 1", "failure_engine_1", "sim/operation/failures/rel_engfai0"},
    {"Engine 2", "failure_engine_2", "sim/operation/failures/rel_engfai1"},
    {"Engine 3", "failure_engine_3", "sim/operation/failures/rel_engfai2"},
    {"Engine 4", "failure_engine_4", "sim/operation/failures/rel_engfai3"},
    {"Generator 1", "failure_generator_1", "sim/operation/failures/rel_genera0"},
    {"Generator 2", "failure_generator_2", "sim/operation/failures/rel_genera1"},
    {"Generator 3", "failure_generator_3", "sim/operation/failures/rel_genera2"},
    {"Generator 4", "failure_generator_4", "sim/operation/failures/rel_genera3"},
    {"Battery 1", "failure_battery_1", "sim/operation/failures/rel_batter0"},
    {"Battery 2", "failure_battery_2", "sim/operation/failures/rel_batter1"},
    {"Flap actuator", "failure_flaps", "sim/operation/failures/rel_flap_act"},
    {"Landing gear actuator", "failure_gear", "sim/operation/failures/rel_gear_act"},
    {"Hydraulic system 1", "failure_hyd_1", "sim/operation/failures/rel_hydpmp"},
    {"Hydraulic system 2", "failure_hyd_2", "sim/operation/failures/rel_hydpmp2"},
    {"APU", "failure_apu", "sim/operation/failures/rel_apu"},
    {"Pitot 1", "failure_pitot_1", "sim/operation/failures/rel_pitot"},
    {"Pitot 2", "failure_pitot_2", "sim/operation/failures/rel_pitot2"},
}
for _, failure in ipairs(failure_definitions) do
    bindings[#bindings + 1] = {failure[2], failure[3], "writable"}
end

local bound = {}
for _, binding in ipairs(bindings) do
    if XPLMFindDataRef(binding[2]) then
        if binding[4] ~= nil then
            dataref("a380x_efb_" .. binding[1], binding[2], binding[3], binding[4])
        else
            dataref("a380x_efb_" .. binding[1], binding[2], binding[3])
        end
        bound[binding[1]] = binding[3]
    else
        message("optional dataref unavailable " .. binding[2])
    end
end

local function finite(value)
    return type(value) == "number" and value == value and math.abs(value) < math.huge
end

local function reading(key, fallback)
    local value = _G["a380x_efb_" .. key]
    if finite(value) then return value end
    return fallback
end

local function write(key, value)
    if bound[key] ~= "writable" or not finite(value) then return false end
    _G["a380x_efb_" .. key] = value
    return true
end

local function native(command)
    if target_aircraft() then command_once(command) end
end

local function clamp(value, low, high)
    return math.max(low, math.min(high, value))
end

local function all_engines_off()
    for index = 0, 3 do
        if (reading("engine_" .. index, 0) or 0) > 0 then return false end
    end
    return true
end

local function is_on_ground()
    return (reading("on_ground", 0) or 0) > 0
end

local window = nil
local active_page = "Dashboard"
local power_state = "LOADED"
local loading_started = nil
local battery_level = 100
local last_update_time = reading("time", 0) or 0
local battery_life_enabled = true
local auto_checklists = true
local metric_units = true
local brightness = 85
local status_message = "X-Plane adapter ready"
local flight_origin, flight_destination, flight_number = "", "", ""
local payload_pax, payload_freight = 0, 0
local fuel_target = clamp(reading("fuel_total", 0) or 0, 0, MAX_FUEL_KG)
local refuel_active = false
local refuel_rate = "REAL"
local checklist_index = 1
local checklist_manual = {}
local tod_target_altitude = 3000
local tod_input = 3
local tod_type = "ANGLE"
local com1_standby_input = math.floor(reading("com1_standby", 121500) or 121500)
local transponder_input = math.floor(reading("transponder", 2000) or 2000)

local function every_door_open()
    for index = 0, 16 do
        if (reading("door_" .. index, 0) or 0) < 0.5 then return false end
    end
    return true
end

function a380x_efb_toggle_all_doors()
    if not target_aircraft() then return false end
    local open_all = not every_door_open()
    for index = 0, 16 do
        local open = (reading("door_" .. index, 0) or 0) >= 0.5
        if open ~= open_all then
            native("sim/flight_controls/door_toggle_" .. (index + 1))
        end
    end
    status_message = "All 17 doors commanded " .. (open_all and "open" or "closed")
    return true
end

local page_names = {
    "Dashboard", "Dispatch", "Ground", "Performance", "Navigation", "ATC",
    "Failures", "Checklists", "Presets", "Settings",
}

local function checklist_condition(name)
    if name == "seatbelts" then return (reading("seatbelts", 0) or 0) > 0 end
    if name == "parking_brake" then return (reading("parking_brake", 0) or 0) > 0.8 end
    if name == "beacon" then return (reading("beacon", 0) or 0) > 0 end
    if name == "gear_down" then
        for index = 0, 4 do
            if (reading("gear_" .. index, 0) or 0) < 0.99 then return false end
        end
        return true
    end
    if name == "spoilers_armed" then return (reading("speedbrake", 0) or 0) < -0.1 end
    if name == "engines_off" then return all_engines_off() end
    if name == "batteries_off" then
        return (reading("battery_1", 0) or 0) == 0 and (reading("battery_2", 0) or 0) == 0
    end
    if name == "fuel_pumps_off" then
        -- The port does not expose every FBW pump circuit. Engine shutdown is
        -- the closest native condition and the label stays explicit in the UI.
        return all_engines_off()
    end
    return false
end

local checklists = {
    {name="COCKPIT PREPARATION", items={
        {"GEAR PINS & COVERS", "REMOVED"}, {"FUEL QTY", "CHECKED"},
        {"SEAT BELTS", "ON", "seatbelts"}, {"BARO REF", "SET"},
    }},
    {name="BEFORE START", items={
        {"PARKING BRAKE", "SET", "parking_brake"}, {"T.O SPEEDS & THRUST", "CHECKED"},
        {"BEACON", "ON", "beacon"},
    }},
    {name="AFTER START", items={
        {"ANTI ICE", "AS RQRD"}, {"PITCH TRIM", "T.O"}, {"RUDDER TRIM", "NEUTRAL"},
    }},
    {name="TAXI", items={
        {"FLIGHT CONTROLS", "CHECKED"}, {"FLAPS SETTING", "CONF"}, {"RADAR", "ON"},
        {"T.O SEAT BELTS", "ON", "seatbelts"}, {"T.O GND SPLRs", "ARMED", "spoilers_armed"},
        {"T.O FLAPS", "SET"}, {"T.O AUTO BRAKE", "RTO"}, {"T.O CONFIG", "TESTED"},
    }},
    {name="LINE-UP", items={
        {"T.O RWY", "CONFIRMED"}, {"CABIN CREW", "ADVISED"}, {"PACKS 1+2", "ON"},
    }},
    {name="<< DEPARTURE CHANGE >>", items={
        {"RWY & SID", "CONFIRMED"}, {"FLAPS SETTING", "CONFIRMED"},
        {"T.O SPEEDS & THRUST", "CHECKED"}, {"FCU ALT", "SET"},
    }},
    {name="APPROACH", items={
        {"BARO REF", "SET"}, {"SEAT BELTS", "ON", "seatbelts"},
        {"MINIMUM", "SET"}, {"AUTO BRAKE", "SET"},
    }},
    {name="LANDING", items={
        {"LDG SEAT BELTS", "ON", "seatbelts"}, {"LDG GEAR", "DOWN", "gear_down"},
        {"GND SPLRs", "ARM", "spoilers_armed"}, {"FLAPS", "LDG"},
    }},
    {name="PARKING", items={
        {"PARK BRAKE OR CHOCKS", "AS RQRD"}, {"ENGINES", "OFF", "engines_off"},
        {"WING LIGHTS", "OFF"}, {"FUEL PUMPS", "OFF", "fuel_pumps_off"},
    }},
    {name="SECURING AIRCRAFT", items={
        {"OXYGEN", "OFF"}, {"EMER EXIT LT", "OFF"}, {"EFBs", "OFF"},
        {"BATTERIES", "OFF", "batteries_off"},
    }},
}
for checklist = 1, #checklists do checklist_manual[checklist] = {} end

function a380x_efb_select_page(page)
    for _, name in ipairs(page_names) do
        if page == name then active_page = page; return true end
    end
    return false
end

function a380x_efb_apply_payload(pax, freight)
    pax = math.floor(clamp(tonumber(pax) or 0, 0, MAX_PAX))
    freight = clamp(tonumber(freight) or 0, 0, MAX_CARGO_KG)
    if not is_on_ground() or not all_engines_off() then
        status_message = "Payload changes require aircraft on ground with engines off"
        return false
    end
    local payload = pax * (DEFAULT_PAX_WEIGHT_KG + DEFAULT_BAG_WEIGHT_KG) + freight
    if not write("payload", payload) then
        status_message = "Native payload dataref unavailable"
        return false
    end
    payload_pax, payload_freight = pax, freight
    status_message = string.format("Payload applied: %d passengers, %.0f kg freight", pax, freight)
    return true
end

function a380x_efb_set_fuel_total(target)
    target = clamp(tonumber(target) or 0, 0, MAX_FUEL_KG)
    local ratios, ratio_total = {}, 0
    for index = 0, 8 do
        local ratio = math.max(0, reading("tank_ratio_" .. index, 0) or 0)
        ratios[index] = ratio
        ratio_total = ratio_total + ratio
    end
    if ratio_total <= 0 then
        ratios = {[0]=0.30, [1]=0.078, [2]=0.22, [3]=0.22, [4]=0.078, [5]=0.023, [6]=0.023, [7]=0.058, [8]=0}
        ratio_total = 1
    end
    local written = 0
    for index = 0, 8 do
        local amount = index == 8 and (target - written) or target * ratios[index] / ratio_total
        if ratios[index] <= 0 then amount = 0 end
        if write("fuel_" .. index, math.max(0, amount)) then written = written + math.max(0, amount) end
    end
    return written > 0 or target == 0
end

function a380x_efb_begin_refuel(target, rate)
    fuel_target = clamp(tonumber(target) or fuel_target, 0, MAX_FUEL_KG)
    if rate == "REAL" or rate == "FAST" or rate == "INSTANT" then refuel_rate = rate end
    if refuel_rate ~= "INSTANT" and (not is_on_ground() or not all_engines_off()) then
        status_message = "Real/fast refuel requires aircraft on ground with engines off"
        return false
    end
    if refuel_rate == "INSTANT" then
        a380x_efb_set_fuel_total(fuel_target)
        refuel_active = false
        status_message = "Instant fuel load applied"
    else
        refuel_active = true
        status_message = "Refuel started"
    end
    return true
end

function a380x_efb_stop_refuel()
    refuel_active = false
    status_message = "Refuel stopped"
end

function a380x_efb_toggle_failure(key)
    if bound[key] ~= "writable" then return false end
    write(key, (reading(key, 0) or 0) == 0 and 6 or 0)
    return true
end

function a380x_efb_apply_preset(name)
    if not is_on_ground() then
        status_message = "Aircraft presets are available on ground only"
        return false
    end
    if name == "COLD_AND_DARK" then
        native("sim/autopilot/servos_off_any")
        native("sim/systems/avionics_off")
        native("sim/electrical/APU_off")
        native("sim/electrical/GPU_off")
        native("sim/electrical/battery_1_off")
        native("sim/electrical/battery_2_off")
        native("sim/fuel/fuel_pumps_off")
        status_message = "Native Cold & Dark preset applied"
    elseif name == "TURNAROUND" then
        native("sim/electrical/battery_1_on")
        native("sim/electrical/battery_2_on")
        native("sim/ground_ops/toggle_gpu_request")
        native("sim/systems/avionics_on")
        status_message = "Native Turnaround preset applied"
    elseif name == "READY_FOR_TAXI" then
        native("sim/operation/quick_start")
        native("sim/systems/avionics_on")
        native("sim/fuel/fuel_pumps_on")
        status_message = "Native Ready for Taxi preset applied"
    else
        return false
    end
    return true
end

local function electrical_power_available()
    if (reading("battery_1", 0) or 0) > 0 or (reading("battery_2", 0) or 0) > 0 then return true end
    if (reading("gpu", 0) or 0) > 0 or (reading("apu", 0) or 0) > 0 then return true end
    for index = 0, 3 do
        if (reading("generator_" .. index, 0) or 0) > 0 then return true end
    end
    return false
end

function a380x_efb_update_core()
    local now = reading("time", last_update_time) or last_update_time
    local delta = clamp(now - last_update_time, 0, 1)
    last_update_time = now

    if power_state == "LOADING" and loading_started and now - loading_started >= 2.5 then
        power_state = "LOADED"
    end
    if battery_life_enabled and power_state == "LOADED" then
        local duration = electrical_power_available() and 10800 or -32400
        battery_level = clamp(battery_level + 100 * delta / duration, 0, 100)
        if battery_level <= 0 then power_state = "EMPTY" end
    end
    if power_state == "EMPTY" and electrical_power_available() and battery_level > 2 then
        power_state, loading_started = "LOADING", now
    end

    if refuel_active then
        local actual = reading("fuel_total", 0) or 0
        local rate = REFUEL_KG_PER_SECOND * (refuel_rate == "FAST" and 5 or 1)
        local difference = fuel_target - actual
        if math.abs(difference) <= math.max(10, rate * delta) then
            a380x_efb_set_fuel_total(fuel_target)
            refuel_active = false
            status_message = "Fuel load completed"
        elseif delta > 0 then
            a380x_efb_set_fuel_total(actual + (difference > 0 and 1 or -1) * rate * delta)
        end
    end

    if window and not target_aircraft() then
        local stale = window
        window = nil
        float_wnd_destroy(stale)
    end
end

local function heading(value)
    imgui.Separator()
    imgui.TextUnformatted(value)
    imgui.Separator()
end

local function format_mass(value)
    if not finite(value) then return "---" end
    if metric_units then return string.format("%.0f kg", value) end
    return string.format("%.0f lb", value * 2.2046226218)
end

local function button_command(label, command, width)
    if imgui.Button(label, width or 180, 34) then native(command) end
end

local function status_bar()
    local zulu = math.floor(reading("zulu", 0) or 0) % 86400
    local charging = electrical_power_available()
    imgui.TextUnformatted(string.format(
        "A380X flyPad     %02d:%02dZ     OAT %.0f C     WIND %03.0f/%02.0f kt     BAT %.0f%%%s",
        math.floor(zulu / 3600), math.floor((zulu % 3600) / 60), reading("oat", 0) or 0,
        reading("wind_direction", 0) or 0, reading("wind_speed", 0) or 0,
        battery_level, charging and " +" or ""
    ))
    imgui.SameLine()
    imgui.SetCursorPosX(math.max(860, imgui.GetWindowWidth() - 280))
    if imgui.Button("STANDBY", 95, 26) then power_state = "STANDBY" end
    imgui.SameLine()
    if imgui.Button("SHUTDOWN", 105, 26) then power_state = "SHUTOFF" end
    imgui.Separator()
end

local function navigation_bar()
    imgui.BeginChild("flypad_nav", 150, 655)
    for _, name in ipairs(page_names) do
        local label = active_page == name and ("> " .. name) or name
        if imgui.Button(label .. "##nav_" .. name, 142, 45) then active_page = name end
    end
    imgui.Separator()
    imgui.TextUnformatted("FBW source\nXP12 adapter")
    imgui.EndChild()
end

local function page_dashboard()
    heading("DASHBOARD")
    imgui.Columns(2)
    imgui.TextUnformatted(string.format("%s -> %s", flight_origin ~= "" and flight_origin or "----", flight_destination ~= "" and flight_destination or "----"))
    imgui.TextUnformatted("Flight " .. (flight_number ~= "" and flight_number or "----"))
    imgui.TextUnformatted(string.format("Position %.3f / %.3f", reading("latitude", 0) or 0, reading("longitude", 0) or 0))
    imgui.TextUnformatted(string.format("Altitude %.0f ft     Ground speed %.0f kt", reading("altitude", 0) or 0, reading("ground_speed", 0) or 0))
    imgui.TextUnformatted(string.format("Active waypoint %.1f NM     ETA %.0f min", reading("gps_distance", 0) or 0, reading("gps_time", 0) or 0))
    imgui.NextColumn()
    imgui.TextUnformatted("AIRCRAFT")
    imgui.TextUnformatted("A380-842 / Trent 972B-84")
    imgui.TextUnformatted("Weight variant WV003")
    imgui.TextUnformatted("Fuel " .. format_mass(reading("fuel_total", 0)))
    imgui.TextUnformatted("Gross weight " .. format_mass(reading("gross_weight", 0)))
    imgui.TextUnformatted(status_message)
    imgui.Columns()
    heading("QUICK ACCESS")
    for _, name in ipairs({"Dispatch", "Ground", "Performance", "Checklists"}) do
        if imgui.Button(name .. "##quick_" .. name, 205, 55) then active_page = name end
        if name ~= "Checklists" then imgui.SameLine() end
    end
end

local function page_dispatch()
    heading("DISPATCH / OVERVIEW")
    local changed
    changed, flight_origin = imgui.InputText("Origin", flight_origin, 4)
    changed, flight_destination = imgui.InputText("Destination", flight_destination, 4)
    changed, flight_number = imgui.InputText("Flight number", flight_number, 12)
    imgui.Separator()
    local gw = reading("gross_weight", 0) or 0
    local fuel = reading("fuel_total", 0) or 0
    local zfw = gw - fuel
    imgui.TextUnformatted("LIVE LOADSHEET")
    imgui.TextUnformatted("Zero fuel weight: " .. format_mass(zfw) .. (zfw > MAX_ZFW_KG and "  OVER LIMIT" or ""))
    imgui.TextUnformatted("Fuel on board:   " .. format_mass(fuel))
    imgui.TextUnformatted("Gross weight:    " .. format_mass(gw) .. (gw > MAX_GW_KG and "  OVER LIMIT" or ""))
    imgui.TextUnformatted("Payload:         " .. format_mass(reading("payload", 0)))
    imgui.Separator()
    imgui.TextUnformatted("SimBrief import, Navigraph login and MSFS flight-folder APIs are not available in FlyWithLua.\nEnter dispatch identifiers here and load the route through X-Plane's native FMS.")
    button_command("OPEN NATIVE FMS", "sim/FMS/CDU_popup", 220)
end

local function page_ground_services()
    heading("GROUND / SERVICES")
    button_command("JETWAYS", "sim/ground_ops/jetway")
    imgui.SameLine(); button_command("SERVICE VEHICLES", "sim/ground_ops/service_plane")
    imgui.SameLine(); button_command("GPU REQUEST", "sim/ground_ops/toggle_gpu_request")
    imgui.TextUnformatted(string.format("GPU %s     availability %s", (reading("gpu", 0) or 0) > 0 and "ON" or "OFF", (reading("gpu_available", 0) or 0) > 0 and "READY" or "NO"))
    imgui.Separator()
    imgui.TextUnformatted("MAIN-DECK PASSENGER DOORS")
    for index = 0, 9 do
        local state = (reading("door_" .. index, 0) or 0) > 0.5 and "OPEN" or "CLOSED"
        if imgui.Button(string.format("D%02d %s##door_%d", index + 1, state, index), 145, 34) then
            native("sim/flight_controls/door_toggle_" .. (index + 1))
        end
        if index % 5 ~= 4 then imgui.SameLine() end
    end
    imgui.TextUnformatted("UPPER-DECK PASSENGER DOORS")
    for index = 10, 15 do
        local station = math.floor((index - 10) / 2) + 1
        local side = index % 2 == 0 and "L" or "R"
        local state = (reading("door_" .. index, 0) or 0) > 0.5 and "OPEN" or "CLOSED"
        if imgui.Button(string.format("U%d%s %s##door_%d", station, side, state, index), 145, 34) then
            native("sim/flight_controls/door_toggle_" .. (index + 1))
        end
        if index ~= 15 then imgui.SameLine() end
    end
    local cockpit_state = (reading("door_16", 0) or 0) > 0.5 and "OPEN" or "CLOSED"
    if imgui.Button("COCKPIT DOOR " .. cockpit_state .. "##door_16", 220, 34) then
        native("sim/flight_controls/door_toggle_17")
    end
    imgui.SameLine()
    local all_door_action = every_door_open() and "CLOSE ALL DOORS" or "OPEN ALL DOORS"
    if imgui.Button(all_door_action .. "##all_doors", 220, 34) then
        a380x_efb_toggle_all_doors()
    end
    imgui.Separator()
    imgui.TextUnformatted("PUSHBACK")
    button_command("NOSE LEFT", "sim/ground_ops/pushback_left", 170)
    imgui.SameLine(); button_command("STRAIGHT", "sim/ground_ops/pushback_straight", 170)
    imgui.SameLine(); button_command("NOSE RIGHT", "sim/ground_ops/pushback_right", 170)
    imgui.SameLine(); button_command("STOP / RELEASE", "sim/ground_ops/pushback_stop", 170)
end

local function page_ground_payload()
    heading("GROUND / PAYLOAD")
    local changed
    changed, payload_pax = imgui.SliderInt("Passengers", payload_pax, 0, MAX_PAX, "%d pax")
    changed, payload_freight = imgui.SliderInt("Freight", payload_freight, 0, MAX_CARGO_KG, "%d kg")
    local target = payload_pax * (DEFAULT_PAX_WEIGHT_KG + DEFAULT_BAG_WEIGHT_KG) + payload_freight
    imgui.TextUnformatted(string.format("Passenger %.0f kg + bag %.0f kg each     target payload %s", DEFAULT_PAX_WEIGHT_KG, DEFAULT_BAG_WEIGHT_KG, format_mass(target)))
    if imgui.Button("APPLY PAYLOAD", 220, 42) then a380x_efb_apply_payload(payload_pax, payload_freight) end
    imgui.TextUnformatted("Current native payload " .. format_mass(reading("payload", 0)))
end

local function page_ground_fuel()
    heading("GROUND / FUEL")
    local actual = reading("fuel_total", 0) or 0
    local changed
    changed, fuel_target = imgui.SliderInt("Target fuel", math.floor(fuel_target), 0, MAX_FUEL_KG, "%d kg")
    imgui.TextUnformatted(string.format("Current %s     Target %s     %.1f%%", format_mass(actual), format_mass(fuel_target), 100 * actual / MAX_FUEL_KG))
    if imgui.Button((refuel_rate == "REAL" and "> " or "") .. "REAL", 130, 32) then refuel_rate = "REAL" end
    imgui.SameLine(); if imgui.Button((refuel_rate == "FAST" and "> " or "") .. "FAST", 130, 32) then refuel_rate = "FAST" end
    imgui.SameLine(); if imgui.Button((refuel_rate == "INSTANT" and "> " or "") .. "INSTANT", 130, 32) then refuel_rate = "INSTANT" end
    imgui.SameLine()
    if not refuel_active then
        if imgui.Button("START", 160, 42) then a380x_efb_begin_refuel(fuel_target, refuel_rate) end
    elseif imgui.Button("STOP", 160, 42) then
        a380x_efb_stop_refuel()
    end
    local eta = refuel_rate == "INSTANT" and 0 or math.abs(fuel_target - actual) / (REFUEL_KG_PER_SECOND * (refuel_rate == "FAST" and 5 or 1))
    imgui.TextUnformatted(string.format("Status: %s     estimated duration %.1f min", refuel_active and "ACTIVE" or "READY", eta / 60))
    imgui.Separator()
    imgui.Columns(3)
    for index = 0, 8 do
        imgui.TextUnformatted(string.format("Tank %d  %s", index + 1, format_mass(reading("fuel_" .. index, 0))))
        if index % 3 ~= 2 then imgui.NextColumn() end
    end
    imgui.Columns()
end

local ground_tab = "Services"
local function page_ground()
    if imgui.Button((ground_tab == "Services" and "> " or "") .. "SERVICES", 180, 38) then ground_tab = "Services" end
    imgui.SameLine(); if imgui.Button((ground_tab == "Payload" and "> " or "") .. "PAYLOAD", 180, 38) then ground_tab = "Payload" end
    imgui.SameLine(); if imgui.Button((ground_tab == "Fuel" and "> " or "") .. "FUEL", 180, 38) then ground_tab = "Fuel" end
    if ground_tab == "Services" then page_ground_services()
    elseif ground_tab == "Payload" then page_ground_payload()
    else page_ground_fuel() end
end

local function tod_average_ground_speed(current_altitude, target_altitude)
    local sim_speed = math.max(1, reading("ground_speed", 0) or 0)
    if current_altitude <= 10000 then return sim_speed end
    local bottom, top = math.max(target_altitude, 0), math.max(current_altitude, target_altitude + 1)
    local below = clamp(10000 - bottom, 0, top - bottom)
    local above = (top - bottom) - below
    return (below * 250 + above * sim_speed) / (top - bottom)
end

local function page_performance()
    heading("PERFORMANCE / TOP OF DESCENT")
    local current = math.floor(reading("altitude", 0) or 0)
    imgui.TextUnformatted(string.format("Current altitude %d ft     current ground speed %.0f kt", current, reading("ground_speed", 0) or 0))
    local changed
    changed, tod_target_altitude = imgui.InputInt("Target altitude ft", tod_target_altitude, 100)
    if imgui.Button((tod_type == "DISTANCE" and "> " or "") .. "KNOWN DISTANCE", 180, 32) then tod_type = "DISTANCE" end
    imgui.SameLine(); if imgui.Button((tod_type == "VS" and "> " or "") .. "KNOWN V/S", 180, 32) then tod_type = "VS" end
    imgui.SameLine(); if imgui.Button((tod_type == "ANGLE" and "> " or "") .. "KNOWN ANGLE", 180, 32) then tod_type = "ANGLE" end
    changed, tod_input = imgui.InputInt(tod_type == "DISTANCE" and "Distance NM" or (tod_type == "VS" and "Vertical speed ft/min" or "Flight path angle deg"), tod_input)
    local vertical = math.max(0, current - tod_target_altitude)
    local speed = tod_average_ground_speed(current, tod_target_altitude)
    if vertical > 0 and tod_input ~= 0 then
        if tod_type == "DISTANCE" then
            local time = math.abs(tod_input) / speed * 60
            local vs = -vertical / time
            local angle = -math.deg(math.atan(vertical / (math.abs(tod_input) * 6076.12)))
            imgui.TextUnformatted(string.format("Desired vertical speed %.0f ft/min     descent angle %.1f deg", vs, angle))
        elseif tod_type == "VS" then
            local distance = speed / 60 * vertical / math.abs(tod_input)
            imgui.TextUnformatted(string.format("Start descent about %.0f NM before target", distance))
        else
            local distance = vertical / math.tan(math.rad(math.abs(tod_input))) * 0.000164579
            imgui.TextUnformatted(string.format("Start descent about %.0f NM before target", distance))
        end
    end
    imgui.Separator()
    imgui.TextUnformatted("The pinned A380 flyPad supplies no A380 takeoff or landing calculator; its aircraft context sets both to null.\nThis page therefore ports the available FlyByWire TOD calculator without inventing A380 performance data.")
end

local function page_navigation()
    heading("NAVIGATION")
    imgui.TextUnformatted("X-Plane native map and EFIS overlays replace the MSFS Bing/Navigraph canvas.")
    button_command("CURRENT MAP", "sim/map/show_current", 200)
    imgui.SameLine(); button_command("LOW ENROUTE", "sim/map/show_low_enroute", 200)
    imgui.SameLine(); button_command("HIGH ENROUTE", "sim/map/show_high_enroute", 200)
    imgui.Separator()
    button_command("MAP ZOOM +", "sim/instruments/map_zoom_in", 160)
    imgui.SameLine(); button_command("MAP ZOOM -", "sim/instruments/map_zoom_out", 160)
    imgui.SameLine(); button_command("AIRPORTS", "sim/instruments/EFIS_apt", 160)
    imgui.SameLine(); button_command("WXR", "sim/instruments/EFIS_wxr", 160)
    imgui.TextUnformatted("Navigraph and ChartFox authentication are MSFS/Coherent integrations and are intentionally not emulated.")
end

local function format_frequency(value)
    return string.format("%.3f", (tonumber(value) or 0) / 1000)
end

local function page_atc()
    heading("ATC / RADIOS")
    imgui.TextUnformatted("COM1 ACTIVE " .. format_frequency(reading("com1", 0)))
    local changed
    changed, com1_standby_input = imgui.InputInt("COM1 standby kHz", com1_standby_input, 5)
    if imgui.Button("SET STANDBY", 180, 34) then write("com1_standby", clamp(com1_standby_input, 118000, 136990)) end
    imgui.SameLine(); button_command("SWAP COM1", "sim/radios/com1_standy_flip", 180)
    imgui.Separator()
    changed, transponder_input = imgui.InputInt("Transponder", transponder_input, 1)
    if imgui.Button("SET CODE", 180, 34) then write("transponder", clamp(transponder_input, 0, 7777)) end
    local xpdr = reading("transponder_mode", 0) or 0
    if imgui.Button("STANDBY", 140, 34) then write("transponder_mode", 1) end
    imgui.SameLine(); if imgui.Button("ALT", 140, 34) then write("transponder_mode", 3) end
    imgui.SameLine(); if imgui.Button("TA/RA", 140, 34) then write("transponder_mode", 7) end
    imgui.TextUnformatted(string.format("Native transponder mode %d", xpdr))
    imgui.Separator()
    imgui.TextUnformatted("Online ATC messages and Hoppie/SimBrief integration require an external client and are not part of X-Plane's native API.")
end

local function page_failures()
    heading("FAILURES")
    imgui.TextUnformatted("Native X-Plane failures; value 6 = failed, 0 = operational.")
    imgui.Columns(2)
    for index, failure in ipairs(failure_definitions) do
        local failed = (reading(failure[2], 0) or 0) ~= 0
        if imgui.Button((failed and "REPAIR " or "FAIL ") .. failure[1] .. "##failure_" .. failure[2], 255, 30) then
            a380x_efb_toggle_failure(failure[2])
        end
        imgui.SameLine(); imgui.TextUnformatted(failed and "FAILED" or "OK")
        if index % 2 == 1 then imgui.NextColumn() else imgui.NextColumn() end
    end
    imgui.Columns()
    imgui.Separator()
    if imgui.Button("REPAIR ALL SYSTEMS", 240, 38) then native("sim/operation/fix_all_systems") end
end

local function page_checklists()
    heading("CHECKLISTS")
    if imgui.Button("<", 40, 30) then checklist_index = checklist_index == 1 and #checklists or checklist_index - 1 end
    imgui.SameLine(); imgui.TextUnformatted(string.format("%d/%d  %s", checklist_index, #checklists, checklists[checklist_index].name))
    imgui.SameLine(); if imgui.Button(">", 40, 30) then checklist_index = checklist_index == #checklists and 1 or checklist_index + 1 end
    imgui.Separator()
    local checklist = checklists[checklist_index]
    for index, item in ipairs(checklist.items) do
        local automatic = auto_checklists and item[3] and checklist_condition(item[3])
        local completed = automatic or checklist_manual[checklist_index][index]
        if imgui.Button((completed and "[x] " or "[ ] ") .. item[1] .. "##check_" .. checklist_index .. "_" .. index, 390, 32) then
            checklist_manual[checklist_index][index] = not checklist_manual[checklist_index][index]
        end
        imgui.SameLine(); imgui.TextUnformatted(item[2] .. (automatic and "  AUTO" or ""))
    end
    imgui.Separator()
    if imgui.Button("RESET CURRENT", 180, 34) then checklist_manual[checklist_index] = {} end
    imgui.SameLine()
    if imgui.Button("RESET ALL", 180, 34) then
        for index = 1, #checklists do checklist_manual[index] = {} end
    end
end

local function page_presets()
    heading("AIRCRAFT PRESETS")
    imgui.TextUnformatted("Translated to the closest native X-Plane commands. Apply only while parked.")
    if imgui.Button("COLD & DARK", 250, 60) then a380x_efb_apply_preset("COLD_AND_DARK") end
    imgui.SameLine(); if imgui.Button("TURNAROUND", 250, 60) then a380x_efb_apply_preset("TURNAROUND") end
    imgui.SameLine(); if imgui.Button("READY FOR TAXI", 250, 60) then a380x_efb_apply_preset("READY_FOR_TAXI") end
    imgui.Separator()
    imgui.TextUnformatted("These presets operate native X-Plane systems. They do not execute the FBW MSFS WASM preset procedure graph.")
end

local function page_settings()
    heading("SETTINGS / FLYPAD")
    local changed
    changed, metric_units = imgui.Checkbox("Metric units", metric_units)
    changed, auto_checklists = imgui.Checkbox("Autofill checklist items from native datarefs", auto_checklists)
    changed, battery_life_enabled = imgui.Checkbox("Tablet battery simulation", battery_life_enabled)
    changed, brightness = imgui.SliderInt("Brightness preference (floating window)", brightness, 10, 100, "%d%%")
    imgui.TextUnformatted("ImGui brightness is retained as a flyPad preference; X-Plane does not expose a per-window compositor brightness control.")
    imgui.Separator()
    imgui.TextUnformatted("ADAPTER STATUS")
    imgui.TextUnformatted(string.format("Bound datarefs: %d     ImGui host: ready", (function() local n=0 for _ in pairs(bound) do n=n+1 end return n end)()))
    imgui.TextUnformatted("Source: pinned FlyByWire A380X EFB / GPL-3.0\nRuntime: X-Plane 12 native datarefs and commands via FlyWithLua NG\n3-D tablet click: opens this floating flyPad window")
end

local page_builders = {
    Dashboard=page_dashboard, Dispatch=page_dispatch, Ground=page_ground,
    Performance=page_performance, Navigation=page_navigation, ATC=page_atc,
    Failures=page_failures, Checklists=page_checklists, Presets=page_presets,
    Settings=page_settings,
}

function a380x_efb_on_build(_window, _x, _y)
    if power_state == "SHUTOFF" or power_state == "STANDBY" then
        imgui.SetCursorPos(math.max(20, imgui.GetWindowWidth() / 2 - 90), math.max(20, imgui.GetWindowHeight() / 2 - 25))
        if imgui.Button("POWER ON", 180, 50) then
            power_state = "LOADING"
            loading_started = reading("time", 0) or 0
        end
        return
    elseif power_state == "EMPTY" then
        imgui.TextUnformatted("EFB BATTERY EMPTY\nConnect electrical power to recharge.")
        return
    elseif power_state == "LOADING" then
        imgui.TextUnformatted("FlyByWire\nflyPad OS\nLoading...")
        return
    end

    status_bar()
    navigation_bar()
    imgui.SameLine()
    imgui.BeginChild("flypad_content", 985, 655)
    page_builders[active_page]()
    imgui.EndChild()
end

function a380x_efb_onclose(_window)
    window = nil
end

function a380x_efb_open()
    if window or not target_aircraft() then return end
    fuel_target = clamp(reading("fuel_total", fuel_target) or fuel_target, 0, MAX_FUEL_KG)
    window = float_wnd_create(WINDOW_WIDTH, WINDOW_HEIGHT, 1, true)
    float_wnd_set_title(window, "A380X flyPad - " .. active_page)
    float_wnd_set_imgui_builder(window, "a380x_efb_on_build")
    float_wnd_set_onclose(window, "a380x_efb_onclose")
end

function a380x_efb_close()
    if not window then return end
    local closing = window
    window = nil
    float_wnd_destroy(closing)
end

function a380x_efb_toggle()
    if window then a380x_efb_close() else a380x_efb_open() end
end

function a380x_efb_next_page()
    local current = 1
    for index, name in ipairs(page_names) do if name == active_page then current = index end end
    active_page = page_names[current == #page_names and 1 or current + 1]
    if window then float_wnd_set_title(window, "A380X flyPad - " .. active_page) end
end

function a380x_efb_power_toggle()
    if power_state == "LOADED" then
        power_state = "STANDBY"
    else
        power_state = "LOADING"
        loading_started = reading("time", 0) or 0
        a380x_efb_open()
    end
end

-- The aircraft-local XPLM plugin owns the physical-tablet commands. Keep the
-- optional ImGui companion on a separate namespace so both can be installed.
create_command("a380x/xp12/efb/window_toggle", "A380X flyPad companion: open / close", "a380x_efb_toggle()", "", "")
create_command("a380x/xp12/efb/window_open", "A380X flyPad companion: open", "a380x_efb_open()", "", "")
create_command("a380x/xp12/efb/window_close", "A380X flyPad companion: close", "a380x_efb_close()", "", "")
create_command("a380x/xp12/efb/window_next_page", "A380X flyPad companion: next page", "a380x_efb_next_page()", "", "")
create_command("a380x/xp12/efb/window_power", "A380X flyPad companion: power / standby", "a380x_efb_power_toggle()", "", "")
add_macro("A380X: flyPad EFB", "a380x_efb_open()", "a380x_efb_close()", "deactivate")
do_every_frame("a380x_efb_update_core()")
message("optional companion loaded; the aircraft-local plugin owns both physical 3-D tablets")
