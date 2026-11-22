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
    memset(&i_cc, 0, sizeof(i_cc));
    if (!dladdr(lookup_sym, &i_obj) || i_obj.dli_fbase != (void *)0x05d18000u) return 0;
    if (!dladdr(vsm_sym, &i_vu2) || i_vu2.dli_fbase != (void *)0x155e6000u) return 0;
    if (!dladdr((void *)0x16925c16u, &i_cc) || i_cc.dli_fbase != (void *)0x1615f000u) return 0;

    /* Exact 8-byte machine-code fingerprints from ILCE-6400 v2.00 libObj.so & viewUnified2.so. */
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

    if (!mtf2_match8(0x05e14e20u, fp_ev_ctor) ||
        !mtf2_match8(0x05e15974u, fp_op_new) ||
        !mtf2_match8(0x05e18d5cu, fp_evmgr_push) ||
        !mtf2_match8(0x0631e814u, fp_inh_mode) ||
        !mtf2_match8(0x157f8ca4u, fp_apply_pop) ||
        !mtf2_match8(0x15b90534u, fp_main_st) ||
        !mtf2_match8(0x15b91430u, fp_binding) ||
        !mtf2_match8(0x15b92110u, fp_refresh) ||
        !mtf2_match8(0x15b95b60u, fp_pop_open) ||
        !mtf2_match8(0x15b96f10u, fp_set_pop_row)) {
        return 0;
    }

    ctx->lookup = (uint32_t (*)(uint32_t, uint32_t, uint32_t, uint32_t *))lookup_sym;
    ctx->is_hit = (int (*)(uint32_t, const struct mtf2_point *))ishit_sym;
    ctx->supported = 1;
    return 1;
}

static void mtf2_sync_refresh(uint32_t menu) {
    void (*binding)(uint32_t) = (void (*)(uint32_t))(uintptr_t)0x15b91431u;
    void (*refresh)(uint32_t, uint32_t) = (void (*)(uint32_t, uint32_t))(uintptr_t)0x15b92111u;
    binding(menu);
    refresh(menu, 0u);
}

static uint32_t mtf2_find_setting_menu_view(uint32_t menu) {
    uint32_t vid = mtf2_rd32(menu, 0x14cu);
    uint32_t vm = mtf2_rd32(0x0713d7ecu, 0u);
    if (!vm) return 0u;
    int cnt = (int)mtf2_rd32(vm, 0u);
    uint32_t entries = mtf2_rd32(vm, 0x20u);
    if (!entries || cnt <= 0 || cnt > 128) return 0u;
    for (int i = 0; i < cnt; i++) {
        uint32_t ent = entries + (uint32_t)i * 16u;
        uint32_t vwrap = mtf2_rd32(ent, 4u);
        if (!vwrap || mtf2_rd32(ent, 8u) != vid) continue;
        if (mtf2_rd32(vwrap, 0u) == 0x15ec8270u) return vwrap;
        uint32_t inner = mtf2_rd32(vwrap, 0x18u);
        if (inner && mtf2_rd32(inner, 0u) == 0x15ec8270u) return inner;
    }
    return 0u;
}

static void mtf2_push_async_event(uint32_t event_id, uint8_t ev_type) {
    uint32_t mgr_ptr = mtf2_rd32(0x07089984u, 0u);
    if (!mgr_ptr) return;
    uint32_t mgr = mtf2_rd32(mgr_ptr, 0u);
    if (!mgr || !event_id) return;
    void *(*op_new)(uint32_t) = (void *(*)(uint32_t))(uintptr_t)0x05e15974u;
    void (*ev_ctor)(void *, uint32_t, uint8_t, uint8_t) =
        (void (*)(void *, uint32_t, uint8_t, uint8_t))(uintptr_t)0x05e14e20u;
    void (*evmgr_push)(uint32_t, void *, uint32_t) =
        (void (*)(uint32_t, void *, uint32_t))(uintptr_t)0x05e18d5du;
    void *ev = op_new(16u);
    if (!ev) return;
    ev_ctor(ev, event_id, ev_type, 1u);
    evmgr_push(mgr, ev, 1u);
}

