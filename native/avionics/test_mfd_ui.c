/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Unit tests of the A380X MFD framework (WP-A): hit testing and layers, click vs outside click,
 * menus, InputField editing (KCCU keys, ENT, ESC, BACKSPACE, overflow, errors), dropdown filter,
 * context menu, dialog, navigation stack, KCCU page keys, CLR INFO, DU gating, cursor timeout.
 *
 * Link: a380x_mfd.c a380x_mfd_ui.c a380x_mfd_draw.c a380x_mfd_page_data.c a380x_mfd_formats.c
 *       a380x_fms_messages.c -lm
 */
#include "a380x_mfd.h"
#include "a380x_mfd_page_data.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail, g_checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(c)) {                                                               \
            ++g_fail;                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)

static A380XMfd g_mfd;
static MfdMsgQueue g_q;
static A380XMfdInputs g_in;
static double g_t = 10.0;

/* ---- Test page ---------------------------------------------------------------------------- */

#define TP(n) MFD_ID(MFD_PAGE_ID_TEST, (n))
static struct {
    MfdFieldValue a, b, c;
    int a_commits, a_clears, b_commits, c_commits;
    int d_clicks, e_item, f_index, f_modified, g_radio, h_item, j_result, show_dialog;
    char f_text[32];
    int under_clicks;
} T;

static void test_page(MfdPageContext *ctx)
{
    static const MfdMenuItem e_items[] = {{"ONE", 0}, {"TWO", 1}, {"THREE", 0}};
    static const char *const f_values[] = {"ALPHA", "BRAVO", "BRAVA", "CHARLIE"};
    static const char *const radio[] = {"A", "B", "C"};
    static const MfdMenuItem ctx_items[] = {{"INSERT", 0}, {"DELETE", 1}};
    MfdUi *ui = ctx->ui;
    MfdFieldDesc d;
    MfdValue out;
    MfdFieldResult r;
    MfdDropdownDesc dd;
    int k;
    char text[32];

    mfd_field_desc_init(&d, MFD_FMT_SPEED_KT);
    d.fmt.min = 90.0;
    d.fmt.max = 350.0;
    r = mfd_input_field(ui, TP(1), 100.0f, 200.0f, &d, &T.a, &out);
    if (r == MFD_FIELD_COMMITTED) { T.a_commits++; T.a.has_value = 1; T.a.num = out.num; T.a.is_computed = 0; }
    if (r == MFD_FIELD_CLEARED) { T.a_clears++; T.a.has_value = 0; }

    mfd_field_desc_init(&d, MFD_FMT_FLIGHT_LEVEL);
    r = mfd_input_field(ui, TP(2), 100.0f, 300.0f, &d, &T.b, &out);
    if (r == MFD_FIELD_COMMITTED) { T.b_commits++; T.b.has_value = 1; T.b.num = out.num; }

    mfd_field_desc_init(&d, MFD_FMT_SPEED_KT);
    d.flags = MFD_FIELD_NOT_CLEARABLE;
    r = mfd_input_field(ui, TP(3), 100.0f, 400.0f, &d, &T.c, &out);
    if (r != MFD_FIELD_NONE) T.c_commits++;

    if (mfd_button(ui, TP(4), mfd_rect(400.0f, 200.0f, 120.0f, 40.0f), "PRESS", 0)) T.d_clicks++;
    k = mfd_menu_button(ui, TP(5), mfd_rect(400.0f, 300.0f, 0.0f, 0.0f), "MENU", e_items, 3, 0);
    if (k >= 0) T.e_item = k;
    /* A button under the menu-button list (to check that the menu wins). */
    if (mfd_button(ui, TP(6), mfd_rect(400.0f, 345.0f, 150.0f, 40.0f), "UNDER", 0)) T.under_clicks++;

    memset(&dd, 0, sizeof(dd));
    dd.values = f_values;
    dd.count = 4;
    dd.selected = T.f_index;
    dd.free_text = 1;
    if (mfd_dropdown(ui, TP(7), mfd_rect(400.0f, 500.0f, 200.0f, 44.0f), &dd, &k, text, sizeof(text))) {
        T.f_modified++;
        if (k >= 0) T.f_index = k;
        snprintf(T.f_text, sizeof(T.f_text), "%s", text);
    }
    k = mfd_radio_group(ui, TP(8), 100.0f, 600.0f, 200.0f, radio, 3, T.g_radio, 0x4u, MFD_COL_CYAN, 0.0f);
    if (k >= 0) T.g_radio = k;
    if (mfd_button(ui, TP(9), mfd_rect(400.0f, 700.0f, 120.0f, 40.0f), "CTX", 0))
        mfd_context_menu_open(ui, TP(10), 400.0f, 745.0f);
    k = mfd_context_menu(ui, TP(10), ctx_items, 2);
    if (k >= 0) T.h_item = k;
    if (T.show_dialog) {
        k = mfd_confirm_dialog(ui, TP(20), mfd_rect(200.0f, 150.0f, 325.0f, 165.0f), "SURE ?", 0);
        if (k != MFD_DIALOG_NONE) { T.j_result = k; T.show_dialog = 0; }
    }
}

