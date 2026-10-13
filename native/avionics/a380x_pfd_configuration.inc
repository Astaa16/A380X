/* SPDX-License-Identifier: GPL-3.0-or-later
 * Lower PFD configuration geometry adapted from FlyByWire A380X
 * fbw-a380x/src/systems/instruments/src/PFD/LowerArea.tsx.
 * Include after the PfdCanvas drawing helpers in a380x_pfd_render.inc.
 *
 * The adapter supplies separate native command targets and actual deployment.
 * These X-Plane ratios are not Airbus SFCC synchro words. Actual pointers below
 * show continuous normalized deployment; the published SFCC electrical-angle
 * conversion and its fault/alpha-lock/flap-relief logic cannot be inferred from
 * these inputs. The cyan destination follows the native physical target, even
 * when that target differs from a real A380 detent. No trim/CG is synthesized.
 */

static int pfd_configuration_ratio_valid(int available, double value)
{
    return available && isfinite(value) && value >= 0.0 && value <= 1.0;
}

static Point pfd_configuration_slat_point(float ratio)
{
    /* Clean/full endpoints of the upstream actual-position schematic. */
    return (Point){15.2f - 9.730075f * ratio, 195.3f + 2.036527f * ratio};
}

static Point pfd_configuration_flap_point(float ratio)
{
    return (Point){31.8f + 29.920622f * ratio, 193.1f + 12.552905f * ratio};
}

static void pfd_configuration_flap_marker(const PfdCanvas *p, Point position, Color color, int filled)
{
    Point marker[] = {{position.x,position.y}, {position.x,position.y+2.6f},
                      {position.x+3.9f,position.y+2.6f}};
    if (filled) pfd_polygon(p,marker,3,color);
    else pfd_polygon_outline(p,marker,3,color);
}

