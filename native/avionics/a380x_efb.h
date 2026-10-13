/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef A380X_EFB_H
#define A380X_EFB_H

int a380x_efb_init(void);
void a380x_efb_shutdown(void);
void a380x_efb_update(float dt_seconds);
void a380x_efb_draw(void);
void a380x_efb_draw_window(int left, int bottom, int right, int top);
void a380x_efb_click(float atlas_x, float atlas_y);

#endif
