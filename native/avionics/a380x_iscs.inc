/* A380 ISCS popup. Coordinates use the shared 720 x 500 EFB atlas. */
static int iscs_tab;
static int iscs_config[8] = {0, 1, 0, 1, 1, 0, 0, 0};
static int iscs_sound[6] = {70, 100, 80, 85, 80, 70};
static XPLMDataRef iscs_sound_refs[6];
static int iscs_sound_refs_ready;
static int iscs_pax = 400, iscs_cargo, iscs_fuel_target = 160000;
static float iscs_saved_payload, iscs_saved_fuel;
static int iscs_saved_load_valid;
static struct { float l, b, r, t; int action; } iscs_hits[96];
static int iscs_hit_count;

static void iscs_hit(float l, float b, float r, float t, int action)
{
    if (iscs_hit_count >= ARRAY_LEN(iscs_hits)) return;
    iscs_hits[iscs_hit_count].l = l; iscs_hits[iscs_hit_count].b = b;
    iscs_hits[iscs_hit_count].r = r; iscs_hits[iscs_hit_count].t = t;
    iscs_hits[iscs_hit_count++].action = action;
}

static void iscs_label(float x, float y, const char *s, EfbColor color)
{ efb_label(x, y, s, color); }

static void iscs_title(float x, float y, const char *s)
{ iscs_label(x, y, s, EFB_WHITE); }

static void iscs_button(float l, float b, float r, float t, const char *s, int action, int enabled)
{
    efb_box(l, b, r, t, enabled ? EFB_HIGHLIGHT : EFB_STATUS);
    iscs_label(l + 7, b + 6, s, enabled ? EFB_BODY : EFB_UNSELECTED);
    if (enabled) iscs_hit(l, b, r, t, action);
}

static void iscs_switch(float x, float y, const char *name, int on, int action)
{
    iscs_label(x, y, name, EFB_WHITE);
    iscs_button(x + 263, y - 4, x + 299, y + 15, on ? "ON" : "OFF", action, 1);
}

static void iscs_slider(float x, float y, float width, const char *name,
                        int value, int maximum, int action)
{
    char number[32];
    iscs_label(x, y + 15, name, EFB_WHITE);
    efb_rule(x, y + 7, x + width, y + 7, EFB_UNSELECTED);
    efb_box(x + width * value / maximum - 5, y + 2,
            x + width * value / maximum + 5, y + 12, EFB_HIGHLIGHT);
    snprintf(number, sizeof(number), "%d", value);
    iscs_label(x + width - 40, y + 15, number, EFB_HIGHLIGHT);
    iscs_hit(x, y - 2, x + width, y + 29, action);
}

static void iscs_mark(void)
{
    static const unsigned char glyph[4][7] = {
        {31, 4, 4, 4, 4, 4, 31},
        {15, 16, 16, 14, 1, 1, 30},
        {15, 16, 16, 16, 16, 16, 15},
        {15, 16, 16, 14, 1, 1, 30}
    };
    int letter, row, col;
    for (letter = 0; letter < 4; ++letter)
        for (row = 0; row < 7; ++row)
            for (col = 0; col < 5; ++col)
                if (glyph[letter][row] & (1u << (4 - col))) {
                    float x = 270.0f + letter * 42.0f + col * 7.0f;
                    float y = 1006.0f - row * 7.0f;
                    efb_box(x, y - 6.0f, x + 6.0f, y, EFB_HIGHLIGHT);
                }
}

