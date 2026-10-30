/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <json.hpp>
#include "a380x_xpkit_shared.h"
using XpJson=nlohmann::json;
static_assert(sizeof(std::atomic<uint32_t>)==4 && sizeof(std::atomic<uint64_t>)==8,"IPC atomic size");
static_assert(std::atomic<uint32_t>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free,"IPC needs lock-free atomics");
inline XpJson xpkit_mfd_json(const A380XXpKitMfd &frame) {
    const auto &dl=frame.list;
    if(dl.overflowed||dl.cmd_count>MFD_DRAW_MAX_CMDS||dl.point_count>MFD_DRAW_MAX_POINTS||
       dl.text_len>MFD_DRAW_MAX_TEXT||!dl.clip_count||dl.clip_count>MFD_DRAW_MAX_CLIPS)
        throw std::runtime_error("Invalid native MFD list bounds");
    XpJson commands=XpJson::array();
    for(uint32_t i=0;i<dl.cmd_count;++i) {
        const auto &c=dl.cmds[i];
        if(c.clip>=dl.clip_count||c.color>=MFD_COL_COUNT||c.kind>MFD_CMD_TEXT)
            throw std::runtime_error("Invalid native MFD command");
        const auto &clip=dl.clips[c.clip];
        for(float value:c.a)if(!std::isfinite(value))throw std::runtime_error("Non-finite MFD position");
        for(float value:{c.width,c.size,clip.x,clip.y,clip.w,clip.h})
            if(!std::isfinite(value))throw std::runtime_error("Non-finite MFD clip/size");
        XpJson payload=nullptr;
        if(c.kind==MFD_CMD_TEXT) {
            if(c.first>dl.text_len||c.count>dl.text_len-c.first)throw std::runtime_error("MFD text bounds");
            payload=std::string(dl.text+c.first,c.count);
        } else if(c.kind>=MFD_CMD_POLYLINE) {
            if(c.first>dl.point_count||c.count>dl.point_count-c.first)throw std::runtime_error("MFD point bounds");
            payload=XpJson::array();
            for(uint32_t p=c.first;p<c.first+c.count;++p) {
                if(!std::isfinite(dl.points[p].x)||!std::isfinite(dl.points[p].y))throw std::runtime_error("MFD point value");
                payload.push_back(dl.points[p].x);payload.push_back(dl.points[p].y);
            }
        }
        commands.push_back({c.kind,mfd_color_rgb((MfdColor)c.color),c.font,c.anchor,c.width,c.size,
            c.a[0],c.a[1],c.a[2],c.a[3],payload,{clip.x,clip.y,clip.w,clip.h},c.layer,c.closed});
    }
    return {{"sequence",frame.sequence.load()},{"powered",frame.powered!=0},
        {"uri",std::string(frame.uri,strnlen(frame.uri,sizeof(frame.uri)))},{"commands",commands}};
}
