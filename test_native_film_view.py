from pathlib import Path
import ctypes
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent


class UiToastV1(ctypes.Structure):
    _fields_ = [
        ('magic', ctypes.c_uint32),
        ('seq', ctypes.c_uint32),
        ('kind', ctypes.c_uint32),
        ('duration_ms', ctypes.c_uint32),
        ('text', ctypes.c_char * 64),
    ]


class NativeFilmViewTests(unittest.TestCase):
    def test_native_enum_input_render_handoff_and_settings(self):
        self.assertTrue((ROOT/'camera/film_native_view.c').exists(),
                        'Native film view is not implemented')
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory)/'view-tests'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-g', '-O1', '-fsanitize=address,undefined',
                            str(ROOT/'camera/test_film_native_view.c'),
                            str(ROOT/'camera/film_native_view.c'),
                            str(ROOT/'camera/film_menu.c'), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertIn('NATIVE_FILM_VIEW_OK', result.stdout)

    def test_v31_corner_toast_hud_and_1120_abi(self):
        headers = {
            'dlfcn.h': '#define RTLD_DEFAULT ((void *)0)\n#define RTLD_NOW 2\n#define RTLD_NOLOAD 4\nvoid *dlopen(const char *,int);\nvoid *dlsym(void *,const char *);\nint dlclose(void *);\n',
            'stdio.h': '#include <stddef.h>\nint snprintf(char *,size_t,const char *,...);\n',
            'stdlib.h': '#include <stddef.h>\nvoid *calloc(size_t,size_t);\n',
            'string.h': '#include <stddef.h>\nvoid *memcpy(void *,const void *,size_t);\nvoid *memset(void *,int,size_t);\nchar *strcpy(char *,const char *);\n',
            'sys/syscall.h': '#define SYS_gettid 224\n#define SYS_clock_gettime 263\n',
            'time.h': '#define CLOCK_MONOTONIC 1\nstruct timespec {long tv_sec,tv_nsec;};\n',
            'unistd.h': 'long syscall(long,...);\n',
        }
        model_c = r'''
#include <stdarg.h>
#include "native_film_ui_trial3.c"
unsigned model_now_ms=1000;
int model_layer_is_active=0;
char model_toast_label[64];
static unsigned char menu_marker[64],ticker_marker[64];
static int model_key(void *p,int key,void *data) {(void)p;(void)key;(void)data;return 0;}
static void model_position(const void *p,unsigned char n,short *x,short *y) {(void)p;(void)n;*x=32767;*y=32767;}
static int model_state(const void *p,unsigned char a,unsigned char b) {(void)p;(void)a;(void)b;return 6;}
static void model_layer(void *p,int active) {(void)p;model_layer_is_active=active;}
static void model_animation(void *p,int active) {(void)p;(void)active;}
static void model_unary(void *p) {(void)p;}
static void model_place(void *p,const float *xy) {(void)p;(void)xy;}
static void model_size(void *p,float w,float h) __attribute__((pcs("aapcs")));
static void model_size(void *p,float w,float h) {(void)p;(void)w;(void)h;}
static void model_color(void *p,const unsigned char *c) {(void)p;(void)c;}
static void model_text(void *p,const char *s) {
    if(p==ui_toast.label && s)snprintf(model_toast_label,sizeof(model_toast_label),"%s",s);
}
long syscall(long n, ...) {
    if(n==SYS_gettid)return 380;
    if(n==SYS_clock_gettime) {
        va_list ap;
        va_start(ap, n);
        int clk = va_arg(ap, int);
        struct timespec *t = va_arg(ap, struct timespec *);
        va_end(ap);
        if(clk==CLOCK_MONOTONIC && t) {
            t->tv_sec=(long)(model_now_ms/1000u);
            t->tv_nsec=(long)((model_now_ms%1000u)*1000000u);
            return 0;
        }
    }
    return 0;
}
void model_init(void) {
    memset(&ui,0,sizeof(ui));
    memset(&ui_theme,0,sizeof(ui_theme));
    memset(&ui_toast,0,sizeof(ui_toast));
    struct film_settings initial={0,0,2,1};
    const struct film_native_view_ops ops={visibility,draw,settings};
    film_native_view_init(&ui.view,&ops,0,&initial);
    ui.system=UI_SYSTEM;ui.keys=(void *)0x59150;*KEY_INSTANCE_SLOT=ui.keys;
    ui.tid=380;ui.running=ui.attached=1;
    ui.layer=menu_marker;ui.ticker=ui.ticker_layer=ticker_marker;
    ui.key=model_key;ui.mouse_position=model_position;ui.mouse_state=model_state;
    ui.layer_active=model_layer;ui.set_animate=model_animation;ui.move_last=model_unary;
    ui.set_position=model_place;ui.panel_size=ui.label_size=model_size;
    ui.set_color=model_color;ui.set_text=model_text;
    ui_toast.bg=(void *)0x2001;
    ui_toast.accent=(void *)0x2002;
    ui_toast.label=(void *)0x2003;
    model_now_ms=1000;
    model_layer_is_active=0;
    model_toast_label[0]=0;
}
void model_tick(unsigned dt) {
    model_now_ms += dt;
    animate(ui.ticker);
}
int model_layer_active(void) { return model_layer_is_active; }
int model_toast_visible(void) { return ui_toast.visible; }
const char *model_toast_text(void) { return model_toast_label; }
'''
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            inc = d / 'inc'
            (inc / 'sys').mkdir(parents=True)
            for name, content in headers.items():
                (inc / name).write_text(content, encoding='utf-8')
            for name in ('film_menu.h', 'film_native_view.h', 'ui_trial_trace.h'):
                (inc / name).write_text((ROOT / 'camera' / name).read_text(encoding='utf-8'), encoding='utf-8')
            (d / 'stubs.h').write_text(
                'static unsigned char ui_sys_stub[4096];\n'
                'static void *key_slot_stub;\n'
                'static unsigned char trace_stub[4096];\n',
                encoding='utf-8',
            )
            src = (ROOT / 'camera/native_film_ui_trial3.c').read_text(encoding='utf-8')
            src = src.replace('#define UI_SYSTEM ((void *)0x956f0)', '')
            src = src.replace('#define KEY_INSTANCE_SLOT ((void *volatile *)0x0713dad0u)', '')
            src = src.replace('#define TRACE ((volatile struct ui_trial_trace *)0x07143970u)', '')
            body = model_c.replace('#include "native_film_ui_trial3.c"', src)
            preamble = (
                '#include "stubs.h"\n'
                '#define UI_SYSTEM ((void *)ui_sys_stub)\n'
                '#define KEY_INSTANCE_SLOT ((void *volatile *)&key_slot_stub)\n'
                '#define TRACE ((volatile struct ui_trial_trace *)trace_stub)\n'
                '#define _Static_assert(cond, msg)\n'
            )
            (d / 'model.c').write_text(preamble + body, encoding='utf-8')
            so_file = d / 'toast_model.so'
            subprocess.run([
                'cc', '-std=c11', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                '-Wno-unknown-attributes', '-Wno-ignored-attributes', '-Wno-unused-function',
                '-DA6400_UI_TRIAL_MS=0', '-DA6400_UI_DISPLAY_MS=20000',
                '-I', str(inc), '-I', str(d),
                str(d / 'model.c'),
                str(ROOT / 'camera/film_native_view.c'),
                str(ROOT / 'camera/film_menu.c'),
                '-o', str(so_file),
            ], check=True)
            lib = ctypes.CDLL(str(so_file))
            lib.model_init()
            lib.model_toast_text.restype = ctypes.c_char_p

            toast = UiToastV1.in_dll(lib, 'a6400_ui_toast_v1')
            self.assertEqual(toast.magic, 0x54534F54)
            self.assertEqual(lib.model_toast_visible(), 0)

            toast.kind = 1
            toast.duration_ms = 3500
            toast.text = '處理中: portra_400...'.encode('utf-8')
            toast.seq = 1
            lib.model_tick(20)
            self.assertEqual(lib.model_toast_visible(), 1)
            self.assertEqual(lib.model_layer_active(), 1)
            self.assertEqual(lib.model_toast_text().decode('utf-8'), '處理中: portra_400...')

            lib.model_tick(3600)
            self.assertEqual(lib.model_toast_visible(), 0)
            self.assertEqual(lib.model_layer_active(), 0)



if __name__ == '__main__':
    unittest.main(verbosity=2)