static void iscs_header(void)
{
    static const char *const tabs[8][2] = {
        {"STARTUP/", "SITUATIONS"}, {"A/C + SIM", "CONFIG"},
        {"LOADING/", "PERFO"}, {"GROUND", "SERVICES"},
        {"FAULT", "SCENARIOS"}, {"SOUNDS/", "ADDONS"},
        {"JOYSTICK/", "ACTIONS"}, {"SETTINGS/", "ACCOUNTS"}
    };
    int i;
    iscs_mark();
    iscs_label(162, 995, "ISCS", EFB_HIGHLIGHT);
    iscs_label(450, 990, "Interactive Simulation", EFB_HIGHLIGHT);
    iscs_label(500, 974, "Control System", EFB_HIGHLIGHT);
    for (i = 0; i < 8; ++i) {
        float x = 155.0f + i * 89.0f;
        iscs_label(x + 2, 946, tabs[i][0], i == iscs_tab ? EFB_WHITE : EFB_HIGHLIGHT);
        iscs_label(x + 2, 932, tabs[i][1], i == iscs_tab ? EFB_WHITE : EFB_HIGHLIGHT);
        if (i == iscs_tab) efb_rule(x + 2, 924, x + 85, 924, EFB_HIGHLIGHT);
        iscs_hit(x, 921, x + 87, 964, 100 + i);
    }
    efb_rule(511, 540, 511, 905, EFB_UNSELECTED);
}

static void iscs_startup(void)
{
    iscs_title(228, 891, "LOAD/SAVE SITUATIONS");
    iscs_label(175, 864, "AVAILABLE SITUATIONS", EFB_UNSELECTED);
    efb_box(175, 688, 495, 852, EFB_SECONDARY);
    iscs_label(184, 833, iscs_saved_load_valid ? "SAVED_LOAD_AND_FUEL" : "CURRENT_SITUATION", EFB_HIGHLIGHT);
    iscs_button(175, 660, 323, 684, "LOAD SAVED LOAD", 1, iscs_saved_load_valid && on_ground() && engines_off());
    iscs_button(338, 660, 495, 684, "SAVE LOAD/FUEL", 2, 1);
    iscs_label(185, 625, "AUTO-SAVING", EFB_WHITE);
    iscs_switch(175, 596, "AUTOSAVE", iscs_config[7], 20);
    iscs_slider(175, 551, 320, "INTERVAL MIN", 30, 60, 90);

    iscs_title(638, 891, "STARTUP");
    iscs_button(526, 850, 675, 875, "ENG 1 + 2 START", 3, on_ground());
    iscs_button(687, 850, 854, 875, "ENG 3 + 4 START", 11, on_ground());
    iscs_button(526, 820, 675, 845, "APU QUICK START", 4, on_ground());
    iscs_button(687, 820, 854, 845, "ADIRS", 5, 1);
    iscs_title(637, 791, "AIRCRAFT STATE");
    iscs_button(526, 750, 627, 774, "COLD + DARK", 6, on_ground());
    iscs_button(638, 750, 741, 774, "TURNAROUND", 7, on_ground());
    iscs_button(750, 750, 854, 774, "READY TAXI", 8, on_ground());
    iscs_title(618, 713, "SIMULATOR PAUSE");
    iscs_button(526, 674, 854, 699, "FREEZE ALL", 9, 1);
    iscs_button(526, 644, 854, 669, "UNFREEZE", 10, 1);
    iscs_label(532, 593, "A380-842 / FOUR ENGINES", EFB_UNSELECTED);
}

