-- SPDX-License-Identifier: GPL-3.0-or-later
--
-- Experimental X-Plane 12 adaptation of FlyByWire A380X logic.
-- Derived from the GPL-3.0 FlyByWire aircraft repository at revision
-- 2baa2b35eadaf4c78e172ce41bbe6b40b4aeafb2, principally:
--   fbw-a380x/src/systems/instruments/src/Common/NXLogic.ts
--   fbw-a380x/src/systems/systems-host/CpiomC/FlightWarningSystem/FwsFlightPhases.ts
--
-- THIS IS SIMULATION SOFTWARE, NOT REAL-WORLD FLIGHT-CONTROL SOFTWARE.
-- The gains are a conservative starting point and require flight-test tuning.

if not SUPPORTS_FLOATING_WINDOWS then
    logMsg("A380X XP12: FlyWithLua NG is required")
end

-- FlyWithLua loads every file in Scripts for every aircraft.  Do not bind or
-- write global flight-control overrides unless this is the intended A380X.
-- Add-on aircraft such as ToLiss and FlightFactor own these overrides while
-- their FBW/control systems are running.
local function is_a380x()
    return PLANE_ICAO == "A388"
        and string.find(AIRCRAFT_PATH or "", "A380X%-XP12") ~= nil
end

if not is_a380x() then return end

-- Pilot inputs and aircraft state.
dataref("a380_pitch_input", "sim/joystick/yoke_pitch_ratio", "readonly")
dataref("a380_roll_input", "sim/joystick/yoke_roll_ratio", "readonly")
dataref("a380_yaw_input", "sim/joystick/yoke_heading_ratio", "readonly")
dataref("a380_g_normal", "sim/flightmodel/forces/g_nrml", "readonly")
dataref("a380_roll_rate", "sim/flightmodel/position/P", "readonly")
dataref("a380_yaw_rate", "sim/flightmodel/position/R", "readonly")
dataref("a380_ias", "sim/flightmodel/position/indicated_airspeed", "readonly")
dataref("a380_agl_m", "sim/flightmodel/position/y_agl", "readonly")
dataref("a380_on_ground", "sim/flightmodel/failures/onground_any", "readonly")
dataref("a380_paused", "sim/time/paused", "readonly")

-- X-Plane's explicit surface override and writable individual surface datarefs.
dataref("a380_override_surfaces", "sim/operation/override/override_control_surfaces", "writable")
dataref("a380_lail1", "sim/flightmodel/controls/lail1def", "writable")
dataref("a380_lail2", "sim/flightmodel/controls/lail2def", "writable")
dataref("a380_rail1", "sim/flightmodel/controls/rail1def", "writable")
dataref("a380_rail2", "sim/flightmodel/controls/rail2def", "writable")
dataref("a380_hstab1_elv1", "sim/flightmodel/controls/hstab1_elv1def", "writable")
dataref("a380_hstab1_elv2", "sim/flightmodel/controls/hstab1_elv2def", "writable")
dataref("a380_hstab2_elv1", "sim/flightmodel/controls/hstab2_elv1def", "writable")
dataref("a380_hstab2_elv2", "sim/flightmodel/controls/hstab2_elv2def", "writable")
dataref("a380_vstab1_rud1", "sim/flightmodel/controls/vstab1_rud1def", "writable")
dataref("a380_vstab1_rud2", "sim/flightmodel/controls/vstab1_rud2def", "writable")
dataref("a380_vstab2_rud1", "sim/flightmodel/controls/vstab2_rud1def", "writable")
dataref("a380_vstab2_rud2", "sim/flightmodel/controls/vstab2_rud2def", "writable")

local a380_n1 = dataref_table("sim/flightmodel/engine/ENGN_N1_")
local a380_throttle = dataref_table("sim/cockpit2/engine/actuators/throttle_ratio")

-- Public custom datarefs consumed by cockpit instruments and OBJ animations.
local fbw_enabled = create_dataref_table("a380x/xp12/fbw/enabled", "Int")
local normal_law = create_dataref_table("a380x/xp12/fbw/normal_law", "Int")
local target_g = create_dataref_table("a380x/xp12/fbw/target_g", "Float")
local elevator_ratio = create_dataref_table("a380x/xp12/fctl/elevator_ratio", "Float")
local aileron_ratio = create_dataref_table("a380x/xp12/fctl/aileron_ratio", "Float")
local rudder_ratio = create_dataref_table("a380x/xp12/fctl/rudder_ratio", "Float")
local flight_phase = create_dataref_table("a380x/xp12/fws/flight_phase", "Int")
local configured_v1 = create_dataref_table("a380x/xp12/config/v1_knots", "Float")