/* ---- Driver ------------------------------------------------------------------------------- */

static void frame(void)
{
    g_in.now = g_t;
    a380x_mfd_frame(&g_mfd, &g_in);
    g_t += 0.02;
    CHECK(!g_mfd.sides[0].dl.overflowed && !g_mfd.sides[1].dl.overflowed);
    CHECK(!g_mfd.sides[0].ui.hits_overflowed);
}

static void frames(int n) { while (n-- > 0) frame(); }
static void wait_s(double s) { double end = g_t + s; while (g_t < end) frame(); }
static void move(MfdSide s, float x, float y) { mfd_pointer_move(&g_mfd, s, x, y); frame(); }

static void click_side(MfdSide s, float x, float y)
{
    move(s, x, y);
    mfd_pointer_button(&g_mfd, s, 1);
    frame();
    mfd_pointer_button(&g_mfd, s, 0);
    frame();
}

static void click(float x, float y) { click_side(MFD_CAPT, x, y); }
static void key(MfdKey k, char ch) { mfd_key(&g_mfd, MFD_CAPT, k, ch); frame(); }
static void type(const char *s) { while (*s) key(MFD_KEY_CHAR, *s++); }
static const char *uri(void) { return a380x_mfd_active_uri(&g_mfd, MFD_CAPT); }
static MfdUi *ui0(void) { return &g_mfd.sides[0].ui; }

static void reset(void)
{
    memset(&T, 0, sizeof(T));
    mfd_msg_init(&g_q);
    memset(&g_in, 0, sizeof(g_in));
    g_in.du_on[0] = g_in.du_on[1] = 1;
    g_in.fms_knob = 1;
    g_in.fmc_a_healthy = g_in.fmc_b_healthy = 1;
    a380x_mfd_init(&g_mfd, NULL, &g_q, NULL);
    frames(2);
}

static void goto_test(void)
{
    a380x_mfd_navigate(&g_mfd, MFD_CAPT, "fms/test/ui");
    frames(2);
}

static int msg_count(void) { return g_q.count; }
static const char *footer_text(void)
{
    const MfdMsgEntry *e = mfd_msg_footer_entry(&g_q);
    return e ? e->text : "";
}

/* ---- Tests -------------------------------------------------------------------------------- */

static void test_uri_and_registry(void)
{
    MfdUri u;
    CHECK(mfd_uri_parse("fms/data/status/acft-status", &u));
    CHECK(strcmp(u.sys, "fms") == 0 && strcmp(u.category, "data") == 0 && strcmp(u.page, "status") == 0 &&
          strcmp(u.extra, "acft-status") == 0);
    CHECK(mfd_uri_parse("atccom/d-atis/received/2/x", &u) && strcmp(u.extra, "2/x") == 0);
    CHECK(mfd_uri_parse("surv/controls", &u) && u.page[0] == '\0');
    reset();
    CHECK(strcmp(uri(), "fms/data/status") == 0);
    CHECK(strcmp(a380x_mfd_active_uri(&g_mfd, MFD_FO), "fms/data/status") == 0);
    CHECK(g_mfd.sides[0].page_index >= 0);
    /* wildcard category */
    a380x_mfd_navigate(&g_mfd, MFD_CAPT, "fms/sec2/test-wild");
    frames(1);
    CHECK(g_mfd.sides[0].page_index >= 0);
    a380x_mfd_navigate(&g_mfd, MFD_CAPT, "fms/data/test-wild");
    frames(1);
    CHECK(g_mfd.sides[0].page_index == -1); /* not a plan category */
}