static void iscs_config_page(void)
{
    iscs_title(232, 891, "SIMULATOR CONFIGURATION");
    iscs_label(175, 857, "ENABLE ISCS IN EFB", EFB_WHITE);
    iscs_button(438, 853, 493, 872, "N/A", 0, 0);
    iscs_switch(175, 830, "USE AVITAB", iscs_config[1], 22);
    iscs_switch(175, 803, "SYNC BARO SETTINGS", iscs_config[2], 23);
    iscs_switch(175, 776, "FCU MOUSE GESTURES", iscs_config[3], 24);
    iscs_title(263, 739, "VISUAL SETTINGS");
    iscs_slider(175, 693, 145, "WING FLEX", 40, 100, 91);
    iscs_slider(343, 693, 145, "GEAR BUMPS", 50, 100, 92);
    iscs_switch(175, 657, "COCKPIT LIGHTING", iscs_config[4], 25);
    iscs_switch(175, 630, "GLASS REFLECTIONS", iscs_config[5], 26);
    iscs_switch(175, 603, "XP PARTICLES", iscs_config[6], 27);
    iscs_button(175, 550, 495, 577, "SAVE CONFIG", 28, 1);
    iscs_label(175, 531, "SESSION PREFERENCES", EFB_UNSELECTED);

    iscs_title(617, 891, "AIRCRAFT CONFIGURATION");
    iscs_label(532, 852, "AIRCRAFT: A380-842", EFB_WHITE);
    iscs_label(532, 822, "ENGINES: 4 X TRENT 972B-84", EFB_WHITE);
    iscs_label(532, 792, "MAIN DECK DOORS: 10", EFB_WHITE);
    iscs_label(532, 762, "UPPER DECK DOORS: 6", EFB_WHITE);
    iscs_label(532, 732, "COCKPIT DOOR: 1", EFB_WHITE);
    iscs_title(596, 685, "PERFORMANCE LIMITS");
    iscs_label(532, 650, "MTOW 510000 KG", EFB_HIGHLIGHT);
    iscs_label(532, 622, "MLW  395000 KG", EFB_HIGHLIGHT);
    iscs_label(532, 594, "MZFW 373000 KG", EFB_HIGHLIGHT);
    iscs_label(532, 566, "FUEL 323546 KG", EFB_HIGHLIGHT);
}

static void iscs_loading(void)
{
    char v[96];
    float zfw = get_float(refs.gross_weight) - get_float(refs.fuel_total);
    iscs_title(259, 891, "AIRCRAFT LOADING");
    iscs_title(290, 866, "PAYLOAD");
    iscs_slider(175, 815, 320, "NO PAX (100 KG EACH)", iscs_pax, 853, 30);
    iscs_slider(175, 765, 320, "CARGO KG", iscs_cargo, 40000, 31);
    snprintf(v, sizeof(v), "TARGET PAYLOAD %d KG", iscs_pax * 100 + iscs_cargo);
    iscs_label(179, 728, v, EFB_WHITE);
    snprintf(v, sizeof(v), "LIVE ZFW %.0f / 373000 KG", zfw);
    iscs_label(179, 702, v, zfw > 373000 ? EFB_RED : EFB_WHITE);
    iscs_button(175, 667, 495, 691, "APPLY LOAD SETTINGS", 32, on_ground() && engines_off());
    efb_rule(175, 649, 495, 649, EFB_UNSELECTED);
    iscs_title(292, 628, "BLOCK FUEL");
    iscs_slider(175, 578, 320, "BLOCK FUEL KG", iscs_fuel_target, 323546, 33);
    iscs_button(175, 546, 325, 570, "QUICK DE/REFUEL", 34, on_ground() && engines_off());
    iscs_button(336, 546, 495, 570, "FUEL 100%", 35, on_ground() && engines_off());

    iscs_title(611, 891, "CG-GW DIAGRAM");
    efb_box(530, 694, 852, 869, EFB_SECONDARY);
    efb_rule(544, 717, 832, 717, EFB_UNSELECTED);
    efb_rule(544, 717, 570, 844, EFB_UNSELECTED);
    efb_rule(832, 717, 806, 844, EFB_UNSELECTED);
    efb_rule(570, 844, 806, 844, EFB_UNSELECTED);
    efb_rule(570, 744, 806, 744, EFB_UNSELECTED);
    snprintf(v, sizeof(v), "MTOW 510000 KG  /  MLW 395000 KG");
    iscs_label(548, 818, v, EFB_WHITE);
    snprintf(v, sizeof(v), "GROSS %.0f KG", get_float(refs.gross_weight));
    iscs_label(615, 770, v, EFB_HIGHLIGHT);
    iscs_label(545, 675, "CG ENVELOPE: NO LIVE CG DATA", EFB_UNSELECTED);
    efb_rule(526, 652, 854, 652, EFB_UNSELECTED);
    iscs_title(622, 627, "TO PERFORMANCE");
    iscs_label(545, 588, "DEPARTURE RUNWAY DATA", EFB_UNSELECTED);
    iscs_label(555, 562, "REQUIRES FMS INTEGRATION", EFB_UNSELECTED);
}

