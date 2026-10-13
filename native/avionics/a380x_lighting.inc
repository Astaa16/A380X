/* Four independent FlyByWire glareshield underside knobs for X-Plane. */
typedef struct {
    const char *name;
    float value;
    XPLMDataRef handle;
    XPLMCommandRef up, down;
    char up_name[96], down_name[96];
} LightingChannel;

static LightingChannel lighting[] = {
    {"captain_flood", 1.0f, NULL, NULL, NULL, "", ""},
    {"integral", 1.0f, NULL, NULL, NULL, "", ""},
    {"lcd", 1.0f, NULL, NULL, NULL, "", ""},
    {"first_officer_flood", 1.0f, NULL, NULL, NULL, "", ""},
};
static XPLMDataRef lighting_panel_brightness;

static void lighting_sync_flood(int index)
{
    float value;
    int spot;
    if (!lighting_panel_brightness || (index != 0 && index != 3)) return;
    value = lighting[index].value;
    spot = index == 0 ? 1 : 2;
    XPLMSetDatavf(lighting_panel_brightness, &value, spot, 1);
}

static float lighting_get(void *refcon) { return ((LightingChannel *)refcon)->value; }
static int lighting_get_int(void *refcon) { return (int)lroundf(((LightingChannel *)refcon)->value * 100.0f); }

static int lighting_command(XPLMCommandRef command, XPLMCommandPhase phase, void *refcon)
{
    int i;
    (void)refcon;
    if (!plugin_enabled || phase != xplm_CommandBegin) return 1;
    for (i = 0; i < ARRAY_LEN(lighting); ++i) {
        LightingChannel *channel = &lighting[i];
        float step = command == channel->up ? 0.05f : command == channel->down ? -0.05f : 0.0f;
        if (step != 0.0f) {
            channel->value = fminf(1.0f, fmaxf(0.0f, roundf((channel->value + step) * 20.0f) / 20.0f));
            lighting_sync_flood(i);
            break;
        }
    }
    return 1;
}

static void lighting_shutdown(void)
{
    int i;
    for (i = 0; i < ARRAY_LEN(lighting); ++i) {
        LightingChannel *channel = &lighting[i];
        if (channel->up) { XPLMUnregisterCommandHandler(channel->up, lighting_command, 1, NULL); channel->up = NULL; }
        if (channel->down) { XPLMUnregisterCommandHandler(channel->down, lighting_command, 1, NULL); channel->down = NULL; }
        if (channel->handle) { XPLMUnregisterDataAccessor(channel->handle); channel->handle = NULL; }
    }
}

static int lighting_init(void)
{
    int i;
    char name[96];
    lighting_panel_brightness = XPLMFindDataRef("sim/cockpit2/switches/panel_brightness_ratio");
    if (!lighting_panel_brightness) return 0;
    for (i = 0; i < ARRAY_LEN(lighting); ++i) {
        LightingChannel *channel = &lighting[i];
        channel->value = 1.0f;
        snprintf(name, sizeof(name), "a380x/xp12/lighting/%s", channel->name);
        if (XPLMFindDataRef(name)) { lighting_shutdown(); return 0; }
        channel->handle = XPLMRegisterDataAccessor(name, xplmType_Int | xplmType_Float, 0,
            lighting_get_int, NULL, lighting_get, NULL, NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL, channel, NULL);
        if (!channel->handle) { lighting_shutdown(); return 0; }
        snprintf(channel->up_name, sizeof(channel->up_name), "a380x/xp12/lighting/%s_up", channel->name);
        snprintf(channel->down_name, sizeof(channel->down_name), "a380x/xp12/lighting/%s_down", channel->name);
        channel->up = XPLMCreateCommand(channel->up_name, channel->up_name);
        channel->down = XPLMCreateCommand(channel->down_name, channel->down_name);
        if (!channel->up || !channel->down) { lighting_shutdown(); return 0; }
        XPLMRegisterCommandHandler(channel->up, lighting_command, 1, NULL);
        XPLMRegisterCommandHandler(channel->down, lighting_command, 1, NULL);
    }
    lighting_sync_flood(0);
    lighting_sync_flood(3);
    return 1;
}
