/* Exact-version RAM trial. Invoked on the natural Appframework thread.
 * All drawing uses owned Sony widgets/layer. No framebuffer writes, input
 * consumption, firmware/configuration patch or independent SD mount.
 */
#define _GNU_SOURCE
#include "film_native_view.h"
#include "film_preview_fmt.h"
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include "ui_trial_trace.h"
#include "mtf2_native_touch.h"
#ifdef A6400_INPUT_DIAGNOSTIC
#include "ui_input_journal.h"
static volatile struct ui_input_journal a6400_ui_input_journal_v1;
#endif

#if defined(A6400_UI_TOUCH_CLOSE_DIAGNOSTIC) && !defined(A6400_UI_RENDER_DIAGNOSTIC)
#error "touch-close diagnostic requires the bounded render diagnostic"
#endif

#if defined(A6400_UI_COMBO_DIAGNOSTIC) && defined(A6400_UI_RENDER_DIAGNOSTIC)
#error "combo and automatic-render diagnostics are mutually exclusive"
#endif

#if defined(A6400_UI_SETTINGS_DIAGNOSTIC) && \
    (A6400_UI_SETTINGS_DIAGNOSTIC != 1 || !defined(A6400_UI_COMBO_DIAGNOSTIC))
#error "settings diagnostic requires the combo diagnostic"
#endif

typedef void *(*key_inst_fn)(void);
static key_inst_fn get_key_instance_fn;
#ifndef A6400_UI_TRIAL_MS
# ifdef A6400_UI_COMBO_DIAGNOSTIC
#  define A6400_UI_TRIAL_MS 180000u
# else
#  define A6400_UI_TRIAL_MS 30000u
# endif
#endif
#if A6400_UI_TRIAL_MS != 0 && (A6400_UI_TRIAL_MS < 30000 || A6400_UI_TRIAL_MS > 600000)
#error "A6400_UI_TRIAL_MS must be 0 (persistent) or between 30000 and 600000 milliseconds"
#endif
#define TRIAL_MS A6400_UI_TRIAL_MS
#ifndef A6400_UI_INSTANCE_SUFFIX
#define A6400_UI_INSTANCE_SUFFIX ""
#endif
#ifndef A6400_UI_DISPLAY_MS
#define A6400_UI_DISPLAY_MS 5000u
#endif
#if A6400_UI_DISPLAY_MS < 1 || A6400_UI_DISPLAY_MS > 20000 || (TRIAL_MS != 0 && A6400_UI_DISPLAY_MS >= TRIAL_MS)
#error "A6400_UI_DISPLAY_MS must be between 1 and 20000 milliseconds"
#endif
static volatile struct ui_trial_trace a6400_ui_trial_trace;
#define TRACE (&a6400_ui_trial_trace)
typedef void (*ctor_fn)(void *);
typedef void (*unary_fn)(void *);
typedef void (*bool_fn)(void *,int);
typedef void (*layer_id_fn)(void *,const uint32_t *);
/* The firmware is base AAPCS soft-float. Compiler/system headers are hard-float:
 * annotate the only native calls with float arguments explicitly. */
typedef void (*size_fn)(void *,float,float) __attribute__((pcs("aapcs")));
typedef void (*position_fn)(void *,const float *);
typedef void (*text_fn)(void *,const char *);
typedef void (*color_fn)(void *,const unsigned char *);
typedef uint32_t (*id_fn)(const char *);
typedef void *(*create_layer_fn)(void *,const uint32_t *);
typedef void (*mouse_position_fn)(const void *,unsigned char,short *,short *);
typedef int (*mouse_state_fn)(const void *,unsigned char,unsigned char);
typedef int (*key_fn)(void *,int,void *);
typedef void (*font_fn)(void *,uint32_t);
typedef uint32_t (*pb_base_fn)(void);
#define A6400_KEY_LEFT      0x0015
#define A6400_KEY_RIGHT     0x01af
#define A6400_KEY_CENTER    0x01ac
#define A6400_KEY_MENU      0x01ab
#define A6400_KEY_SHUTTER   0x00a7
#define A6400_KEY_TRASH     0x01f8
#define A6400_KEY_PLAY      0x01f4
#define A6400_KEY_WHEEL_CCW 0x10000000
#define A6400_KEY_WHEEL_CW  0x10000001
#define A6400_POPUP_SHOW_MS 3500u

#ifdef A6400_UI_LANG_SC
#define UI_S_ON                 "开启"
#define UI_S_OFF                "关闭"
#define UI_S_TAB0               "01 总览"
#define UI_S_TAB1               "02 胶片"
#define UI_S_TAB2               "03 细节"
#define UI_S_TAB3               "04 V4"
#define UI_S_CLOSE_BTN          "✕ 关闭"
#define UI_S_QUICK_ALBUM        "相册预览 (%u) →"
#define UI_S_V4_CNT_FMT         "V4 (%u/3) >"
#define UI_S_QUAL_12M           "12MP 超清"
#define UI_S_QUAL_6M            "6MP 高清"
#define UI_S_QUAL_1M7           "1.7MP 极速"
#define UI_S_HDR_BUSY           "V4.0 | %s | 转档:%u 已转:%u"
#define UI_S_HDR_IDLE           "V4.0 | %s | 已转:%u (%u/%u)"
#define UI_S_HDR_INIT           "A6400 V4.0 | %s"
#define UI_S_HDR_BOOT           "A6400 V4.0 | 关闭"
#define UI_S_POPUP_FMT          "即时预览小窗: %s"
#define UI_S_ALBUM_FMT          "机内相册预览: %s"
#define UI_S_TOUCH_FMT          "原生MENU触控: %s"
#define UI_S_TOUCH_UNSUP        "不支持"
#define UI_S_BROWSE_FMT         "浏览套色相册 (%u张)"
#define UI_S_ALB_LUT_TOP        "[LUT相册] %s"
#define UI_S_ALB_LUT_DEF        "已套滤镜"
#define UI_S_ALB_LUT_BOT        "◀/▶ 换张 | 轻触屏幕: 切换原图对比"
#define UI_S_ALB_ORIG_TOP       "[原图对比] %s"
#define UI_S_ALB_ORIG_DEF       "原始照片"
#define UI_S_ALB_ORIG_BOT       "◀/▶ 换张 | 轻触屏幕: 切回 LUT 套色"
#define UI_S_ALB_NONE_TOP       "[原图] 本张未套滤镜"
#define UI_S_ALB_NONE_BOT       "◀/▶ 换张 | 按播放键/快门退出"
#define UI_S_ALB_DIR_TOP        "[套色相册 %u/%u] %s"
#define UI_S_ALB_DIR_BOT        "< 上一张 | 点中间或按快门退出 | 下一张 >"
#define UI_S_HINT_COMBO         "MENU + 放大镜"
#define UI_S_HINT_COMBO_RAM     "MENU + 放大镜 | RAM"
#define UI_S_HINT_AUTO20        "20秒自动隐藏"
#else
#define UI_S_ON                 "開啟"
#define UI_S_OFF                "關閉"
#define UI_S_TAB0               "01 總覽"
#define UI_S_TAB1               "02 膠片"
#define UI_S_TAB2               "03 細節"
#define UI_S_TAB3               "04 V4"
#define UI_S_CLOSE_BTN          "✕ 關閉"
#define UI_S_QUICK_ALBUM        "相冊預覽 (%u) →"
#define UI_S_V4_CNT_FMT         "V4 (%u/3) >"
#define UI_S_QUAL_12M           "12MP 超清"
#define UI_S_QUAL_6M            "6MP 高清"
#define UI_S_QUAL_1M7           "1.7MP 極速"
#define UI_S_HDR_BUSY           "V4.0 | %s | 轉檔:%u 已轉:%u"
#define UI_S_HDR_IDLE           "V4.0 | %s | 已轉:%u (%u/%u)"
#define UI_S_HDR_INIT           "A6400 V4.0 | %s"
#define UI_S_HDR_BOOT           "A6400 V4.0 | 關閉"
#define UI_S_POPUP_FMT          "即時預覽小窗: %s"
#define UI_S_ALBUM_FMT          "機內相冊預覽: %s"
#define UI_S_TOUCH_FMT          "原生MENU觸控: %s"
#define UI_S_TOUCH_UNSUP        "不支援"
#define UI_S_BROWSE_FMT         "瀏覽套色相冊 (%u張)"
#define UI_S_ALB_LUT_TOP        "[LUT相冊] %s"
#define UI_S_ALB_LUT_DEF        "已套濾鏡"
#define UI_S_ALB_LUT_BOT        "◀/▶ 換張 | 輕觸螢幕: 切換原圖對比"
#define UI_S_ALB_ORIG_TOP       "[原圖對比] %s"
#define UI_S_ALB_ORIG_DEF       "原始照片"
#define UI_S_ALB_ORIG_BOT       "◀/▶ 換張 | 輕觸螢幕: 切回 LUT 套色"
#define UI_S_ALB_NONE_TOP       "[原圖] 本張未套濾鏡"
#define UI_S_ALB_NONE_BOT       "◀/▶ 換張 | 按播放鍵/快門退出"
#define UI_S_ALB_DIR_TOP        "[套色相冊 %u/%u] %s"
#define UI_S_ALB_DIR_BOT        "< 上一張 | 點中間或按快門退出 | 下一張 >"
#define UI_S_HINT_COMBO         "MENU + 放大鏡"
#define UI_S_HINT_COMBO_RAM     "MENU + 放大鏡 | RAM"
#define UI_S_HINT_AUTO20        "20秒自動隱藏"
#endif

static const char *quality_label(uint32_t q) {
    if(q==2u)return UI_S_QUAL_12M;
    if(q==1u)return UI_S_QUAL_6M;
    return UI_S_QUAL_1M7;
}
#ifdef A6400_CHARO_SHUTTER
typedef void (*beep_play_fn)(void *,int);
#define A6400_CHARO_SHUTTER_KEY 0xa7
#define A6400_CHARO_SOUND_ID 0x34
#define A6400_CHARO_CONNECT_SOUND_ID 0x33
#define A6400_CHARO_PUSH_SOUND_ID 0x35
#ifndef A6400_CHARO_DELAY_MS
#define A6400_CHARO_DELAY_MS 0u
#endif
#ifndef A6400_CHARO_COOLDOWN_MS
#define A6400_CHARO_COOLDOWN_MS 450u
#endif
static int charo_sound_id(int mode) {
#if defined(A6400_CHARO_MULTI) && A6400_CHARO_MULTI
    if(mode==1)return A6400_CHARO_CONNECT_SOUND_ID;
    if(mode==2)return A6400_CHARO_SOUND_ID;
    if(mode==3)return A6400_CHARO_PUSH_SOUND_ID;
    return 0;
#else
    return mode?A6400_CHARO_SOUND_ID:0;
#endif
}
static const char *charo_mode_label(int mode,int custom) {
#ifdef A6400_UI_LANG_SC
#if defined(A6400_CHARO_MULTI) && A6400_CHARO_MULTI
    if(mode==1)return "日文 Charo";
    if(mode==2)return custom?"SD自定义":"Bling Bling";
    if(mode==3)return "iPhone 快门";
    return "静音";
#else
    if(!mode)return "静音";
    return custom?"Charo (SD)":"Charo (开启)";
#endif
#else
#if defined(A6400_CHARO_MULTI) && A6400_CHARO_MULTI
    if(mode==1)return "日文 Charo";
    if(mode==2)return custom?"SD自訂":"Bling Bling";
    if(mode==3)return "iPhone 快門";
    return "靜音";
#else
    if(!mode)return "靜音";
    return custom?"Charo (SD)":"Charo (開啟)";
#endif
#endif
}
#endif