static void test_navigation_stack(void)
{
    int i;
    char u[64];
    reset();
    CHECK(g_mfd.sides[0].depth == 1);
    goto_test();
    CHECK(strcmp(uri(), "fms/test/ui") == 0 && g_mfd.sides[0].depth == 2);
    a380x_mfd_navigate(&g_mfd, MFD_CAPT, "fms/test/ui"); /* identical URI ignored */
    frames(1);
    CHECK(g_mfd.sides[0].depth == 2);
    a380x_mfd_navigate(&g_mfd, MFD_CAPT, "back");
    frames(1);
    CHECK(strcmp(uri(), "fms/data/status") == 0 && g_mfd.sides[0].depth == 1);
    a380x_mfd_navigate(&g_mfd, MFD_CAPT, "back"); /* depth < 2: ignored */
    frames(1);
    CHECK(strcmp(uri(), "fms/data/status") == 0);
    for (i = 0; i < 40; ++i) {
        snprintf(u, sizeof(u), "fms/data/status/%d", i);
        a380x_mfd_navigate(&g_mfd, MFD_CAPT, u);
        frames(1);
    }
    CHECK(g_mfd.sides[0].depth == MFD_NAV_STACK_CAP);
    CHECK(strcmp(g_mfd.sides[0].stack[0], "fms/data/status/8") == 0);
    CHECK(strcmp(g_mfd.sides[0].stack[31], "fms/data/status/39") == 0);
    /* forced reload of fms/active/perf pushes again */
    reset();
    key(MFD_KEY_PERF, 0);
    CHECK(strcmp(uri(), "fms/active/perf") == 0);
    i = g_mfd.sides[0].depth;
    key(MFD_KEY_PERF, 0);
    CHECK(g_mfd.sides[0].depth == i + 1);
}

static void test_page_keys_and_clrinfo(void)
{
    static const struct { MfdKey k; const char *u; } keys[] = {
        {MFD_KEY_DIR, "fms/active/f-pln-direct-to"}, {MFD_KEY_INIT, "fms/active/init"},
        {MFD_KEY_NAVAID, "fms/position/navaids"}, {MFD_KEY_FPLN, "fms/active/f-pln/top"},
        {MFD_KEY_DEST, "fms/active/f-pln/dest"}, {MFD_KEY_SECINDEX, "fms/sec/index"},
        {MFD_KEY_SURV, "surv/controls"}, {MFD_KEY_ATCCOM, "atccom/connect"}};
    size_t i;
    reset();
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        key(keys[i].k, 0);
        CHECK(strcmp(uri(), keys[i].u) == 0);
    }
    /* FO keys do not affect CAPT */
    mfd_key(&g_mfd, MFD_FO, MFD_KEY_INIT, 0);
    frame();
    CHECK(strcmp(uri(), "atccom/connect") == 0);
    CHECK(strcmp(a380x_mfd_active_uri(&g_mfd, MFD_FO), "fms/active/init") == 0);
    /* CLR INFO: Type I removed */
    mfd_msg_add(&g_q, MFD_MSG_NOT_IN_DATABASE, NULL, NULL, NULL, NULL, NULL);
    CHECK(msg_count() == 1);
    key(MFD_KEY_CLRINFO, 0);
    CHECK(msg_count() == 0);
    CHECK(strcmp(uri(), "atccom/connect") == 0);
}

