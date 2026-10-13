/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "a380x_efis_nd.h"

int main(void)
{
    float x,y;
    assert(efis_nd_project(0,0,1,0,0,&x,&y));
    assert(fabsf(x)<.001f && fabsf(y-60.04046f)<.01f);
    assert(efis_nd_project(0,0,0,1,90,&x,&y));
    assert(fabsf(x)<.001f && fabsf(y-60.04046f)<.01f);
    assert(efis_nd_project(0,179.9,0,-179.9,0,&x,&y));
    assert(fabsf(x-12.00809f)<.01f && fabsf(y)<.001f);
    assert(efis_nd_project(89,0,89,90,0,&x,&y));
    assert(isfinite(x) && isfinite(y) && hypotf(x,y)<86);
    assert(!efis_nd_project(NAN,0,0,0,0,&x,&y));
    assert(!efis_nd_project(0,0,91,0,0,&x,&y));
    assert(efis_nd_in_map(0,0,96,1));
    assert(efis_nd_in_map(70,70,100,1));
    assert(!efis_nd_in_map(80,60,100,1)); /* 53deg outside100degARC */
    assert(!efis_nd_in_map(0,-20,100,1));
    assert(efis_nd_in_map(0,-20,100,0));
    assert(!efis_nd_in_map(0,101,100,0));
    assert(!efis_nd_in_map(NAN,0,100,0));
    puts("EFIS ND projection/100-degree clipping tests passed");
    return 0;
}
