/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X ISIS 1 core: logic + display list. Native translation of FlyByWire
 * fbw-a380x/src/systems/instruments/src/ISISlegacy (index.tsx, ISISDisplayUnit.tsx,
 * ArtificialHorizon*.tsx, PitchScale.tsx, RollScale.tsx, RollIndex.tsx, AirplaneSymbol.tsx,
 * AirspeedIndicator.tsx, VerticalTape.tsx, AltitudeIndicator.tsx, DigitalAltitudeIndicator.tsx,
 * MachIndicator.tsx, PressureIndicator.tsx, LandingSystem.tsx, BugSetupDisplay.tsx, Bug.ts,
 * Att10sFlag.tsx, AutoBrightness.tsx, ISISUtils.tsx, style.scss) and of the FBW cockpit
 * behaviour templates A32NX_Interior_ISIS.xml (knob / push buttons).
 *
 * Coordinates: FBW's SVG is 512 x 512 user units, y down. The device is 1024 x 1024 texels,
 * so the base matrix is scale(2). The attitude group carries FBW's CSS transform
 * `scale(1.1) translate(-30px, -30px)` (transform-origin 0 0), i.e. 2.2 texels per unit.
 * Everything is emitted as triangles with a one-texel alpha fringe (anti-aliasing that does
 * not depend on MSAA in X-Plane's device framebuffer). Clipping (FBW nested <svg> viewports)
 * is done on the CPU, so no scissor or stencil state is touched.
 *
 * No XPLM and no OpenGL here: include after a380x_isis.h and a380x_isis_font.h, plus
 * <math.h>, <stdio.h>, <stdlib.h>, <string.h>. All statics are prefixed isis_.
 */

/* ---- Colours (FBW Common/definitions.scss + ISISlegacy/style.scss) ------------------------- */
typedef struct { float r, g, b; } IsisColor;
static const IsisColor ISIS_WHITE = {1.0f, 1.0f, 1.0f};
static const IsisColor ISIS_AMBER = {230.0f / 255.0f, 128.0f / 255.0f, 0.0f};
static const IsisColor ISIS_CYAN = {0.0f, 1.0f, 1.0f};
static const IsisColor ISIS_GREEN = {0.0f, 1.0f, 0.0f};
static const IsisColor ISIS_MAGENTA = {1.0f, 148.0f / 255.0f, 1.0f};
static const IsisColor ISIS_RED = {1.0f, 0.0f, 0.0f};
static const IsisColor ISIS_YELLOW = {1.0f, 1.0f, 0.0f};
static const IsisColor ISIS_BLACK = {0.0f, 0.0f, 0.0f};
static const IsisColor ISIS_SKY = {6.0f / 255.0f, 152.0f / 255.0f, 1.0f};      /* .sky #0698ff */
static const IsisColor ISIS_EARTH = {156.0f / 255.0f, 72.0f / 255.0f, 12.0f / 255.0f}; /* .earth #9c480c */

#define ISIS_HPA_PER_INHG 33.8638867f
#define ISIS_STD_INHG 29.92f
#define ISIS_AH_STROKE 3.332f      /* .ArtificialHorizon stroke-width */
#define ISIS_ROOT_STROKE 2.0f      /* .ISIS stroke-width */
#define ISIS_POWER_UP_SECONDS 90.0f /* ISISDisplayUnit powerUpTime */
#define ISIS_STANDBY_SECONDS 10.0f
#define ISIS_PAGE_TIMEOUT 15.0f    /* index.tsx: bugs page closes after 15 s without input */
#define ISIS_ATT_RST_HOLD 2.0f     /* Att10sFlag: RST pressed for 2 s */
#define ISIS_ATT_RST_SECONDS 10.0f
#define ISIS_BRIGHT_MIN 0.03f      /* AutoBrightness.tsx */
#define ISIS_BRIGHT_MAX 0.99f
#define ISIS_BRIGHT_STEP 0.025f
#define ISIS_BRIGHT_INTERVAL 0.15f

static float isis_clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static double isis_js_round(double v) { return floor(v + 0.5); } /* JavaScript Math.round */

/* ---- Logic ----------------------------------------------------------------------------------- */

static int isis_has_power(const IsisInputs *in)
{
    /* ISISDisplayUnit: (indicatedAirspeed > 50 && DC HOT 1) || DC ESS. */
    return (isfinite(in->ias_kts) && in->ias_kts > 50.0f && in->dc_hot) || in->dc_ess;
}

static void isis_reset(IsisState *s, int powered)
{
    int i;
    memset(s, 0, sizeof(*s));
    /* FBW: cold-and-dark spawn -> Off (90 s self-test on power-up); otherwise the unit is
     * already aligned and shows the display immediately. */
    s->du = powered ? ISIS_DU_ON : ISIS_DU_OFF;
    s->page = ISIS_PAGE_PFD;
    s->selected_bug = ISIS_BUG_SPD3; /* index.tsx useState(5) */
    for (i = 0; i < ISIS_BUGS; ++i) s->bug_value[i] = i < ISIS_BUG_SPD0 ? 100 : 30; /* FLT defaults */
    s->baro_saved_inhg = ISIS_STD_INHG;
    s->ambient = 0.5f;
    s->auto_brightness = 0.5f;
    s->brightness = 0.5f;
}

static void isis_bug_limits(int bug, int *lo, int *hi, int *inc)
{
    /* Bug.ts bugPropertiesByType */
    if (bug < ISIS_BUG_SPD0) { *lo = 100; *hi = 50000; *inc = 100; }
    else { *lo = 30; *hi = 660; *inc = 1; }
}

static void isis_brightness_step(IsisState *s, float step)
{
    s->manual_offset = isis_clampf(s->manual_offset + step, ISIS_BRIGHT_MIN - s->auto_brightness,
                                   ISIS_BRIGHT_MAX - s->auto_brightness);
    s->brightness = isis_clampf(s->auto_brightness + s->manual_offset, ISIS_BRIGHT_MIN, ISIS_BRIGHT_MAX);
}

static void isis_set_baro(IsisState *s, float inhg)
{
    s->baro_write = 1;
    s->baro_write_inhg = isis_clampf(inhg, 22.0f, 32.99f);
}

static void isis_menu_select(IsisState *s)
{
    switch (s->menu_index) {
    case ISIS_MENU_SPD1: s->page = ISIS_PAGE_BUGS; s->selected_bug = ISIS_BUG_SPD3; break;
    case ISIS_MENU_SPD2: s->page = ISIS_PAGE_BUGS; s->selected_bug = ISIS_BUG_SPD2; break;
    case ISIS_MENU_ALT: s->page = ISIS_PAGE_BUGS; s->selected_bug = ISIS_BUG_ALT0; break;
    case ISIS_MENU_METERS: s->metric_alt = !s->metric_alt; break;     /* ISIS_METRIC_ALTITUDE */
    case ISIS_MENU_BARO_UNIT: s->baro_unit_inhg = !s->baro_unit_inhg; break; /* ISIS_BARO_UNIT_INHG */
    case ISIS_MENU_EXIT: s->page = ISIS_PAGE_PFD; break;
    default: break; /* DISPLAY NAV / DISPLAY FIX: not available in FBW */
    }
}

/* One physical control event. pressed = 1 on command begin, 0 on command end. */
static void isis_button(IsisState *s, const IsisInputs *in, IsisButton button, int pressed)
{
    int lo, hi, inc;
    if (!pressed) {
        if (button == ISIS_BTN_PLUS) s->plus_held = 0;
        else if (button == ISIS_BTN_MINUS) s->minus_held = 0;
        else if (button == ISIS_BTN_MENU && s->menu_held) {
            s->menu_held = 0;
            if (!s->menu_hold_used && s->du != ISIS_DU_OFF) {
                s->page = s->page == ISIS_PAGE_MENU ? ISIS_PAGE_PFD : ISIS_PAGE_MENU;
                s->menu_index = 0;
                s->idle = 0.0f;
            }
        }
        return;
    }
    ++s->button_count;
    if (s->du == ISIS_DU_OFF) return;
    /* The SND only uses + / - (brightness); MODE (SFD/SND swap) is a pair action. */
    if (s->role == ISIS_ROLE_SND && button != ISIS_BTN_PLUS && button != ISIS_BTN_MINUS) return;
    s->idle = 0.0f;
    switch (button) {
    case ISIS_BTN_MODE: /* SFD <-> SND swap: isis_pair_swap(), called by the adapter */
        break;
    case ISIS_BTN_LS: /* FBW BTN_ID LS: toggles L:A32NX_ISIS_LS_ACTIVE */
        s->ls_active = !s->ls_active;
        break;
    case ISIS_BTN_PLUS:
    case ISIS_BTN_MINUS: {
        int plus = button == ISIS_BTN_PLUS;
        if (s->page == ISIS_PAGE_BUGS)
            s->selected_bug = plus ? (7 + s->selected_bug) % 6 : (5 + s->selected_bug) % 6;
        else if (s->page == ISIS_PAGE_MENU)
            s->menu_index = (s->menu_index + (plus ? ISIS_MENU_COUNT - 1 : 1)) % ISIS_MENU_COUNT;
        else {
            /* AutoBrightness: held + / - steps 0.025 every 150 ms, first step at once. */
            if (plus) s->plus_held = 1; else s->minus_held = 1;
            s->hold_timer = 0.0f;
            isis_brightness_step(s, plus ? ISIS_BRIGHT_STEP : -ISIS_BRIGHT_STEP);
        }
        break;
    }
    case ISIS_BTN_MENU:
        /* Short press: MENU page. Held 2 s: ATT RST (FBW BTN_ID RST on this pushbutton). */
        s->menu_held = 1; s->menu_hold = 0.0f; s->menu_hold_used = 0;
        break;
    case ISIS_BTN_SET_PUSH:
        if (s->page == ISIS_PAGE_BUGS) s->bug_on[s->selected_bug] = !s->bug_on[s->selected_bug];
        else if (s->page == ISIS_PAGE_MENU) isis_menu_select(s);
        else if (!s->baro_std) { /* FBW knob LEFT_SINGLE_CODE: QNH -> STD, saving QNH */
            s->baro_std = 1;
            s->baro_saved_inhg = isfinite(in->baro_inhg) ? in->baro_inhg : ISIS_STD_INHG;
            isis_set_baro(s, ISIS_STD_INHG);
        } else {
            s->baro_std = 0;
            isis_set_baro(s, s->baro_saved_inhg);
        }
        break;
    case ISIS_BTN_SET_UP:
    case ISIS_BTN_SET_DOWN: {
        int up = button == ISIS_BTN_SET_UP;
        if (s->page == ISIS_PAGE_BUGS) {
            int *value = &s->bug_value[s->selected_bug];
            if (!s->bug_on[s->selected_bug]) break; /* index.tsx: inactive bugs do not move */
            isis_bug_limits(s->selected_bug, &lo, &hi, &inc);
            if ((up && *value >= hi) || (!up && *value <= lo)) break;
            *value = *value + (up ? inc : -inc);
            if (*value > hi) *value = hi;
            if (*value < lo) *value = lo;
        } else if (s->page == ISIS_PAGE_MENU) {
            s->menu_index = (s->menu_index + (up ? 1 : ISIS_MENU_COUNT - 1)) % ISIS_MENU_COUNT;
        } else if (!s->baro_std) {
            float base = s->baro_write ? s->baro_write_inhg : in->baro_inhg;
            if (!isfinite(base)) base = ISIS_STD_INHG;
            if (s->baro_unit_inhg) /* K:KOHLSMAN_INC / DEC */
                isis_set_baro(s, (float)(isis_js_round(base * 100.0) + (up ? 1 : -1)) / 100.0f);
            else /* 1 hPa per detent */
                isis_set_baro(s, (float)(isis_js_round(base * ISIS_HPA_PER_INHG) + (up ? 1 : -1)) / ISIS_HPA_PER_INHG);
        }
        break;
    }
    default: break;
    }
}

/* Two ISIS: exactly one is the SFD. MODE on either unit swaps the roles; the new SFD takes
 * over the SFD settings (bugs, baro, LS, units) so the standby flight data stay continuous. */
static void isis_pair_swap(IsisState *unit)
{
    int sfd = unit[0].role == ISIS_ROLE_SFD ? 0 : 1, snd = 1 - sfd, i;
    IsisState *from = &unit[sfd], *to = &unit[snd];
    for (i = 0; i < ISIS_BUGS; ++i) { to->bug_value[i] = from->bug_value[i]; to->bug_on[i] = from->bug_on[i]; }
    to->selected_bug = from->selected_bug;
    to->ls_active = from->ls_active; to->baro_std = from->baro_std;
    to->baro_unit_inhg = from->baro_unit_inhg; to->metric_alt = from->metric_alt;
    to->baro_saved_inhg = from->baro_saved_inhg;
    from->role = ISIS_ROLE_SND; to->role = ISIS_ROLE_SFD;
    for (i = 0; i < 2; ++i) {
        unit[i].page = ISIS_PAGE_PFD; unit[i].att_rst = 0;
        unit[i].menu_held = 0; unit[i].plus_held = unit[i].minus_held = 0;
    }
}

/* An SFD is always displayed: an unpowered SFD hands the role to a live SND. */
static void isis_pair_update(IsisState *unit)
{
    int sfd = unit[0].role == ISIS_ROLE_SFD ? 0 : 1;
    if (unit[0].role == unit[1].role) { unit[0].role = ISIS_ROLE_SFD; unit[1].role = ISIS_ROLE_SND; return; }
    if (unit[sfd].du == ISIS_DU_OFF && unit[1 - sfd].du != ISIS_DU_OFF) isis_pair_swap(unit);
}

static void isis_update(IsisState *s, const IsisInputs *in, float dt)
{
    int elec = isis_has_power(in);
    if (!(dt >= 0.0f)) dt = 0.0f;
    if (dt > 1.0f) dt = 1.0f;
    s->clock = fmodf(s->clock + dt, 3600.0f);
    /* ISISDisplayUnit useUpdate: the timer, then useEffect: the transitions. */
    if (s->du_timer_active) {
        if (s->du_timer > 0.0f) s->du_timer -= dt;
        else if (s->du == ISIS_DU_STANDBY) { s->du = ISIS_DU_OFF; s->du_timer_active = 0; }
        else if (s->du == ISIS_DU_SELFTEST) { s->du = ISIS_DU_ON; s->du_timer_active = 0; }
    }
    if (s->du == ISIS_DU_ON && !elec) {
        s->du = ISIS_DU_STANDBY; s->du_timer = ISIS_STANDBY_SECONDS; s->du_timer_active = 1;
    } else if (s->du == ISIS_DU_STANDBY && elec) {
        s->du = ISIS_DU_ON; s->du_timer_active = 0;
    } else if (s->du == ISIS_DU_OFF && elec) {
        s->du = ISIS_DU_SELFTEST; s->du_timer = ISIS_POWER_UP_SECONDS; s->du_timer_active = 1;
    } else if (s->du == ISIS_DU_SELFTEST && !elec) {
        s->du = ISIS_DU_OFF; s->du_timer_active = 0;
    }
    if (s->du == ISIS_DU_OFF || s->du == ISIS_DU_SELFTEST || s->role == ISIS_ROLE_SND) {
        s->page = ISIS_PAGE_PFD; s->att_rst = 0; s->menu_held = 0;
    }
    /* Pages close after 15 s without pilot input (index.tsx). */
    if (s->page != ISIS_PAGE_PFD) {
        s->idle += dt;
        if (s->idle > ISIS_PAGE_TIMEOUT) { s->page = ISIS_PAGE_PFD; s->idle = 0.0f; }
    }
    /* Att10sFlag */
    if (s->menu_held) {
        s->menu_hold += dt;
        if (s->menu_hold >= ISIS_ATT_RST_HOLD && !s->menu_hold_used) {
            s->menu_hold_used = 1;
            if (s->du == ISIS_DU_ON && !s->att_rst) { s->att_rst = 1; s->att_rst_timer = ISIS_ATT_RST_SECONDS; }
        }
    }
    if (s->att_rst) {
        if (s->att_rst_timer > 0.0f) s->att_rst_timer -= dt;
        else s->att_rst = 0;
    }
    /* AutoBrightness: X-Plane's photo-cell value replaces GLASSCOCKPIT AUTOMATIC BRIGHTNESS. */
    s->auto_brightness = isis_clampf(isfinite(s->ambient) ? s->ambient : 0.5f, 0.0f, 1.0f);
    if ((s->plus_held || s->minus_held) && s->page == ISIS_PAGE_PFD) {
        s->hold_timer += dt;
        while (s->hold_timer >= ISIS_BRIGHT_INTERVAL) {
            s->hold_timer -= ISIS_BRIGHT_INTERVAL;
            isis_brightness_step(s, s->plus_held ? ISIS_BRIGHT_STEP : -ISIS_BRIGHT_STEP);
        }
    }
    isis_brightness_step(s, 0.0f);
    /* RollIndex SideslipIndicator: LagFilter(0.8) stepped every 500 ms. */
    s->slip_timer += dt;
    while (s->slip_timer >= 0.5f) {
        float g = isfinite(in->lateral_g) ? in->lateral_g : 0.0f;
        float acc = isis_clampf((float)isis_js_round(g * 100.0) / 100.0f, -0.3f, 0.3f);
        float input = -acc * 30.0f / 0.3f, scaled = 0.5f * 0.8f, sum0 = scaled + 2.0f;
        float output = (input + s->slip_in) * scaled / sum0 + (2.0f - scaled) / sum0 * s->slip_out;
        s->slip_timer -= 0.5f;
        s->slip_in = input;
        if (isfinite(output)) { s->slip_out = output; s->slip_offset = output; }
        else s->slip_offset = 0.0f;
    }
    /* MachIndicator hysteresis */
    if (isfinite(in->mach)) {
        if (in->mach > 0.5f && !s->mach_visible) s->mach_visible = 1;
        else if (in->mach < 0.45f && s->mach_visible) s->mach_visible = 0;
    }
}

/* ---- Display list primitives ------------------------------------------------------------- */
enum { ISIS_START, ISIS_MIDDLE, ISIS_END };
enum { ISIS_MAX_PATH = 192 };
typedef struct { float m[6], clip[4]; int clipped; } IsisSave;

static void isis_save(const IsisList *l, IsisSave *s)
{
    memcpy(s->m, l->m, sizeof(s->m)); memcpy(s->clip, l->clip, sizeof(s->clip)); s->clipped = l->clipped;
}
static void isis_restore(IsisList *l, const IsisSave *s)
{
    memcpy(l->m, s->m, sizeof(s->m)); memcpy(l->clip, s->clip, sizeof(s->clip)); l->clipped = s->clipped;
}
static void isis_mul(IsisList *l, float a, float b, float c, float d, float e, float f)
{
    float *m = l->m, n[6];
    n[0] = m[0] * a + m[2] * b; n[1] = m[1] * a + m[3] * b;
    n[2] = m[0] * c + m[2] * d; n[3] = m[1] * c + m[3] * d;
    n[4] = m[0] * e + m[2] * f + m[4]; n[5] = m[1] * e + m[3] * f + m[5];
    memcpy(m, n, sizeof(n));
}
static void isis_translate(IsisList *l, float x, float y) { isis_mul(l, 1, 0, 0, 1, x, y); }
static void isis_scale(IsisList *l, float s) { isis_mul(l, s, 0, 0, s, 0, 0); }
static void isis_rotate(IsisList *l, float degrees, float cx, float cy)
{
    float r = degrees * 3.14159265358979f / 180.0f;
    isis_translate(l, cx, cy);
    isis_mul(l, cosf(r), sinf(r), -sinf(r), cosf(r), 0, 0);
    isis_translate(l, -cx, -cy);
}
static void isis_xf(const IsisList *l, float x, float y, float *ox, float *oy)
{
    *ox = l->m[0] * x + l->m[2] * y + l->m[4];
    *oy = l->m[1] * x + l->m[3] * y + l->m[5];
}
static float isis_unit(const IsisList *l) { return sqrtf(fabsf(l->m[0] * l->m[3] - l->m[1] * l->m[2])); }

static void isis_list_reset(IsisList *l)
{
    l->vertex_count = l->batch_count = l->overflow = l->font_misses = 0;
    l->clipped = 0;
    l->m[0] = 2.0f; l->m[1] = 0.0f; l->m[2] = 0.0f; l->m[3] = 2.0f; l->m[4] = 0.0f; l->m[5] = 0.0f;
}

/* Nested <svg x y width height> viewport: local origin moves to (x, y), content is clipped. */
static void isis_viewport(IsisList *l, float x, float y, float w, float h)
{
    float x0, y0, x1, y1, t;
    isis_translate(l, x, y);
    isis_xf(l, 0, 0, &x0, &y0);
    isis_xf(l, w, h, &x1, &y1);
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    if (l->clipped) {
        x0 = fmaxf(x0, l->clip[0]); y0 = fmaxf(y0, l->clip[1]);
        x1 = fminf(x1, l->clip[2]); y1 = fminf(y1, l->clip[3]);
    }
    l->clip[0] = x0; l->clip[1] = y0; l->clip[2] = x1; l->clip[3] = y1;
    l->clipped = 1;
}

static IsisVertex isis_vertex(float x, float y, float u, float v, IsisColor c, float a)
{
    IsisVertex out;
    out.x = x; out.y = y; out.u = u; out.v = v; out.r = c.r; out.g = c.g; out.b = c.b; out.a = a;
    return out;
}
static IsisVertex isis_lerp(const IsisVertex *a, const IsisVertex *b, float t)
{
    IsisVertex o;
    o.x = a->x + (b->x - a->x) * t; o.y = a->y + (b->y - a->y) * t;
    o.u = a->u + (b->u - a->u) * t; o.v = a->v + (b->v - a->v) * t;
    o.r = a->r + (b->r - a->r) * t; o.g = a->g + (b->g - a->g) * t;
    o.b = a->b + (b->b - a->b) * t; o.a = a->a + (b->a - a->a) * t;
    return o;
}

/* Sutherland-Hodgman against one axis-aligned plane. axis 0 = x, 1 = y; keep >= / <= edge. */
static int isis_clip_plane(const IsisVertex *in, int n, IsisVertex *out, int axis, float edge, int keep_greater)
{
    int i, count = 0;
    for (i = 0; i < n; ++i) {
        const IsisVertex *a = &in[i], *b = &in[(i + 1) % n];
        float da = (axis ? a->y : a->x) - edge, db = (axis ? b->y : b->x) - edge;
        int ina, inb;
        if (!keep_greater) { da = -da; db = -db; }
        ina = da >= 0.0f; inb = db >= 0.0f;
        if (ina) out[count++] = *a;
        if (ina != inb) out[count++] = isis_lerp(a, b, da / (da - db));
    }
    return count;
}

static void isis_emit_tri(IsisList *l, const IsisVertex *a, const IsisVertex *b, const IsisVertex *c, int textured)
{
    IsisBatch *batch;
    if (l->vertex_count + 3 > ISIS_MAX_VERTICES) { l->overflow = 1; return; }
    if (!l->batch_count || l->batch[l->batch_count - 1].textured != textured) {
        if (l->batch_count >= ISIS_MAX_BATCHES) { l->overflow = 1; return; }
        batch = &l->batch[l->batch_count++];
        batch->first = l->vertex_count; batch->count = 0; batch->textured = textured;
    }
    batch = &l->batch[l->batch_count - 1];
    l->vertex[l->vertex_count++] = *a;
    l->vertex[l->vertex_count++] = *b;
    l->vertex[l->vertex_count++] = *c;
    batch->count += 3;
}

static void isis_tri(IsisList *l, IsisVertex a, IsisVertex b, IsisVertex c, int textured)
{
    IsisVertex p[12], q[12];
    int n, i;
    if (!l->clipped) { isis_emit_tri(l, &a, &b, &c, textured); return; }
    if (fminf(a.x, fminf(b.x, c.x)) >= l->clip[0] && fmaxf(a.x, fmaxf(b.x, c.x)) <= l->clip[2] &&
        fminf(a.y, fminf(b.y, c.y)) >= l->clip[1] && fmaxf(a.y, fmaxf(b.y, c.y)) <= l->clip[3]) {
        isis_emit_tri(l, &a, &b, &c, textured); return;
    }
    p[0] = a; p[1] = b; p[2] = c;
    n = isis_clip_plane(p, 3, q, 0, l->clip[0], 1);
    n = isis_clip_plane(q, n, p, 0, l->clip[2], 0);
    n = isis_clip_plane(p, n, q, 1, l->clip[1], 1);
    n = isis_clip_plane(q, n, p, 1, l->clip[3], 0);
    for (i = 1; i + 1 < n; ++i) isis_emit_tri(l, &p[0], &p[i], &p[i + 1], textured);
}

static void isis_quad(IsisList *l, IsisVertex a, IsisVertex b, IsisVertex c, IsisVertex d, int textured)
{
    isis_tri(l, a, b, c, textured);
    isis_tri(l, a, c, d, textured);
}

/* Transform local points to device texels, dropping consecutive duplicates. */
static int isis_device_points(const IsisList *l, const float *pts, int n, float out[][2], int closed)
{
    int i, count = 0;
    if (n > ISIS_MAX_PATH) n = ISIS_MAX_PATH;
    for (i = 0; i < n; ++i) {
        float x, y;
        isis_xf(l, pts[2 * i], pts[2 * i + 1], &x, &y);
        if (count && fabsf(x - out[count - 1][0]) < 1e-4f && fabsf(y - out[count - 1][1]) < 1e-4f) continue;
        out[count][0] = x; out[count][1] = y; ++count;
    }
    if (closed && count > 2 && fabsf(out[0][0] - out[count - 1][0]) < 1e-4f &&
        fabsf(out[0][1] - out[count - 1][1]) < 1e-4f) --count;
    return count;
}

/* One-texel anti-aliasing fringe along polygon edges. side = +1 outside the polygon, -1 inside. */
static void isis_edge_fringe(IsisList *l, float d[][2], int n, int closed, IsisColor c, int side)
{
    double area = 0.0;
    int i, edges = closed ? n : n - 1;
    float sigma;
    for (i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        area += (double)d[i][0] * d[j][1] - (double)d[j][0] * d[i][1];
    }
    sigma = area >= 0.0 ? 1.0f : -1.0f;
    for (i = 0; i < edges; ++i) {
        int j = (i + 1) % n;
        float dx = d[j][0] - d[i][0], dy = d[j][1] - d[i][1], len = sqrtf(dx * dx + dy * dy), nx, ny;
        if (len < 1e-5f) continue;
        nx = -dy / len * -sigma * (float)side; ny = dx / len * -sigma * (float)side;
        isis_quad(l, isis_vertex(d[i][0], d[i][1], 0, 0, c, 1), isis_vertex(d[j][0], d[j][1], 0, 0, c, 1),
                  isis_vertex(d[j][0] + nx, d[j][1] + ny, 0, 0, c, 0),
                  isis_vertex(d[i][0] + nx, d[i][1] + ny, 0, 0, c, 0), 0);
    }
}

static int isis_point_in_tri(const float *p, const float *a, const float *b, const float *c)
{
    float d1 = (p[0] - b[0]) * (a[1] - b[1]) - (a[0] - b[0]) * (p[1] - b[1]);
    float d2 = (p[0] - c[0]) * (b[1] - c[1]) - (b[0] - c[0]) * (p[1] - c[1]);
    float d3 = (p[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (p[1] - a[1]);
    int neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

/* Filled simple polygon (SVG fill, nonzero == even-odd for simple outlines): ear clipping. */
static void isis_fill(IsisList *l, const float *pts, int n, IsisColor c, int aa)
{
    float d[ISIS_MAX_PATH][2];
    int index[ISIS_MAX_PATH], count, i, guard = 0;
    double area = 0.0;
    count = isis_device_points(l, pts, n, d, 1);
    if (count < 3) return;
    for (i = 0; i < count; ++i) {
        int j = (i + 1) % count;
        area += (double)d[i][0] * d[j][1] - (double)d[j][0] * d[i][1];
        index[i] = i;
    }
    n = count;
    while (n > 3 && guard++ < 4 * ISIS_MAX_PATH) {
        int found = 0;
        for (i = 0; i < n && !found; ++i) {
            int ia = index[(i + n - 1) % n], ib = index[i], ic = index[(i + 1) % n], k;
            float cross = (d[ib][0] - d[ia][0]) * (d[ic][1] - d[ib][1]) - (d[ib][1] - d[ia][1]) * (d[ic][0] - d[ib][0]);
            if ((area > 0.0 && cross <= 0.0f) || (area < 0.0 && cross >= 0.0f)) continue;
            for (k = 0; k < n; ++k) {
                int ik = index[k];
                if (ik == ia || ik == ib || ik == ic) continue;
                if (isis_point_in_tri(d[ik], d[ia], d[ib], d[ic])) break;
            }
            if (k < n) continue;
            isis_tri(l, isis_vertex(d[ia][0], d[ia][1], 0, 0, c, 1), isis_vertex(d[ib][0], d[ib][1], 0, 0, c, 1),
                     isis_vertex(d[ic][0], d[ic][1], 0, 0, c, 1), 0);
            memmove(&index[i], &index[i + 1], sizeof(int) * (size_t)(n - i - 1));
            --n; found = 1;
        }
        if (!found) break;
    }
    for (i = 1; i + 1 < n; ++i)
        isis_tri(l, isis_vertex(d[index[0]][0], d[index[0]][1], 0, 0, c, 1),
                 isis_vertex(d[index[i]][0], d[index[i]][1], 0, 0, c, 1),
                 isis_vertex(d[index[i + 1]][0], d[index[i + 1]][1], 0, 0, c, 1), 0);
    if (aa) isis_edge_fringe(l, d, count, 1, c, 1);
}

static void isis_rect(IsisList *l, float x, float y, float w, float h, IsisColor c, int aa)
{
    const float p[] = {x, y, x + w, y, x + w, y + h, x, y + h};
    isis_fill(l, p, 4, c, aa);
}

/* SVG stroke: miter joins (limit 4), butt or square caps, one-texel alpha fringe each side. */
static void isis_stroke(IsisList *l, const float *pts, int n, int closed, float width, IsisColor c, int square_cap)
{
    float d[ISIS_MAX_PATH][2], miter[ISIS_MAX_PATH][2];
    float w = width * isis_unit(l), h0 = w * 0.5f - 0.5f, h1 = w * 0.5f + 0.5f;
    int count = isis_device_points(l, pts, n, d, closed), i, segments;
    if (count < 2) return;
    if (count < 3) closed = 0;
    if (h0 < 0.0f) h0 = 0.0f;
    segments = closed ? count : count - 1;
    if (!closed && square_cap) {
        float dx = d[1][0] - d[0][0], dy = d[1][1] - d[0][1], len = sqrtf(dx * dx + dy * dy);
        if (len > 0) { d[0][0] -= dx / len * w * 0.5f; d[0][1] -= dy / len * w * 0.5f; }
        dx = d[count - 1][0] - d[count - 2][0]; dy = d[count - 1][1] - d[count - 2][1]; len = sqrtf(dx * dx + dy * dy);
        if (len > 0) { d[count - 1][0] += dx / len * w * 0.5f; d[count - 1][1] += dy / len * w * 0.5f; }
    }
    for (i = 0; i < count; ++i) {
        int has_in = closed || i > 0, has_out = closed || i < count - 1;
        float nin[2] = {0, 0}, nout[2] = {0, 0}, mx, my, len, cosine;
        if (has_in) {
            int p = (i + count - 1) % count;
            float dx = d[i][0] - d[p][0], dy = d[i][1] - d[p][1];
            len = sqrtf(dx * dx + dy * dy);
            if (len > 0) { nin[0] = -dy / len; nin[1] = dx / len; }
        }
        if (has_out) {
            int q = (i + 1) % count;
            float dx = d[q][0] - d[i][0], dy = d[q][1] - d[i][1];
            len = sqrtf(dx * dx + dy * dy);
            if (len > 0) { nout[0] = -dy / len; nout[1] = dx / len; }
        }
        if (!has_in) { miter[i][0] = nout[0]; miter[i][1] = nout[1]; continue; }
        if (!has_out) { miter[i][0] = nin[0]; miter[i][1] = nin[1]; continue; }
        mx = nin[0] + nout[0]; my = nin[1] + nout[1]; len = sqrtf(mx * mx + my * my);
        if (len < 1e-4f) { miter[i][0] = nin[0]; miter[i][1] = nin[1]; continue; }
        mx /= len; my /= len;
        cosine = mx * nin[0] + my * nin[1];
        if (cosine < 0.25f) cosine = 0.25f; /* SVG stroke-miterlimit 4 */
        miter[i][0] = mx / cosine; miter[i][1] = my / cosine;
    }
    for (i = 0; i < segments; ++i) {
        int j = (i + 1) % count;
        IsisVertex li = isis_vertex(d[i][0] + miter[i][0] * h0, d[i][1] + miter[i][1] * h0, 0, 0, c, 1);
        IsisVertex lj = isis_vertex(d[j][0] + miter[j][0] * h0, d[j][1] + miter[j][1] * h0, 0, 0, c, 1);
        IsisVertex ri = isis_vertex(d[i][0] - miter[i][0] * h0, d[i][1] - miter[i][1] * h0, 0, 0, c, 1);
        IsisVertex rj = isis_vertex(d[j][0] - miter[j][0] * h0, d[j][1] - miter[j][1] * h0, 0, 0, c, 1);
        IsisVertex lo_i = isis_vertex(d[i][0] + miter[i][0] * h1, d[i][1] + miter[i][1] * h1, 0, 0, c, 0);
        IsisVertex lo_j = isis_vertex(d[j][0] + miter[j][0] * h1, d[j][1] + miter[j][1] * h1, 0, 0, c, 0);
        IsisVertex ro_i = isis_vertex(d[i][0] - miter[i][0] * h1, d[i][1] - miter[i][1] * h1, 0, 0, c, 0);
        IsisVertex ro_j = isis_vertex(d[j][0] - miter[j][0] * h1, d[j][1] - miter[j][1] * h1, 0, 0, c, 0);
        if (h0 > 0.0f) isis_quad(l, li, lj, rj, ri, 0);
        isis_quad(l, lo_i, lo_j, lj, li, 0);
        isis_quad(l, ri, rj, ro_j, ro_i, 0);
    }
}

static void isis_line(IsisList *l, float x1, float y1, float x2, float y2, float width, IsisColor c)
{
    const float p[] = {x1, y1, x2, y2};
    isis_stroke(l, p, 2, 0, width, c, 0);
}
static void isis_rect_stroke(IsisList *l, float x, float y, float w, float h, float width, IsisColor c)
{
    const float p[] = {x, y, x + w, y, x + w, y + h, x, y + h};
    isis_stroke(l, p, 4, 1, width, c, 0);
}
static void isis_circle_stroke(IsisList *l, float cx, float cy, float r, float width, IsisColor c)
{
    float p[2 * 32];
    int i;
    for (i = 0; i < 32; ++i) {
        float a = (float)i * 2.0f * 3.14159265358979f / 32.0f;
        p[2 * i] = cx + r * cosf(a); p[2 * i + 1] = cy + r * sinf(a);
    }
    isis_stroke(l, p, 32, 1, width, c, 0);
}

/* SVG <text>: FBW "Ecam" face from the embedded atlas at the exact drawn texel size. */
static int isis_face_for(IsisList *l, float px)
{
    int i, best = 0;
    for (i = 0; i < ISIS_FACE_COUNT; ++i) {
        if (fabsf(isis_face_size[i] - px) < 0.05f) return i;
        if (fabsf(isis_face_size[i] - px) < fabsf(isis_face_size[best] - px)) best = i;
    }
    ++l->font_misses;
    return best;
}

static float isis_text_advance(int face, const char *text, float spacing)
{
    float width = 0.0f;
    const unsigned char *p = (const unsigned char *)text;
    for (; *p; ++p) {
        const IsisGlyph *g = isis_glyph_find(face, *p);
        if (g) width += g->advance + spacing;
    }
    return width;
}

static void isis_text(IsisList *l, float x, float y, float size, int anchor, IsisColor c, const char *text,
                      float letter_spacing, int middle_baseline)
{
    float unit = isis_unit(l), ex = l->m[0] / unit, ey = l->m[1] / unit, fx = l->m[2] / unit, fy = l->m[3] / unit;
    int face = isis_face_for(l, size * unit), rotated = fabsf(l->m[1]) > 1e-5f || fabsf(l->m[2]) > 1e-5f;
    float spacing = letter_spacing * unit, width = isis_text_advance(face, text, spacing), px, py, shift, drop;
    const unsigned char *p = (const unsigned char *)text;
    isis_xf(l, x, y, &px, &py);
    shift = anchor == ISIS_MIDDLE ? -width * 0.5f : anchor == ISIS_END ? -width : 0.0f;
    /* alignment-baseline="middle": the digits' cap height is centred on y. */
    drop = middle_baseline ? isis_face_size[face] * isis_font_cap_height * 0.5f : 0.0f;
    px += ex * shift + fx * drop; py += ey * shift + fy * drop;
    if (!rotated) { px = roundf(px); py = roundf(py); }
    for (; *p; ++p) {
        const IsisGlyph *g = isis_glyph_find(face, *p);
        if (!g) { ++l->font_misses; continue; }
        if (g->w && g->h) {
            float tlx = px + ex * g->left - fx * g->top, tly = py + ey * g->left - fy * g->top;
            float u0 = (float)g->x / ISIS_FONT_WIDTH, u1 = (float)(g->x + g->w) / ISIS_FONT_WIDTH;
            float v0 = (float)g->y / ISIS_FONT_HEIGHT, v1 = (float)(g->y + g->h) / ISIS_FONT_HEIGHT;
            IsisVertex a = isis_vertex(tlx, tly, u0, v0, c, 1);
            IsisVertex b = isis_vertex(tlx + ex * g->w, tly + ey * g->w, u1, v0, c, 1);
            IsisVertex d = isis_vertex(tlx + fx * g->h, tly + fy * g->h, u0, v1, c, 1);
            IsisVertex e = isis_vertex(tlx + ex * g->w + fx * g->h, tly + ey * g->w + fy * g->h, u1, v1, c, 1);
            isis_quad(l, a, b, e, d, 1);
        }
        px += ex * (g->advance + spacing); py += ey * (g->advance + spacing);
    }
}

/* ---- FBW components ------------------------------------------------------------------------ */

static void isis_draw_pitch_scale(IsisList *l)
{
    const float px = 6.7f, center = 270.0f;
    static const float arrows[6][20] = {
        {198, -258, 244, -258, 244, -246, 228, -246, 256, -218, 284, -246, 268, -246, 268, -258, 314, -258, 256, -200},
        {216, -162, 232, -162, 256, -120, 280, -162, 296, -162, 256, -92},
        {220, -66, 232, -66, 256, -24, 280, -66, 292, -66, 256.5f, -4},
        {220, 578, 232, 578, 256, 536, 280, 578, 292, 578, 256.5f, 516},
        {216, 674, 232, 674, 256, 632, 280, 674, 296, 674, 256, 604},
        {198, 770, 244, 770, 244, 758, 228, 758, 256, 730, 284, 758, 268, 758, 268, 770, 314, 770, 256, 712},
    };
    static const int arrow_points[6] = {10, 6, 6, 6, 6, 10};
    int d, k;
    for (d = -30; d <= 30; d += 10) {
        float y = center - (float)d * px;
        char text[8];
        if (!d) { isis_line(l, -256, y, 768, y, ISIS_AH_STROKE, ISIS_WHITE); continue; }
        snprintf(text, sizeof(text), "%d", abs(d));
        isis_text(l, 225, y, 28, ISIS_END, ISIS_WHITE, text, 0, 1);
        isis_line(l, 231.843f, y, 280.157f, y, ISIS_AH_STROKE, ISIS_WHITE);
    }
    for (d = -25; d <= 25; d += 10) {
        float y = center - (float)d * px;
        isis_line(l, 243.5f, y, 268.5f, y, ISIS_AH_STROKE, ISIS_WHITE);
    }
    for (k = -55; k <= 55; k += 10) {
        float y = center - (float)k * 0.5f * px;
        isis_line(l, 248.5f, y, 263.5f, y, ISIS_AH_STROKE, ISIS_WHITE);
    }
    for (k = 0; k < 6; ++k) isis_stroke(l, arrows[k], arrow_points[k], 1, 2.0f, ISIS_RED, 0);
}

static void isis_draw_roll_index(IsisList *l, const IsisState *s)
{
    static const float index[] = {256, 131, 266, 149, 246, 149};
    float slip[] = {246, 149, 266, 149, 271, 158, 241, 158};
    int i;
    isis_rect(l, -256, -256, 1024, 409, ISIS_SKY, 0);
    isis_fill(l, index, 3, ISIS_BLACK, 1);
    isis_stroke(l, index, 3, 1, ISIS_AH_STROKE, ISIS_WHITE, 0);
    for (i = 0; i < 4; ++i) slip[2 * i] += s->slip_offset;
    isis_fill(l, slip, 4, ISIS_BLACK, 1);
    isis_stroke(l, slip, 4, 1, ISIS_AH_STROKE, ISIS_WHITE, 0);
}

static void isis_draw_roll_scale(IsisList *l)
{
    static const int angles[] = {-60, -45, -30, -20, -10, 10, 20, 30, 45, 60};
    static const float pointer[] = {256, 130, 241, 108, 271, 108};
    const float radius = 138.0f, pi = 3.14159265358979f;
    float arc[2 * 41];
    int i;
    for (i = 0; i <= 40; ++i) {
        float a = (-30.0f + 60.0f * (float)i / 40.0f) * pi / 180.0f;
        arc[2 * i] = 256.0f + radius * sinf(a); arc[2 * i + 1] = 269.0f - radius * cosf(a);
    }
    isis_stroke(l, arc, 41, 0, 3.0f, ISIS_WHITE, 1); /* white-line, stroke-linecap square */
    for (i = 0; i < 10; ++i) {
        float a = (float)angles[i] * pi / 180.0f, sine = sinf(a), cosine = cosf(a);
        float x = 256.0f + radius * sine, y = 269.0f - radius * cosine;
        isis_line(l, x, y, x + 20.0f * sine, y - 20.0f * cosine, 3.0f, ISIS_WHITE);
    }
    isis_fill(l, pointer, 3, ISIS_YELLOW, 1);
}

static void isis_cubic(float *out, int *n, float x0, float y0, float x1, float y1, float x2, float y2,
                       float x3, float y3, int steps)
{
    int i;
    for (i = 1; i <= steps; ++i) {
        float t = (float)i / (float)steps, u = 1.0f - t;
        out[2 * *n] = u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3;
        out[2 * *n + 1] = u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3;
        ++*n;
    }
}

/* ArtificialHorizon Mask: "M 0 0 v 512 h 512 v -512 z M 122.1 123.5 c 43.9 -23.5 217.9 -25.5
 * 271.4 0 v 266 c -43.9 23.5 -217.9 25.5 -271.4 0 z" (the window is a hole). */
static void isis_draw_mask(IsisList *l)
{
    enum { STEPS = 48 };
    float top[2 * (STEPS + 1)], bottom[2 * (STEPS + 1)], window[4 * (STEPS + 1)];
    float d[ISIS_MAX_PATH][2];
    int nt = 1, nb = 1, i, count;
    top[0] = 122.1f; top[1] = 123.5f;
    isis_cubic(top, &nt, 122.1f, 123.5f, 166.0f, 100.0f, 340.0f, 98.0f, 393.5f, 123.5f, STEPS);
    bottom[0] = 393.5f; bottom[1] = 389.5f;
    isis_cubic(bottom, &nb, 393.5f, 389.5f, 349.6f, 413.0f, 175.6f, 415.0f, 122.1f, 389.5f, STEPS);
    isis_rect(l, 0, 0, 122.1f, 512, ISIS_BLACK, 0);
    isis_rect(l, 393.5f, 0, 512 - 393.5f, 512, ISIS_BLACK, 0);
    for (i = 0; i + 1 < nt; ++i) {
        const float q[] = {top[2 * i], 0, top[2 * i + 2], 0, top[2 * i + 2], top[2 * i + 3], top[2 * i], top[2 * i + 1]};
        isis_fill(l, q, 4, ISIS_BLACK, 0);
    }
    for (i = 0; i + 1 < nb; ++i) {
        const float q[] = {bottom[2 * i], bottom[2 * i + 1], bottom[2 * i + 2], bottom[2 * i + 3],
                           bottom[2 * i + 2], 512, bottom[2 * i], 512};
        isis_fill(l, q, 4, ISIS_BLACK, 0);
    }
    memcpy(window, top, sizeof(float) * 2 * (size_t)nt);
    memcpy(window + 2 * nt, bottom, sizeof(float) * 2 * (size_t)nb);
    count = isis_device_points(l, window, nt + nb, d, 1);
    isis_edge_fringe(l, d, count, 1, ISIS_BLACK, -1);
}

static void isis_draw_horizon(IsisList *l, const IsisState *s, const IsisInputs *in)
{
    IsisSave roll, pitch;
    /* MSFS PLANE BANK is positive left and PLANE PITCH positive nose down. */
    float fbw_roll = -in->roll_deg, fbw_pitch = -in->pitch_deg;
    isis_save(l, &roll);
    isis_rotate(l, fbw_roll, 256, 256);
    isis_save(l, &pitch);
    isis_translate(l, 0, -fbw_pitch * 6.7f);
    isis_rect(l, -256, -498, 1024, 768, ISIS_SKY, 0);
    isis_rect(l, -256, 270, 1024, 768, ISIS_EARTH, 0);
    isis_draw_pitch_scale(l);
    isis_restore(l, &pitch);
    isis_draw_roll_index(l, s);
    isis_rect(l, -256, 388, 1024, 396, ISIS_EARTH, 0);
    isis_restore(l, &roll);
    isis_draw_roll_scale(l);
    isis_draw_mask(l);
}

static void isis_draw_att10s(IsisList *l, const IsisState *s)
{
    char text[32];
    int seconds = (int)fmax(0.0, ceil(s->att_rst_timer));
    snprintf(text, sizeof(text), "ATT %02ds", seconds > 99 ? 99 : seconds);
    isis_rect(l, 178, 154, 160, 40, ISIS_YELLOW, 1);
    isis_text(l, 256, 186, 30, ISIS_MIDDLE, ISIS_BLACK, text, 0, 0);
}

static void isis_draw_airspeed(IsisList *l, const IsisState *s, const IsisInputs *in)
{
    static const float arrow[] = {143.545f, 285.827f, 143.545f, 254.173f, 123.553f, 270.0f};
    IsisSave save;
    double value = in->ias_kts, clamped = fmax(fmin(value, 660.0), 30.0), lowest;
    int ticks = (int)isis_js_round(55.0 * 2.0 / 5.0), i;
    isis_save(l, &save);
    isis_viewport(l, 12, 112, 108, 296);
    isis_translate(l, 0, (float)(clamped * 16.5 / 5.0));
    lowest = fmax(isis_js_round((clamped - 55.0) / 5.0) * 5.0, 30.0);
    if (lowest < value - 55.0) lowest += 5.0;
    for (i = 0; i < ticks; ++i) {
        double element = lowest + i * 5.0;
        int speed = (int)element;
        float offset = (float)(-element * 16.5 / 5.0), length;
        if (element > 660.0) continue;
        if (speed > 250 && speed % 10 == 5) continue;
        length = speed % 20 == 10 ? 13.5f : 5.0f;
        if (speed % 20 == 0) {
            char text[8];
            snprintf(text, sizeof(text), "%d", speed);
            isis_text(l, 104, 158 + 12 + offset, 30, ISIS_END, ISIS_WHITE, text, 0, 0);
        }
        isis_line(l, 108 - length, 158 + offset, 108, 158 + offset, 4.0f, ISIS_WHITE);
    }
    for (i = ISIS_BUG_SPD0; i < ISIS_BUGS; ++i) {
        float offset;
        if (!s->bug_on[i]) continue;
        offset = (float)(-s->bug_value[i] * 16.5 / 5.0);
        isis_line(l, 86, 158 + offset, 108, 158 + offset, 7.0f, ISIS_CYAN);
    }
    isis_restore(l, &save);
    isis_rect(l, 121.887f, 250.841f, 23.324f, 35.4025f, ISIS_BLACK, 0);
    isis_fill(l, arrow, 3, ISIS_YELLOW, 1);
}

static void isis_draw_altitude_tape(IsisList *l, const IsisState *s, double altitude)
{
    IsisSave save;
    double clamped = fmax(fmin(altitude, 50000.0), -2000.0), lowest;
    int ticks = (int)isis_js_round(1000.0 * 2.0 / 100.0), i;
    isis_save(l, &save);
    isis_viewport(l, 395, 112, 108, 296);
    isis_translate(l, 0, (float)(clamped * 21.5 / 100.0));
    lowest = fmax(isis_js_round((clamped - 1000.0) / 100.0) * 100.0, -2000.0);
    if (lowest < altitude - 1000.0) lowest += 100.0;
    for (i = 0; i < ticks; ++i) {
        double element = lowest + i * 100.0;
        long alt = (long)element;
        float offset = (float)(-element * 21.5 / 100.0);
        int show = alt % 500 == 0;
        if (element > 50000.0) continue;
        if (show) {
            char digits[16], text[4];
            isis_line(l, 0.5f, 158 + offset, 5.5f, 158 + offset, 6.0f, ISIS_WHITE);
            snprintf(digits, sizeof(digits), "%05ld", labs(alt));
            memcpy(text, digits, 3); text[3] = 0;
            isis_text(l, 7, 158 + 12 + offset, 30, ISIS_START, ISIS_WHITE, text, 0, 0);
        } else {
            isis_line(l, 0, 158 + offset, 19, 158 + offset, ISIS_AH_STROKE, ISIS_WHITE);
        }
    }
    for (i = ISIS_BUG_ALT0; i <= ISIS_BUG_ALT1; ++i) {
        float offset;
        if (!s->bug_on[i]) continue;
        offset = (float)(-s->bug_value[i] * 21.5 / 100.0);
        if (s->bug_value[i] % 500 == 0) {
            const float box[] = {3, 151 + offset, 3, 142 + offset, 68, 142 + offset, 68, 174 + offset, 3, 174 + offset, 3, 165 + offset};
            const float t1[] = {0, 150 + offset, 10, 142 + offset, 0, 142 + offset};
            const float t2[] = {0, 165 + offset, 10, 173 + offset, 0, 173 + offset};
            isis_stroke(l, box, 6, 0, ISIS_AH_STROKE, ISIS_CYAN, 0);
            isis_fill(l, t1, 3, ISIS_CYAN, 1);
            isis_fill(l, t2, 3, ISIS_CYAN, 1);
        } else {
            isis_line(l, 0, 158 + offset, 25, 158 + offset, 5.0f, ISIS_CYAN);
        }
    }
    isis_restore(l, &save);
}

/* DigitalAltitudeIndicator.tsx Drum and digit element functions. */
typedef void (*IsisDrumText)(IsisList *, double value, float offset, IsisColor color);

static void isis_number_text(char *out, size_t size, double value)
{
    if (isnan(value)) snprintf(out, size, "NaN");
    else snprintf(out, size, "%.0f", value == 0.0 ? 0.0 : value);
}
static void isis_tens_digits(IsisList *l, double value, float offset, IsisColor color)
{
    char text[16];
    IsisSave save;
    if (value < 0) isis_number_text(text, sizeof(text), value + 100);
    else isis_number_text(text, sizeof(text), value >= 100 ? value - 100 : value);
    if (value >= 0 && strlen(text) < 2) strcat(text, "0"); /* padEnd(2, '0') */
    isis_save(l, &save); isis_translate(l, 0, offset);
    isis_text(l, 24, 40, 28, ISIS_START, color, text, 3.1f, 0); /* FontTens */
    isis_restore(l, &save);
}
static void isis_hundreds_digit(IsisList *l, double value, float offset, IsisColor color)
{
    char text[16];
    IsisSave save;
    isis_number_text(text, sizeof(text), value < 0 ? value + 1 : value >= 10 ? fmod(value, 10) : value);
    isis_save(l, &save); isis_translate(l, 0, offset);
    isis_text(l, 73.5f, 32, 36, ISIS_START, color, text, 0, 0);
    isis_restore(l, &save);
}
static void isis_thousands_digit(IsisList *l, double value, float offset, IsisColor color)
{
    char text[16] = "";
    IsisSave save;
    if (!isnan(value)) isis_number_text(text, sizeof(text), fmod(value, 10));
    isis_save(l, &save); isis_translate(l, 0, offset);
    isis_text(l, 48, 32, 36, ISIS_START, color, text, 0, 0);
    isis_restore(l, &save);
}
static void isis_ten_thousands_digit(IsisList *l, double value, float offset, IsisColor color)
{
    char text[16] = "";
    IsisSave save;
    if (!isnan(value)) isis_number_text(text, sizeof(text), value);
    isis_save(l, &save); isis_translate(l, 0, offset);
    isis_text(l, 22, 32, 36, ISIS_START, color, text, 0, 0);
    isis_restore(l, &save);
}

static void isis_drum(IsisList *l, double range, double spacing, double distance, double position, double value,
                      IsisColor color, IsisDrumText element, int show_zero, int mirror)
{
    int ticks = (int)fmax(isis_js_round(range * 2.0 / spacing), 3.0), i, negative;
    double highest_position = isis_js_round((position + range) / spacing) * spacing, highest_value;
    IsisSave save;
    if (highest_position > position + range) highest_position -= spacing;
    if (highest_position == 0) highest_position += 1;
    highest_value = isis_js_round((value + range) / spacing) * spacing;
    if (highest_value > value + range) highest_value -= spacing;
    negative = value < 0 || (value == 0 && signbit(value));
    if (!mirror) {
        if (negative) highest_value += value > 0 ? 100 : -100;
        else highest_value += value >= 0 ? 100 : -100;
    }
    isis_save(l, &save);
    isis_translate(l, 0, (float)(position * distance / spacing));
    for (i = 0; i < ticks; ++i) {
        double element_position = highest_position - i * spacing;
        double element_value = highest_value - i * spacing;
        if (!mirror) element_value = fmod(element_value, 100.0);
        if (!show_zero && element_value == 0) element_value = NAN;
        element(l, fabs(element_value), (float)(-element_position * distance / spacing), color);
    }
    isis_restore(l, &save);
}

static void isis_draw_digital_altitude(IsisList *l, const IsisState *s, const IsisInputs *in, double altitude)
{
    static const float outline[] = {466.749f, 243.344f, 417.602f, 243.344f, 417.602f, 251.674f, 340.966f, 251.674f,
                                    340.966f, 288.326f, 417.602f, 288.326f, 417.602f, 296.656f, 466.749f, 296.656f};
    int negative = altitude < 0, in_bug = 0, i;
    IsisColor color = in->mda_ft != 0 && isfinite(in->mda_ft) && altitude < in->mda_ft ? ISIS_AMBER : ISIS_GREEN;
    double clamped = fmax(fmin(altitude, 50000.0), -1500.0);
    double tens = fmod(clamped, 100.0);
    double hundreds = trunc(fmod(clamped / 100.0, 10.0)), hundreds_position = 0;
    double thousands, thousands_position = 0, ten_thousands, ten_thousands_position = 0;
    IsisSave save;
    if (fabs(tens) > 80) hundreds_position = negative ? tens / 20 + 4 : tens / 20 - 4;
    thousands = trunc(fmod(clamped / 1000.0, 10.0));
    if (fabs(hundreds) >= 9) thousands_position = hundreds_position;
    ten_thousands = trunc(fmod(clamped / 10000.0, 10.0));
    if (fabs(thousands) >= 9) ten_thousands_position = thousands_position;
    for (i = ISIS_BUG_ALT0; i <= ISIS_BUG_ALT1; ++i)
        if (s->bug_on[i] && fabs(s->bug_value[i] - altitude) < 100) in_bug = 1;
    isis_fill(l, outline, 8, ISIS_BLACK, 0);
    isis_save(l, &save);
    isis_viewport(l, 325, 252, 100, 37);
    isis_drum(l, 1, 1, 42, ten_thousands_position, ten_thousands, color, isis_ten_thousands_digit, 0, 0);
    isis_drum(l, 1, 1, 42, thousands_position, thousands, color, isis_thousands_digit, ten_thousands != 0, 0);
    isis_drum(l, 1, 1, 42, hundreds_position, hundreds, color, isis_hundreds_digit, 1, fabs(altitude) < 500);
    isis_restore(l, &save);
    isis_save(l, &save);
    isis_viewport(l, 400, 243.344f, 100, 53.312f);
    isis_drum(l, 30, 20, 40, tens, tens, color, isis_tens_digits, 1, fabs(altitude) < 50);
    isis_restore(l, &save);
    isis_stroke(l, outline, 8, 0, 5.0f, in_bug ? ISIS_CYAN : ISIS_YELLOW, 0);
    if (negative) {
        isis_text(l, 343, 243, 36, ISIS_START, ISIS_WHITE, "N", 0, 0);
        isis_text(l, 343, 284, 36, ISIS_START, ISIS_WHITE, "E", 0, 0);
        isis_text(l, 343, 325, 36, ISIS_START, ISIS_WHITE, "G", 0, 0);
    }
}

static void isis_draw_metric_altitude(IsisList *l, double altitude)
{
    char text[16];
    snprintf(text, sizeof(text), "%.0f", fabs(isis_js_round(altitude * 0.3048)));
    if (altitude < 0) isis_text(l, 238, 94, 30, ISIS_START, ISIS_WHITE, "NEG", 0, 0);
    isis_rect_stroke(l, 235, 65, 165, 32, 2.0f, ISIS_YELLOW);
    isis_text(l, 373, 94, 30, ISIS_END, ISIS_GREEN, text, 0, 0);
    isis_text(l, 377, 94, 30, ISIS_START, ISIS_CYAN, "M", 0, 0);
}

static void isis_draw_airplane_symbol(IsisList *l)
{
    static const float left[] = {176.032f, 263.336f, 222.68f, 263.336f, 222.68f, 279.996f, 212.684f, 279.996f,
                                 212.684f, 273.332f, 176.032f, 273.332f};
    static const float centre[] = {249.336f, 263.336f, 262.664f, 263.336f, 262.664f, 276.664f, 249.336f, 276.664f};
    static const float right[] = {335.968f, 263.336f, 289.32f, 263.336f, 289.32f, 279.996f, 299.316f, 279.996f,
                                  299.316f, 273.332f, 335.968f, 273.332f};
    isis_fill(l, left, 6, ISIS_BLACK, 0); isis_stroke(l, left, 6, 1, 5.0f, ISIS_YELLOW, 0);
    isis_fill(l, centre, 4, ISIS_BLACK, 0); isis_stroke(l, centre, 4, 1, 5.0f, ISIS_YELLOW, 0);
    isis_fill(l, right, 6, ISIS_BLACK, 0); isis_stroke(l, right, 6, 1, 5.0f, ISIS_YELLOW, 0);
}

static void isis_draw_deviation(IsisList *l, float dots, int available)
{
    static const float circles[4] = {14.161f, 51.646f, 126.616f, 164.101f};
    static const float diamond[] = {75.6f, 9.1f, 75.6f, 9.9f, 83.9443f, 16.6f, 93.9575f, 16.6f, 102.3018f, 9.9f,
                                    102.3018f, 8.3f, 93.9575f, 1.6f, 83.9443f, 1.6f};
    static const float below[] = {0.75f, 9.1f, 0.75f, 9.9f, 9.0943f, 16.6f, 14.1009f, 16.6f, 14.1009f, 1.6f, 9.0943f, 1.6f};
    static const float above[] = {177.5f, 9.1f, 177.5f, 9.9f, 169.1557f, 16.6f, 164.1491f, 16.6f, 164.1491f, 1.6f,
                                  169.1557f, 1.6f};
    int i;
    isis_rect(l, 0, 0, 178.26f, 18.326f, ISIS_BLACK, 0);
    for (i = 0; i < 4; ++i) isis_circle_stroke(l, circles[i], 9.1625f, 2.9155f, ISIS_AH_STROKE, ISIS_WHITE);
    if (available && isfinite(dots)) {
        if (dots >= -2.0f && dots <= 2.0f) {
            IsisSave save;
            isis_save(l, &save);
            isis_translate(l, dots * 37.485f, 0);
            isis_fill(l, diamond, 8, ISIS_MAGENTA, 1);
            isis_restore(l, &save);
        } else if (dots < -2.0f) isis_fill(l, below, 6, ISIS_MAGENTA, 1);
        else isis_fill(l, above, 6, ISIS_MAGENTA, 1);
    }
    isis_line(l, 89.131f, 2.499f, 89.131f, 15.827f, ISIS_AH_STROKE, ISIS_YELLOW);
}

static void isis_draw_landing_system(IsisList *l, const IsisInputs *in)
{
    IsisSave save;
    isis_save(l, &save);
    isis_translate(l, 166.869f, 376.62f);
    isis_draw_deviation(l, in->loc_dots, in->loc_valid);
    isis_restore(l, &save);
    isis_translate(l, 340.133f, 180.869f);
    isis_mul(l, 0, 1, -1, 0, 0, 0); /* rotate(90) */
    isis_draw_deviation(l, in->gs_dots, in->gs_valid);
    isis_restore(l, &save);
}

static void isis_draw_pressure(IsisList *l, const IsisState *s, const IsisInputs *in)
{
    char text[32];
    float hpa = in->baro_inhg * ISIS_HPA_PER_INHG;
    if (!isfinite(hpa)) return;
    if (s->baro_std) {
        isis_text(l, 250, 450, 36, ISIS_MIDDLE, ISIS_CYAN, "STD", 0, 0);
        return;
    }
    /* \xa0 padding of PressureIndicator.tsx rendered as spaces of the same advance. */
    if (s->baro_unit_inhg) snprintf(text, sizeof(text), "  %4.0f/%.2f", isis_js_round(hpa), (double)in->baro_inhg);
    else snprintf(text, sizeof(text), "%4.0f    ", isis_js_round(hpa));
    isis_text(l, 256, 450, 28, ISIS_MIDDLE, ISIS_CYAN, text, 0, 0);
}

static void isis_draw_flag(IsisList *l, int which)
{
    /* ISISDisplayUnit self-test flags (.SelfTest font-size 36px, TextBackground on FillYellow). */
    if (which == 0) { isis_rect(l, 190, 174, 110, 40, ISIS_YELLOW, 1); isis_text(l, 245, 206, 36, ISIS_MIDDLE, ISIS_BLACK, "ATT", 0, 0); }
    if (which == 1) { isis_rect(l, 40, 244, 84, 40, ISIS_YELLOW, 1); isis_text(l, 82, 277, 36, ISIS_MIDDLE, ISIS_BLACK, "SPD", 0, 0); }
    if (which == 2) { isis_rect(l, 358, 244, 110, 40, ISIS_YELLOW, 1); isis_text(l, 415, 277, 36, ISIS_MIDDLE, ISIS_BLACK, "ALT", 0, 0); }
}

static void isis_draw_selftest(IsisList *l, const IsisState *s, const IsisInputs *in)
{
    char text[16];
    isis_draw_flag(l, 0); isis_draw_flag(l, 1); isis_draw_flag(l, 2);
    isis_rect(l, 150, 332, 180, 40, ISIS_YELLOW, 1);
    isis_text(l, 160, 365, 30, ISIS_START, ISIS_BLACK, "INIT", 0, 0);
    snprintf(text, sizeof(text), "%ds", (int)fmax(0.0, ceil(s->du_timer)));
    isis_text(l, 325, 365, 30, ISIS_END, ISIS_BLACK, text, 0, 0);
    isis_draw_pressure(l, s, in);
}

static void isis_draw_pfd(IsisList *l, const IsisState *s, const IsisInputs *in)
{
    IsisSave base;
    double altitude = floor(in->altitude_ft); /* ArtificialHorizonDisplay: Math.floor(alt) */
    int att = in->att_valid && isfinite(in->pitch_deg) && isfinite(in->roll_deg);
    int spd = in->spd_valid && isfinite(in->ias_kts);
    int alt = in->alt_valid && isfinite(in->altitude_ft);
    isis_save(l, &base);
    isis_scale(l, 1.1f);              /* .ArtificialHorizon { transform: scale(1.1) translate(-30px, -30px) } */
    isis_translate(l, -30, -30);
    if (s->att_rst) isis_draw_att10s(l, s);
    else if (att) isis_draw_horizon(l, s, in);
    if (spd) isis_draw_airspeed(l, s, in);
    if (alt) {
        isis_draw_altitude_tape(l, s, altitude);
        isis_draw_digital_altitude(l, s, in, altitude);
        if (s->metric_alt) isis_draw_metric_altitude(l, altitude);
    }
    if (att || s->att_rst) isis_draw_airplane_symbol(l);
    if (s->ls_active) isis_draw_landing_system(l, in);
    isis_draw_pressure(l, s, in);
    if (spd && s->mach_visible && isfinite(in->mach)) {
        char text[16];
        snprintf(text, sizeof(text), "%.2f", (double)in->mach);
        isis_text(l, 68, 445, 30, ISIS_START, ISIS_GREEN, text + 1, 0, 0); /* toFixed(2).slice(1) */
    }
    isis_restore(l, &base);
    /* Failure flags (not in FBW legacy): FBW's own self-test flag symbols over the lost data. */
    if (!att && !s->att_rst) isis_draw_flag(l, 0);
    if (!spd) isis_draw_flag(l, 1);
    if (!alt) isis_draw_flag(l, 2);
}

static void isis_bug_box(IsisList *l, const IsisState *s, float x, float y, int bug, int altitude)
{
    char text[16];
    float width = altitude ? 108.0f : 75.0f;
    /* .BlinkInfinite: 0.5 s period, stroke background 0-50 %, white 51-100 %. */
    int blink = bug == s->selected_bug && fmodf(s->clock, 0.5f) < 0.25f;
    isis_rect(l, x - width / 2, y - 34, width, 40, ISIS_BLACK, 0);
    isis_rect_stroke(l, x - width / 2, y - 34, width, 40, ISIS_ROOT_STROKE, blink ? ISIS_BLACK : ISIS_WHITE);
    snprintf(text, sizeof(text), altitude ? "%05d" : "%03d", s->bug_value[bug]);
    isis_text(l, x, y, 32, ISIS_MIDDLE, ISIS_WHITE, text, 0, 0);
}

static void isis_draw_bugs(IsisList *l, const IsisState *s)
{
    static const float box[] = {150, 136, 150, 361, 345, 361, 345, 136};
    static const int speed_bug[4] = {ISIS_BUG_SPD3, ISIS_BUG_SPD2, ISIS_BUG_SPD1, ISIS_BUG_SPD0};
    int i;
    isis_text(l, 60, 60, 32, ISIS_START, ISIS_CYAN, "EXIT\x01", 0, 0);
    isis_text(l, 256, 60, 32, ISIS_MIDDLE, ISIS_WHITE, "BUGS", 0, 0);
    isis_text(l, 150, 100, 32, ISIS_MIDDLE, ISIS_WHITE, "SPD", 0, 0);
    isis_text(l, 335, 100, 32, ISIS_MIDDLE, ISIS_WHITE, "ALT", 0, 0);
    isis_text(l, 196, 126, 16, ISIS_START, ISIS_WHITE, "(+)", 0, 0);
    isis_text(l, 110, 180, 16, ISIS_START, ISIS_WHITE, "(-)", 0, 0);
    isis_stroke(l, box, 4, 1, ISIS_ROOT_STROKE, ISIS_WHITE, 0);
    for (i = 0; i < 4; ++i) {
        float y = 150.0f + 75.0f * (float)i;
        if (!s->bug_on[speed_bug[i]]) isis_text(l, 35, y, 32, ISIS_START, ISIS_WHITE, "OFF", 0, 0);
        isis_bug_box(l, s, 150, y, speed_bug[i], 0);
    }
    for (i = 0; i < 2; ++i) {
        float y = 225.0f + 75.0f * (float)i;
        if (!s->bug_on[ISIS_BUG_ALT0 + i]) isis_text(l, 409, y, 32, ISIS_START, ISIS_WHITE, "OFF", 0, 0);
        isis_bug_box(l, s, 344, y, ISIS_BUG_ALT0 + i, 1);
    }
    isis_text(l, 170, 450, 32, ISIS_START, ISIS_CYAN, "SET/SELECT\x02", 0, 0);
}

/* MENU page: not part of FBW ISISlegacy. Same typography and layout grammar as the FBW
 * BUGS page; it reaches FBW's own settings (bugs, ISIS_METRIC_ALTITUDE, ISIS_BARO_UNIT_INHG). */
static void isis_draw_menu(IsisList *l, const IsisState *s)
{
    static const char *const labels[ISIS_MENU_COUNT] = {
        "SET SPD BUG 1", "SET SPD BUG 2", "SET ALT BUG", "DISPLAY METERS", "SET BARO UNIT",
        "DISPLAY NAV", "DISPLAY FIX", "EXIT"};
    static const int bugs[3] = {ISIS_BUG_SPD3, ISIS_BUG_SPD2, ISIS_BUG_ALT0};
    int i;
    isis_text(l, 256, 60, 32, ISIS_MIDDLE, ISIS_WHITE, "MENU", 0, 0);
    for (i = 0; i < ISIS_MENU_COUNT; ++i) {
        float y = 116.0f + 38.0f * (float)i;
        char value[24] = "";
        IsisColor color = ISIS_CYAN;
        if (i <= ISIS_MENU_ALT) {
            if (s->bug_on[bugs[i]]) snprintf(value, sizeof(value), i == ISIS_MENU_ALT ? "%05d" : "%03d", s->bug_value[bugs[i]]);
            else { snprintf(value, sizeof(value), "OFF"); color = ISIS_WHITE; }
        } else if (i == ISIS_MENU_METERS) snprintf(value, sizeof(value), s->metric_alt ? "ON" : "OFF");
        else if (i == ISIS_MENU_BARO_UNIT) snprintf(value, sizeof(value), s->baro_unit_inhg ? "HPA/IN" : "HPA");
        else if (i == ISIS_MENU_NAV || i == ISIS_MENU_FIX) { snprintf(value, sizeof(value), "NOT AVAIL"); color = ISIS_AMBER; }
        if (i == s->menu_index) isis_rect_stroke(l, 26, y - 29, 460, 36, ISIS_ROOT_STROKE, ISIS_CYAN);
        isis_text(l, 36, y, 28, ISIS_START, ISIS_WHITE, labels[i], 0, 0);
        if (value[0]) isis_text(l, 476, y, 28, ISIS_END, color, value, 0, 0);
    }
    isis_text(l, 170, 450, 32, ISIS_START, ISIS_CYAN, "SET/SELECT\x02", 0, 0);
}

/* ---- SND (standby navigation display) ----------------------------------------------------- *
 * Not in FBW (ISIS 2 is disabled upstream). Same 512-unit grid, colours, strokes and font as the
 * FBW ISIS; Airbus ND conventions: rotating heading rose (ticks 5/10 deg, labels every 30 deg in
 * tens), fixed yellow lubber line and aircraft symbol, VOR1 single and VOR2 double white bearing
 * pointers, VOR ident / frequency and DME in the lower corners. Data with no reliable source is
 * not invented: no heading -> amber HDG NOT AVAIL; no signal -> no pointer, frequency only. */
#define ISIS_SND_CX 256.0f
#define ISIS_SND_CY 292.0f
#define ISIS_SND_R 160.0f

static void isis_snd_pointer(IsisList *l, float relative_deg, int dual)
{
    IsisSave save;
    float head = ISIS_SND_CY - (ISIS_SND_R - 18.0f), tail = ISIS_SND_CY + (ISIS_SND_R - 18.0f);
    isis_save(l, &save);
    isis_rotate(l, relative_deg, ISIS_SND_CX, ISIS_SND_CY);
    if (!dual) {
        const float arrow[] = {ISIS_SND_CX - 12, head + 22, ISIS_SND_CX, head, ISIS_SND_CX + 12, head + 22};
        isis_line(l, ISIS_SND_CX, head, ISIS_SND_CX, ISIS_SND_CY - 40, 3.0f, ISIS_WHITE);
        isis_line(l, ISIS_SND_CX, ISIS_SND_CY + 40, ISIS_SND_CX, tail, 3.0f, ISIS_WHITE);
        isis_stroke(l, arrow, 3, 0, 3.0f, ISIS_WHITE, 0);
    } else {
        const float arrow[] = {ISIS_SND_CX - 16, head + 26, ISIS_SND_CX, head, ISIS_SND_CX + 16, head + 26};
        int k;
        for (k = -1; k <= 1; k += 2) {
            float x = ISIS_SND_CX + 6.0f * (float)k;
            isis_line(l, x, head + 14, x, ISIS_SND_CY - 40, 3.0f, ISIS_WHITE);
            isis_line(l, x, ISIS_SND_CY + 40, x, tail, 3.0f, ISIS_WHITE);
        }
        isis_stroke(l, arrow, 3, 0, 3.0f, ISIS_WHITE, 0);
        isis_line(l, ISIS_SND_CX - 6, tail - 10, ISIS_SND_CX + 6, tail - 10, 3.0f, ISIS_WHITE);
    }
    isis_restore(l, &save);
}

static void isis_snd_receiver(IsisList *l, const IsisInputs *in, int index)
{
    char text[24];
    float x = index ? 492.0f : 20.0f;
    int anchor = index ? ISIS_END : ISIS_START;
    isis_text(l, x, 430, 28, anchor, ISIS_WHITE, index ? "VOR2" : "VOR1", 0, 0);
    if (in->nav[index].received && in->nav[index].ident[0]) {
        snprintf(text, sizeof(text), "%.7s", in->nav[index].ident);
        isis_text(l, x, 462, 28, anchor, ISIS_WHITE, text, 0, 0);
    } else if (in->nav[index].frequency_10khz > 0) {
        snprintf(text, sizeof(text), "%d.%02d", in->nav[index].frequency_10khz / 100, in->nav[index].frequency_10khz % 100);
        isis_text(l, x, 462, 28, anchor, ISIS_CYAN, text, 0, 0);
    }
    if (in->nav[index].received && in->nav[index].has_dme && isfinite(in->nav[index].dme_nm) && in->nav[index].dme_nm >= 0) {
        float dme = in->nav[index].dme_nm;
        snprintf(text, sizeof(text), dme < 20.0f ? "%.1f" : "%.0f", (double)fminf(dme, 999.0f));
        if (index) {
            isis_text(l, x - 46, 494, 28, ISIS_END, ISIS_GREEN, text, 0, 0);
            isis_text(l, x, 494, 28, ISIS_END, ISIS_CYAN, "NM", 0, 0);
        } else {
            float width = isis_text_advance(isis_face_for(l, 28 * isis_unit(l)), text, 0) / isis_unit(l);
            isis_text(l, x, 494, 28, ISIS_START, ISIS_GREEN, text, 0, 0);
            isis_text(l, x + width + 6, 494, 28, ISIS_START, ISIS_CYAN, "NM", 0, 0);
        }
    }
}

static void isis_draw_snd(IsisList *l, const IsisInputs *in)
{
    static const float lubber[] = {ISIS_SND_CX, ISIS_SND_CY - ISIS_SND_R - 18, ISIS_SND_CX, ISIS_SND_CY - ISIS_SND_R + 22};
    char text[16];
    int i;
    if (!in->heading_valid || !isfinite(in->heading_mag_deg)) {
        isis_text(l, 256, 190, 32, ISIS_MIDDLE, ISIS_AMBER, "HDG NOT AVAIL", 0, 0);
    } else {
        IsisSave save;
        float heading = fmodf(fmodf(in->heading_mag_deg, 360.0f) + 360.0f, 360.0f);
        int shown = (int)isis_js_round(heading) % 360;
        isis_save(l, &save);
        isis_rotate(l, -heading, ISIS_SND_CX, ISIS_SND_CY);
        for (i = 0; i < 72; ++i) {
            IsisSave tick;
            float length = i % 2 ? 10.0f : 20.0f;
            isis_save(l, &tick);
            isis_rotate(l, (float)i * 5.0f, ISIS_SND_CX, ISIS_SND_CY);
            isis_line(l, ISIS_SND_CX, ISIS_SND_CY - ISIS_SND_R, ISIS_SND_CX, ISIS_SND_CY - ISIS_SND_R + length,
                      ISIS_AH_STROKE, ISIS_WHITE);
            if (i % 6 == 0) {
                snprintf(text, sizeof(text), "%d", i / 2);
                isis_text(l, ISIS_SND_CX, ISIS_SND_CY - ISIS_SND_R + 50, 28, ISIS_MIDDLE, ISIS_WHITE, text, 0, 0);
            }
            isis_restore(l, &tick);
        }
        isis_restore(l, &save);
        for (i = 1; i >= 0; --i)
            if (in->nav[i].received && !in->nav[i].localizer && isfinite(in->nav[i].relative_bearing_deg))
                isis_snd_pointer(l, in->nav[i].relative_bearing_deg, i);
        /* Digital magnetic heading (the SND's own reference, not FMS data). */
        isis_rect(l, 213, 20, 86, 42, ISIS_BLACK, 0);
        isis_rect_stroke(l, 213, 20, 86, 42, ISIS_ROOT_STROKE, ISIS_WHITE);
        snprintf(text, sizeof(text), "%03d", shown);
        isis_text(l, 256, 54, 32, ISIS_MIDDLE, ISIS_GREEN, text, 0, 0);
        isis_stroke(l, lubber, 2, 0, 5.0f, ISIS_YELLOW, 0);
    }
    {
        static const float fuselage[] = {256, 262, 256, 322}, wing[] = {222, 282, 290, 282}, tail[] = {242, 314, 270, 314};
        isis_stroke(l, fuselage, 2, 0, 5.0f, ISIS_YELLOW, 0);
        isis_stroke(l, wing, 2, 0, 5.0f, ISIS_YELLOW, 0);
        isis_stroke(l, tail, 2, 0, 5.0f, ISIS_YELLOW, 0);
    }
    if (in->heading_valid && isfinite(in->heading_mag_deg)) isis_text(l, 20, 54, 28, ISIS_START, ISIS_WHITE, "MAG", 0, 0);
    isis_snd_receiver(l, in, 0);
    isis_snd_receiver(l, in, 1);
}

static void isis_build(IsisList *l, const IsisState *s, const IsisInputs *in)
{
    isis_list_reset(l);
    if (s->du == ISIS_DU_OFF) return;
    if (s->du == ISIS_DU_SELFTEST) { isis_draw_selftest(l, s, in); return; }
    if (s->role == ISIS_ROLE_SND) isis_draw_snd(l, in);
    else if (s->page == ISIS_PAGE_BUGS) isis_draw_bugs(l, s);
    else if (s->page == ISIS_PAGE_MENU) isis_draw_menu(l, s);
    else isis_draw_pfd(l, s, in);
}

static uint64_t isis_list_hash(const IsisList *l)
{
    uint64_t h = 1469598103934665603ULL;
    size_t i, n = (size_t)l->vertex_count * (sizeof(IsisVertex) / sizeof(uint32_t));
    for (i = 0; i < n; ++i) { /* FNV-1a over 32-bit words of the vertex array */
        uint32_t word;
        memcpy(&word, (const unsigned char *)l->vertex + i * sizeof(word), sizeof(word));
        h = (h ^ word) * 1099511628211ULL;
    }
    for (i = 0; i < (size_t)l->batch_count; ++i) h = (h ^ (uint64_t)(l->batch[i].count * 2 + l->batch[i].textured)) * 1099511628211ULL;
    return h;
}
