/* Native Sony A6400 MENU Full-Touch Integration (MTF2).
 *
 * Adapted under the MIT License from ivica00/bionz-x-research
 * `mods/menu_touch_full/menu_touch_full.c` (MenuTouchFull v3, commit ffa974b,
 * hardware-verified on ILCE-6400 v2.00 in E0520/E0522).
 *
 * Unlike the standalone dual-hook SMOD (which shares libObj.so RX padding and
 * release hook 0x06309d48 with A6400-Extra-Menu), this module executes directly
 * on the natural Appframework UI thread inside our existing animate() callback,
 * completely eliminating hook/padding conflicts and making native MENU touch
 * cold-boot persistent and user-toggleable.
 *
 * Safety contract:
 *  - Verifies libObj.so, viewUnified2.so, and CautionConfig.so load bases via
 *    dladdr() (without dereferencing unmapped memory) AND validates 10 exact
 *    8-byte machine-code prologues before enabling.
 *  - On non-matching cameras/firmware (e.g. ILCE-6100), self-disables cleanly
 *    with zero memory dereferences to firmware internals.
 *  - Suppressed whenever our Film Menu overlay or Album Preview is open, or
 *    when the user toggles native MENU touch OFF.
 */
#ifndef A6400_MTF2_NATIVE_TOUCH_H
#define A6400_MTF2_NATIVE_TOUCH_H

#include <stdint.h>
#include <string.h>

#define MTF2_MAX_TABS   6u
#define MTF2_MAX_PAGES  15u
#define MTF2_MAX_ROWS   6u

#define MTF2_ACT_NONE         0u
#define MTF2_ACT_TAB          1u
#define MTF2_ACT_PAGE         2u
#define MTF2_ACT_ROW_SELECT   3u
#define MTF2_ACT_ROW_ENTER    4u
#define MTF2_ACT_SWIPE_PAGE   5u
#define MTF2_ACT_POPUP_SELECT 6u
#define MTF2_ACT_POPUP_SCROLL 7u
#define MTF2_ACT_POPUP_CANCEL 8u

struct mtf2_point {
    float x;
    float y;
};

struct mtf2_profile {
    uint32_t ev_ctor, op_new, evmgr_push, inh_mode, evmgr_ptr, vm_ptr;
    uint32_t apply_pop, main_st, popup_up, popup_down, binding, cancel_pop;
    uint32_t enter_sel, refresh, pop_open, confirm_pop, set_pop_row;
    uint32_t view_vtbl, menu_vtbl;
    uint32_t idx_child, sel_idx, set_sel_idx, node_vtbl;
};

struct mtf2_ctx {
    int supported;
    int was_pressed;
    int press_valid;
    int16_t press_x;
    int16_t press_y;
    int16_t last_x;
    int16_t last_y;
    uint32_t last_action;
    uint32_t hits;
#if defined(__arm__) && !defined(A6400_MTF2_HOST_MOCK)
    uint32_t (*lookup)(uint32_t, uint32_t, uint32_t, uint32_t *);
    int (*is_hit)(uint32_t, const struct mtf2_point *);
    uint32_t obj_base;
    uint32_t vu2_base;
    uint32_t cc_base;
    const struct mtf2_profile *prof;
#endif
};

static inline int mtf2_iabs(int v) { return v < 0 ? -v : v; }

#if defined(__arm__) && !defined(A6400_MTF2_HOST_MOCK)

static inline uint32_t mtf2_rd32(uint32_t addr, unsigned off) {
    return *(const volatile uint32_t *)(uintptr_t)(addr + off);
}

static inline unsigned mtf2_rd8(uint32_t addr, unsigned off) {
    return *(const volatile uint8_t *)(uintptr_t)(addr + off);
}

static int mtf2_match8(uint32_t addr, const unsigned char expected[8]) {
    return memcmp((const void *)(uintptr_t)addr, expected, 8u) == 0;
}