static void test_header_menus(void)
{
    reset();
    /* open ACTIVE menu */
    click(96.0f, 71.0f);
    CHECK(mfd_ui_menu_is_open(ui0(), MFD_ID(MFD_SCOPE_HEADER, 10)));
    /* click POSITION while ACTIVE is open: ACTIVE closes and POSITION opens in the same click */
    click(288.0f, 71.0f);
    CHECK(!mfd_ui_menu_is_open(ui0(), MFD_ID(MFD_SCOPE_HEADER, 10)));
    CHECK(mfd_ui_menu_is_open(ui0(), MFD_ID(MFD_SCOPE_HEADER, 11)));
    /* outside click closes */
    click(384.0f, 700.0f);
    CHECK(ui0()->open_menu == 0);
    /* disabled WIND row (index 3) keeps the menu open and does not navigate */
    click(96.0f, 71.0f);
    click(60.0f, 98.0f + 2.0f + 36.0f * 3.0f + 18.0f);
    CHECK(mfd_ui_menu_is_open(ui0(), MFD_ID(MFD_SCOPE_HEADER, 10)));
    CHECK(strcmp(uri(), "fms/data/status") == 0);
    /* INIT row (index 4) navigates; unregistered -> NOT FOUND, back after 500 ms */
    click(60.0f, 98.0f + 2.0f + 36.0f * 4.0f + 18.0f);
    CHECK(strcmp(uri(), "fms/active/init") == 0);
    CHECK(g_mfd.sides[0].page_index == -1);
    wait_s(0.6);
    CHECK(strcmp(uri(), "fms/data/status") == 0);
    /* system selector: SURV */
    click(100.0f, 22.0f);
    CHECK(mfd_ui_menu_is_open(ui0(), MFD_ID(MFD_SCOPE_HEADER, 1)));
    CHECK(ui0()->focus_id == MFD_ID(MFD_SCOPE_HEADER, 1)); /* embedded field focused */
    click(60.0f, 44.0f + 2.0f + 36.0f * 2.0f + 18.0f);
    CHECK(strcmp(uri(), "surv/controls") == 0);
    CHECK(ui0()->focus_id == 0);
    /* FCU BKUP is a stub page (no auto-back) */
    click(100.0f, 22.0f);
    click(60.0f, 44.0f + 2.0f + 36.0f * 3.0f + 18.0f);
    CHECK(strcmp(uri(), "fcubkup/afs") == 0 && g_mfd.sides[0].page_index == -2);
    wait_s(0.7);
    CHECK(strcmp(uri(), "fcubkup/afs") == 0);
    /* single-item selector navigates immediately */
    click(288.0f, 71.0f);
    CHECK(strcmp(uri(), "fcubkup/efis") == 0);
}

static void test_buttons_and_hits(void)
{
    reset();
    goto_test();
    click(460.0f, 220.0f);
    CHECK(T.d_clicks == 1);
    /* press on the button, release elsewhere: no click */
    move(MFD_CAPT, 460.0f, 220.0f);
    mfd_pointer_button(&g_mfd, MFD_CAPT, 1);
    frame();
    move(MFD_CAPT, 600.0f, 220.0f);
    mfd_pointer_button(&g_mfd, MFD_CAPT, 0);
    frame();
    CHECK(T.d_clicks == 1);
    /* hover */
    move(MFD_CAPT, 460.0f, 220.0f);
    CHECK(mfd_ui_is_hovered(ui0(), TP(4)));
    /* menu button: open, disabled item ignored, enabled item selected, menu over UNDER button */
    click(420.0f, 320.0f);
    CHECK(mfd_ui_menu_is_open(ui0(), TP(5)));
    click(430.0f, 340.0f + 2.0f + 36.0f + 18.0f); /* TWO (disabled), above the UNDER button */
    CHECK(mfd_ui_menu_is_open(ui0(), TP(5)) && T.under_clicks == 0);
    click(430.0f, 340.0f + 2.0f + 18.0f); /* ONE, over UNDER */
    CHECK(T.e_item == 0 && !mfd_ui_menu_is_open(ui0(), TP(5)) && T.under_clicks == 0);
    click(430.0f, 365.0f); /* menu closed: now UNDER gets it */
    CHECK(T.under_clicks == 1);
    /* wheel over a non-scrolling menu keeps scroll 0 */
    click(420.0f, 320.0f);
    move(MFD_CAPT, 430.0f, 360.0f);
    mfd_pointer_wheel(&g_mfd, MFD_CAPT, -2);
    frame();
    CHECK(ui0()->menu_scroll == 0.0f);
    click(10.0f, 800.0f);
    CHECK(ui0()->open_menu == 0);
    /* radio: disabled row 2 ignored */
    click(150.0f, 600.0f + 39.0f + 17.0f);
    CHECK(T.g_radio == 1);
    click(150.0f, 600.0f + 78.0f + 17.0f);
    CHECK(T.g_radio == 1);
}

