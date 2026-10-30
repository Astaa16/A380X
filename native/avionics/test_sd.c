/* SPDX-License-Identifier: GPL-3.0-or-later
 * Reuse the tested software rasterizer, excluding its unrelated MFD fixtures.
 * These are isolated display fixtures, never live simulator input.
 */
#define A380X_MFD_RASTER_ONLY
#include "test_mfd_render.c"
#include "a380x_sd.h"

static int checks;
#define SD_CHECK(c) do { ++checks; assert(c); } while(0)
static MfdDrawList sd_list;

static int occurrences(const char *s,MfdColor color)
{
    int n=0;
    for(uint32_t i=0;i<sd_list.cmd_count;++i) {
        MfdCmd *c=&sd_list.cmds[i];
        if(c->kind==MFD_CMD_TEXT && c->color==color && c->count==strlen(s) &&
           !memcmp(sd_list.text+c->first,s,c->count)) ++n;
    }
    return n;
}

static void sd_fixture(const SdData *data,int page,const char *dir,const char *name)
{
    a380x_sd_draw(data,page,&sd_list);
    SD_CHECK(!sd_list.overflowed);
    for(uint32_t i=0;i<sd_list.cmd_count;++i)
        for(int k=0;k<4;++k) SD_CHECK(isfinite(sd_list.cmds[i].a[k]));
    for(uint32_t i=0;i<sd_list.point_count;++i)
        SD_CHECK(isfinite(sd_list.points[i].x)&&isfinite(sd_list.points[i].y));
    if(dir) { render(&sd_list); SD_CHECK(write_ppm(dir,name)); }
}

int main(int argc,char **argv)
{
    const char *dir=argc>1?argv[1]:NULL;
    SdData data={0};
    SD_CHECK(mfd_font_decode(g_atlas));
    SD_CHECK(a380x_sd_page_supported(SD_ENGINE));
    SD_CHECK(a380x_sd_page_supported(SD_APU));
    SD_CHECK(!a380x_sd_page_supported(14));
    SD_CHECK(!a380x_sd_value(NAN,1,0,100).valid);
    SD_CHECK(!a380x_sd_value(INFINITY,1,0,100).valid);
    SD_CHECK(!a380x_sd_value(-1,1,0,100).valid);
    SD_CHECK(!a380x_sd_value(101,1,0,100).valid);
    SD_CHECK(!a380x_sd_value(0,0,0,100).valid);
    SD_CHECK(a380x_sd_value(0,1,0,100).valid);
    sd_fixture(&data,SD_ENGINE,dir,"sd_off");
    SD_CHECK(sd_list.cmd_count==1);
    data.powered=1;
    sd_fixture(&data,SD_ENGINE,dir,"sd_engine_cold");
    SD_CHECK(occurrences("ENGINE",MFD_COL_WHITE)==1);
    SD_CHECK(occurrences("XX",MFD_COL_AMBER)>=32);
    for(int i=0;i<4;++i) {
        SdEngine *e=&data.engine[i]; e->active=1;
        e->n2=a380x_sd_value(60.1f+i,1,0,200);
        e->fuel_flow_kg_h=a380x_sd_value(3600.0f*(i+1),1,0,100000);
        e->oil_temperature_c=a380x_sd_value(80+i,1,-100,500);
        e->oil_pressure_psi=a380x_sd_value(90+i,1,0,1000);
    }
    sd_fixture(&data,SD_ENGINE,dir,"sd_engine_running");
    SD_CHECK(occurrences("3600",MFD_COL_GREEN)==1);
    SD_CHECK(occurrences("14400",MFD_COL_GREEN)==1);
    SD_CHECK(occurrences("XX",MFD_COL_AMBER)>=20); /* No fabricated N3, QT, VIB or NAC. */
    data.engine[0].starting=1;
    data.engine[1].oil_pressure_psi=a380x_sd_value(10,1,0,1000);
    data.engine[2].oil_temperature_c=a380x_sd_value(190,1,-100,500);
    data.engine[3].n2=(SdValue){NAN,1};
    sd_fixture(&data,SD_ENGINE,dir,"sd_engine_start_invalid");
    SD_CHECK(occurrences("START",MFD_COL_WHITE)==1);
    SD_CHECK(occurrences("10",MFD_COL_RED)==1);
    SD_CHECK(occurrences("190",MFD_COL_AMBER)==1);
    sd_fixture(&data,SD_APU,dir,"sd_apu_unavailable");
    SD_CHECK(occurrences("AVAIL",MFD_COL_GREEN)==0);
    SD_CHECK(occurrences("XX",MFD_COL_AMBER)>=10);
    data.apu_n1=a380x_sd_value(99,1,0,150);
    data.apu_egt_c=a380x_sd_value(464,1,-100,1400);
    data.apu_available=data.apu_bleed_valid=data.apu_bleed_open=1;
    sd_fixture(&data,SD_APU,dir,"sd_apu_available");
    SD_CHECK(occurrences("AVAIL",MFD_COL_GREEN)==1);
    SD_CHECK(occurrences("464",MFD_COL_GREEN)==1);
    SD_CHECK(occurrences("XX",MFD_COL_AMBER)>=9); /* Two independent GEN models not fabricated. */
    sd_fixture(&data,14,NULL,"unsupported");
    SD_CHECK(occurrences("PAGE NOT AVAILABLE",MFD_COL_AMBER)==1);
    printf("test_sd: %d checks OK (isolated native ECAM fixtures)\n",checks);
    return 0;
}
