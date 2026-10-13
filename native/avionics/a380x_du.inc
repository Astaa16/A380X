/* Display Unit Control Panel. Two sides, four active potentiometers each.
 * OIT, PFD/ND exchange, and RECONF are unavailable in the A380X reference.
 */
typedef struct {
    const char *name;
    float level;
    XPLMDataRef handle;
    XPLMCommandRef up, down;
    char up_name[96], down_name[96];
} DuChannel;

enum { DU_PFD, DU_ND, DU_WX_TERR, DU_MFD, DU_CHANNELS_PER_SIDE };
static DuChannel du_channels[] = {
    {"captain_pfd", 1.0f, NULL, NULL, NULL, "", ""},
    {"captain_nd", 1.0f, NULL, NULL, NULL, "", ""},
    {"captain_wx_terr", 1.0f, NULL, NULL, NULL, "", ""},
    {"captain_mfd", 1.0f, NULL, NULL, NULL, "", ""},
    {"first_officer_pfd", 1.0f, NULL, NULL, NULL, "", ""},
    {"first_officer_nd", 1.0f, NULL, NULL, NULL, "", ""},
    {"first_officer_wx_terr", 1.0f, NULL, NULL, NULL, "", ""},
    {"first_officer_mfd", 1.0f, NULL, NULL, NULL, "", ""},
};

static float du_get(void *refcon) { return ((DuChannel *)refcon)->level; }
static int du_get_int(void *refcon) { return (int)lroundf(((DuChannel *)refcon)->level * 100.0f); }

static int du_command(XPLMCommandRef command, XPLMCommandPhase phase, void *refcon)
{
    int i;
    (void)refcon;
    if (!plugin_enabled || phase != xplm_CommandBegin) return 1;
    for (i = 0; i < ARRAY_LEN(du_channels); ++i) {
        DuChannel *channel = &du_channels[i];
        float step = command == channel->up ? 0.05f :
                     command == channel->down ? -0.05f : 0.0f;
        if (step != 0.0f) {
            channel->level = fminf(1.0f, fmaxf(0.0f,
                roundf((channel->level + step) * 20.0f) / 20.0f));
            break;
        }
    }
    return 1;
}

static void du_shutdown(void)
{
    int i;
    for (i = 0; i < ARRAY_LEN(du_channels); ++i) {
        DuChannel *channel = &du_channels[i];
        if (channel->up) { XPLMUnregisterCommandHandler(channel->up, du_command, 1, NULL); channel->up = NULL; }
        if (channel->down) { XPLMUnregisterCommandHandler(channel->down, du_command, 1, NULL); channel->down = NULL; }
        if (channel->handle) { XPLMUnregisterDataAccessor(channel->handle); channel->handle = NULL; }
    }
}

static int du_init(void)
{
    int i;
    char name[96];
    for (i = 0; i < ARRAY_LEN(du_channels); ++i) {
        DuChannel *channel = &du_channels[i];
        channel->level = 1.0f;
        snprintf(name, sizeof(name), "a380x/xp12/du/%s", channel->name);
        if (XPLMFindDataRef(name)) { du_shutdown(); return 0; }
        channel->handle = XPLMRegisterDataAccessor(name, xplmType_Int | xplmType_Float, 0,
            du_get_int, NULL, du_get, NULL, NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL, channel, NULL);
        if (!channel->handle) { du_shutdown(); return 0; }
        snprintf(channel->up_name, sizeof(channel->up_name), "a380x/xp12/du/%s_up", channel->name);
        snprintf(channel->down_name, sizeof(channel->down_name), "a380x/xp12/du/%s_down", channel->name);
        channel->up = XPLMCreateCommand(channel->up_name, channel->up_name);
        channel->down = XPLMCreateCommand(channel->down_name, channel->down_name);
        if (!channel->up || !channel->down) { du_shutdown(); return 0; }
        XPLMRegisterCommandHandler(channel->up, du_command, 1, NULL);
        XPLMRegisterCommandHandler(channel->down, du_command, 1, NULL);
    }
    return 1;
}