static void test_input_field(void)
{
    char ff[32];
    reset();
    goto_test();
    /* focus, type 250, ENT -> validating 500 ms -> committed */
    click(130.0f, 218.0f);
    CHECK(ui0()->focus_id == TP(1) && ui0()->edit == MFD_EDIT_FOCUSED);
    CHECK(mfd_ui_wants_keyboard(ui0())); /* touchscreen mode */
    type("25");
    CHECK(ui0()->edit == MFD_EDIT_EDITING && strcmp(ui0()->buf, "25") == 0);
    key(MFD_KEY_BACKSPACE, 0);
    CHECK(strcmp(ui0()->buf, "2") == 0);
    type("50");
    key(MFD_KEY_CHAR, '9'); /* length limit 3 */
    CHECK(strcmp(ui0()->buf, "250") == 0);
    key(MFD_KEY_ENT, 0);
    CHECK(ui0()->focus_id == 0 && T.a_commits == 0);
    wait_s(0.3);
    CHECK(T.a_commits == 0);
    wait_s(0.3);
    CHECK(T.a_commits == 1 && T.a.has_value && fabs(T.a.num - 250.0) < 1e-9);
    /* ESC cancels: no commit, no message */
    click(130.0f, 218.0f);
    type("1");
    key(MFD_KEY_ESC, 0);
    wait_s(0.6);
    CHECK(ui0()->focus_id == 0 && T.a_commits == 1 && T.a.num == 250.0 && msg_count() == 0);
    /* format error -> message, value unchanged */
    click(130.0f, 218.0f);
    type("A");
    key(MFD_KEY_ENT, 0);
    wait_s(0.6);
    CHECK(T.a_commits == 1 && msg_count() == 1);
    CHECK(strncmp(footer_text(), "FORMAT ERROR", 12) == 0);
    mfd_msg_clear_latest(&g_q);
    /* out of range */
    click(130.0f, 218.0f);
    type("999");
    key(MFD_KEY_ENT, 0);
    wait_s(0.6);
    CHECK(T.a_commits == 1 && strncmp(footer_text(), "ENTRY OUT OF RANGE", 18) == 0);
    mfd_msg_clear_latest(&g_q);
    /* unmodified ENT re-submits the formatted value */
    click(130.0f, 218.0f);
    key(MFD_KEY_ENT, 0);
    wait_s(0.6);
    CHECK(T.a_commits == 2 && T.a.num == 250.0);
    /* BACKSPACE on an unmodified field then ENT clears */
    click(130.0f, 218.0f);
    key(MFD_KEY_BACKSPACE, 0);
    CHECK(ui0()->edit == MFD_EDIT_EDITING && ui0()->buf[0] == '\0');
    key(MFD_KEY_ENT, 0);
    wait_s(0.6);
    CHECK(T.a_clears == 1 && !T.a.has_value);
    /* not clearable: BACKSPACE ignored while unmodified */
    T.c.has_value = 1; T.c.num = 140;
    click(130.0f, 418.0f);
    key(MFD_KEY_BACKSPACE, 0);
    CHECK(ui0()->edit == MFD_EDIT_FOCUSED);
    key(MFD_KEY_ESC, 0);
    /* mouse-down outside commits (validated), then the click target acts */
    click(130.0f, 218.0f);
    type("180");
    click(460.0f, 220.0f);
    CHECK(T.d_clicks == 1 && ui0()->focus_id == 0);
    wait_s(0.6);
    CHECK(T.a_commits == 3 && T.a.num == 180.0);
    /* clicking another field commits the old one and focuses the new one */
    click(130.0f, 218.0f);
    type("200");
    click(130.0f, 318.0f);
    CHECK(ui0()->focus_id == TP(2));
    wait_s(0.6);
    CHECK(T.a_commits == 4 && T.a.num == 200.0);
    /* overflow (FL: 3 + 2 digits) */
    type("FL3");
    CHECK(ui0()->overflow == 1);
    type("50");
    CHECK(strcmp(ui0()->buf, "FL350") == 0);
    key(MFD_KEY_CHAR, '0'); /* limit 5 */
    CHECK(strcmp(ui0()->buf, "FL350") == 0);
    key(MFD_KEY_ENT, 0);
    CHECK(ui0()->overflow == 0);
    wait_s(0.6);
    CHECK(T.b_commits == 1 && T.b.num == 350.0);
    /* PLUSMINUS toggles the sign */
    click(130.0f, 218.0f);
    key(MFD_KEY_PLUSMINUS, 0);
    CHECK(strcmp(ui0()->buf, "-") == 0);
    key(MFD_KEY_PLUSMINUS, 0);
    CHECK(strcmp(ui0()->buf, "+") == 0);
    /* navigation drops the field without validation */
    type("1");
    key(MFD_KEY_INIT, 0);
    CHECK(ui0()->focus_id == 0);
    wait_s(0.6);
    CHECK(T.a_commits == 4);
    /* KCCU mode: no PC keyboard capture */
    reset();
    g_in.kbd_on[0] = 1;
    goto_test();
    click(130.0f, 218.0f);
    CHECK(ui0()->focus_id == TP(1) && !mfd_ui_wants_keyboard(ui0()));
    /* FO keys do not reach the CAPT field */
    mfd_key(&g_mfd, MFD_FO, MFD_KEY_CHAR, '7');
    frame();
    CHECK(ui0()->edit == MFD_EDIT_FOCUSED);
    (void)ff;
}

