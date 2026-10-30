/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <cassert>
#include <iostream>
#include <memory>
#include "a380x_xpkit_wire.hpp"
#include "a380x_mfd_draw.h"
int main(){
    auto frame=std::make_unique<A380XXpKitMfd>();frame->powered=1;frame->sequence.store(2);
    strcpy(frame->uri,"fms/active/init");mfd_draw_reset(&frame->list);
    mfd_draw_clip_push(&frame->list,mfd_rect(0,0,768,1024));
    mfd_draw_fill_rect(&frame->list,mfd_rect(0,0,768,1024),MFD_COL_BLACK);
    auto payload=xpkit_mfd_json(*frame);assert(payload["powered"]==true);assert(payload["uri"]=="fms/active/init");
    assert(payload["commands"].size()==1);assert(payload["commands"][0].size()==14);
    assert(XpJson::parse(payload.dump())==payload);
    auto rejects=[&]{try{(void)xpkit_mfd_json(*frame);return false;}catch(const std::runtime_error&){return true;}};
    frame->list.cmd_count=MFD_DRAW_MAX_CMDS+1;assert(rejects());frame->list.cmd_count=1;
    frame->list.cmds[0].clip=MFD_DRAW_MAX_CLIPS;assert(rejects());frame->list.cmds[0].clip=0;
    frame->list.cmds[0].a[0]=NAN;assert(rejects());frame->list.cmds[0].a[0]=0;
    frame->list.cmds[0].kind=MFD_CMD_TEXT;frame->list.cmds[0].first=UINT32_MAX;assert(rejects());
    frame->list.cmds[0].first=0;frame->list.text_len=3;frame->list.cmds[0].count=3;
    memcpy(frame->list.text,"\"\\\n",3);payload=xpkit_mfd_json(*frame);
    assert(XpJson::parse(payload.dump())["commands"][0][10]=="\"\\\n");
    frame->list.cmds[0].kind=MFD_CMD_POLYLINE;frame->list.cmds[0].count=1;frame->list.point_count=1;
    frame->list.points[0]={1,INFINITY};assert(rejects());
    frame->list.points[0]={1,2};assert(xpkit_mfd_json(*frame)["commands"][0][10]==XpJson::array({1,2}));
    std::cout<<"Windows display wire: valid lists, JSON escaping, command/clip/text/point bounds and finite values passed\n";
}