-- Start disabled until the independently authored Plane Maker model has its
-- stabilizer sign/range and every overridden surface verified. The experimental
-- law remains available through the toggle command for controlled testing.
fbw_enabled[0] = 0
configured_v1[0] = 145
normal_law[0] = 0
target_g[0] = 1
elevator_ratio[0] = 0
aileron_ratio[0] = 0
rudder_ratio[0] = 0
flight_phase[0] = 1

local function clamp(value, minimum, maximum)
    return math.max(minimum, math.min(maximum, value))
end

local function deadzone(value, width)
    if math.abs(value) <= width then return 0 end
    local sign = value < 0 and -1 or 1
    return sign * (math.abs(value) - width) / (1 - width)
end

local function rate_limit(current, requested, rate_per_second, dt)
    local maximum_step = rate_per_second * dt
    return current + clamp(requested - current, -maximum_step, maximum_step)
end

-- Lua translations of the FBW NXLogic timing and memory primitives.
local function ConfirmNode(seconds, rising_edge)
    return { t = seconds, rising = rising_edge ~= false, timer = 0, previous_input = nil, output = false }
end

local function confirm_write(node, value, dt)
    value = value and true or false
    if node.previous_input == nil then
        node.previous_input = value
        node.output = value
        return value
    end

    local delayed_edge = (node.rising and value) or ((not node.rising) and (not value))
    local edge_started = node.rising and (not node.previous_input) and value
        or (not node.rising) and node.previous_input and (not value)

    if not delayed_edge then
        node.timer = 0
        node.output = value
    elseif edge_started then
        node.timer = node.t
        node.output = not value
    elseif node.timer > 0 then
        node.timer = math.max(node.timer - dt, 0)
        node.output = node.timer == 0 and value or not value
    else
        node.output = value
    end

    node.previous_input = value
    return node.output
end

local function MemoryNode(set_has_priority)
    return { set_star = set_has_priority ~= false, value = false }
end

local function memory_write(node, set_value, reset_value)
    if set_value and reset_value then
        node.value = node.set_star
    elseif set_value then
        node.value = true
    elseif reset_value then
        node.value = false
    end
    return node.value
end

local ground_confirm = ConfirmNode(1, true)
local engine_confirm = ConfirmNode(3, true) -- shorter than FBW's 30 s while the port is being tested
local speed_above_80_memory = MemoryNode(true)
local speed_above_v1_memory = MemoryNode(true)
local shutdown_timer = 0

local pitch_integral = 0
local previous_g = 1
local elevator_command = 0
local aileron_command = 0
local rudder_command = 0

local MAX_ELEVATOR_DEG = 24
local MAX_AILERON_DEG = 25
local MAX_RUDDER_DEG = 30

local function engines_running()
    local count = 0
    for index = 0, 3 do
        if (a380_n1[index] or 0) > 15 then count = count + 1 end
    end
    return count
end

local function engines_takeoff_power()
    local count = 0
    for index = 0, 3 do
        if (a380_throttle[index] or 0) >= 0.85 then count = count + 1 end
    end
    return count >= 2
end

local function update_flight_phase(dt)
    local grounded = confirm_write(ground_confirm, a380_on_ground == 1, dt)
    local running_count = engines_running()
    local one_engine_running = confirm_write(engine_confirm, running_count >= 1, dt)
    local takeoff_power = engines_takeoff_power()
    local radio_height_ft = math.max(a380_agl_m, 0) * 3.280839895

    local above_80 = memory_write(speed_above_80_memory, a380_ias > 83, a380_ias < 77)
    local v1 = math.max(configured_v1[0], 1)
    local above_v1 = memory_write(speed_above_v1_memory, a380_ias > v1 + 3, a380_ias < v1 - 3)

    local phase
    if not one_engine_running and grounded then
        shutdown_timer = shutdown_timer + dt
        phase = shutdown_timer >= 300 and 12 or 1
    else
        shutdown_timer = 0
        if grounded and takeoff_power and not above_80 then phase = 3
        elseif grounded and takeoff_power and above_80 and not above_v1 then phase = 4
        elseif grounded and takeoff_power and above_v1 then phase = 5
        elseif (not grounded) and takeoff_power and radio_height_ft < 400 then phase = 6
        elseif (not grounded) and takeoff_power and radio_height_ft < 1500 then phase = 7
        elseif (not grounded) and radio_height_ft > 800 then phase = 8
        elseif (not grounded) then phase = 9
        elseif grounded and above_80 then phase = 10
        elseif grounded and one_engine_running then phase = 11
        else phase = 2 end
    end
    flight_phase[0] = phase