static void draw_pfd_configuration(const PfdCanvas *p)
{
    static const Point wing[] = {
        {15.2f,195.5f},{27.6f,195.5f},{31.7f,195.7f},{31.6f,193.1f},
        {27.6f,192.2f},{25.6f,191.9f},{22.6f,191.8f},{19.1f,191.9f},{15.3f,192.7f}
    };
    static const Point slat_marks[] = {{14.1f,194.5f},{9.6f,195.4f},{5.0f,196.4f}};
    static const Point flap_marks[] = {
        {32.3f,193.7f},{39.9f,196.8f},{47.3f,199.9f},{54.7f,203.0f},{62.1f,206.1f}
    };
    static const char *config_text[] = {"0","1","1+F","2","3","FULL"};
    int flap_ok = pfd_configuration_ratio_valid(pfd_guidance.flap_valid,pfd_guidance.flap_ratio);
    int slat_ok = pfd_configuration_ratio_valid(pfd_guidance.slat_valid,pfd_guidance.slat_ratio);
    int flap_target_ok = pfd_configuration_ratio_valid(1,pfd_guidance.flap_target_ratio);
    int slat_target_ok = pfd_configuration_ratio_valid(1,pfd_guidance.slat_target_ratio);
    int flap_moving = flap_ok && flap_target_ok &&
        fabs(pfd_guidance.flap_ratio-pfd_guidance.flap_target_ratio) > 0.015;
    int slat_moving = slat_ok && slat_target_ok &&
        fabs(pfd_guidance.slat_ratio-pfd_guidance.slat_target_ratio) > 0.015;
    int moving = flap_moving || slat_moving;
    int configuration_visible = pfd_guidance.flap_handle_index > 0 ||
        (flap_ok && pfd_guidance.flap_ratio > 0.01) ||
        (slat_ok && pfd_guidance.slat_ratio > 0.01) || !flap_ok || !slat_ok ||
        pfd_guidance.flap_fault || pfd_guidance.slat_fault;
    int i;

    pfd_polygon_outline(p,wing,ARRAY_LEN(wing),WHITE);
    if (configuration_visible) {
        int config = pfd_guidance.flap_config;
        int target_known = flap_target_ok && slat_target_ok && config >= 0 && config < ARRAY_LEN(config_text);
        Color target_color = !flap_ok || !slat_ok || !target_known ? AMBER : moving ? CYAN : GREEN;
        pfd_text_center(p,23.7f,202.3f,target_known ? config_text[config] : "XX",target_color);
        if (moving) pfd_box(p,15.4f,196.8f,31.6f,203.0f,CYAN);
        Color slat_color = pfd_guidance.slat_fault ? AMBER : GREEN;
        Color flap_color = pfd_guidance.flap_fault ? AMBER : GREEN;
        pfd_text(p,3.8f,191.1f,"S",slat_ok && !pfd_guidance.slat_fault ? WHITE : AMBER);
        pfd_text(p,47.2f,210.8f,"F",flap_ok && !pfd_guidance.flap_fault ? WHITE : AMBER);
        if (slat_ok) {
            Point actual = pfd_configuration_slat_point((float)pfd_guidance.slat_ratio);
            Point index[] = {{actual.x,actual.y},{actual.x-1.3f,actual.y-1.9f},
                             {actual.x+0.1f,actual.y-2.6f}};
            for (i=0;i<ARRAY_LEN(slat_marks);++i)
                pfd_circle(p,slat_marks[i].x,slat_marks[i].y,0.8f,WHITE);
            pfd_line(p,15.2f,195.4f,actual.x,actual.y,slat_color);
            pfd_polygon_outline(p,index,3,slat_color);
            if (slat_moving) {
                Point target = pfd_configuration_slat_point((float)pfd_guidance.slat_target_ratio);
                pfd_circle(p,target.x,target.y-1.0f,0.8f,CYAN);
            }
        } else pfd_text(p,9.0f,195.5f,"XX",AMBER);
        if (flap_ok) {
            Point actual = pfd_configuration_flap_point((float)pfd_guidance.flap_ratio);
            for (i=0;i<ARRAY_LEN(flap_marks);++i) {
                Point marker[] = {{flap_marks[i].x,flap_marks[i].y},
                                  {flap_marks[i].x,flap_marks[i].y+1.7f},
                                  {flap_marks[i].x+1.9f,flap_marks[i].y+1.7f}};
                pfd_polygon(p,marker,3,WHITE);
            }
            pfd_line(p,31.8f,193.1f,actual.x,actual.y,flap_color);
            pfd_configuration_flap_marker(p,actual,flap_color,0);
            if (flap_moving) {
                Point target = pfd_configuration_flap_point((float)pfd_guidance.flap_target_ratio);
                pfd_configuration_flap_marker(p,target,CYAN,1);
            }
        } else pfd_text(p,32.6f,195.5f,"XX",AMBER);
    }

    if (pfd_guidance.spoilers_armed ||
        (pfd_configuration_ratio_valid(pfd_guidance.speedbrake_valid,pfd_guidance.spoiler_ratio) &&
         pfd_guidance.spoiler_ratio > 0.01)) {
        pfd_line(p,25.3f,187.5f,27.4f,184.5f,WHITE);
        pfd_line(p,27.5f,189.0f,30.6f,187.2f,WHITE);
        if (pfd_configuration_ratio_valid(pfd_guidance.speedbrake_valid,pfd_guidance.spoiler_ratio) &&
            pfd_guidance.spoiler_ratio > 0.01) {
            /* Ratio-only input: use proportional schematic travel, not an
             * invented 5.15-degree Airbus visibility threshold or fault state. */
            float angle = (float)pfd_guidance.spoiler_ratio * (float)M_PI * 0.5f;
            pfd_line(p,23.0f,191.6f,23.0f+5.0f*cosf(angle),191.6f-5.0f*sinf(angle),GREEN);
        }
        if (pfd_guidance.spoilers_armed) {
            Point armed[] = {{21.6f,183.5f},{24.6f,183.5f},{23.1f,186.5f}};
            pfd_polygon(p,armed,3,CYAN);
        }
    }

    if (pfd_guidance.gear_down) {
        Point gear[] = {{18.4f,204.3f},{28.4f,204.3f},{23.4f,209.8f}};
        pfd_polygon_outline(p,gear,3,GREEN);
        pfd_line(p,20.9f,204.3f,20.9f,206.9f,GREEN);
        pfd_line(p,23.4f,204.3f,23.4f,209.8f,GREEN);
        pfd_line(p,25.9f,204.3f,25.9f,206.9f,GREEN);
    }
}
