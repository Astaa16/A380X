/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Both NDs as X-Plane 12.1+ plugin cockpit devices (a380x.xp12.nd.captain /
 * a380x.xp12.nd.first_officer) on the FlyByWire 768 x 1024 ND canvas. ARC mode is drawn in the
 * FBW layout for ARC, ROSE and PLAN (a380x_nd_fbw.inc); the airport zoom still uses the native
 * 180 x 234 layout, scaled up with glScalef and with text_at() on the scalable EIS atlas.
 *
 * The device quads already exist in a380x_mfd_screens.obj and are shown only while
 * a380x/xp12/xpkit/nd_devices_active is 1, which this file publishes once the devices exist.
 * Like the PFD devices, they are created from XPluginStart before the OBJs load.
 */

#define ND_LAYOUT_W 180.0f
#define ND_LAYOUT_H 234.0f
#define ND_DEVICE_W 768
#define ND_DEVICE_H 1024
#define ND_DEVICE_SCALE ((float)ND_DEVICE_W / ND_LAYOUT_W) /* native layout fills the width */

typedef struct {
    const char *device_id, *device_name, *side_name;
    int side;
    XPLMAvionicsID handle;
    float last_rheo;
} NdDevice;

static NdDevice nd_devices[2] = {
    {"a380x.xp12.nd.captain", "A380X ND CAPT", "CAPT", 0, NULL, -1.0f},
    {"a380x.xp12.nd.first_officer", "A380X ND F/O", "F/O", 1, NULL, -1.0f},
};
static int nd_devices_created;
static XPLMDataRef nd_devices_active_ref;

static int nd_devices_active_get(void *refcon) { (void)refcon; return nd_devices_created; }

static void nd_device_draw(void *refcon)
{
    NdDevice *d = (NdDevice *)refcon;
    GLboolean cull = glIsEnabled(GL_CULL_FACE);
    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);                     /* X-Plane does not clear the device FBO */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE); /* keep alpha = 1: the screen never blends */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (plugin_enabled && d) {
        const Side *side = &refs.side[d->side];
        pfd_sync_glare();
        text_use_atlas = 1;
        if (!efis[d->side].zoom) {
            draw_nd_fbw(side, d->side);            /* ARC, ROSE LS/VOR/NAV, PLAN */
        } else {                                   /* airport zoom (OANS): native layout */
            text_atlas_size = 9.0f;
            glPushMatrix();
            glTranslatef(0.0f, (ND_DEVICE_H - ND_LAYOUT_H * ND_DEVICE_SCALE) * 0.5f, 0.0f);
            glScalef(ND_DEVICE_SCALE, ND_DEVICE_SCALE, 1.0f);
            glLineWidth(ND_DEVICE_SCALE * 0.75f);
            draw_nd(side, 0.0f, 0.0f, d->side_name);
            glPopMatrix();
        }
        text_use_atlas = 0;
        text_atlas_size = 9.0f;
        glLineWidth(1.0f);
    }
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    if (cull) glEnable(GL_CULL_FACE);
}

static void nd_device_bezel_draw(float r, float g, float b, void *refcon)
{
    (void)r; (void)g; (void)b; (void)refcon;
    glColor4f(0.08f, 0.08f, 0.09f, 1.0f);
    glBegin(GL_QUADS);
    glVertex2f(0.0f, 0.0f); glVertex2f((float)ND_DEVICE_W, 0.0f);
    glVertex2f((float)ND_DEVICE_W, (float)ND_DEVICE_H); glVertex2f(0.0f, (float)ND_DEVICE_H);
    glEnd();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* The DU ND knob is the only dimmer; ambient keeps the screen readable in daylight. */
static float nd_device_brightness(float rheo, float ambient, float bus_ratio, void *refcon)
{
    NdDevice *d = (NdDevice *)refcon;
    float knob;
    (void)rheo; (void)bus_ratio;
    if (!d || !plugin_enabled || !refs.side[d->side].display_valid) return 0.0f;
    knob = du_channels[d->side * DU_CHANNELS_PER_SIDE + DU_ND].level;
    return fminf(1.0f, fmaxf(0.0f, knob * (ambient > 0.0f ? ambient : 1.0f)));
}

static void nd_devices_destroy(void)
{
    int s;
    for (s = 0; s < 2; ++s) {
        if (nd_devices[s].handle && mfda_av.loaded) mfda_av.destroy(nd_devices[s].handle);
        nd_devices[s].handle = NULL;
    }
    nd_devices_created = 0;
    if (nd_devices_active_ref) { XPLMUnregisterDataAccessor(nd_devices_active_ref); nd_devices_active_ref = NULL; }
}

/* Called from XPluginStart after mfda_devices_enable() and again from XPluginEnable. */
static void nd_devices_enable(void)
{
    int s;
    XPLMCreateAvionics_t p;
    if (!nd_devices_active_ref && !XPLMFindDataRef("a380x/xp12/xpkit/nd_devices_active"))
        nd_devices_active_ref = XPLMRegisterDataAccessor("a380x/xp12/xpkit/nd_devices_active", xplmType_Int, 0,
            nd_devices_active_get, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (nd_devices_created || !mfda_av.loaded) return;
    for (s = 0; s < 2; ++s) {
        NdDevice *d = &nd_devices[s];
        memset(&p, 0, sizeof(p));
        p.structSize = (int)sizeof(p);
        p.screenWidth = ND_DEVICE_W; p.screenHeight = ND_DEVICE_H;
        p.bezelWidth = p.screenWidth; p.bezelHeight = p.screenHeight;
        p.screenOffsetX = 0; p.screenOffsetY = 0;
        p.drawOnDemand = 0;
        p.bezelDrawCallback = nd_device_bezel_draw;
        p.drawCallback = nd_device_draw;
        p.brightnessCallback = nd_device_brightness;
        p.deviceID = (char *)d->device_id;
        p.deviceName = (char *)d->device_name;
        p.refcon = d;
        d->handle = mfda_av.create(&p);
        if (!d->handle) {
            XPLMDebugString("A380X ND: XPLMCreateAvionicsEx failed - NDs unavailable\n");
            for (s = 0; s < 2; ++s) {
                if (nd_devices[s].handle) mfda_av.destroy(nd_devices[s].handle);
                nd_devices[s].handle = NULL;
            }
            return;
        }
        mfda_av.set_rheo(d->handle, 1.0f);
    }
    nd_devices_created = 1;
    XPLMDebugString("A380X ND: captain and first officer cockpit devices created\n");
}

/* Flight loop: keep each device rheostat in step with its DU ND knob (see pfd_devices_update). */
static void nd_devices_update(void)
{
    int s;
    if (!nd_devices_created) return;
    for (s = 0; s < 2; ++s) {
        NdDevice *d = &nd_devices[s];
        float knob = du_channels[d->side * DU_CHANNELS_PER_SIDE + DU_ND].level;
        if (!d->handle) continue;
        if (fabsf(knob - d->last_rheo) > 0.001f || !mfda_av.is_bound(d->handle)) {
            d->last_rheo = knob;
            mfda_av.set_rheo(d->handle, knob);
        }
    }
}