static void test_dropdown_context_dialog(void)
{
    reset();
    goto_test();
    /* open, type BRA -> filtered to BRAVO, BRAVA; click first filtered row = BRAVO (index 1) */
    click(450.0f, 522.0f);
    CHECK(mfd_ui_menu_is_open(ui0(), TP(7)) && ui0()->focus_id == TP(7));
    type("BRA");
    click(450.0f, 544.0f + 2.0f + 18.0f);
    CHECK(T.f_modified == 1 && T.f_index == 1 && !mfd_ui_menu_is_open(ui0(), TP(7)));
    /* free text: type ZULU + ENT -> modified(-1, "ZULU") after validation */
    click(450.0f, 522.0f);
    type("ZULU");
    key(MFD_KEY_ENT, 0);
    wait_s(0.6);
    CHECK(T.f_modified == 2 && strcmp(T.f_text, "ZULU") == 0 && T.f_index == 1);
    /* toggle closes without validation */
    click(450.0f, 522.0f);
    click(450.0f, 522.0f);
    CHECK(!mfd_ui_menu_is_open(ui0(), TP(7)) && ui0()->focus_id == 0);
    /* context menu: disabled item does nothing but closes after 100 ms; enabled item fires */
    click(460.0f, 720.0f);
    CHECK(mfd_context_menu_is_open(ui0(), TP(10)));
    wait_s(0.15);
    click(430.0f, 745.0f + 2.0f + 21.0f);
    CHECK(T.h_item == 0 && !mfd_context_menu_is_open(ui0(), TP(10)));
    click(460.0f, 720.0f);
    wait_s(0.15);
    click(430.0f, 745.0f + 2.0f + 42.0f + 21.0f); /* disabled */
    CHECK(T.h_item == 0 && !mfd_context_menu_is_open(ui0(), TP(10)));
    /* confirmation dialog swallows clicks; CONFIRM */
    T.show_dialog = 1;
    frames(2);
    click(300.0f, 200.0f); /* inside dialog, nothing */
    CHECK(T.j_result == 0 && T.show_dialog == 1);
    click(525.0f - 7.0f - 20.0f, 150.0f + 165.0f - 27.0f);
    CHECK(T.j_result == MFD_DIALOG_CONFIRM);
}

static void test_footer_overlays_du(void)
{
    reset();
    /* MSG LIST opens from the footer and closes on navigation */
    click(55.0f, 988.0f);
    CHECK(g_mfd.sides[0].msg_list_open == 1);
    /* overlay swallows clicks on the page below (DATA/STATUS SWAP) */
    click(392.0f, 697.0f);
    CHECK(g_mfd.sides[0].msg_list_open == 1);
    click(48.0f, 930.0f); /* CLOSE */
    CHECK(g_mfd.sides[0].msg_list_open == 0);
    click(55.0f, 988.0f);
    key(MFD_KEY_SURV, 0);
    CHECK(g_mfd.sides[0].msg_list_open == 0);
    /* footer button with a message = CLEAR INFO */
    reset();
    mfd_msg_add(&g_q, MFD_MSG_NOT_IN_DATABASE, NULL, NULL, NULL, NULL, NULL);
    click(55.0f, 988.0f);
    CHECK(msg_count() == 0 && g_mfd.sides[0].msg_list_open == 0);
    /* FMS PAGE NOT AVAIL: page receives no input */
    reset();
    goto_test();
    g_in.fms_failed[0] = 1;
    click(460.0f, 220.0f);
    CHECK(T.d_clicks == 0);
    g_in.fms_failed[0] = 0;
    frames(1);
    click(460.0f, 220.0f);
    CHECK(T.d_clicks == 1);
    /* DU off: input ignored, display list empty, state persists */
    g_in.du_on[0] = 0;
    frame();
    click(460.0f, 220.0f);
    key(MFD_KEY_SURV, 0);
    CHECK(T.d_clicks == 1 && strcmp(uri(), "fms/test/ui") == 0);
    CHECK(a380x_mfd_display_list(&g_mfd, MFD_CAPT)->cmd_count == 0);
    g_in.du_on[0] = 1;
    frames(1);
    CHECK(a380x_mfd_display_list(&g_mfd, MFD_CAPT)->cmd_count > 0);
    /* cursor hides 5 s after the last move */
    move(MFD_CAPT, 300.0f, 300.0f);
    CHECK(ui0()->cursor_visible);
    wait_s(5.1);
    CHECK(!ui0()->cursor_visible && ui0()->hover_id == 0);
    /* FMS label */
    g_in.fmc_a_healthy = 0;
    CHECK(strcmp(a380x_mfd_fms_label(&g_in, MFD_CAPT), "FMS 1-C") == 0);
    CHECK(strcmp(a380x_mfd_fms_label(&g_in, MFD_FO), "FMS 2") == 0);
    g_in.fms_knob = 0;
    CHECK(strcmp(a380x_mfd_fms_label(&g_in, MFD_CAPT), "FMS 2") == 0);
}

