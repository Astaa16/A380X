/* SPDX-License-Identifier: GPL-3.0-or-later
 * Pedestal cockpit-door switch. The flight crew's inside handle remains a
 * separate native door control.
 */
typedef struct {
    int position; /* -1 UNLOCK, 0 NORM, 1 LOCK */
    int locked;
    int handlers_registered;
    XPLMDataRef doors;
    XPLMDataRef position_ref, locked_ref;
    XPLMCommandRef unlock, lock, door_toggle;
} CockpitDoorState;
static CockpitDoorState cockpit_door;

static int cockpit_door_get_position(void *refcon)
{
    (void)refcon;
    return cockpit_door.position;
}
static int cockpit_door_get_locked(void *refcon)
{
    (void)refcon;
    return cockpit_door.locked;
}
static float cockpit_door_open_ratio(void)
{
    float ratio=0.0f;
    XPLMGetDatavf(cockpit_door.doors,&ratio,16,1);
    return ratio;
}
static int cockpit_door_command(XPLMCommandRef command, XPLMCommandPhase phase,
                                void *refcon)
{
    (void)refcon;
    if (!plugin_enabled) return 1;
    if (phase==xplm_CommandBegin) {
        if (command==cockpit_door.unlock) {
            cockpit_door.position=-1;
            cockpit_door.locked=0;
            if (cockpit_door_open_ratio()<0.05f)
                XPLMCommandOnce(cockpit_door.door_toggle);
        } else if (command==cockpit_door.lock) {
            cockpit_door.position=1;
            cockpit_door.locked=cockpit_door_open_ratio()<0.05f;
        }
    } else if (phase==xplm_CommandEnd) {
        if ((command==cockpit_door.unlock && cockpit_door.position==-1) ||
            (command==cockpit_door.lock && cockpit_door.position==1))
            cockpit_door.position=0;
    }
    return 1;
}
static void cockpit_door_update(void)
{
    /* The electrically operated lock catches whenever the door closes. */
    cockpit_door.locked=cockpit_door.position!=-1 &&
                        cockpit_door_open_ratio()<0.05f;
}
static void cockpit_door_shutdown(void)
{
    if (cockpit_door.unlock && cockpit_door.handlers_registered) {
        XPLMUnregisterCommandHandler(cockpit_door.unlock,cockpit_door_command,1,NULL);
    }
    if (cockpit_door.lock && cockpit_door.handlers_registered) {
        XPLMUnregisterCommandHandler(cockpit_door.lock,cockpit_door_command,1,NULL);
    }
    cockpit_door.unlock=NULL;
    cockpit_door.lock=NULL;
    cockpit_door.handlers_registered=0;
    if (cockpit_door.position_ref) {
        XPLMUnregisterDataAccessor(cockpit_door.position_ref);
        cockpit_door.position_ref=NULL;
    }
    if (cockpit_door.locked_ref) {
        XPLMUnregisterDataAccessor(cockpit_door.locked_ref);
        cockpit_door.locked_ref=NULL;
    }
}
static int cockpit_door_init(void)
{
    memset(&cockpit_door,0,sizeof(cockpit_door));
    cockpit_door.doors=XPLMFindDataRef("sim/flightmodel2/misc/door_open_ratio");
    cockpit_door.door_toggle=XPLMFindCommand("sim/flight_controls/door_toggle_17");
    if (!cockpit_door.doors || !cockpit_door.door_toggle ||
        XPLMFindDataRef("a380x/xp12/cockpit_door/position") ||
        XPLMFindDataRef("a380x/xp12/cockpit_door/locked")) return 0;
    cockpit_door.position_ref=XPLMRegisterDataAccessor(
        "a380x/xp12/cockpit_door/position",xplmType_Int,0,
        cockpit_door_get_position,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL);
    cockpit_door.locked_ref=XPLMRegisterDataAccessor(
        "a380x/xp12/cockpit_door/locked",xplmType_Int,0,
        cockpit_door_get_locked,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL);
    /* XPLM retains commands across aircraft reloads; CreateCommand reuses them. */
    cockpit_door.unlock=XPLMCreateCommand("a380x/xp12/cockpit_door/unlock",
                                           "Hold CKPT DOOR at UNLOCK");
    cockpit_door.lock=XPLMCreateCommand("a380x/xp12/cockpit_door/lock",
                                         "Hold CKPT DOOR at LOCK");
    if (!cockpit_door.position_ref || !cockpit_door.locked_ref ||
        !cockpit_door.unlock || !cockpit_door.lock) {
        cockpit_door_shutdown();
        return 0;
    }
    XPLMRegisterCommandHandler(cockpit_door.unlock,cockpit_door_command,1,NULL);
    XPLMRegisterCommandHandler(cockpit_door.lock,cockpit_door_command,1,NULL);
    cockpit_door.handlers_registered=1;
    cockpit_door_update();
    return 1;
}