static uint32_t mtf2_handle_popup(struct mtf2_ctx *ctx, uint32_t menu,
                                  int16_t x, int16_t y, int dx, int dy, int has_press) {
    uint32_t popup = menu + 0xfb20u;
    uint32_t pnode = mtf2_rd32(popup, 0x6490u);
    if (!pnode || mtf2_rd32(pnode, 0u) != 0x16c183e8u) return MTF2_ACT_NONE;

    void (*cancel_popup)(uint32_t) = (void (*)(uint32_t))(uintptr_t)0x15b9184du;
    void (*popup_up)(uint32_t) = (void (*)(uint32_t))(uintptr_t)0x15b9134du;
    void (*popup_down)(uint32_t) = (void (*)(uint32_t))(uintptr_t)0x15b91377u;
    void (*set_popup_row)(uint32_t, uint32_t) =
        (void (*)(uint32_t, uint32_t))(uintptr_t)0x15b96f11u;
    void (*confirm_popup)(uint32_t) = (void (*)(uint32_t))(uintptr_t)0x15b9638bu;
    void (*apply_popup)(uint32_t) = (void (*)(uint32_t))(uintptr_t)0x157f8ca5u;
    uint32_t view = mtf2_find_setting_menu_view(menu);

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
        if (view) mtf2_push_async_event(0x15010102u, 4u);
        else cancel_popup(menu);
        return MTF2_ACT_POPUP_CANCEL;
    }
    return MTF2_ACT_NONE;
}

static uint32_t mtf2_dispatch_release(struct mtf2_ctx *ctx,
                                      int16_t x, int16_t y, int dx, int dy, int has_press) {
    if (!ctx->supported || x == 32767 || y == 32767) return MTF2_ACT_NONE;

    uint32_t err = ~0u;
    uint32_t menu = ctx->lookup(0xaadca2c7u, 0xdfdc8dddu, 0xa056f604u, &err);
    if (err || !menu || mtf2_rd32(menu, 0u) != 0x15f1f1d0u || mtf2_rd32(menu, 0x1cu) != menu) {
        return MTF2_ACT_NONE;
    }

    int (*main_state)(uint32_t) = (int (*)(uint32_t))(uintptr_t)0x15b90535u;
    int (*inherited_mode)(uint32_t) = (int (*)(uint32_t))(uintptr_t)0x0631e815u;
    int (*popup_open)(uint32_t) = (int (*)(uint32_t))(uintptr_t)0x15b95b61u;
    if (mtf2_rd8(menu, 0x21u) != 2u || inherited_mode(menu) > 2) {
        return MTF2_ACT_NONE;
    }

    if (popup_open(menu + 0xfb20u)) {
        return mtf2_handle_popup(ctx, menu, x, y, dx, dy, has_press);
    }

    if (!main_state(menu)) return MTF2_ACT_NONE;

    uint32_t root = mtf2_rd32(menu, 0x186acu);
    if (!root || mtf2_rd32(root, 0u) != 0x16c183e8u) return MTF2_ACT_NONE;

    int (*selected_index)(uint32_t, unsigned *) =
        (int (*)(uint32_t, unsigned *))(uintptr_t)0x16925c17u;
    int (*indexed_child)(uint32_t, unsigned, uint32_t *) =
        (int (*)(uint32_t, unsigned, uint32_t *))(uintptr_t)0x16925bc7u;
    int (*set_selected_index)(uint32_t, unsigned) =
        (int (*)(uint32_t, unsigned))(uintptr_t)0x169260dbu;
    int (*enter_selected)(uint32_t, uint32_t) =
        (int (*)(uint32_t, uint32_t))(uintptr_t)0x15b92045u;

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
                    mtf2_sync_refresh(menu);
                }
                return MTF2_ACT_TAB;
            }
        }
    }

    if (mtf2_rd8(menu, 0x1d8u)) return MTF2_ACT_NONE;

    uint32_t tab_node = 0;
    if (indexed_child(root, tab_sel, &tab_node) != 1 || !tab_node ||
        mtf2_rd32(tab_node, 0u) != 0x16c183e8u) {
        return MTF2_ACT_NONE;
    }

    unsigned pages = mtf2_rd32(tab_node, 8u), page_sel = 0;
    uint32_t page_node = 0;
    if (pages < 1u || pages > MTF2_MAX_PAGES ||
        selected_index(tab_node, &page_sel) != 1 || page_sel < 1u || page_sel > pages ||
        indexed_child(tab_node, page_sel, &page_node) != 1 || !page_node ||
        mtf2_rd32(page_node, 0u) != 0x16c183e8u) {
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
        mtf2_sync_refresh(menu);
        return MTF2_ACT_SWIPE_PAGE;
    }

    for (unsigned p = 0; p < pages; p++) {
        if (ctx->is_hit(menu + 0x166b4u + p * 0x220u, &pt)) {
            if (p + 1u != page_sel && set_selected_index(tab_node, p + 1u) == 1) {
                mtf2_sync_refresh(menu);
            }
            return MTF2_ACT_PAGE;
        }
    }

    for (unsigned r = 0; r < rows; r++) {
        if (ctx->is_hit(menu + 0x9350u + r * 0x1118u, &pt)) {
            if (r + 1u != row_sel) {
                if (set_selected_index(page_node, r + 1u) == 1) {
                    mtf2_sync_refresh(menu);
                }
                return MTF2_ACT_ROW_SELECT;
            }
            uint32_t view = mtf2_find_setting_menu_view(menu);
            if (view) {
                mtf2_push_async_event(0x15010052u, 4u);
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
