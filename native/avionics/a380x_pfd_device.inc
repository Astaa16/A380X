/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Both PFDs as X-Plane 12.1+ plugin cockpit devices (a380x.xp12.pfd.captain /
 * a380x.xp12.pfd.first_officer), drawn by the existing native renderer at
 * PFD_PIXEL_WIDTH x PFD_PIXEL_HEIGHT. The shared panel-texture regions the PFDs used
 * before stay dark in X-Plane 12.4 on this aircraft, while the cockpit-device MFDs,
 * NDs and SD render, so the PFDs follow the MFD device pattern (a380x_mfd_adapter.inc).
 *
 * Devices are created from XPluginStart, before X-Plane loads the OBJs that reference
 * them with ATTR_cockpit_device. They reuse the entry points resolved by mfda_av_load().
 */

typedef struct {
    const char *device_id, *device_name, *side_name;
    int side;
    XPLMAvionicsID handle;
    float last_rheo;
} PfdDevice;

static PfdDevice pfd_devices[2] = {
    {"a380x.xp12.pfd.captain", "A380X PFD CAPT", "CAPT", 0, NULL, -1.0f},
    {"a380x.xp12.pfd.first_officer", "A380X PFD F/O", "F/O", 1, NULL, -1.0f},
};
static int pfd_devices_created;

static void pfd_device_draw(void *refcon)
{
    PfdDevice *d = (PfdDevice *)refcon;
    GLboolean cull = glIsEnabled(GL_CULL_FACE);
    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);                     /* X-Plane does not clear the device FBO */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE); /* keep alpha = 1: the screen never blends */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (plugin_enabled && d) {
        pfd_sync_glare();
        draw_pfd(&refs.side[d->side], 0.0f, 0.0f, d->side_name);
    }
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    if (cull) glEnable(GL_CULL_FACE);
}

static void pfd_device_bezel_draw(float r, float g, float b, void *refcon)
{
    (void)r; (void)g; (void)b; (void)refcon;
    glColor4f(0.08f, 0.08f, 0.09f, 1.0f);
    glBegin(GL_QUADS);
    glVertex2f(0.0f, 0.0f); glVertex2f(PFD_PIXEL_WIDTH, 0.0f);
    glVertex2f(PFD_PIXEL_WIDTH, PFD_PIXEL_HEIGHT); glVertex2f(0.0f, PFD_PIXEL_HEIGHT);
    glEnd();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* The DU knob is the only dimmer; ambient keeps the screen readable in daylight (as the MFD). */
static float pfd_device_brightness(float rheo, float ambient, float bus_ratio, void *refcon)
{
    PfdDevice *d = (PfdDevice *)refcon;
    float knob;
    (void)rheo; (void)bus_ratio; /* PFD power is decided by the aircraft logic, not the XP bus */
    if (!d || !plugin_enabled || !refs.side[d->side].display_valid) return 0.0f;
    knob = du_channels[d->side * DU_CHANNELS_PER_SIDE + DU_PFD].level;
    return fminf(1.0f, fmaxf(0.0f, knob * (ambient > 0.0f ? ambient : 1.0f)));
}

/* Flight loop: keep the device rheostat (instrument_brightness_ratio channel) in step with the
 * DU knob, as the MFD does. A value set before X-Plane binds the device to its OBJ is not kept. */
static void pfd_devices_update(void)
{
    int s;
    if (!pfd_devices_created) return;
    for (s = 0; s < 2; ++s) {
        PfdDevice *d = &pfd_devices[s];
        float knob = du_channels[d->side * DU_CHANNELS_PER_SIDE + DU_PFD].level;
        if (!d->handle) continue;
        if (fabsf(knob - d->last_rheo) > 0.001f || !mfda_av.is_bound(d->handle)) {
            d->last_rheo = knob;
            mfda_av.set_rheo(d->handle, knob);
        }
    }
}

static void pfd_devices_destroy(void)
{
    int s;
    for (s = 0; s < 2; ++s) {
        if (pfd_devices[s].handle && mfda_av.loaded) mfda_av.destroy(pfd_devices[s].handle);
        pfd_devices[s].handle = NULL;
    }
    pfd_devices_created = 0;
}

/* Called from XPluginStart after mfda_devices_enable() (which resolves the device API) and
 * again from XPluginEnable as a no-op safety net. Never fatal: without devices the PFDs are
 * simply not shown. */
static void pfd_devices_enable(void)
{
    int s;
    XPLMCreateAvionics_t p;
    if (pfd_devices_created || !mfda_av.loaded) return;
    for (s = 0; s < 2; ++s) {
        PfdDevice *d = &pfd_devices[s];
        memset(&p, 0, sizeof(p));
        p.structSize = (int)sizeof(p);
        p.screenWidth = (int)PFD_PIXEL_WIDTH; p.screenHeight = (int)PFD_PIXEL_HEIGHT;
        p.bezelWidth = (int)PFD_PIXEL_WIDTH; p.bezelHeight = (int)PFD_PIXEL_HEIGHT;
        p.screenOffsetX = 0; p.screenOffsetY = 0;
        p.drawOnDemand = 0;
        p.bezelDrawCallback = pfd_device_bezel_draw;
        p.drawCallback = pfd_device_draw;
        p.brightnessCallback = pfd_device_brightness;
        p.deviceID = (char *)d->device_id;
        p.deviceName = (char *)d->device_name;
        p.refcon = d;
        d->handle = mfda_av.create(&p);
        if (!d->handle) {
            XPLMDebugString("A380X PFD: XPLMCreateAvionicsEx failed - PFDs unavailable\n");
            pfd_devices_destroy();
            return;
        }
        mfda_av.set_rheo(d->handle, 1.0f);
    }
    pfd_devices_created = 1;
    XPLMDebugString("A380X PFD: captain and first officer cockpit devices created\n");
}
