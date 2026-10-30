/* SPDX-License-Identifier: GPL-3.0-or-later
 * One native FMS, two native page controllers, six WebKit texture views.
 * This transports the existing faithful C page ports, not an independent JS FMC.
 * Input stays in the tested native cockpit-device/KCCU/keyboard adapter.
 */
static uint64_t xpkit_mfd_hash[2];
/* Cursor-only motion must not trigger JSON serialization, WebKit rasterization,
 * a 3 MB snapshot and a GPU texture upload. Keep the native cursor layer local.
 * Compact payloads too: cursor polylines otherwise still change the page hash. */
static MfdDrawList xpkit_mfd_page[2];
static void xpkit_mfd_page_list(const MfdDrawList *source,MfdDrawList *page) {
    mfd_draw_reset(page);
    page->clip_count=source->clip_count;
    memcpy(page->clips,source->clips,source->clip_count*sizeof(source->clips[0]));
    for(uint32_t i=0;i<source->cmd_count;++i) {
        MfdCmd cmd=source->cmds[i];
        if(cmd.layer==MFD_LAYER_CURSOR)continue;
        if(cmd.kind==MFD_CMD_TEXT) {
            memcpy(page->text+page->text_len,source->text+cmd.first,cmd.count);
            cmd.first=page->text_len;page->text_len+=cmd.count;
        } else if(cmd.kind>=MFD_CMD_POLYLINE) {
            memcpy(page->points+page->point_count,source->points+cmd.first,cmd.count*sizeof(MfdPoint));
            cmd.first=page->point_count;page->point_count+=cmd.count;
        }
        page->cmds[page->cmd_count++]=cmd;
    }
}
static void xpkit_mfd_publish(void) {
    for (int side=0;side<2;++side) {
        const MfdDrawList *dl=a380x_mfd_display_list(&mfda_mfd,(MfdSide)side);
        if (!dl || dl->overflowed) continue;
        MfdDrawList *page=&xpkit_mfd_page[side];
        xpkit_mfd_page_list(dl,page);
        uint64_t hash=mfda_list_hash(page,mfda_dev[side].du_on);
        A380XXpKitMfd *out=&xpkit_shared->mfd[side];
        if (atomic_load(&out->sequence) && hash==xpkit_mfd_hash[side] &&
            !strcmp(out->uri,mfda_mfd.sides[side].uri.uri)) continue;
        uint32_t seq=atomic_load_explicit(&out->sequence,memory_order_relaxed);
        atomic_store_explicit(&out->sequence,seq+1,memory_order_release);
        out->powered=mfda_dev[side].du_on;
        snprintf(out->uri,sizeof(out->uri),"%s",mfda_mfd.sides[side].uri.uri);
        memcpy(&out->list,page,sizeof(*page));
        atomic_store_explicit(&out->sequence,seq+2,memory_order_release);
        xpkit_mfd_hash[side]=hash;
    }
}