static uint32_t mtf2_find_module_base(const char *suffix) {
    int fd = (int)syscall(SYS_open, "/proc/self/maps", 0, 0);
    if (fd < 0) return 0u;
    char buf[1024], line[256];
    unsigned pos = 0u;
    uint32_t found = 0u;
    ssize_t n;
    while (!found && (n = (ssize_t)syscall(SYS_read, fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            char c = buf[i];
            if (c == '\n') {
                line[pos] = 0;
                if (pos >= 32u && strstr(line, "r-xp 00000000") && strstr(line, suffix)) {
                    uint32_t v = 0u;
                    unsigned k = 0u;
                    for (; k < pos && line[k] != '-'; ++k) {
                        char h = line[k];
                        unsigned d = (h >= '0' && h <= '9') ? (unsigned)(h - '0') :
                                     (h >= 'a' && h <= 'f') ? (unsigned)(h - 'a' + 10) : 16u;
                        if (d >= 16u) { k = 0u; break; }
                        v = (v << 4) | d;
                    }
                    if (k >= 6u) { found = v; break; }
                }
                pos = 0u;
            } else if (pos + 1u < sizeof(line)) {
                line[pos++] = c;
            }
        }
    }
    (void)syscall(SYS_close, fd);
    return found;
}

static int mtf2_probe_and_init(struct mtf2_ctx *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    void *lookup_sym = dlsym(RTLD_DEFAULT,
        "_ZN2ux6wgtlay14WidgetLayouter9getWidgetEjjjRNS0_9ErrorCodeE");
    void *ishit_sym = dlsym(RTLD_DEFAULT,
        "_ZN2ux6wgtsys6Widget5isHitERKNS_4core7Vector2E");
    void *vsm_sym = dlsym(RTLD_DEFAULT, "ViewSettingMenuToInstance");
    if (!lookup_sym || !ishit_sym || !vsm_sym) return 0;

    Dl_info i_obj, i_vu2, i_cc;
    memset(&i_obj, 0, sizeof(i_obj));
    memset(&i_vu2, 0, sizeof(i_vu2));
    if (!dladdr(lookup_sym, &i_obj) || !i_obj.dli_fbase) return 0;
    if (!dladdr(vsm_sym, &i_vu2) || !i_vu2.dli_fbase) return 0;
    uint32_t obj_b = (uint32_t)(uintptr_t)i_obj.dli_fbase;
    uint32_t vu2_b = (uint32_t)(uintptr_t)i_vu2.dli_fbase;
    uint32_t cc_b = mtf2_find_module_base("CautionConfig.so");
    if (!cc_b) return 0;

    /* Module-relative profile table (ILCE-6400 / ILCE-6100). Each entry is
     * guarded by 10 exact 8-byte machine-code fingerprints + dladdr base check. */
    static const struct mtf2_profile profiles[1] = {
        { /* ILCE-6400 v2.00 */
            0x000fce20u, 0x000fd974u, 0x00100d5cu, 0x00606814u, 0x01371984u, 0x014257ecu,
            0x00212ca4u, 0x005aa534u, 0x005ab34cu, 0x005ab376u, 0x005ab430u, 0x005ab84cu,
            0x005ac044u, 0x005ac110u, 0x005afb60u, 0x005b038au, 0x005b0f10u,
            0x008e2270u, 0x009391d0u,
            0x007c6bc6u, 0x007c6c16u, 0x007c70dau, 0x00ab93e8u
        }
    };
    static const unsigned char fp_ev_ctor[8]     = {0x12,0xc6,0x8f,0xe2,0x6d,0xca,0x8c,0xe2};
    static const unsigned char fp_op_new[8]      = {0x12,0xc6,0x8f,0xe2,0x6d,0xca,0x8c,0xe2};
    static const unsigned char fp_evmgr_push[8]  = {0x78,0x47,0xc0,0x46,0x12,0xc6,0x8f,0xe2};
    static const unsigned char fp_inh_mode[8]    = {0x98,0xb5,0x00,0xaf,0x90,0xf8,0x21,0x40};
    static const unsigned char fp_apply_pop[8]   = {0x2d,0xe9,0xf0,0x43,0x93,0xb0,0x4f,0xf0};
    static const unsigned char fp_main_st[8]     = {0xd0,0xf8,0xe0,0x01,0x80,0xb5,0xa0,0xf1};
    static const unsigned char fp_binding[8]     = {0xb0,0xb5,0x00,0xf5,0xc2,0x35,0x8a,0xb0};
    static const unsigned char fp_refresh[8]     = {0x00,0xf5,0xc2,0x33,0x03,0xf5,0x2a,0x73};
    static const unsigned char fp_pop_open[8]    = {0x00,0xf5,0xc8,0x40,0x80,0xb5,0x00,0xaf};
    static const unsigned char fp_set_pop_row[8] = {0xb0,0xb5,0x00,0xaf,0x04,0x46,0x0d,0x46};

    for (unsigned pi = 0u; pi < sizeof(profiles) / sizeof(profiles[0]); ++pi) {
        const struct mtf2_profile *p = &profiles[pi];
        memset(&i_cc, 0, sizeof(i_cc));
        if (!dladdr((void *)(uintptr_t)(cc_b + p->sel_idx), &i_cc) ||
            i_cc.dli_fbase != (void *)(uintptr_t)cc_b) {
            continue;
        }
        if (mtf2_match8(obj_b + p->ev_ctor, fp_ev_ctor) &&
            mtf2_match8(obj_b + p->op_new, fp_op_new) &&
            mtf2_match8(obj_b + p->evmgr_push, fp_evmgr_push) &&
            mtf2_match8(obj_b + p->inh_mode, fp_inh_mode) &&
            mtf2_match8(vu2_b + p->apply_pop, fp_apply_pop) &&
            mtf2_match8(vu2_b + p->main_st, fp_main_st) &&
            mtf2_match8(vu2_b + p->binding, fp_binding) &&
            mtf2_match8(vu2_b + p->refresh, fp_refresh) &&
            mtf2_match8(vu2_b + p->pop_open, fp_pop_open) &&
            mtf2_match8(vu2_b + p->set_pop_row, fp_set_pop_row)) {
            ctx->lookup = (uint32_t (*)(uint32_t, uint32_t, uint32_t, uint32_t *))lookup_sym;
            ctx->is_hit = (int (*)(uint32_t, const struct mtf2_point *))ishit_sym;
            ctx->obj_base = obj_b;
            ctx->vu2_base = vu2_b;
            ctx->cc_base = cc_b;
            ctx->prof = p;
            ctx->supported = 1;
            return 1;
        }
    }
    return 0;
}

static void mtf2_sync_refresh(const struct mtf2_ctx *ctx, uint32_t menu) {
    void (*binding)(uint32_t) =
        (void (*)(uint32_t))(uintptr_t)(ctx->vu2_base + ctx->prof->binding + 1u);
    void (*refresh)(uint32_t, uint32_t) =
        (void (*)(uint32_t, uint32_t))(uintptr_t)(ctx->vu2_base + ctx->prof->refresh + 1u);
    binding(menu);
    refresh(menu, 0u);
}

static uint32_t mtf2_find_setting_menu_view(const struct mtf2_ctx *ctx, uint32_t menu) {
    uint32_t vid = mtf2_rd32(menu, 0x14cu);
    uint32_t vm = mtf2_rd32(ctx->obj_base + ctx->prof->vm_ptr, 0u);
    if (!vm) return 0u;
    int cnt = (int)mtf2_rd32(vm, 0u);
    uint32_t entries = mtf2_rd32(vm, 0x20u);
    if (!entries || cnt <= 0 || cnt > 128) return 0u;
    uint32_t view_vtbl = ctx->vu2_base + ctx->prof->view_vtbl;
    for (int i = 0; i < cnt; i++) {
        uint32_t ent = entries + (uint32_t)i * 16u;
        uint32_t vwrap = mtf2_rd32(ent, 4u);
        if (!vwrap || mtf2_rd32(ent, 8u) != vid) continue;
        if (mtf2_rd32(vwrap, 0u) == view_vtbl) return vwrap;
        uint32_t inner = mtf2_rd32(vwrap, 0x18u);
        if (inner && mtf2_rd32(inner, 0u) == view_vtbl) return inner;
    }
    return 0u;
}

static void mtf2_push_async_event(const struct mtf2_ctx *ctx, uint32_t event_id, uint8_t ev_type) {
    uint32_t mgr_ptr = mtf2_rd32(ctx->obj_base + ctx->prof->evmgr_ptr, 0u);
    if (!mgr_ptr) return;
    uint32_t mgr = mtf2_rd32(mgr_ptr, 0u);
    if (!mgr || !event_id) return;
    void *(*op_new)(uint32_t) =
        (void *(*)(uint32_t))(uintptr_t)(ctx->obj_base + ctx->prof->op_new);
    void (*ev_ctor)(void *, uint32_t, uint8_t, uint8_t) =
        (void (*)(void *, uint32_t, uint8_t, uint8_t))(uintptr_t)(ctx->obj_base + ctx->prof->ev_ctor);
    void (*evmgr_push)(uint32_t, void *, uint32_t) =
        (void (*)(uint32_t, void *, uint32_t))(uintptr_t)(ctx->obj_base + ctx->prof->evmgr_push + 1u);
    void *ev = op_new(16u);
    if (!ev) return;
    ev_ctor(ev, event_id, ev_type, 1u);
    evmgr_push(mgr, ev, 1u);
}

static uint32_t mtf2_handle_popup(struct mtf2_ctx *ctx, uint32_t menu,
                                  int16_t x, int16_t y, int dx, int dy, int has_press) {
    uint32_t popup = menu + 0xfb20u;
    uint32_t pnode = mtf2_rd32(popup, 0x6490u);
    uint32_t node_vtbl = ctx->cc_base + ctx->prof->node_vtbl;
    if (!pnode || mtf2_rd32(pnode, 0u) != node_vtbl) return MTF2_ACT_NONE;

    void (*cancel_popup)(uint32_t) =
        (void (*)(uint32_t))(uintptr_t)(ctx->vu2_base + ctx->prof->cancel_pop + 1u);
    void (*popup_up)(uint32_t) =
        (void (*)(uint32_t))(uintptr_t)(ctx->vu2_base + ctx->prof->popup_up + 1u);
    void (*popup_down)(uint32_t) =
        (void (*)(uint32_t))(uintptr_t)(ctx->vu2_base + ctx->prof->popup_down + 1u);
    void (*set_popup_row)(uint32_t, uint32_t) =
        (void (*)(uint32_t, uint32_t))(uintptr_t)(ctx->vu2_base + ctx->prof->set_pop_row + 1u);
    void (*confirm_popup)(uint32_t) =
        (void (*)(uint32_t))(uintptr_t)(ctx->vu2_base + ctx->prof->confirm_pop + 1u);
    void (*apply_popup)(uint32_t) =
        (void (*)(uint32_t))(uintptr_t)(ctx->vu2_base + ctx->prof->apply_pop + 1u);
    uint32_t view = mtf2_find_setting_menu_view(ctx, menu);

    unsigned total = mtf2_rd32(pnode, 8u);
    unsigned vis_cnt = mtf2_rd8(popup, 0x6480u);
    if (total < 1u || total > 128u || vis_cnt < 1u || vis_cnt > MTF2_MAX_ROWS) {
        return MTF2_ACT_NONE;
    }

    if (has_press && mtf2_iabs(dy) >= 36 && mtf2_iabs(dy) > mtf2_iabs(dx)) {
        if (dy < 0) popup_down(menu);
        else popup_up(menu);
        return MTF2_ACT_POPUP_SCROLL;
    }

    unsigned top = mtf2_rd8(popup, 0x648cu);
    struct mtf2_point pt = {(float)x, (float)y};

    for (unsigned r = 0; r < vis_cnt; r++) {
        unsigned opt = top + r + 1u;
        if (opt > total) break;
        if (ctx->is_hit(popup + 0x1070u + r * 0x398u, &pt) ||
            ctx->is_hit(popup + 0x36a8u + r * 0x220u, &pt)) {
            set_popup_row(popup, r);
            *(volatile uint32_t *)(uintptr_t)(menu + 0x1fcu) = top + r;
            if (view) apply_popup(view);
            else confirm_popup(popup);
            return MTF2_ACT_POPUP_SELECT;
        }
    }

    if ((!ctx->is_hit(popup + 0x64cu, &pt) || x < 80 || x > 560 || y < 32 || y > 456) &&
        !ctx->is_hit(popup + 0x60d8u, &pt) && !ctx->is_hit(popup + 0x5c04u, &pt) &&
        !ctx->is_hit(popup + 0x2780u, &pt)) {
        if (view) mtf2_push_async_event(ctx, 0x15010102u, 4u);
        else cancel_popup(menu);
        return MTF2_ACT_POPUP_CANCEL;
    }
    return MTF2_ACT_NONE;
}

static uint32_t mtf2_dispatch_release(struct mtf2_ctx *ctx,
                                      int16_t x, int16_t y, int dx, int dy, int has_press) {
    if (!ctx->supported || !ctx->prof || x == 32767 || y == 32767) return MTF2_ACT_NONE;

    uint32_t err = ~0u;
    uint32_t menu = ctx->lookup(0xaadca2c7u, 0xdfdc8dddu, 0xa056f604u, &err);
    uint32_t menu_vtbl = ctx->vu2_base + ctx->prof->menu_vtbl;
    uint32_t node_vtbl = ctx->cc_base + ctx->prof->node_vtbl;
    if (err || !menu || mtf2_rd32(menu, 0u) != menu_vtbl || mtf2_rd32(menu, 0x1cu) != menu) {
        return MTF2_ACT_NONE;
    }

    int (*main_state)(uint32_t) =
        (int (*)(uint32_t))(uintptr_t)(ctx->vu2_base + ctx->prof->main_st + 1u);
    int (*inherited_mode)(uint32_t) =
        (int (*)(uint32_t))(uintptr_t)(ctx->obj_base + ctx->prof->inh_mode + 1u);
    int (*popup_open)(uint32_t) =
        (int (*)(uint32_t))(uintptr_t)(ctx->vu2_base + ctx->prof->pop_open + 1u);
    if (mtf2_rd8(menu, 0x21u) != 2u || inherited_mode(menu) > 2) {
        return MTF2_ACT_NONE;
    }

    if (popup_open(menu + 0xfb20u)) {
        return mtf2_handle_popup(ctx, menu, x, y, dx, dy, has_press);
    }

    if (!main_state(menu)) return MTF2_ACT_NONE;

    uint32_t root = mtf2_rd32(menu, 0x186acu);
    if (!root || mtf2_rd32(root, 0u) != node_vtbl) return MTF2_ACT_NONE;

    int (*selected_index)(uint32_t, unsigned *) =
        (int (*)(uint32_t, unsigned *))(uintptr_t)(ctx->cc_base + ctx->prof->sel_idx + 1u);
    int (*indexed_child)(uint32_t, unsigned, uint32_t *) =
        (int (*)(uint32_t, unsigned, uint32_t *))(uintptr_t)(ctx->cc_base + ctx->prof->idx_child + 1u);
    int (*set_selected_index)(uint32_t, unsigned) =
        (int (*)(uint32_t, unsigned))(uintptr_t)(ctx->cc_base + ctx->prof->set_sel_idx + 1u);
    int (*enter_selected)(uint32_t, uint32_t) =
        (int (*)(uint32_t, uint32_t))(uintptr_t)(ctx->vu2_base + ctx->prof->enter_sel + 1u);

    unsigned tabs = mtf2_rd32(root, 8u), tab_sel = 0;
    if (tabs < 1u || tabs > MTF2_MAX_TABS ||
        selected_index(root, &tab_sel) != 1 || tab_sel < 1u || tab_sel > tabs) {
        return MTF2_ACT_NONE;
    }

    struct mtf2_point pt = {(float)x, (float)y};
    int is_swipe = has_press && mtf2_iabs(dx) >= 48 && mtf2_iabs(dx) > 2 * mtf2_iabs(dy);

    if (!is_swipe) {
        for (unsigned i = 0; i < tabs; i++) {
            if (ctx->is_hit(menu + 0x0b9cu + i * 0x220u, &pt)) {
                if (i + 1u != tab_sel && set_selected_index(root, i + 1u) == 1) {
                    mtf2_sync_refresh(ctx, menu);
                }
                return MTF2_ACT_TAB;
            }
        }
    }

    if (mtf2_rd8(menu, 0x1d8u)) return MTF2_ACT_NONE;

    uint32_t tab_node = 0;
    if (indexed_child(root, tab_sel, &tab_node) != 1 || !tab_node ||
        mtf2_rd32(tab_node, 0u) != node_vtbl) {
        return MTF2_ACT_NONE;
    }

    unsigned pages = mtf2_rd32(tab_node, 8u), page_sel = 0;
    uint32_t page_node = 0;
    if (pages < 1u || pages > MTF2_MAX_PAGES ||
        selected_index(tab_node, &page_sel) != 1 || page_sel < 1u || page_sel > pages ||
        indexed_child(tab_node, page_sel, &page_node) != 1 || !page_node ||
        mtf2_rd32(page_node, 0u) != node_vtbl) {
        return MTF2_ACT_NONE;
    }

    unsigned rows = mtf2_rd32(page_node, 8u), row_sel = 0;
    if (rows < 1u || rows > MTF2_MAX_ROWS ||
        selected_index(page_node, &row_sel) != 1 || row_sel < 1u || row_sel > rows ||
        mtf2_rd32(menu, 0x1f8u) != row_sel - 1u) {
        return MTF2_ACT_NONE;
    }

    if (is_swipe) {
        if (dx < 0) {
            if (page_sel < pages) set_selected_index(tab_node, page_sel + 1u);
            else set_selected_index(root, tab_sel < tabs ? tab_sel + 1u : 1u);
        } else {
            if (page_sel > 1u) set_selected_index(tab_node, page_sel - 1u);
            else set_selected_index(root, tab_sel > 1u ? tab_sel - 1u : tabs);
        }
        mtf2_sync_refresh(ctx, menu);
        return MTF2_ACT_SWIPE_PAGE;
    }

    for (unsigned p = 0; p < pages; p++) {
        if (ctx->is_hit(menu + 0x166b4u + p * 0x220u, &pt)) {
            if (p + 1u != page_sel && set_selected_index(tab_node, p + 1u) == 1) {
                mtf2_sync_refresh(ctx, menu);
            }
            return MTF2_ACT_PAGE;
        }
    }

    for (unsigned r = 0; r < rows; r++) {
        if (ctx->is_hit(menu + 0x9350u + r * 0x1118u, &pt)) {
            if (r + 1u != row_sel) {
                if (set_selected_index(page_node, r + 1u) == 1) {
                    mtf2_sync_refresh(ctx, menu);
                }
                return MTF2_ACT_ROW_SELECT;
            }
            uint32_t view = mtf2_find_setting_menu_view(ctx, menu);
            if (view) {
                mtf2_push_async_event(ctx, 0x15010052u, 4u);
            } else {
                uint32_t leaf = 0;
                if (indexed_child(page_node, r + 1u, &leaf) == 1 && leaf) {
                    enter_selected(menu, leaf);
                }
            }
            return MTF2_ACT_ROW_ENTER;
        }
    }
    return MTF2_ACT_NONE;
}

#else /* Host unit-test stub */

static int mtf2_probe_and_init(struct mtf2_ctx *ctx) {
    memset(ctx, 0, sizeof(*ctx));
#ifdef A6400_MTF2_HOST_MOCK
    ctx->supported = 1;
    return 1;
#else
    return 0;
#endif
}

#ifdef A6400_MTF2_HOST_MOCK
static uint32_t mtf2_host_mock_dispatch(int16_t x, int16_t y, int dx, int dy, int has_press);
static uint32_t mtf2_dispatch_release(struct mtf2_ctx *ctx,
                                      int16_t x, int16_t y, int dx, int dy, int has_press) {
    if (!ctx->supported || x == 32767 || y == 32767) return MTF2_ACT_NONE;
    return mtf2_host_mock_dispatch(x, y, dx, dy, has_press);
}
#else
static uint32_t mtf2_dispatch_release(struct mtf2_ctx *ctx,
                                      int16_t x, int16_t y, int dx, int dy, int has_press) {
    (void)ctx; (void)x; (void)y; (void)dx; (void)dy; (void)has_press;
    return MTF2_ACT_NONE;
}
#endif

#endif /* __arm__ */

/* Poll-driven press/drag/release tracker called once per animate() tick.
 * Sony clears mouse_position to (32767, 32767) upon release (state==6), so we
 * latch the final valid coordinates during state==5 and dispatch on 5 -> 6. */
static inline uint32_t mtf2_on_touch_poll(struct mtf2_ctx *ctx, int allow,
                                          int touch_state, short x, short y) {
    if (!ctx->supported || !allow) {
        ctx->was_pressed = 0;
        ctx->press_valid = 0;
        return MTF2_ACT_NONE;
    }
    if (touch_state == 5) {
        int valid_xy = (x >= 0 && x < 640 && y >= 0 && y < 480);
        if (!ctx->was_pressed) {
            ctx->was_pressed = 1;
            if (valid_xy) {
                ctx->press_x = x;
                ctx->press_y = y;
                ctx->last_x = x;
                ctx->last_y = y;
                ctx->press_valid = 1;
            } else {
                ctx->press_valid = 0;
            }
        } else if (valid_xy) {
            if (!ctx->press_valid) {
                ctx->press_x = x;
                ctx->press_y = y;
                ctx->press_valid = 1;
            }
            ctx->last_x = x;
            ctx->last_y = y;
        }
        return MTF2_ACT_NONE;
    }
    if (touch_state == 6 && ctx->was_pressed) {
        ctx->was_pressed = 0;
        int has_press = ctx->press_valid;
        ctx->press_valid = 0;
        if (!has_press) return MTF2_ACT_NONE;
        int16_t rx = (x >= 0 && x < 640 && y >= 0 && y < 480) ? x : ctx->last_x;
        int16_t ry = (x >= 0 && x < 640 && y >= 0 && y < 480) ? y : ctx->last_y;
        int dx = (int)rx - (int)ctx->press_x;
        int dy = (int)ry - (int)ctx->press_y;
        uint32_t act = mtf2_dispatch_release(ctx, rx, ry, dx, dy, 1);
        if (act != MTF2_ACT_NONE) {
            ctx->last_action = act;
            ctx->hits++;
        }
        return act;
    }
    if (touch_state != 6) {
        ctx->was_pressed = 0;
        ctx->press_valid = 0;
    }
    return MTF2_ACT_NONE;
}

#endif /* A6400_MTF2_NATIVE_TOUCH_H */