static void iscs_ground(void)
{
    char v[48];
    float doors[20] = {0};
    int row, side;
    static const int main_indices[5][2] = {{0,1},{2,3},{4,5},{6,7},{8,9}};
    static const int upper_indices[3][2] = {{10,11},{12,13},{14,15}};
    if (refs.doors) XPLMGetDatavf(refs.doors, doors, 0, 20);
    iscs_title(272, 891, "GROUND SUPPLIES");
    iscs_switch(175, 853, "EXTERNAL POWER", gpu_connected(), 40);
    iscs_label(175, 823, "HIGH PRESSURE AIR", EFB_WHITE);
    iscs_button(437, 819, 493, 838, "N/A", 0, 0);
    iscs_label(175, 795, "LOW PRESSURE AIR", EFB_WHITE);
    iscs_button(437, 791, 493, 810, "N/A", 0, 0);
    iscs_label(175, 767, "CHOCKS", EFB_WHITE);
    iscs_button(437, 763, 493, 782, "N/A", 0, 0);
    efb_rule(175, 740, 495, 740, EFB_UNSELECTED);
    iscs_title(297, 716, "PUSHBACK");
    iscs_button(175, 672, 495, 700, "START PUSHBACK", 41, on_ground());
    iscs_button(175, 637, 495, 665, "STOP PUSHBACK", 42, on_ground());
    iscs_button(175, 582, 325, 610, "JETWAY", 43, on_ground());
    iscs_button(336, 582, 495, 610, "SERVICES", 44, on_ground());
    iscs_button(175, 547, 495, 575, "TOGGLE ALL 20 DOORS", 45, on_ground());

    iscs_title(641, 891, "AIRCRAFT DOORS");
    iscs_label(538, 864, "MAIN DECK", EFB_HIGHLIGHT);
    for (row = 0; row < 5; ++row) for (side = 0; side < 2; ++side) {
        int index = main_indices[row][side];
        float x = side ? 700.0f : 538.0f, y = 835.0f - row * 27.0f;
        snprintf(v, sizeof(v), "M%d%c %s", row + 1, side ? 'R' : 'L', doors[index] > .5f ? "OPEN" : "CLOSED");
        iscs_button(x, y, x + 150, y + 22, v, 50 + index, on_ground());
    }
    iscs_label(538, 687, "UPPER DECK", EFB_HIGHLIGHT);
    for (row = 0; row < 3; ++row) for (side = 0; side < 2; ++side) {
        int index = upper_indices[row][side];
        float x = side ? 700.0f : 538.0f, y = 658.0f - row * 27.0f;
        snprintf(v, sizeof(v), "U%d%c %s", row + 1, side ? 'R' : 'L', doors[index] > .5f ? "OPEN" : "CLOSED");
        iscs_button(x, y, x + 150, y + 22, v, 50 + index, on_ground());
    }
    iscs_button(538, 581, 850, 603, doors[16] > .5f ? "COCKPIT DOOR OPEN" : "COCKPIT DOOR CLOSED", 66, on_ground());
    iscs_label(538, 568, "CARGO HOLDS", EFB_HIGHLIGHT);
    for (row = 0; row < 3; ++row) {
        static const char *const names[] = {"FWD", "AFT", "BULK"};
        float x = 538.0f + row * 105.0f;
        float ratio = doors[17 + row];
        snprintf(v, sizeof(v), "%s %s", names[row], ratio > .99f ? "OPEN" : ratio < .01f ? "CLOSED" : "MOVING");
        iscs_button(x, 540, x + 102, 562, v, 67 + row, on_ground());
    }
}