static struct a6400_ui_state {
    struct film_native_view view;
    void *system,*layer,*ticker,*background,*panels[6],*labels[8],*keys;
    void *ticker_layer;
    uint32_t ticker_layer_id;
    uint32_t layer_id,tid,started,last_tick,ticks;
    uint32_t opened_ms;
    int ever_opened;
#ifdef A6400_UI_TOUCH_CLOSE_DIAGNOSTIC
    int last_touch;
#endif
#ifdef A6400_UI_COMBO_DIAGNOSTIC
    int last_menu,last_zoom,last_other;
#elif defined(A6400_UI_TOUCH_CLOSE_DIAGNOSTIC)
    uint32_t snapshot_abi_padding[2];
#else
    uint32_t snapshot_abi_padding[3];
#endif
    uintptr_t vtable[42];
    int attached,running;
    ctor_fn widget_ctor,panel_ctor,label_ctor;
    layer_id_fn set_layer;
    position_fn set_position;
    size_fn panel_size,label_size;
    text_fn set_text;
    color_fn set_color;
    bool_fn layer_active,layer_clear,set_animate;
    unary_fn move_last;
    id_fn make_id;
    create_layer_fn create_layer;
    mouse_position_fn mouse_position;
    mouse_state_fn mouse_state;
    key_fn key;
    font_fn set_font;
    char texts[8][80];
} ui;
_Static_assert(sizeof(ui)==1120,"UI snapshot ABI must remain 1120 bytes");
_Static_assert(offsetof(struct a6400_ui_state,system)==0x68,"ui.system ABI mismatch");
_Static_assert(offsetof(struct a6400_ui_state,tid)==0xc0,"ui.tid ABI mismatch");
_Static_assert(offsetof(struct a6400_ui_state,started)==0xc4,"ui.started ABI mismatch");
_Static_assert(offsetof(struct a6400_ui_state,last_tick)==0xc8,"ui.last_tick ABI mismatch");
_Static_assert(offsetof(struct a6400_ui_state,attached)==0x18c,"ui.attached ABI mismatch");
_Static_assert(offsetof(struct a6400_ui_state,running)==0x190,"ui.running ABI mismatch");

/* Owned control word, separate from the pinned 1120-byte UI snapshot ABI.
 * A matching external manager may write 1; only the natural UI callback
 * stops owned layers and publishes 2. Retained widgets keep this SO loaded. */
volatile unsigned a6400_ui_stop_requested_v1;
__attribute__((visibility("default")))
volatile uint32_t a6400_ui_hires_v1 = 0u;

/* Owned corner-toast ABI, separate from the 1120-byte UI snapshot ABI. */
struct a6400_ui_toast_v1 {
    uint32_t magic;
    uint32_t seq;
    uint32_t kind;
    uint32_t duration_ms;
    char text[64];
};
__attribute__((visibility("default")))
volatile struct a6400_ui_toast_v1 a6400_ui_toast_v1 = { 0x54534f54u, 0u, 0u, 0u, "" };

struct a6400_ui_status_v1 {
    uint32_t magic;       /* 0x53544156u 'STAV' */
    uint32_t seq;
    uint32_t applied;
    uint32_t scanned;
    uint32_t max_files;
    uint32_t custom_charo;
    uint32_t active_jobs;
};
__attribute__((visibility("default")))
volatile struct a6400_ui_status_v1 a6400_ui_status_v1 = { 0x53544156u, 0u, 0u, 0u, 1024u, 0u, 0u };

__attribute__((visibility("default")))
volatile struct a6400_ui_v4_v1 a6400_ui_v4_v1 = {
    A6400_V4_MAGIC, A6400_V4_VERSION,
    1u, 1u, 1u,          /* preview_popup_en, album_preview_en, native_touch_en default ON */
    A6400_DISP_NONE, 0u, 0u,
    0u, 0u,
    0u, 0u, 0u, 0u,
    {0}, {{0}}
};

static struct {
    pb_base_fn get_pb_base;
    struct mtf2_ctx mtf2;
    uint32_t seen_latest_seq;
    int popup_active;
    uint32_t popup_until_ms;
    int album_mode;          /* 0=off, 1=playback-synced, 2=direct-menu-browser */
    int album_compare_orig;
    unsigned album_idx;      /* pb_offset in mode 1, or 0..album_count-1 in mode 2 */
    int last_pb_active;
    int last_nav_left, last_nav_right, last_nav_center, last_nav_exit;
    int album_touch_down;
    short album_touch_x, album_touch_y;
    int mtf2_wait_release;
} ui_v4;

static struct {
    void *bg, *accent, *label;
    uint32_t seq, until_ms;
    int visible;
    char text[64];
} ui_toast;

/* Decorative theme panels kept outside `ui` so the 1120-byte snapshot ABI stays exact. */
static struct {
    void *card_border, *card_panel, *header_accent, *header_divider, *page_btn;
    void *footer_divider, *status_dot, *page_btn_accent;
    void *grid_h, *grid_v1, *grid_v2, *grid_vc;
    void *tab_bg[4], *tab_bar[4], *tab_lbl[4];
    void *close_bg, *close_lbl;
    void *row_accents[6];
    void *extra_panels[4], *extra_accents[4], *extra_labels[4];
    void *sub_labels[6];
    void *chips[10][3];
    char extra_texts[4][48];
    uint32_t status_seq;
#ifdef A6400_CHARO_SHUTTER
    beep_play_fn beep_play;
    int shutter_sound_enabled;
    int last_shutter_key;
    int shutter_pending;
    uint32_t shutter_arm_ms;
    uint32_t last_shutter_beep_ms;
#endif
} ui_theme;

