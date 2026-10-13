-- Minimal host mock for exercising the control-law script outside X-Plane.

local script_path = assert(arg[1], "pass A380X_XP12_FBW.lua as the first argument")
local custom_datarefs = {}

SUPPORTS_FLOATING_WINDOWS = true
PLANE_ICAO = "A388"
AIRCRAFT_PATH = "/Aircraft/A380X-XP12/"
SIM_PERIOD = 0.02

function logMsg(_) end

function dataref(variable_name, _, _)
    _G[variable_name] = 0
end

function dataref_table(_)
    local values = {}
    for index = 0, 63 do values[index] = 0 end
    return values
end

function create_dataref_table(name, _)
    local values = { [0] = 0 }
    custom_datarefs[name] = values
    return values
end

function create_command(_, _, _, _, _) end
function do_every_frame(_) end
function do_on_exit(_) end

assert(loadfile(script_path))()

-- The independent development aircraft keeps the experimental law disabled
-- until it is explicitly enabled for isolated tests.
assert(custom_datarefs["a380x/xp12/fbw/enabled"][0] == 0, "experimental law should start disabled")
a380x_fbw_toggle()
assert(custom_datarefs["a380x/xp12/fbw/enabled"][0] == 1, "toggle did not enable experimental law")

-- Airborne, full-aft input: the law should command +2.5 g and enable override.
a380_paused = 0
a380_on_ground = 0
a380_ias = 220
a380_g_normal = 1
a380_pitch_input = 1
a380_roll_input = 0
a380_yaw_input = 0
a380x_fbw_update()

assert(a380_override_surfaces == 1, "surface override was not enabled")
assert(math.abs(custom_datarefs["a380x/xp12/fbw/target_g"][0] - 2.5) < 0.001, "unexpected target g")
assert(custom_datarefs["a380x/xp12/fbw/normal_law"][0] == 1, "normal law was not selected")
assert(a380_hstab1_elv1 < 0, "aft stick should command trailing-edge-up elevator")

-- A different aircraft must immediately receive normal X-Plane controls.
PLANE_ICAO = "C172"
a380x_fbw_update()
assert(a380_override_surfaces == 0, "override leaked into another aircraft")

print("FlyWithLua mock checks passed")