static void iscs_faults(void)
{
    char v[80];
    iscs_title(450, 891, "RANDOM FAULTS");
    iscs_label(392, 865, "BASED ON FLIGHT TIME", EFB_UNSELECTED);
    iscs_label(175, 818, "ENABLE RANDOM FAULTS", EFB_WHITE);
    iscs_button(438, 814, 493, 833, "N/A", 0, 0);
    iscs_label(531, 820, "FLIGHT TIME FACTOR: N/A", EFB_UNSELECTED);
    iscs_button(685, 746, 850, 773, "SAVE AS DEFAULT", 72, 0);
    efb_rule(175, 721, 850, 721, EFB_UNSELECTED);
    iscs_title(380, 694, "CUSTOM FAULT SELECTION");
    iscs_button(175, 653, 258, 677, "PREVIOUS", 73, 1);
    snprintf(v, sizeof(v), "%d / 17  %s", selected_failure + 1, failure_names[selected_failure]);
    iscs_label(274, 661, v, EFB_HIGHLIGHT);
    iscs_button(550, 653, 625, 677, "NEXT", 74, 1);
    iscs_button(641, 653, 745, 677, "APPLY NOW", 75, 1);
    iscs_button(752, 653, 850, 677, "RESET ALL", 76, 1);
    iscs_label(187, 609, "X-PLANE SYSTEM FAILURES", EFB_UNSELECTED);
    iscs_label(187, 573, "SELECT A SYSTEM, THEN APPLY OR REPAIR", EFB_UNSELECTED);
}

static void iscs_sounds(void)
{
    static const char *names[6] = {"ENGINE VOL", "SYSTEM VOL", "COCKPIT SND", "ENVIRONMENT", "GROUND CNTCT", "AURAL ALERT"};
    int i;
    iscs_title(276, 891, "SOUND SETTINGS");
    iscs_label(200, 852, "USE X-PLANE MASTER AUDIO SLIDERS", EFB_UNSELECTED);
    iscs_title(262, 806, "COMPONENT VOLUME");
    for (i = 0; i < 6; ++i) {
        if (iscs_sound_refs[i])
            iscs_sound[i] = (int)lroundf(get_float(iscs_sound_refs[i]) * 100.0f);
        if (iscs_sound[i] < 0) iscs_sound[i] = 0;
        if (iscs_sound[i] > 100) iscs_sound[i] = 100;
        iscs_slider(175, 764.0f - i * 34.0f, 320, names[i], iscs_sound[i], 100, 80 + i);
    }
    iscs_button(175, 549, 495, 576, "SAVE THESE SETTINGS", 86, 1);
    iscs_title(628, 891, "ADDONS");
    iscs_label(580, 852, "NO ADDONS AT THIS TIME", EFB_UNSELECTED);
}

static void iscs_joystick(void)
{
    iscs_title(247, 891, "JOYSTICK CONFIGURATION");
    iscs_label(175, 851, "PRIMARY JOYSTICK: X-PLANE INPUT", EFB_WHITE);
    iscs_label(175, 820, "AXIS CALIBRATION: X-PLANE SETTINGS", EFB_UNSELECTED);
    iscs_title(312, 771, "TILLER");
    iscs_label(175, 733, "USE X-PLANE NOSEWHEEL STEERING", EFB_WHITE);
    iscs_title(310, 683, "BRAKES");
    iscs_label(175, 646, "PEDAL BRAKES: X-PLANE INPUT", EFB_WHITE);
    iscs_button(175, 552, 495, 579, "SAVE THESE SETTINGS", 87, 1);
    iscs_title(610, 891, "THROTTLE CONFIGURATION");
    iscs_label(537, 851, "FOUR ENGINE THROTTLES", EFB_WHITE);
    iscs_label(537, 820, "DETENTS: NATIVE A380 CONTROLS", EFB_UNSELECTED);
    efb_rule(526, 747, 852, 747, EFB_UNSELECTED);
    iscs_title(650, 720, "ACTIONS");
    iscs_button(526, 674, 852, 704, "X-PLANE JOYSTICK SETTINGS", 88, 0);
}