static void test_draw_list(void)
{
    static MfdDrawList dl;
    int i;
    MfdKey k;
    char ch;
    mfd_draw_reset(&dl);
    mfd_draw_set_layer(&dl, MFD_LAYER_MENU);
    mfd_draw_fill_rect(&dl, mfd_rect(0, 0, 10, 10), MFD_COL_WHITE);
    mfd_draw_set_layer(&dl, MFD_LAYER_PAGE);
    mfd_draw_clip_push(&dl, mfd_rect(5, 5, 100, 100));
    mfd_draw_text(&dl, MFD_FONT_SZ, 20.0f, MFD_COL_GREEN, MFD_ANCHOR_LEFT, 10, 30, "AB\xC2\xB0");
    mfd_draw_clip_pop(&dl);
    {
        uint16_t order[8];
        CHECK(mfd_draw_order(&dl, order) == 2 && order[0] == 1 && order[1] == 0);
    }
    CHECK(dl.cmds[1].clip == 1 && dl.clips[1].x == 5.0f);
    CHECK(fabsf(mfd_text_width("AB\xC2\xB0", 20.0f) - 3.0f * 0.626953125f * 20.0f) < 1e-4f);
    for (i = 0; i < MFD_DRAW_MAX_CMDS + 10; ++i) mfd_draw_fill_rect(&dl, mfd_rect(0, 0, 1, 1), MFD_COL_RED);
    CHECK(dl.overflowed && dl.cmd_count == MFD_DRAW_MAX_CMDS);
    CHECK(mfd_key_from_name("q", &k, &ch) && k == MFD_KEY_CHAR && ch == 'Q');
    CHECK(mfd_key_from_name("slash", &k, &ch) && ch == '/');
    CHECK(mfd_key_from_name("clrinfo", &k, &ch) && k == MFD_KEY_CLRINFO);
    CHECK(!mfd_key_from_name("bogus", &k, &ch));
}

static void wild_page(MfdPageContext *ctx) { (void)ctx; }

int main(void)
{
    char d[16];
    a380x_mfd_page_data_register();
    mfd_register_page("fms/test/ui", MFD_PAGE_ID_TEST, "UI", test_page, MFD_PAGE_FMS | MFD_PAGE_TITLE_CATEGORY);
    mfd_register_page("fms/*/test-wild", MFD_PAGE_ID_TEST, "WILD", wild_page, MFD_PAGE_FMS);
    CHECK(mfd_airac_cycle_dates("2410", d, sizeof(d)) && strcmp(d, "03OCT-31OCT") == 0);
    CHECK(mfd_airac_cycle_dates("2609", d, sizeof(d)) && strcmp(d, "03SEP-01OCT") == 0);
    CHECK(mfd_airac_cycle_dates("2001", d, sizeof(d)) && strcmp(d, "02JAN-30JAN") == 0);
    test_draw_list();
    test_uri_and_registry();
    test_navigation_stack();
    test_page_keys_and_clrinfo();
    test_header_menus();
    test_buttons_and_hits();
    test_input_field();
    test_dropdown_context_dialog();
    test_footer_overlays_du();
    if (g_fail) {
        printf("test_mfd_ui: %d of %d checks FAILED\n", g_fail, g_checks);
        return 1;
    }
    printf("test_mfd_ui: %d checks OK\n", g_checks);
    return 0;
}