end

local function write_surfaces(elevator, aileron, rudder)
    -- X-Plane positive elevator deflection is trailing-edge down, opposite to
    -- the positive nose-up command used here.
    local elevator_deg = -elevator * MAX_ELEVATOR_DEG
    local left_aileron_deg = aileron * MAX_AILERON_DEG
    local right_aileron_deg = -left_aileron_deg
    local rudder_deg = rudder * MAX_RUDDER_DEG

    a380_lail1, a380_lail2 = left_aileron_deg, left_aileron_deg
    a380_rail1, a380_rail2 = right_aileron_deg, right_aileron_deg
    a380_hstab1_elv1, a380_hstab1_elv2 = elevator_deg, elevator_deg
    a380_hstab2_elv1, a380_hstab2_elv2 = elevator_deg, elevator_deg
    a380_vstab1_rud1, a380_vstab1_rud2 = rudder_deg, rudder_deg
    a380_vstab2_rud1, a380_vstab2_rud2 = rudder_deg, rudder_deg
end

local function release_override()
    a380_override_surfaces = 0
    normal_law[0] = 0
    pitch_integral = 0
end

function a380x_fbw_update()
    local dt = clamp(SIM_PERIOD or 0.016, 0.001, 0.05)
    -- Aircraft changes trigger a FlyWithLua reload, but do not release an
    -- override during the transition: the newly loaded aircraft may own it.
    if not is_a380x() then return end
    if fbw_enabled[0] == 0 or a380_paused == 1 then
        release_override()
        return
    end

    a380_override_surfaces = 1
    update_flight_phase(dt)

    local pitch = deadzone(clamp(a380_pitch_input, -1, 1), 0.03)
    local roll = deadzone(clamp(a380_roll_input, -1, 1), 0.03)
    local yaw = deadzone(clamp(a380_yaw_input, -1, 1), 0.03)
    local airborne = a380_on_ground == 0 and a380_ias > 80

    if airborne then
        normal_law[0] = 1

        -- Neutral stick commands 1 g. Full aft commands +2.5 g clean and
        -- full forward commands 0 g. This is intentionally bounded.
        local requested_g = pitch >= 0 and (1 + 1.5 * pitch) or (1 + pitch)
        target_g[0] = clamp(requested_g, 0, 2.5)

        local g_error = target_g[0] - a380_g_normal
        pitch_integral = clamp(pitch_integral + g_error * dt, -0.35, 0.35)
        local g_rate = (a380_g_normal - previous_g) / dt
        local scheduled_gain = clamp(170 / math.max(a380_ias, 120), 0.55, 1.25)
        local elevator_request = scheduled_gain * (0.55 * g_error + 0.22 * pitch_integral - 0.035 * g_rate)

        -- Roll-rate command with bank-rate damping. The output remains a
        -- normalized surface request; Plane Maker defines actual authority.
        local target_roll_rate = roll * 18
        local aileron_request = 0.055 * (target_roll_rate - a380_roll_rate)
        local rudder_request = yaw - 0.018 * a380_yaw_rate

        elevator_command = rate_limit(elevator_command, clamp(elevator_request, -1, 1), 0.8, dt)
        aileron_command = rate_limit(aileron_command, clamp(aileron_request, -1, 1), 1.4, dt)
        rudder_command = rate_limit(rudder_command, clamp(rudder_request, -1, 1), 1.0, dt)
    else
        -- Direct law on the ground and at very low speed.
        normal_law[0] = 0
        target_g[0] = 1
        pitch_integral = 0
        elevator_command = rate_limit(elevator_command, pitch, 1.5, dt)
        aileron_command = rate_limit(aileron_command, roll, 2.0, dt)
        rudder_command = rate_limit(rudder_command, yaw, 2.0, dt)
    end

    previous_g = a380_g_normal
    elevator_ratio[0] = elevator_command
    aileron_ratio[0] = aileron_command
    rudder_ratio[0] = rudder_command
    write_surfaces(elevator_command, aileron_command, rudder_command)
end

function a380x_fbw_toggle()
    fbw_enabled[0] = fbw_enabled[0] == 0 and 1 or 0
    if fbw_enabled[0] == 0 then release_override() end
end

create_command(
    "a380x/xp12/fbw/toggle",
    "Toggle the experimental A380X fly-by-wire law",
    "a380x_fbw_toggle()",
    "",
    ""
)

do_every_frame("a380x_fbw_update()")
do_on_exit("release_override()")