static void iscs_settings(void)
{
    iscs_title(287, 891, "PRODUCT INFO");
    iscs_label(175, 851, "AIRCRAFT: A380-842", EFB_WHITE);
    iscs_label(175, 821, "SIMULATOR: X-PLANE 12", EFB_WHITE);
    iscs_label(175, 791, "INTERFACE: A380 ISCS", EFB_WHITE);
    efb_rule(175, 767, 495, 767, EFB_UNSELECTED);
    iscs_title(292, 742, "ACCOUNT IDS");
    iscs_label(175, 704, "SIMBRIEF: EXTERNAL CLIENT REQUIRED", EFB_UNSELECTED);
    iscs_label(175, 674, "CPDLC: EXTERNAL CLIENT REQUIRED", EFB_UNSELECTED);
    iscs_title(604, 891, "STARTUP BEHAVIOUR");
    iscs_switch(532, 851, "METRIC UNITS", metric_units, 89);
    iscs_switch(532, 822, "AUTO CHECKLIST", automatic_checklists, 93);
    iscs_switch(532, 793, "BATTERY SIM", battery_simulation, 94);
    efb_rule(526, 755, 852, 755, EFB_UNSELECTED);
    iscs_title(626, 727, "USER INTERFACE");
    iscs_slider(532, 678, 315, "BRIGHTNESS", brightness, 100, 95);
    iscs_label(532, 613, "WINDOW AND COCKPIT TABLETS", EFB_UNSELECTED);
    iscs_button(526, 550, 852, 578, "SAVE PREFERENCES", 96, 1);
}

static void a380x_iscs_draw_window(int left, int bottom, int right, int top)
{
    static const char *const sound_names[6] = {
        "sim/operation/sound/engine_volume_ratio",
        "sim/operation/sound/ui_volume_ratio",
        "sim/operation/sound/interior_volume_ratio",
        "sim/operation/sound/enviro_volume_ratio",
        "sim/operation/sound/ground_volume_ratio",
        "sim/operation/sound/warning_volume_ratio"
    };
    static void (*const pages[8])(void) = {iscs_startup, iscs_config_page,
        iscs_loading, iscs_ground, iscs_faults, iscs_sounds, iscs_joystick,
        iscs_settings};
    efb_host_left = left; efb_host_bottom = bottom;
    efb_host_right = right; efb_host_top = top;
    efb_in_window = 1;
    if (!iscs_sound_refs_ready) {
        int i;
        for (i = 0; i < 6; ++i) iscs_sound_refs[i] = XPLMFindDataRef(sound_names[i]);
        iscs_sound_refs_ready = 1;
    }
    iscs_hit_count = 0;
    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    efb_box(EFB_LEFT, EFB_BOTTOM, EFB_RIGHT, EFB_TOP, EFB_BODY);
    iscs_header();
    pages[iscs_tab]();
    efb_in_window = 0;
}

