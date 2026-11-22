#include "film_native_view.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned now, drawings, visibility_calls, persistence;
static int visible;
static char labels[10][80];
static struct film_settings saved;
static void show(void *p, int active) {
    (void)p; visible=active; ++visibility_calls;
}
static void draw(void *p,const struct film_menu *m) {
    (void)p; ++drawings;
    for(unsigned i=0;i<10;i++) {
        char text[80];int x,y,w,h;
        int used=film_native_view_row(m,i,text,sizeof(text),&x,&y,&w,&h);
        assert(used>=0);
        if(used)assert(x>=40 && x+w<=602 && y>=82 && y+h<=396);
        strcpy(labels[i],used?text:"");
    }
}
static void persist(void *p,const struct film_settings *s) {
    (void)p; saved=*s; ++persistence;
}
static void tick(struct film_native_view *v,int menu,int zoom,int other,int touch,int x,int y) {
    now+=20; film_native_view_step(v,now,menu,zoom,other,touch,x,y);
}
static void chord(struct film_native_view *v) {
    tick(v,0,0,0,6,0,0);
    for(int i=0;i<34;i++) tick(v,1,1,0,6,0,0);
    tick(v,0,0,0,6,0,0);
}
static void tap(struct film_native_view *v,int x,int y) {
    tick(v,0,0,0,5,x,y);
    tick(v,0,0,0,6,32767,32767);
}
int main(void) {
    const struct film_native_view_ops ops={show,draw,persist};
    struct film_native_view v;
    film_native_view_init(&v,&ops,0,0);
    assert(!v.menu.visible && !drawings && !persistence);
    chord(&v);assert(visible && v.menu.visible && visibility_calls==1);
    assert(!strcmp(labels[0],"LUT 關閉"));assert(!strcmp(labels[1],"Portra 400"));
    unsigned previous=drawings;
    for(int i=0;i<20;i++)tick(&v,0,0,0,6,32767,32767);
    assert(drawings==previous); /* Release enum 6 is not truthy/down. */
    tick(&v,0,0,0,5,120,150);assert(!persistence && v.menu.selected==0);
    previous=drawings;
    for(int i=0;i<10;i++)tick(&v,0,0,0,5,120,150);
    assert(drawings==previous); /* No repeated rendering for a held touch. */
    tick(&v,0,0,0,6,32767,32767);
    assert(persistence==1 && saved.enabled==1 && !strcmp(labels[0],"LUT 開啟"));
    tick(&v,0,0,0,6,32767,32767);assert(persistence==1);
    tap(&v,320,150);assert(v.menu.page==FILM_MENU_STOCKS);
    assert(!strcmp(labels[0],"* Portra 400"));
    assert(!strcmp(labels[2],"  Fuji Provia"));
    assert(!strcmp(labels[5],"  Classic Chrome"));
    assert(!strcmp(labels[9],"  Polaroid PX680+"));
    tap(&v,170,170);assert(v.menu.page==FILM_MENU_HOME && v.menu.settings.stock==2);
    assert(persistence==2 && saved.stock==2);
    tap(&v,210,46);assert(v.menu.page==FILM_MENU_STOCKS); /* Top tab 02 膠片 */
    tap(&v,460,235);assert(v.menu.page==FILM_MENU_HOME && v.menu.settings.stock==5);
    assert(persistence==3 && saved.stock==5);
    tick(&v,0,0,1,6,0,0);assert(!visible && !v.menu.visible);
    chord(&v);assert(visible && v.menu.settings.enabled && v.menu.settings.stock==5);
    tap(&v,120,310);assert(v.menu.page==FILM_MENU_DETAILS);
    assert(!strcmp(labels[0],"01 / 顆粒強度: 標準"));
    assert(!strcmp(labels[1],"02 / 底片光暈: 開啟"));
    tap(&v,300,130);assert(v.menu.settings.grain==3 && persistence==4 && saved.grain==3);
    assert(!strcmp(labels[0],"01 / 顆粒強度: 濃厚"));
    tap(&v,300,130);assert(v.menu.settings.grain==0 && persistence==5 && saved.grain==0);
    assert(!strcmp(labels[0],"01 / 顆粒強度: 無 (加速)"));
    tap(&v,300,230);assert(v.menu.settings.halation==0 && persistence==6 && saved.halation==0);
    assert(!strcmp(labels[1],"02 / 底片光暈: 關閉 (加速)"));
    tap(&v,300,340);assert(v.menu.settings.grain==2 && v.menu.settings.halation==1 && persistence==7);
    tap(&v,438,46);assert(v.menu.page==FILM_MENU_V4); /* Top tab 04 V4 */
    assert(!strcmp(labels[0],"即時預覽小窗: 開啟"));
    assert(!strcmp(labels[1],"機內相冊預覽: 開啟"));
    assert(!strcmp(labels[2],"原生MENU觸控: 開啟"));
    tap(&v,324,46);assert(v.menu.page==FILM_MENU_DETAILS);
    tap(&v,96,46);assert(v.menu.page==FILM_MENU_HOME);
    tap(&v,510,310);assert(v.menu.page==FILM_MENU_V4);
    tap(&v,550,46);assert(!visible && !v.menu.visible); /* Top-right close pill */
    chord(&v);assert(visible);
    tick(&v,0,0,0,5,120,150); /* Incomplete gesture followed by invalid input. */
    tick(&v,0,0,0,0,120,150);assert(!visible && persistence==7);
    film_native_view_hide(&v);assert(!visible && persistence==7);
    assert(film_native_view_row(&v.menu,10,labels[0],80,0,0,0,0)==-1);
    puts("NATIVE_FILM_VIEW_OK");
}