static uint32_t now_ms(void) {
    struct timespec t;
    if(syscall(SYS_clock_gettime,CLOCK_MONOTONIC,&t))return 0;
    return (uint32_t)((uint64_t)t.tv_sec*1000u+(unsigned)t.tv_nsec/1000000u);
}
static void input_snapshot(unsigned stage) {
#ifdef A6400_INPUT_DIAGNOSTIC
    void *owned[UI_INPUT_OWNED]={0};
    owned[0]=ui.background;
    for(unsigned i=0;i<6;i++)owned[i+1]=ui.panels[i];
    for(unsigned i=0;i<8;i++)owned[i+7]=ui.labels[i];
    owned[15]=ui.ticker;
    if(ui.layer)owned[16]=*(void **)((unsigned char *)ui.layer+0x38);
    if(ui.ticker_layer)owned[17]=*(void **)((unsigned char *)ui.ticker_layer+0x38);
    ui_input_capture(&a6400_ui_input_journal_v1,stage,ui.system,owned,
        UI_INPUT_OWNED,now_ms(),(uint32_t)syscall(SYS_gettid));
#else
    (void)stage;
#endif
}
static void record(const char *event,int a,int b) {
    ui_trial_record(TRACE,event,now_ms(),(uint32_t)syscall(SYS_gettid),
                    (uint32_t)a,(uint32_t)b,ui.ticks,
                    (unsigned)ui.running | ((unsigned)ui.view.menu.visible<<1));
}
static void *api(const char *name,uint32_t offset) {
    (void)offset;
    void *p=dlsym(RTLD_DEFAULT,name);
    if(!p){record("API_MISMATCH",(int)offset,0);return 0;}
    return p;
}
static __attribute__((noinline)) int resolve(void) {
#define API(member,type,name,offset) do {ui.member=(type)api(name,offset);if(!ui.member)return -1;}while(0)
    API(widget_ctor,ctor_fn,"_ZN2ux6wgtsys6WidgetC1Ev",0x5ed5ad);
    API(panel_ctor,ctor_fn,"_ZN9GEN_PanelC1Ev",0x4149ad);
    API(label_ctor,ctor_fn,"_ZN9GEN_LabelC1Ev",0x3c8d41);
    API(set_layer,layer_id_fn,"_ZN2ux6wgtsys6Widget10setLayerIDERKNS_4core2IDE",0x5ed50d);
    API(set_position,position_fn,"_ZN2ux6wgtsys6Widget11setLocalPosERKNS_4core7Vector2E",0x5ed45d);
    API(panel_size,size_fn,"_ZN9GEN_Panel7setSizeEff",0x414def);
    API(label_size,size_fn,"_ZN9GEN_Label7setSizeEff",0x3c9381);
    API(set_text,text_fn,"_ZN9GEN_Label13setTextDirectEPKc",0x3c88cf);
    API(set_font,font_fn,"_ZN9GEN_Label7setFontEj",0x3c89af);
    API(set_color,color_fn,"_ZN9GEN_Panel14setColorDirectERKN6stella6CColorE",0x4148e5);
    API(layer_active,bool_fn,"_ZN2ux6wgtsys5Layer9setActiveEb",0x5ef12f);
    API(layer_clear,bool_fn,"_ZN2ux6wgtsys5Layer8setClearEb",0x5ef159);
    API(set_animate,bool_fn,"_ZN2ux6wgtsys10WidgetBase10setAnimateEb",0x606951);
    API(move_last,unary_fn,"_ZN2ux6wgtsys9LayerBase8moveLastEv",0x601331);
    API(make_id,id_fn,"_ZN2ux4core2ID5genIDEPKc",0x63dcd9);
    API(create_layer,create_layer_fn,"_ZN2ux6wgtsys12WidgetSystem11createLayerERKNS_4core2IDE",0x5f0f49);
    API(mouse_position,mouse_position_fn,"_ZNK2ux6wgtsys12WidgetSystem24getMousePosInFramebufferEhRsS2_",0x5f1019);
    API(mouse_state,mouse_state_fn,"_ZNK2ux6wgtsys12WidgetSystem13getMouseStateEhh",0x5f1045);
    API(key,key_fn,"_ZN10InfraInput20GetPhyKeyStatusCacheEiP21_InfraInputPhyKeyData",0x860fa9);
    get_key_instance_fn=(key_inst_fn)api("_ZN10InfraInput11GetInstanceEv",0x860385);
    if(!get_key_instance_fn)return -1;
#ifdef A6400_CHARO_SHUTTER
    ui_theme.beep_play=(beep_play_fn)api("_ZN13AppWidgetBase8beepPlayEi",0);
    if(!ui_theme.beep_play)return -1;
#endif
    /* Optional playback detector: absent on non-matching models is non-fatal. */
    ui_v4.get_pb_base=(pb_base_fn)dlsym(RTLD_DEFAULT,
        "_ZN27CmnForUseMultipleViewStatus20getCurrentViewPbBaseEv");
#undef API
    return 0;
}
static void place(void *widget,int label,int x,int y,int w,int h) {
    if(!widget)return;
    const float xy[2]={(float)x,(float)y};
    ui.set_position(widget,xy);
    (label?ui.label_size:ui.panel_size)(widget,(float)w,(float)h);
}
static void paint(void *panel,const unsigned char *c) {
    if(panel)ui.set_color(panel,c);
}
static void write_lbl(void *label,const char *s) {
    if(label)ui.set_text(label,s);
}
static void *tile_panel(unsigned i) { return i<6u?ui.panels[i]:(i<10u?ui_theme.extra_panels[i-6u]:0); }
static void *tile_accent(unsigned i) { return i<6u?ui_theme.row_accents[i]:(i<10u?ui_theme.extra_accents[i-6u]:0); }
static void *tile_label(unsigned i) { return i<6u?ui.labels[i+1u]:(i<10u?ui_theme.extra_labels[i-6u]:0); }
static char *tile_text_buf(unsigned i,unsigned *cap) {
    if(i<6u){*cap=(unsigned)sizeof(ui.texts[0]);return ui.texts[i+1u];}
    *cap=(unsigned)sizeof(ui_theme.extra_texts[0]);return ui_theme.extra_texts[i-6u];
}
/* Signature 3-color RGBA palette triads for all 10 built-in film stocks. */
static const unsigned char stock_palettes[10][3][4] = {
    { {236,186,134,255}, {214,148,128,255}, {156,166,132,255} }, /* 0 Portra 400 */
    { {232, 68, 56,255}, { 72,184,198,255}, { 44, 68,122,255} }, /* 1 CineStill 800T */
    { { 56,168,124,255}, { 78,152,214,255}, {226,178, 92,255} }, /* 2 Fuji Provia */
    { {242,192, 58,255}, {214,118, 64,255}, {156,108, 72,255} }, /* 3 Kodak Gold */
    { {236,236,234,255}, {124,124,128,255}, { 44, 44, 48,255} }, /* 4 Tri-X 400 */
    { { 88,134,136,255}, {184,132, 86,255}, { 62, 74, 86,255} }, /* 5 Classic Chrome */
    { {228, 66, 86,255}, { 58,118,224,255}, {238,184, 48,255} }, /* 6 Agfa Ultra 100 */
    { {228,214,184,255}, {118,168,162,255}, {146,114, 96,255} }, /* 7 Polaroid 669 */
    { {244,246,248,255}, {148,154,162,255}, { 52, 56, 62,255} }, /* 8 Fuji Acros */
    { {226,174, 94,255}, {136,184,166,255}, {118, 86,112,255} }, /* 9 Polaroid PX680+ */
};
static void clear_chips(unsigned slot) {
    static const unsigned char clear_c[4]={0,0,0,0};
    if(slot>=10u)return;
    for(unsigned k=0;k<3u;k++){
        place(ui_theme.chips[slot][k],0,0,0,0,0);
        paint(ui_theme.chips[slot][k],clear_c);
    }
}
static void draw_swiss_toggle(unsigned slot,int x,int y,int on,const unsigned char *on_c) {
    static const unsigned char track_off[4]={44,48,54,255};
    static const unsigned char knob_on[4]={250,252,255,255};
    static const unsigned char knob_off[4]={148,154,164,255};
    static const unsigned char clear_c[4]={0,0,0,0};
    if(slot>=10u)return;
    place(ui_theme.chips[slot][0],0,x,y,52,26);
    paint(ui_theme.chips[slot][0],on?on_c:track_off);
    place(ui_theme.chips[slot][1],0,on?(x+29):(x+3),y+3,20,20);
    paint(ui_theme.chips[slot][1],on?knob_on:knob_off);
    place(ui_theme.chips[slot][2],0,0,0,0,0);
    paint(ui_theme.chips[slot][2],clear_c);
}
static void draw_swiss_bar3(unsigned slot,int x,int y,int w_each,int h,int gap,int level,const unsigned char *lit_c) {
    static const unsigned char dim_c[4]={42,45,52,240};
    if(slot>=10u)return;
    for(unsigned k=0;k<3u;k++){
        place(ui_theme.chips[slot][k],0,x+(int)k*(w_each+gap),y,w_each,h);
        paint(ui_theme.chips[slot][k],((int)k<level)?lit_c:dim_c);
    }
}
static void draw_film_swatch(unsigned slot,int x,int y,int stock,int horiz_strip) {
    if(slot>=10u)return;
    if(stock<0 || stock>=10)stock=0;
    for(unsigned k=0;k<3u;k++){
        if(horiz_strip)place(ui_theme.chips[slot][k],0,x+(int)k*52,y,48,8);
        else place(ui_theme.chips[slot][k],0,x+(int)k*10,y,8,31);
        paint(ui_theme.chips[slot][k],stock_palettes[stock][k]);
    }
}
static void draw_v4_segments(unsigned slot,int x,int y) {
    static const unsigned char c_orange[4]={245,86,18,255};
    static const unsigned char c_white[4]={232,236,242,255};
    static const unsigned char c_sky[4]={76,186,238,255};
    static const unsigned char c_off[4]={42,45,52,240};
    if(slot>=10u)return;
    place(ui_theme.chips[slot][0],0,x,y,48,8);
    paint(ui_theme.chips[slot][0],a6400_ui_v4_v1.preview_popup_en?c_orange:c_off);
    place(ui_theme.chips[slot][1],0,x+52,y,48,8);
    paint(ui_theme.chips[slot][1],a6400_ui_v4_v1.album_preview_en?c_white:c_off);
    place(ui_theme.chips[slot][2],0,x+104,y,48,8);
    paint(ui_theme.chips[slot][2],(a6400_ui_v4_v1.native_touch_ok && a6400_ui_v4_v1.native_touch_en)?c_sky:c_off);
}
static void draw_gallery_icon(unsigned slot,int x,int y,int has_photos) {
    static const unsigned char back_c[4]={98,104,114,235};
    static const unsigned char frame_on[4]={250,252,255,255};
    static const unsigned char frame_off[4]={74,78,88,235};
    static const unsigned char inner_on[4]={232,76,16,255};
    static const unsigned char inner_off[4]={18,20,24,255};
    if(slot>=10u)return;
    place(ui_theme.chips[slot][0],0,x+8,y,34,24);
    paint(ui_theme.chips[slot][0],back_c);
    place(ui_theme.chips[slot][1],0,x,y+6,34,24);
    paint(ui_theme.chips[slot][1],has_photos?frame_on:frame_off);
    place(ui_theme.chips[slot][2],0,x+3,y+9,28,18);
    paint(ui_theme.chips[slot][2],has_photos?inner_on:inner_off);
}
static void show_menu_chrome(int page) {
    static const unsigned char background[4]={0,0,0,152};
    static const unsigned char swiss_border[4]={218,222,228,222};
    static const unsigned char swiss_bg[4]={14,15,18,246};
    static const unsigned char hairline_main[4]={84,90,100,220};
    static const unsigned char hairline_grid[4]={54,58,66,220};
    static const unsigned char clear_c[4]={0,0,0,0};
    place(ui.background,0,0,0,640,480);paint(ui.background,background);
    place(ui_theme.card_border,0,32,20,576,440);paint(ui_theme.card_border,swiss_border);
    place(ui_theme.card_panel,0,33,21,574,438);paint(ui_theme.card_panel,swiss_bg);
    place(ui_theme.header_accent,0,0,0,0,0);paint(ui_theme.header_accent,clear_c);
    place(ui_theme.header_divider,0,33,74,574,1);paint(ui_theme.header_divider,hairline_main);
    place(ui_theme.footer_divider,0,33,402,574,1);paint(ui_theme.footer_divider,hairline_main);
    int is_home=(page==FILM_MENU_HOME);
    int is_stocks=(page==FILM_MENU_STOCKS || page==FILM_MENU_STOCKS_P2);
    int is_v4=(page==FILM_MENU_V4);
    place(ui_theme.grid_h,0,(is_home||is_v4)?33:0,(is_home||is_v4)?238:0,(is_home||is_v4)?574:0,(is_home||is_v4)?1:0);
    paint(ui_theme.grid_h,(is_home||is_v4)?hairline_grid:clear_c);
    place(ui_theme.grid_v1,0,is_home?224:0,is_home?75:0,is_home?1:0,is_home?327:0);
    paint(ui_theme.grid_v1,is_home?hairline_grid:clear_c);
    place(ui_theme.grid_v2,0,is_home?416:0,is_home?75:0,is_home?1:0,is_home?327:0);
    paint(ui_theme.grid_v2,is_home?hairline_grid:clear_c);
    place(ui_theme.grid_vc,0,(is_stocks||is_v4)?320:0,(is_stocks||is_v4)?75:0,(is_stocks||is_v4)?1:0,(is_stocks||is_v4)?327:0);
    paint(ui_theme.grid_vc,(is_stocks||is_v4)?hairline_grid:clear_c);
}
static void hide_menu_widgets(void) {
    static const unsigned char clear_c[4]={0,0,0,0};
    place(ui.background,0,0,0,0,0);paint(ui.background,clear_c);
    place(ui_theme.card_border,0,0,0,0,0);paint(ui_theme.card_border,clear_c);
    place(ui_theme.card_panel,0,0,0,0,0);paint(ui_theme.card_panel,clear_c);
    place(ui_theme.header_accent,0,0,0,0,0);paint(ui_theme.header_accent,clear_c);
    place(ui_theme.header_divider,0,0,0,0,0);paint(ui_theme.header_divider,clear_c);
    place(ui_theme.footer_divider,0,0,0,0,0);paint(ui_theme.footer_divider,clear_c);
    place(ui_theme.status_dot,0,0,0,0,0);paint(ui_theme.status_dot,clear_c);
    place(ui_theme.page_btn,0,0,0,0,0);paint(ui_theme.page_btn,clear_c);
    place(ui_theme.page_btn_accent,0,0,0,0,0);paint(ui_theme.page_btn_accent,clear_c);
    place(ui_theme.grid_h,0,0,0,0,0);paint(ui_theme.grid_h,clear_c);
    place(ui_theme.grid_v1,0,0,0,0,0);paint(ui_theme.grid_v1,clear_c);
    place(ui_theme.grid_v2,0,0,0,0,0);paint(ui_theme.grid_v2,clear_c);
    place(ui_theme.grid_vc,0,0,0,0,0);paint(ui_theme.grid_vc,clear_c);
    place(ui_theme.close_bg,0,0,0,0,0);paint(ui_theme.close_bg,clear_c);
    place(ui_theme.close_lbl,1,0,0,0,0);write_lbl(ui_theme.close_lbl,"");
    for(unsigned t=0;t<4u;t++){
        place(ui_theme.tab_bg[t],0,0,0,0,0);paint(ui_theme.tab_bg[t],clear_c);
        place(ui_theme.tab_bar[t],0,0,0,0,0);paint(ui_theme.tab_bar[t],clear_c);
        place(ui_theme.tab_lbl[t],1,0,0,0,0);write_lbl(ui_theme.tab_lbl[t],"");
    }
    for(unsigned i=0;i<6u;i++){
        place(ui_theme.sub_labels[i],1,0,0,0,0);write_lbl(ui_theme.sub_labels[i],"");
    }
    for(unsigned i=0;i<10u;i++){
        place(tile_panel(i),0,0,0,0,0);paint(tile_panel(i),clear_c);
        place(tile_accent(i),0,0,0,0,0);paint(tile_accent(i),clear_c);
        clear_chips(i);
    }
    for(unsigned i=0;i<8u;i++){
        ui.texts[i][0]=0;
        place(ui.labels[i],1,0,0,0,0);
        write_lbl(ui.labels[i],ui.texts[i]);
    }
    for(unsigned i=0;i<4u;i++){
        ui_theme.extra_texts[i][0]=0;
        place(ui_theme.extra_labels[i],1,0,0,0,0);
        write_lbl(ui_theme.extra_labels[i],ui_theme.extra_texts[i]);
    }
}
static void hide_toast_widgets(void) {
    static const unsigned char clear_c[4]={0,0,0,0};
    if(!ui_toast.bg)return;
    place(ui_toast.bg,0,0,0,0,0);ui.set_color(ui_toast.bg,clear_c);
    place(ui_toast.accent,0,0,0,0,0);ui.set_color(ui_toast.accent,clear_c);
    ui_toast.text[0]=0;
    place(ui_toast.label,1,0,0,0,0);ui.set_text(ui_toast.label,ui_toast.text);
}
static void v4_publish_display(uint32_t mode,uint32_t slot) {
    a6400_ui_v4_v1.album_slot=slot;
    a6400_ui_v4_v1.display_mode=mode;
    __sync_synchronize();
    a6400_ui_v4_v1.display_seq+=1u;
}
static void v4_dismiss_popup(void) {
    if(!ui_v4.popup_active)return;
    ui_v4.popup_active=0;
    ui_v4.popup_until_ms=0;
    if(!ui_v4.album_mode)v4_publish_display(A6400_DISP_NONE,0u);
}
static void v4_draw_album_hud(const char *top_text,const char *bot_text,int show_lut) {
    static const unsigned char bar_lut[4]={14,15,18,242};
    static const unsigned char bar_orig[4]={32,34,40,236};
    static const unsigned char accent_c[4]={245,86,18,255};
    hide_menu_widgets();
    hide_toast_widgets();
    ui_toast.visible=0;
    place(ui.panels[0],0,0,0,640,34);
    paint(ui.panels[0],show_lut?bar_lut:bar_orig);
    place(ui_theme.header_accent,0,0,34,640,2);
    paint(ui_theme.header_accent,accent_c);
    place(ui.panels[1],0,0,444,640,36);
    paint(ui.panels[1],bar_lut);
    snprintf(ui.texts[0],sizeof(ui.texts[0]),"%s",top_text?top_text:"");
    snprintf(ui.texts[7],sizeof(ui.texts[7]),"%s",bot_text?bot_text:"");
    place(ui.labels[0],1,14,3,612,28);
    ui.set_text(ui.labels[0],ui.texts[0]);
    place(ui.labels[7],1,14,448,612,28);
    ui.set_text(ui.labels[7],ui.texts[7]);
    ui.move_last(ui.layer);
    ui.layer_active(ui.layer,1);
}
static void v4_album_sync_view(void) {
    if(!ui_v4.album_mode)return;
    char top[80],bot[80];
    if(ui_v4.album_mode==1) {
        uint32_t total_shots=a6400_ui_v4_v1.session_shots;
        int slot=-1;
        if(total_shots>0u && ui_v4.album_idx<total_shots && ui_v4.album_idx<A6400_SESSION_SHOTS_CAP) {
            uint32_t pos=(total_shots-1u-ui_v4.album_idx)%A6400_SESSION_SHOTS_CAP;
            slot=(int)a6400_ui_v4_v1.shot_slots[pos];
        }
        if(slot>=0 && (unsigned)slot<A6400_ALBUM_SLOTS) {
            char cap[48];
            for(unsigned i=0;i<47u;i++)cap[i]=a6400_ui_v4_v1.slot_labels[slot][i];
            cap[47]=0;
            if(!ui_v4.album_compare_orig) {
                snprintf(top,sizeof(top),UI_S_ALB_LUT_TOP,cap[0]?cap:UI_S_ALB_LUT_DEF);
                snprintf(bot,sizeof(bot),UI_S_ALB_LUT_BOT);
                v4_draw_album_hud(top,bot,1);
                v4_publish_display(A6400_DISP_ALBUM,(uint32_t)slot);
            } else {
                snprintf(top,sizeof(top),UI_S_ALB_ORIG_TOP,cap[0]?cap:UI_S_ALB_ORIG_DEF);
                snprintf(bot,sizeof(bot),UI_S_ALB_ORIG_BOT);
                v4_draw_album_hud(top,bot,0);
                v4_publish_display(A6400_DISP_NONE,0u);
            }
        } else {
            snprintf(top,sizeof(top),UI_S_ALB_NONE_TOP);
            snprintf(bot,sizeof(bot),UI_S_ALB_NONE_BOT);
            v4_draw_album_hud(top,bot,0);
            v4_publish_display(A6400_DISP_NONE,0u);
        }
    } else {
        uint32_t cnt=a6400_ui_v4_v1.album_count;
        if(cnt==0u) {
            ui_v4.album_mode=0;
            v4_publish_display(A6400_DISP_NONE,0u);
            ui.layer_active(ui.layer,0);
            return;
        }
        if(cnt>A6400_ALBUM_SLOTS)cnt=A6400_ALBUM_SLOTS;
        if(ui_v4.album_idx>=cnt)ui_v4.album_idx=cnt-1u;
        uint32_t head=a6400_ui_v4_v1.album_head%A6400_ALBUM_SLOTS;
        uint32_t slot=(head+A6400_ALBUM_SLOTS-(ui_v4.album_idx%A6400_ALBUM_SLOTS))%A6400_ALBUM_SLOTS;
        char cap[48];
        for(unsigned i=0;i<47u;i++)cap[i]=a6400_ui_v4_v1.slot_labels[slot][i];
        cap[47]=0;
        snprintf(top,sizeof(top),UI_S_ALB_DIR_TOP,
                 (unsigned)(ui_v4.album_idx+1u),(unsigned)cnt,cap[0]?cap:UI_S_ALB_LUT_DEF);
        snprintf(bot,sizeof(bot),UI_S_ALB_DIR_BOT);
        v4_draw_album_hud(top,bot,1);
        v4_publish_display(A6400_DISP_ALBUM,slot);
    }
}
static void v4_exit_album(void) {
    if(!ui_v4.album_mode)return;
    ui_v4.album_mode=0;
    ui_v4.album_compare_orig=0;
    ui_v4.album_touch_down=0;
    v4_publish_display(A6400_DISP_NONE,0u);
    if(!ui.view.menu.visible) {
        hide_menu_widgets();
        ui.layer_active(ui.layer,0);
    }
    record("ALBUM_EXIT",0,0);
}
static void visibility(void *unused,int active) {
    (void)unused;
    input_snapshot(active?3:5);
    record(active?"SHOW_BEFORE_MOVE":"HIDE_BEFORE_ACTIVE",active,0);
    if(active){ui.move_last(ui.layer);record("SHOW_AFTER_MOVE",0,0);}
    ui.layer_active(ui.layer,active);
    input_snapshot(active?4:6);
    if(active){ui.opened_ms=now_ms();ui.ever_opened=1;}
#if defined(A6400_UI_COMBO_DIAGNOSTIC) || (TRIAL_MS == 0 && !defined(A6400_UI_RENDER_DIAGNOSTIC))
    else ui.opened_ms=0;
#endif
    record(active?"FILM_OPEN":"FILM_CLOSE",ui.view.menu.settings.enabled,ui.view.menu.settings.stock);
}
static void draw(void *unused,const struct film_menu *menu) {
    (void)unused;
    record("DRAW_BEGIN",menu->page,menu->selected);
    v4_dismiss_popup();
    if(ui_v4.album_mode) {
        ui_v4.album_mode=0;
        ui_v4.album_compare_orig=0;
        ui_v4.album_touch_down=0;
        v4_publish_display(A6400_DISP_NONE,0u);
    }
    hide_toast_widgets();
    ui_toast.visible=0;
    show_menu_chrome(menu->page);

    static const unsigned char clear_c[4]={0,0,0,0};
    static const unsigned char h_orange[4]={234,78,16,255};
    static const unsigned char h_orange_bright[4]={250,96,24,255};
    static const unsigned char h_ember[4]={42,20,12,248};
    static const unsigned char h_cell[4]={22,24,28,248};
    static const unsigned char h_tab_idle[4]={20,22,26,236};
    static const unsigned char h_tab_active[4]={34,36,42,250};
    static const unsigned char h_close_idle[4]={38,34,38,245};
    static const unsigned char h_close_press[4]={216,52,44,255};
    static const unsigned char h_white[4]={244,246,250,255};
    static const unsigned char h_silver[4]={142,148,158,240};
    static const unsigned char h_muted[4]={58,62,70,220};
    static const unsigned char h_sky[4]={76,186,238,255};
    static const unsigned char h_dock_idle[4]={26,28,34,248};
    static const unsigned char h_dock_active[4]={46,24,14,250};

    /* 1. Top Hasselblad Swiss 4-Tab Bar + Close Pill */
    static const char *const tab_names[4]={UI_S_TAB0,UI_S_TAB1,UI_S_TAB2,UI_S_TAB3};
    int cur_tab=(menu->page==FILM_MENU_STOCKS || menu->page==FILM_MENU_STOCKS_P2)?1:
                (menu->page==FILM_MENU_DETAILS?2:(menu->page==FILM_MENU_V4?3:0));
    for(unsigned t=0;t<4u;t++){
        int tx=42+(int)t*114;
        int hit_id=FILM_HIT_TAB_HOME+(int)t;
        int is_cur=((int)t==cur_tab);
        int is_sel=(menu->selected==hit_id);
        place(ui_theme.tab_bg[t],0,tx,28,108,38);
        paint(ui_theme.tab_bg[t],is_sel?h_orange:(is_cur?h_tab_active:h_tab_idle));
        place(ui_theme.tab_bar[t],0,tx,63,108,3);
        paint(ui_theme.tab_bar[t],is_cur?h_orange:clear_c);
        place(ui_theme.tab_lbl[t],1,tx+8,33,90,28);
        write_lbl(ui_theme.tab_lbl[t],tab_names[t]);
    }
    place(ui_theme.close_bg,0,506,28,90,38);
    paint(ui_theme.close_bg,menu->selected==FILM_HIT_CLOSE?h_close_press:h_close_idle);
    place(ui_theme.close_lbl,1,512,33,76,28);
    write_lbl(ui_theme.close_lbl,UI_S_CLOSE_BTN);

    /* 2. Bottom Swiss Telemetry Bar */
    if(a6400_ui_status_v1.magic==0x53544156u && a6400_ui_status_v1.seq>0u) {
        ui_theme.status_seq=a6400_ui_status_v1.seq;
        unsigned qmax=a6400_ui_status_v1.max_files?(unsigned)a6400_ui_status_v1.max_files:1024u;
        if(a6400_ui_status_v1.active_jobs>0u) {
            snprintf(ui.texts[0],sizeof(ui.texts[0]),UI_S_HDR_BUSY,
                     menu->settings.enabled?UI_S_ON:UI_S_OFF,
                     (unsigned)a6400_ui_status_v1.active_jobs,(unsigned)a6400_ui_status_v1.applied);
        } else {
            snprintf(ui.texts[0],sizeof(ui.texts[0]),UI_S_HDR_IDLE,
                     menu->settings.enabled?UI_S_ON:UI_S_OFF,
                     (unsigned)a6400_ui_status_v1.applied,(unsigned)a6400_ui_status_v1.scanned,qmax);
        }
    } else {
        snprintf(ui.texts[0],sizeof(ui.texts[0]),UI_S_HDR_INIT,
                 menu->settings.enabled?UI_S_ON:UI_S_OFF);
    }
    place(ui_theme.status_dot,0,46,425,10,10);
    paint(ui_theme.status_dot,
          (a6400_ui_status_v1.magic==0x53544156u && a6400_ui_status_v1.active_jobs>0u)?h_white:
          (menu->settings.enabled?h_orange:h_muted));
    place(ui.labels[0],1,64,416,342,28);
    write_lbl(ui.labels[0],ui.texts[0]);

    /* Clear sub_labels before populating active page */
    for(unsigned i=0;i<6u;i++){
        place(ui_theme.sub_labels[i],1,0,0,0,0);
        write_lbl(ui_theme.sub_labels[i],"");
    }

    /* 3. Content Cells across all 4 Hasselblad Swiss Pages */
    int stocks_page=(menu->page==FILM_MENU_STOCKS || menu->page==FILM_MENU_STOCKS_P2);
    int details_page=(menu->page==FILM_MENU_DETAILS);
    int v4_page=(menu->page==FILM_MENU_V4);
    unsigned v4_on_cnt=(a6400_ui_v4_v1.preview_popup_en?1u:0u)+
                       (a6400_ui_v4_v1.album_preview_en?1u:0u)+
                       ((a6400_ui_v4_v1.native_touch_ok && a6400_ui_v4_v1.native_touch_en)?1u:0u);
    static const char *const home_subs[6]={
        "01 / LUT","02 / STOCK","03 / RES","04 / FX",
#ifdef A6400_CHARO_SHUTTER
        "05 / AUDIO",
#else
        "05 / V4",
#endif
        "06 / V4"
    };
    static const char *const v4_subs[4]={"01 / POPUP","02 / ALBUM","03 / TOUCH","04 / GALLERY"};

    for(unsigned slot=0;slot<10u;slot++) {
        int x=0,y=0,w=0,h=0;
        unsigned cap=0;
        char *tbuf=tile_text_buf(slot,&cap);
        int used=film_native_view_row(menu,slot,tbuf,cap,&x,&y,&w,&h);
        if(used<=0) {
            tbuf[0]=0;
            place(tile_panel(slot),0,0,0,0,0);paint(tile_panel(slot),clear_c);
            place(tile_accent(slot),0,0,0,0,0);paint(tile_accent(slot),clear_c);
            clear_chips(slot);
            place(tile_label(slot),1,0,0,0,0);write_lbl(tile_label(slot),"");
            continue;
        }
        /* Live RAM state text overrides */
        if(menu->page==FILM_MENU_HOME) {
            if(slot==2u)snprintf(tbuf,cap,"%s",quality_label(a6400_ui_hires_v1));
#ifdef A6400_CHARO_SHUTTER
            else if(slot==4u) {
                int custom=(a6400_ui_status_v1.magic==0x53544156u && a6400_ui_status_v1.custom_charo);
                snprintf(tbuf,cap,"%s",charo_mode_label(ui_theme.shutter_sound_enabled,custom));
            }
#endif
            else if(slot==5u)snprintf(tbuf,cap,UI_S_V4_CNT_FMT,v4_on_cnt);
        } else if(v4_page) {
            if(slot==0u)snprintf(tbuf,cap,UI_S_POPUP_FMT,a6400_ui_v4_v1.preview_popup_en?UI_S_ON:UI_S_OFF);
            else if(slot==1u)snprintf(tbuf,cap,UI_S_ALBUM_FMT,a6400_ui_v4_v1.album_preview_en?UI_S_ON:UI_S_OFF);
            else if(slot==2u)snprintf(tbuf,cap,UI_S_TOUCH_FMT,
                                      !a6400_ui_v4_v1.native_touch_ok?UI_S_TOUCH_UNSUP:
                                      (a6400_ui_v4_v1.native_touch_en?UI_S_ON:UI_S_OFF));
            else if(slot==3u)snprintf(tbuf,cap,UI_S_BROWSE_FMT,(unsigned)a6400_ui_v4_v1.album_count);
        }

        int hit_id=-1;
        if(stocks_page) hit_id=FILM_HIT_STOCK_BASE+(int)slot;
        else if(details_page) hit_id=(slot==2u)?5:(int)slot;
        else if(v4_page) hit_id=(slot<2u)?(int)slot:(int)(slot+1u);
        else {
            static const int home_hits[6]={0,1,7,4,3,FILM_HIT_TAB_V4};
            hit_id=home_hits[slot];
        }
        int is_pressed=(menu->selected==hit_id);

        /* Hasselblad signature color logic: solid International Orange hero cell for LUT ON & active Gallery */
        const unsigned char *bg=h_cell;
        if(is_pressed) bg=h_orange_bright;
        else if(menu->page==FILM_MENU_HOME && slot==0u && menu->settings.enabled) bg=h_orange;
        else if(menu->page==FILM_MENU_HOME && slot==2u && a6400_ui_hires_v1==2u) bg=h_ember;
        else if(stocks_page && (int)slot==menu->settings.stock) bg=h_ember;
        else if(v4_page && slot==3u && a6400_ui_v4_v1.album_count>0u) bg=h_orange;
        else if(details_page && ((slot==0u && menu->settings.grain>0) || (slot==1u && menu->settings.halation))) bg=h_ember;

        record("ROW_PANEL_GEOMETRY",(int)slot,used);
        place(tile_panel(slot),0,x,y,w,h);
        paint(tile_panel(slot),bg);

        int lx=x+12,ly=y+10,lw=w-24,lh=h-20;
        if(menu->page==FILM_MENU_HOME) {
            /* Top-left 8x8 Swiss square pip + top-right index sub_label + bottom geometric bar */
            const unsigned char *pip=h_muted;
            if(slot==0u) pip=menu->settings.enabled?h_white:h_muted;
            else if(slot==1u) pip=stock_palettes[(menu->settings.stock>=0 && menu->settings.stock<10)?menu->settings.stock:0][0];
            else if(slot==2u) pip=a6400_ui_hires_v1?h_orange:h_silver;
            else if(slot==3u) pip=menu->settings.halation?h_orange:h_silver;
            else if(slot==4u) pip=
#ifdef A6400_CHARO_SHUTTER
                ui_theme.shutter_sound_enabled?h_sky:h_muted;
#else
                h_orange;
#endif
            else pip=v4_on_cnt?h_orange:h_muted;
            place(tile_accent(slot),0,x+14,y+16,8,8);
            paint(tile_accent(slot),pip);
            place(ui_theme.sub_labels[slot],1,x+28,y+10,w-42,24);
            write_lbl(ui_theme.sub_labels[slot],home_subs[slot]);
            /* Bottom 152px Swiss geometric indicator bar */
            if(slot==0u) {
                draw_swiss_bar3(slot,x+13,y+126,48,8,4,menu->settings.enabled?3:0,h_white);
            } else if(slot==1u) {
                draw_film_swatch(slot,x+13,y+126,menu->settings.stock,1);
            } else if(slot==2u) {
                draw_swiss_bar3(slot,x+13,y+126,48,8,4,(int)a6400_ui_hires_v1+1,h_orange);
            } else if(slot==3u) {
                draw_swiss_bar3(slot,x+13,y+126,48,8,4,menu->settings.grain,menu->settings.halation?h_orange:h_silver);
            } else if(slot==4u) {
#ifdef A6400_CHARO_SHUTTER
                draw_swiss_bar3(slot,x+13,y+126,48,8,4,ui_theme.shutter_sound_enabled?3:0,h_sky);
#else
                draw_v4_segments(slot,x+13,y+126);
#endif
            } else {
                draw_v4_segments(slot,x+13,y+126);
            }
            lx=x+12;ly=y+64;lw=w-24;lh=38;
        } else if(stocks_page) {
            int is_cur=((int)slot==menu->settings.stock);
            place(tile_accent(slot),0,x,y,is_cur?6:3,h);
            paint(tile_accent(slot),is_cur?h_orange:h_muted);
            draw_film_swatch(slot,x+14,y+12,(int)slot,0);
            lx=x+46;ly=y+11;lw=w-58;lh=32;
        } else if(details_page) {
            place(tile_accent(slot),0,x,y,4,h);
            paint(tile_accent(slot),(slot<2u)?h_orange:h_silver);
            if(slot==0u) {
                draw_swiss_bar3(slot,x+22,y+40,28,12,8,menu->settings.grain,h_orange);
                lx=x+130;ly=y+28;lw=w-146;lh=36;
            } else if(slot==1u) {
                draw_swiss_toggle(slot,x+22,y+33,menu->settings.halation,h_orange);
                lx=x+90;ly=y+28;lw=w-106;lh=36;
            } else {
                clear_chips(slot);
                lx=x+24;ly=y+28;lw=w-40;lh=36;
            }
        } else if(v4_page) {
            int is_on=(slot==0u && a6400_ui_v4_v1.preview_popup_en) ||
                      (slot==1u && a6400_ui_v4_v1.album_preview_en) ||
                      (slot==2u && a6400_ui_v4_v1.native_touch_ok && a6400_ui_v4_v1.native_touch_en) ||
                      (slot==3u && a6400_ui_v4_v1.album_count>0u);
            place(tile_accent(slot),0,x,y,4,h);
            paint(tile_accent(slot),(slot==3u && is_on)?h_white:(is_on?h_orange:h_muted));
            place(ui_theme.sub_labels[slot],1,x+84,y+16,w-100,24);
            write_lbl(ui_theme.sub_labels[slot],v4_subs[slot]);
            if(slot==0u) draw_swiss_toggle(slot,x+18,y+20,a6400_ui_v4_v1.preview_popup_en!=0u,h_orange);
            else if(slot==1u) draw_swiss_toggle(slot,x+18,y+20,a6400_ui_v4_v1.album_preview_en!=0u,h_orange);
            else if(slot==2u) draw_swiss_toggle(slot,x+18,y+20,
                                                (a6400_ui_v4_v1.native_touch_ok && a6400_ui_v4_v1.native_touch_en),
                                                h_sky);
            else draw_gallery_icon(slot,x+18,y+18,a6400_ui_v4_v1.album_count>0u);
            lx=x+16;ly=y+84;lw=w-30;lh=38;
        }
        place(tile_label(slot),1,lx,ly,lw,lh);
        write_lbl(tile_label(slot),tbuf);
    }

    /* 4. Bottom-Right Persistent Quick Album Action Button */
    place(ui_theme.page_btn,0,420,411,176,38);
    paint(ui_theme.page_btn,
          menu->selected==FILM_HIT_ALBUM_QUICK?h_orange_bright:
          (a6400_ui_v4_v1.album_count>0u?h_dock_active:h_dock_idle));
    place(ui_theme.page_btn_accent,0,420,411,4,38);
    paint(ui_theme.page_btn_accent,a6400_ui_v4_v1.album_count>0u?h_orange:h_silver);
    snprintf(ui.texts[7],sizeof(ui.texts[7]),UI_S_QUICK_ALBUM,(unsigned)a6400_ui_v4_v1.album_count);
    place(ui.labels[7],1,430,416,156,28);
    write_lbl(ui.labels[7],ui.texts[7]);
    record("FILM_DRAW",menu->page,menu->selected);
}
static void settings(void *unused,const struct film_settings *s) {
    (void)unused;
    /* The persistent reader samples ui.view.menu.settings directly. Keep the
     * callback native and allocation-free; photo work stays in the daemon. */
    record("FILM_SETTINGS_SNAPSHOT",s->enabled,s->stock);
}
static void stop_trial(const char *why) {
    record("STOP_BEGIN",0,0);
    v4_dismiss_popup();
    v4_exit_album();
    film_native_view_hide(&ui.view);
    record("STOP_MENU_HIDDEN",0,0);
    if(ui.layer)ui.layer_active(ui.layer,0);
    ui_toast.visible=0;
#ifdef A6400_CHARO_SHUTTER
    ui_theme.shutter_pending=0;
#endif
    ui.running=0;
    input_snapshot(7);
    /* setAnimate only changes the flag/counter; retain allocated native nodes. */
    record("STOP_TICKER_BEFORE",0,0);
    if(ui.ticker)ui.set_animate(ui.ticker,0);
    record("STOP_LAYER_BEFORE",0,0);
    if(ui.ticker_layer)ui.layer_active(ui.ticker_layer,0);
    input_snapshot(8);
    record(why,(int)ui.ticks,0);
}
int a6400_ui_stop_v2(void) {
    if(!ui.attached || (uint32_t)syscall(SYS_gettid)!=ui.tid)return -1;
    stop_trial("FILM_EXPLICIT_STOP");return 0;
}
static int stop_old_v1(void) {
    void *old=dlopen("/tmp_bt/a6400-film-ui-v1.so",RTLD_NOW|RTLD_NOLOAD);
    if(!old)return 0;
    uintptr_t entry=(uintptr_t)dlsym(old,"a6400_ui_attach_v1");
    if(!entry){dlclose(old);return -1;}
    uintptr_t base=entry-0x12e0u;
    if(base!=0x10a73000u){dlclose(old);return -2;}
    /* This cleanup is solely for the hash-pinned v1 already observed in RAM.
     * Validate its owned state/vtable before calling native setters on it. */
    uintptr_t *old_ui=(uintptr_t *)(base+0x14088u);
    void *ticker=(void *)old_ui[0x70/4],*layer=(void *)old_ui[0x6c/4];
    if(old_ui[0x68/4]!=(uintptr_t)ui.system ||
       old_ui[0xb8/4]!=ui.tid || old_ui[0x170/4]!=1 || !ticker || !layer ||
       *(uintptr_t *)ticker!=(uintptr_t)old_ui+0xd0u ||
       old_ui[(0xd0+32*4)/4]!=base+0xaa4u) {
        dlclose(old);return -2;
    }
    old_ui[0x174/4]=0;
    ui.set_animate(ticker,0);ui.layer_active(layer,0);
    record("FILM_OLD_V1_STOPPED",(int)(uintptr_t)ticker,0);
    dlclose(old);return 0;
}
static void animate(void *self) {
    if(!ui.running)return;
    if(self!=ui.ticker || (uint32_t)syscall(SYS_gettid)!=ui.tid) {
        ui.running=0;record("FILM_BAD_TICK_CONTEXT",0,0);return;
    }
    if(a6400_ui_stop_requested_v1==1u) {
        stop_trial("FILM_STOP_REQUESTED");
        __sync_synchronize();a6400_ui_stop_requested_v1=2u;return;
    }
    uint32_t now=now_ms();
    if(!now){stop_trial("FILM_CLOCK_ERROR");return;}
    if(!ui.started) {ui.started=now;record("FILM_TICK_READY",(int)(uintptr_t)self,0);}
    ++ui.ticks;
#if TRIAL_MS != 0
    if(now-ui.started>=TRIAL_MS) {
        stop_trial("FILM_TRIAL_STOPPED");return;
    }
#endif
    if(ui.opened_ms && now-ui.opened_ms>=A6400_UI_DISPLAY_MS) {
#if defined(A6400_UI_COMBO_DIAGNOSTIC) || (TRIAL_MS == 0 && !defined(A6400_UI_RENDER_DIAGNOSTIC))
        /* Hide only: the separate active ticker still watches the chord. */
        film_native_view_hide(&ui.view);
        record("COMBO_AUTO_HIDE",0,0);return;
#else
        stop_trial("TRIAL_AUTO_HIDE");return;
#endif
    }
#if !defined(A6400_UI_COMBO_DIAGNOSTIC) && !(TRIAL_MS == 0 && !defined(A6400_UI_RENDER_DIAGNOSTIC))
    if(ui.ever_opened && !ui.view.menu.visible) {
        stop_trial("TRIAL_CLOSE_DONE");return;
    }
#endif
    if(now-ui.last_tick<15u)return;
    ui.last_tick=now;
    if(ui_toast.bg && ui.layer) {
        if(ui.view.menu.visible || ui_v4.album_mode) {
            ui_toast.visible=0;
        } else {
            uint32_t seq=a6400_ui_toast_v1.seq;
            if(seq!=ui_toast.seq && a6400_ui_toast_v1.magic==0x54534f54u) {
                for(unsigned i=0;i<63;i++)ui_toast.text[i]=a6400_ui_toast_v1.text[i];
                ui_toast.text[63]=0;
                uint32_t kind=a6400_ui_toast_v1.kind;
                uint32_t dur=a6400_ui_toast_v1.duration_ms;
                ui_toast.seq=seq;
                if(ui_toast.text[0]) {
                    static const unsigned char bg_c[4]={18,22,29,235};
                    static const unsigned char k1_c[4]={234,158,28,255};
                    static const unsigned char k2_c[4]={46,196,96,255};
                    static const unsigned char k3_c[4]={220,62,62,255};
                    hide_menu_widgets();
                    place(ui_toast.bg,0,24,390,480,36);
                    place(ui_toast.accent,0,24,390,8,36);
                    place(ui_toast.label,1,40,394,456,28);
                    ui.set_color(ui_toast.bg,bg_c);
                    ui.set_color(ui_toast.accent,kind==2?k2_c:kind==3?k3_c:k1_c);
                    ui.set_text(ui_toast.label,ui_toast.text);
                    ui.move_last(ui.layer);
                    ui.layer_active(ui.layer,1);
                    ui_toast.visible=1;
                    ui_toast.until_ms=now+(dur?dur:3000u);
                } else if(ui_toast.visible) {
                    ui.layer_active(ui.layer,0);
                    ui_toast.visible=0;
                }
            }
            if(ui_toast.visible && (int32_t)(now-ui_toast.until_ms)>=0) {
                ui.layer_active(ui.layer,0);
                ui_toast.visible=0;
            }
        }
    }
#ifdef A6400_UI_RENDER_DIAGNOSTIC
    /* Isolate rendering from the entire input state machine. A simulated
     * chord can lose its release latch after a startup dispatch gap and
     * therefore never open. No synthetic events reach Sony. */
    if(!ui.ever_opened && now-ui.started>=1000u) {
        record("RENDER_DIAGNOSTIC_BEGIN",(int)(now-ui.started),0);
        ui.view.menu.visible=1;
        draw(0,&ui.view.menu);
        visibility(0,1);
    }
#ifdef A6400_UI_TOUCH_CLOSE_DIAGNOSTIC
    /* Read-only native getters: no capture, focus, mode or key setters.
     * The enum is 5=pressed, 6=released. Other rows remain inert in this
     * diagnostic; only a complete Close gesture can stop our owned UI. */
    if(ui.view.menu.visible) {
        short x=32767,y=32767;
        ui.mouse_position(ui.system,0,&x,&y);
        int state=ui.mouse_state(ui.system,0,0);
        if(state!=ui.last_touch) {
            record(state==5?"CLOSE_TOUCH_DOWN":state==6?"CLOSE_TOUCH_UP":"CLOSE_TOUCH_INVALID",x,y);
            ui.last_touch=state;
        }
        if(state==5 && film_menu_hit(ui.view.menu.page,x,y)!=2)x=y=32767;
        unsigned change=film_native_view_step(&ui.view,now,0,0,0,state,x,y);
        if(change&FILM_MENU_CLOSED) {
            stop_trial("TRIAL_TOUCH_CLOSE");return;
        }
    }
#endif
    return;
#endif
    if(!get_key_instance_fn || get_key_instance_fn()!=ui.keys){stop_trial("FILM_KEY_INSTANCE_CHANGED");return;}
    int menu=ui.key(ui.keys,0x1ab,0),zoom=ui.key(ui.keys,0x70dc,0),other=0;
    static const int dismiss_keys[]={0x3,0x15,0x30,0x66,0xa7,0x1ac,0x1af,
        0x1f0,0x1f4,0x1f7,0x1f8,0x1f9,0x7095,0x70a9,0x70aa,0x70ab,
        0x70ad,0x70c4,0x10000000,0x10000001,0x10000003,0x10000004};
    for(unsigned i=0;i<sizeof(dismiss_keys)/sizeof(*dismiss_keys);i++)
        if(ui.key(ui.keys,dismiss_keys[i],0)==1)other=1;
    short x=32767,y=32767;
    ui.mouse_position(ui.system,0,&x,&y);
    int touch=ui.mouse_state(ui.system,0,0);
#ifdef A6400_UI_COMBO_DIAGNOSTIC
    /* Like the validated Close-only round, this does not consume native keys
     * or touch. Observed physical actions dismiss our layer; native redraw
     * ordering is not guaranteed because Sony still receives those events.
     * Settings are enabled only in the separate RAM-settings diagnostic. */
    if(ui.view.menu.have_sample && now-ui.view.menu.last_sample>250u)
        record("COMBO_GAP",(int)(now-ui.view.menu.last_sample),
            ui.view.menu.released | (ui.view.menu.pending<<1) |
            (ui.view.menu.fired<<2) | ((menu==1)<<3) |
            ((zoom==1)<<4) | ((other!=0)<<5));
    if(menu!=ui.last_menu || zoom!=ui.last_zoom || other!=ui.last_other) {
        record("COMBO_KEYS",menu,zoom);
        if(other)record("COMBO_OTHER",other,0);
        ui.last_menu=menu;ui.last_zoom=zoom;ui.last_other=other;
    }
#ifndef A6400_UI_SETTINGS_DIAGNOSTIC
    if(touch==5 && film_menu_hit(ui.view.menu.page,x,y)!=2)x=y=32767;
#endif
#endif
    int v4_busy_before=(ui.view.menu.visible || ui_v4.album_mode || ui_v4.popup_active);
    unsigned change=film_native_view_step(&ui.view,now,menu,zoom,other,touch,x,y);
    if(change&FILM_MENU_SETTINGS_CHANGED)record("FILM_TAP",x,y);
    if(change&FILM_MENU_HIRES_TOGGLED) {
        a6400_ui_hires_v1=(a6400_ui_hires_v1+1u)%3u;
        draw(0,&ui.view.menu);
        record("FILM_HIRES_TOGGLE",(int)a6400_ui_hires_v1,0);
    }
    if(change&FILM_MENU_POPUP_TOGGLED) {
        a6400_ui_v4_v1.preview_popup_en^=1u;
        if(!a6400_ui_v4_v1.preview_popup_en)v4_dismiss_popup();
        draw(0,&ui.view.menu);
        record("V4_POPUP_TOGGLE",(int)a6400_ui_v4_v1.preview_popup_en,0);
    }
    if(change&FILM_MENU_ALBUM_TOGGLED) {
        a6400_ui_v4_v1.album_preview_en^=1u;
        if(!a6400_ui_v4_v1.album_preview_en && ui_v4.album_mode==1)v4_exit_album();
        draw(0,&ui.view.menu);
        record("V4_ALBUM_TOGGLE",(int)a6400_ui_v4_v1.album_preview_en,0);
    }
    if(change&FILM_MENU_TOUCH_TOGGLED) {
        if(a6400_ui_v4_v1.native_touch_ok)a6400_ui_v4_v1.native_touch_en^=1u;
        draw(0,&ui.view.menu);
        record("V4_TOUCH_TOGGLE",(int)a6400_ui_v4_v1.native_touch_en,(int)a6400_ui_v4_v1.native_touch_ok);
    }
    if(change&FILM_MENU_ALBUM_BROWSE) {
        if(a6400_ui_v4_v1.album_count>0u) {
            film_native_view_hide(&ui.view);
            v4_dismiss_popup();
            ui_v4.album_mode=2;
            ui_v4.album_compare_orig=0;
            ui_v4.album_idx=0u;
            ui_v4.album_touch_down=0;
            v4_album_sync_view();
            record("V4_ALBUM_BROWSE_OPEN",(int)a6400_ui_v4_v1.album_count,0);
        } else {
            draw(0,&ui.view.menu);
        }
    }
    /* V4.0 Poll #5: Instant filter preview pop-up card after shot finishes. */
    if(a6400_ui_v4_v1.magic==A6400_V4_MAGIC && a6400_ui_v4_v1.latest_seq!=ui_v4.seen_latest_seq) {
        ui_v4.seen_latest_seq=a6400_ui_v4_v1.latest_seq;
        if(a6400_ui_v4_v1.preview_popup_en && !ui.view.menu.visible && !ui_v4.album_mode) {
            ui_v4.popup_active=1;
            ui_v4.popup_until_ms=now+A6400_POPUP_SHOW_MS;
            v4_publish_display(A6400_DISP_POPUP,a6400_ui_v4_v1.album_head);
            record("V4_POPUP_SHOW",(int)ui_v4.seen_latest_seq,0);
        }
    }
    if(ui_v4.popup_active) {
        if((int32_t)(now-ui_v4.popup_until_ms)>=0 || touch==5 || other || menu==1 ||
           ui.view.menu.visible || ui_v4.album_mode) {
            v4_dismiss_popup();
            record("V4_POPUP_HIDE",0,0);
        }
    }
    /* V4.0 Poll #2: In-camera graded album preview (Playback auto-sync + direct browser). */
    int pb_now=(ui_v4.get_pb_base && ui_v4.get_pb_base()!=0u)?1:0;
    if(pb_now && !ui_v4.last_pb_active) {
        if(a6400_ui_v4_v1.album_preview_en && !ui.view.menu.visible &&
           a6400_ui_v4_v1.album_count>0u && a6400_ui_v4_v1.session_shots>0u) {
            v4_dismiss_popup();
            ui_v4.album_mode=1;
            ui_v4.album_compare_orig=0;
            ui_v4.album_idx=0u;
            ui_v4.album_touch_down=0;
            v4_album_sync_view();
            record("V4_PB_ALBUM_ENTER",(int)a6400_ui_v4_v1.session_shots,(int)a6400_ui_v4_v1.album_count);
        }
    } else if(!pb_now && ui_v4.last_pb_active && ui_v4.album_mode==1) {
        v4_exit_album();
    }
    ui_v4.last_pb_active=pb_now;
    if(ui_v4.album_mode && !ui.view.menu.visible) {
        int nav_left=(ui.key(ui.keys,A6400_KEY_LEFT,0)==1 || ui.key(ui.keys,A6400_KEY_WHEEL_CCW,0)==1);
        int nav_right=(ui.key(ui.keys,A6400_KEY_RIGHT,0)==1 || ui.key(ui.keys,A6400_KEY_WHEEL_CW,0)==1);
        int nav_center=(ui.key(ui.keys,A6400_KEY_CENTER,0)==1);
        int nav_exit=(menu==1 || ui.key(ui.keys,A6400_KEY_SHUTTER,0)==1 ||
                      ui.key(ui.keys,A6400_KEY_TRASH,0)==1 ||
                      (ui_v4.album_mode==2 && ui.key(ui.keys,A6400_KEY_PLAY,0)==1));
        int dirty=0;
        if(nav_exit && !ui_v4.last_nav_exit) {
            v4_exit_album();
        } else {
            if(nav_left && !ui_v4.last_nav_left) {
                unsigned limit=(ui_v4.album_mode==1)?a6400_ui_v4_v1.session_shots:a6400_ui_v4_v1.album_count;
                if(ui_v4.album_mode==1 && limit>A6400_SESSION_SHOTS_CAP)limit=A6400_SESSION_SHOTS_CAP;
                if(ui_v4.album_mode==2 && limit>A6400_ALBUM_SLOTS)limit=A6400_ALBUM_SLOTS;
                if(limit>0u && ui_v4.album_idx+1u<limit) {
                    ui_v4.album_idx+=1u;
                    ui_v4.album_compare_orig=0;
                    dirty=1;
                }
            }
            if(nav_right && !ui_v4.last_nav_right) {
                if(ui_v4.album_idx>0u) {
                    ui_v4.album_idx-=1u;
                    ui_v4.album_compare_orig=0;
                    dirty=1;
                }
            }
            if(nav_center && !ui_v4.last_nav_center && ui_v4.album_mode==1) {
                ui_v4.album_compare_orig^=1;
                dirty=1;
            }
            if(touch==5) {
                if(!ui_v4.album_touch_down && x>=0 && x<640 && y>=0 && y<480) {
                    ui_v4.album_touch_down=1;
                    ui_v4.album_touch_x=x;
                    ui_v4.album_touch_y=y;
                }
            } else if(touch==6 && ui_v4.album_touch_down) {
                short tx=ui_v4.album_touch_x;
                ui_v4.album_touch_down=0;
                if(ui_v4.album_mode==1) {
                    ui_v4.album_compare_orig^=1;
                    dirty=1;
                } else {
                    unsigned limit=a6400_ui_v4_v1.album_count;
                    if(limit>A6400_ALBUM_SLOTS)limit=A6400_ALBUM_SLOTS;
                    if(tx<200) {
                        if(limit>0u && ui_v4.album_idx+1u<limit){ui_v4.album_idx+=1u;dirty=1;}
                    } else if(tx>440) {
                        if(ui_v4.album_idx>0u){ui_v4.album_idx-=1u;dirty=1;}
                    } else {
                        v4_exit_album();
                    }
                }
            }
            if(dirty && ui_v4.album_mode)v4_album_sync_view();
        }
        ui_v4.last_nav_left=nav_left;
        ui_v4.last_nav_right=nav_right;
        ui_v4.last_nav_center=nav_center;
        ui_v4.last_nav_exit=nav_exit;
    } else {
        ui_v4.last_nav_left=ui_v4.last_nav_right=ui_v4.last_nav_center=ui_v4.last_nav_exit=0;
        ui_v4.album_touch_down=0;
    }
    /* V4.0 Native MENU Full Touch (MTF2, credit: ivica00). Suppressed whenever our
     * Film Menu, Album Preview, or Instant Pop-up is active (or until the finger
     * lifts after closing one of them), or when toggled OFF. */
    int v4_busy_now=(ui.view.menu.visible || ui_v4.album_mode || ui_v4.popup_active);
    if(v4_busy_now) {
        ui_v4.mtf2_wait_release=1;
    } else if(v4_busy_before) {
        ui_v4.mtf2_wait_release=(touch==5)?1:0;
    } else if(ui_v4.mtf2_wait_release && touch==6) {
        ui_v4.mtf2_wait_release=0;
    }
    int allow_mtf2=(a6400_ui_v4_v1.native_touch_en!=0u) &&
                   !v4_busy_before && !v4_busy_now && !ui_v4.mtf2_wait_release;
    if(mtf2_on_touch_poll(&ui_v4.mtf2,allow_mtf2,touch,x,y)!=MTF2_ACT_NONE) {
        a6400_ui_v4_v1.native_touch_hits=ui_v4.mtf2.hits;
        record("MTF2_ACT",(int)ui_v4.mtf2.last_action,(int)ui_v4.mtf2.hits);
    }
#ifdef A6400_CHARO_SHUTTER
    if(change&FILM_MENU_SOUND_TOGGLED) {
#if defined(A6400_CHARO_MULTI) && A6400_CHARO_MULTI
        ui_theme.shutter_sound_enabled=(ui_theme.shutter_sound_enabled+1)&3;
#else
        ui_theme.shutter_sound_enabled^=1;
#endif
        if(!ui_theme.shutter_sound_enabled)ui_theme.shutter_pending=0;
        draw(0,&ui.view.menu);
        record("CHARO_SOUND_TOGGLE",ui_theme.shutter_sound_enabled,0);
        int preview_id=charo_sound_id(ui_theme.shutter_sound_enabled);
        if(preview_id && ui_theme.beep_play) {
            ui_theme.last_shutter_beep_ms=now;
            ui_theme.beep_play(ui.background,preview_id);
            record("CHARO_MENU_BEEP_PREVIEW",preview_id,(int)now);
        }
    }
    int shutter_down=ui.key(ui.keys,A6400_CHARO_SHUTTER_KEY,0);
    if(shutter_down==1 && ui_theme.last_shutter_key==0 && ui_theme.shutter_sound_enabled) {
        ui_theme.shutter_pending=1;
        ui_theme.shutter_arm_ms=now;
        record("CHARO_SHUTTER_ARMED",charo_sound_id(ui_theme.shutter_sound_enabled),(int)now);
    }
    ui_theme.last_shutter_key=(shutter_down==1)?1:0;
    if(ui_theme.shutter_pending &&
       ui_theme.shutter_sound_enabled &&
       ui_theme.beep_play
#if A6400_CHARO_DELAY_MS > 0
       && (uint32_t)(now-ui_theme.shutter_arm_ms)>=A6400_CHARO_DELAY_MS
#endif
       ) {
        ui_theme.shutter_pending=0;
        int active_id=charo_sound_id(ui_theme.shutter_sound_enabled);
        if(active_id &&
           (!ui_theme.last_shutter_beep_ms ||
            (uint32_t)(now-ui_theme.last_shutter_beep_ms)>=A6400_CHARO_COOLDOWN_MS)) {
            ui_theme.last_shutter_beep_ms=now;
            ui_theme.beep_play(ui.background,active_id);
            record("CHARO_SHUTTER_BEEP",active_id,(int)now);
        }
    }
#endif
    if(ui.view.menu.visible && a6400_ui_status_v1.magic==0x53544156u &&
       a6400_ui_status_v1.seq!=ui_theme.status_seq) {
        draw(0,&ui.view.menu);
    }
    if(ui.ticks%300u==0)record("FILM_ALIVE",(int)ui.ticks,ui.view.menu.visible);
}
static void *make(int label) {
    void *p=calloc(1,label?0x400u:0x200u);
    if(p){
        record(label?"LABEL_CTOR_BEFORE":"PANEL_CTOR_BEFORE",0,0);
        (label?ui.label_ctor:ui.panel_ctor)(p);
        if(label){
            /* The first five IDs are static firmware resources. The native
             * Label's sixth entry was 0x0100061d, a runtime override from a
             * previous boot: it is absent now and the original font resolver
             * rejects it. Retain the constructor's unset sixth entry. */
            static const uint32_t attributes[6]={0x02076000,0x0207a00f,
                0x0207a000,0x02066027,0x02076002,0xffffffff};
            memcpy((unsigned char *)p+0x2b4,attributes,sizeof(attributes));
            record("LABEL_FONT_BEFORE",(int)(uintptr_t)p,0);
            ui.set_font(p,attributes[0]);
            record("LABEL_FONT_AFTER",*(int *)((unsigned char *)p+0x1f4),0);
            if(!*(void **)((unsigned char *)p+0x1f4))return 0;
        }
        record("WIDGET_LAYER_BEFORE",label,0);
        ui.set_layer(p,&ui.layer_id);
        record("WIDGET_LAYER_AFTER",label,0);
    }
    return p;
}
int a6400_ui_attach_v1(void *system,unsigned tid) {
    record("FILM_ATTACH_BEGIN",(int)tid,(int)(uintptr_t)system);
    if(ui.attached || !system || (uintptr_t)system<0x10000u || tid!=(uint32_t)syscall(SYS_gettid))return -1;
    if(resolve())return -2;
    ui.system=system;ui.tid=tid;ui.keys=get_key_instance_fn();
    if(!ui.keys || (uintptr_t)ui.keys<0x10000u || ui.key(ui.keys,0x1ab,0)<0 || ui.key(ui.keys,0x70dc,0)<0)return -3;
    if(stop_old_v1())return -7;
    input_snapshot(0);
    struct film_settings initial={0,0,2,1};
    const struct film_native_view_ops ops={visibility,draw,settings};
    film_native_view_init(&ui.view,&ops,0,&initial);
#if defined(A6400_UI_COMBO_DIAGNOSTIC)
    ui.layer_id=ui.make_id("A6400_FILM_COMBO_V3" A6400_UI_INSTANCE_SUFFIX);
#elif defined(A6400_UI_TOUCH_CLOSE_DIAGNOSTIC)
    ui.layer_id=ui.make_id("A6400_FILM_CLOSE_TOUCH_V3");
#elif defined(A6400_INPUT_DIAGNOSTIC) && A6400_INPUT_DIAGNOSTIC == 2
    ui.layer_id=ui.make_id("A6400_FILM_INPUT2_V3");
#elif defined(A6400_INPUT_DIAGNOSTIC)
    ui.layer_id=ui.make_id("A6400_FILM_INPUT1_V3");
#elif defined(A6400_UI_RENDER_DIAGNOSTIC)
    ui.layer_id=ui.make_id("A6400_FILM_NATIVE_FONTFIX3_V3");
#else
    ui.layer_id=ui.make_id("A6400_FILM_NATIVE_TRIAL_V3");
#endif
    record("CREATE_LAYER_BEFORE",0,0);
    ui.layer=ui.create_layer(system,&ui.layer_id);
    if(!ui.layer)return -4;
    ui.layer_active(ui.layer,0);ui.layer_clear(ui.layer,1);
    record("FILM_LAYER_CREATED",(int)(uintptr_t)ui.layer,0);
    /* Z-order: backdrop -> Swiss frame/hairlines -> tabs -> tile bgs -> accents/chips -> dock -> labels -> toast */
    ui.background=make(0);
    if(!ui.background)return -5;
    ui_theme.card_border=make(0);
    ui_theme.card_panel=make(0);
    ui_theme.header_accent=make(0);
    ui_theme.header_divider=make(0);
    ui_theme.footer_divider=make(0);
    ui_theme.status_dot=make(0);
    ui_theme.grid_h=make(0);
    ui_theme.grid_v1=make(0);
    ui_theme.grid_v2=make(0);
    ui_theme.grid_vc=make(0);
    if(!ui_theme.card_border || !ui_theme.card_panel ||
       !ui_theme.header_accent || !ui_theme.header_divider ||
       !ui_theme.footer_divider || !ui_theme.status_dot ||
       !ui_theme.grid_h || !ui_theme.grid_v1 || !ui_theme.grid_v2 || !ui_theme.grid_vc)return -5;
    for(unsigned t=0;t<4u;t++){
        ui_theme.tab_bg[t]=make(0);
        ui_theme.tab_bar[t]=make(0);
        if(!ui_theme.tab_bg[t] || !ui_theme.tab_bar[t])return -5;
    }
    ui_theme.close_bg=make(0);
    if(!ui_theme.close_bg)return -5;
    for(unsigned i=0;i<6u;i++){
        ui.panels[i]=make(0);
        if(!ui.panels[i])return -5;
    }
    for(unsigned i=0;i<4u;i++){
        ui_theme.extra_panels[i]=make(0);
        if(!ui_theme.extra_panels[i])return -5;
    }
    for(unsigned i=0;i<6u;i++){
        ui_theme.row_accents[i]=make(0);
        if(!ui_theme.row_accents[i])return -5;
    }
    for(unsigned i=0;i<4u;i++){
        ui_theme.extra_accents[i]=make(0);
        if(!ui_theme.extra_accents[i])return -5;
    }
    for(unsigned i=0;i<10u;i++){
        for(unsigned k=0;k<3u;k++){
            ui_theme.chips[i][k]=make(0);
            if(!ui_theme.chips[i][k])return -5;
        }
    }
    ui_theme.page_btn=make(0);
    ui_theme.page_btn_accent=make(0);
    if(!ui_theme.page_btn || !ui_theme.page_btn_accent)return -5;
    for(unsigned i=0;i<8u;i++){ui.labels[i]=make(1);if(!ui.labels[i])return -5;}
    for(unsigned i=0;i<4u;i++){ui_theme.extra_labels[i]=make(1);if(!ui_theme.extra_labels[i])return -5;}
    for(unsigned i=0;i<6u;i++){ui_theme.sub_labels[i]=make(1);if(!ui_theme.sub_labels[i])return -5;}
    for(unsigned t=0;t<4u;t++){ui_theme.tab_lbl[t]=make(1);if(!ui_theme.tab_lbl[t])return -5;}
    ui_theme.close_lbl=make(1);
    if(!ui_theme.close_lbl)return -5;
    ui_toast.bg=make(0);
    ui_toast.accent=make(0);
    ui_toast.label=make(1);
    if(ui_toast.bg && ui_toast.accent && ui_toast.label) {
        hide_toast_widgets();
    } else {
        ui_toast.bg=ui_toast.accent=ui_toast.label=0;
    }
    hide_menu_widgets();
#ifdef A6400_CHARO_SHUTTER
    ui_theme.shutter_sound_enabled=1;
    ui_theme.last_shutter_key=0;
    ui_theme.shutter_pending=0;
    ui_theme.shutter_arm_ms=0;
    ui_theme.last_shutter_beep_ms=0;
#endif
    strcpy(ui.texts[0],UI_S_HDR_BOOT);
    for(unsigned i=0;i<A6400_SESSION_SHOTS_CAP;i++)a6400_ui_v4_v1.shot_slots[i]=-1;
    a6400_ui_v4_v1.native_touch_ok=(uint32_t)mtf2_probe_and_init(&ui_v4.mtf2);
    record("V4_MTF2_INIT",(int)a6400_ui_v4_v1.native_touch_ok,(int)(ui_v4.get_pb_base!=0));
#ifdef A6400_UI_SETTINGS_DIAGNOSTIC
    strcpy(ui.texts[7],UI_S_HINT_COMBO_RAM);
#elif defined(A6400_UI_COMBO_DIAGNOSTIC)
    strcpy(ui.texts[7],UI_S_HINT_COMBO);
#elif defined(A6400_UI_TOUCH_CLOSE_DIAGNOSTIC)
    strcpy(ui.texts[7],UI_S_HINT_AUTO20);
#elif defined(A6400_UI_RENDER_DIAGNOSTIC) && A6400_UI_DISPLAY_MS % 1000 == 0
    snprintf(ui.texts[7],sizeof(ui.texts[7]),"%u second display test",A6400_UI_DISPLAY_MS/1000u);
#elif defined(A6400_UI_RENDER_DIAGNOSTIC)
    snprintf(ui.texts[7],sizeof(ui.texts[7]),"%u ms display test",(unsigned)A6400_UI_DISPLAY_MS);
#else
    strcpy(ui.texts[7],UI_S_HINT_COMBO);
#endif
    record("TITLE_TEXT_BEFORE",0,0);
    place(ui.labels[0],1,76,48,488,34);ui.set_text(ui.labels[0],ui.texts[0]);
    place(ui.labels[7],1,76,394,312,28);ui.set_text(ui.labels[7],ui.texts[7]);
    record("FILM_WIDGETS_CREATED",7,8);
    input_snapshot(1);
    ui.ticker=calloc(1,0x200u);
    if(!ui.ticker)return -5;
    ui.widget_ctor(ui.ticker);
    uintptr_t *original=*(uintptr_t **)ui.ticker;
    uintptr_t expected_animate=(uintptr_t)dlsym(RTLD_DEFAULT,"_ZN2ux6wgtsys10WidgetBase9onAnimateEv");
    if(!original || !expected_animate || original[32]!=expected_animate)return -6;
    memcpy(ui.vtable,original-2,sizeof(ui.vtable));
    ui.vtable[34]=(uintptr_t)animate;
    *(uintptr_t **)ui.ticker=ui.vtable+2; /* Clone only our new Widget's table. */
#if defined(A6400_UI_COMBO_DIAGNOSTIC)
    ui.ticker_layer_id=ui.make_id("A6400_FILM_COMBO_TICKER_V3" A6400_UI_INSTANCE_SUFFIX);
#elif defined(A6400_UI_TOUCH_CLOSE_DIAGNOSTIC)
    ui.ticker_layer_id=ui.make_id("A6400_FILM_CLOSE_TOUCH_TICKER_V3");
#elif defined(A6400_INPUT_DIAGNOSTIC) && A6400_INPUT_DIAGNOSTIC == 2
    ui.ticker_layer_id=ui.make_id("A6400_FILM_INPUT2_TICKER_V3");
#elif defined(A6400_INPUT_DIAGNOSTIC)
    ui.ticker_layer_id=ui.make_id("A6400_FILM_INPUT1_TICKER_V3");
#elif defined(A6400_UI_RENDER_DIAGNOSTIC)
    ui.ticker_layer_id=ui.make_id("A6400_FILM_NATIVE_FONTFIX3_TICKER_V3");
#else
    ui.ticker_layer_id=ui.make_id("A6400_FILM_NATIVE_TICKER_V3");
#endif
    ui.ticker_layer=ui.create_layer(system,&ui.ticker_layer_id);
    if(!ui.ticker_layer)return -4;
    ui.layer_clear(ui.ticker_layer,0);
    ui.set_layer(ui.ticker,&ui.ticker_layer_id);
    ui.layer_active(ui.ticker_layer,1);
    ui.attached=ui.running=1;
    ui.set_animate(ui.ticker,1);
    input_snapshot(2);
    record("FILM_ATTACHED_NATIVE",(int)ui.layer_id,(int)(uintptr_t)ui.ticker);
    return 0;
}