static void a380x_iscs_click(float x, float y)
{
    int i, action = -1;
    for (i = iscs_hit_count - 1; i >= 0; --i)
        if (x >= iscs_hits[i].l && x <= iscs_hits[i].r &&
            y >= iscs_hits[i].b && y <= iscs_hits[i].t) {
            action = iscs_hits[i].action; break;
        }
    if (action >= 100 && action < 108) { iscs_tab = action - 100; return; }
    if (action >= 50 && action <= 69) {
        if (!on_ground()) { set_status("Door controls are ground-only"); return; }
        char name[64];
        snprintf(name, sizeof(name), "sim/flight_controls/door_toggle_%d", action - 49);
        run_command(name); return;
    }
    if (action >= 80 && action <= 85) {
        int value = (int)(100 * (x - 175) / 320);
        if (value < 0) value = 0;
        if (value > 100) value = 100;
        iscs_sound[action - 80] = value;
        if (iscs_sound_refs[action - 80])
            XPLMSetDataf(iscs_sound_refs[action - 80], value / 100.0f);
        return;
    }
    switch (action) {
    case 1:
        if (iscs_saved_load_valid) {
            set_payload(iscs_saved_payload);
            set_fuel(iscs_saved_fuel);
        }
        break;
    case 2:
        iscs_saved_payload = get_float(refs.payload);
        iscs_saved_fuel = get_float(refs.fuel_total);
        iscs_saved_load_valid = 1;
        set_status("Payload/fuel snapshot saved for this session");
        break;
    case 3:
        run_command("sim/starters/engage_starter_1");
        run_command("sim/starters/engage_starter_2");
        break;
    case 11:
        run_command("sim/starters/engage_starter_3");
        run_command("sim/starters/engage_starter_4");
        break;
    case 4:
        /* APU MASTER on, then START (the A380 network intercepts APU_start for its ECB). */
        if (elec_network_active()) {
            XPLMDataRef master = XPLMFindDataRef("sim/cockpit2/electrical/APU_starter_switch");
            if (master != NULL && XPLMGetDatai(master) < 1) XPLMSetDatai(master, 1);
        }
        run_command("sim/electrical/APU_start");
        break;
    case 5: run_command("a380x/xp12/adirs/window_toggle"); break;
    case 6: preset_cold_dark(); break;
    case 7: preset_turnaround(); break;
    case 8: preset_ready(); break;
    case 9: run_command("sim/operation/pause_on"); break;
    case 10: run_command("sim/operation/pause_off"); break;
    case 20: iscs_config[7] = !iscs_config[7]; break;
    case 22: case 23: case 24: case 25: case 26: case 27:
        iscs_config[action - 21] = !iscs_config[action - 21]; break;
    case 28: set_status("ISCS configuration applied for this session"); break;
    case 30: iscs_pax = (int)(853 * (x - 175) / 320); break;
    case 31: iscs_cargo = (int)(40000 * (x - 175) / 320); break;
    case 32: set_payload((float)(iscs_pax * 100 + iscs_cargo)); break;
    case 33: iscs_fuel_target = (int)(MAX_FUEL_KG * (x - 175) / 320); break;
    case 34: set_fuel((float)iscs_fuel_target); break;
    case 35: set_fuel(MAX_FUEL_KG); break;
    case 40: gpu_toggle(); break;
    case 41: run_command("sim/ground_ops/pushback_straight"); break;
    case 42: run_command("sim/ground_ops/pushback_stop"); break;
    case 43: run_command("sim/ground_ops/jetway"); break;
    case 44: run_command("sim/ground_ops/service_plane"); break;
    case 45: toggle_all_doors(); break;
    case 73: selected_failure = (selected_failure + 16) % 17; break;
    case 74: selected_failure = (selected_failure + 1) % 17; break;
    case 75: action_failures(2); break;
    case 76: action_failures(3); break;
    case 89: metric_units = !metric_units; break;
    case 93: automatic_checklists = !automatic_checklists; break;
    case 94: battery_simulation = !battery_simulation; break;
    case 95: brightness = 20 + (int)(80 * (x - 532) / 315); break;
    default: break;
    }
    if (iscs_pax < 0) iscs_pax = 0;
    if (iscs_pax > 853) iscs_pax = 853;
    if (iscs_cargo < 0) iscs_cargo = 0;
    if (iscs_cargo > 40000) iscs_cargo = 40000;
    if (iscs_fuel_target < 0) iscs_fuel_target = 0;
    if (iscs_fuel_target > (int)MAX_FUEL_KG) iscs_fuel_target = (int)MAX_FUEL_KG;
}
