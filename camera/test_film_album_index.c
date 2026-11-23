#include "film_album_index.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    struct film_album_index idx;
    film_album_index_reset(&idx);
    assert(!film_album_name_ok("DSC0001.JPG"));
    assert(!film_album_name_ok("F123456.JPG"));
    assert(!film_album_name_ok("F12345678.JPG"));
    assert(!film_album_name_ok("f1234567.JPG"));
    assert(film_album_name_ok("F1234567.JPG"));
    assert(film_album_index_add(&idx,"F0000002.JPG")==0);
    assert(film_album_index_add(&idx,"F0000001.JPG")==0);
    assert(film_album_index_add(&idx,"F0000002.JPG")==1);
    assert(film_album_index_add(&idx,"nope")<0);
    assert(idx.count==2);
    assert(!strcmp(idx.names[0],"F0000001.JPG"));
    assert(!strcmp(idx.names[1],"F0000002.JPG"));
    assert(film_album_browse_count(0)==0);
    assert(film_album_browse_count(20)==20);
    assert(film_album_browse_count(16)==16);
    assert(film_album_browse_count(1025)==1024);
    /* A pending album flip is decoded before the next DCIM inventory. */
    assert(film_album_preempts_scan(1,1, 2,1, 1,1, 6,1, 1,1, 0,5000,0)==1);
    assert(film_album_preempts_scan(1,1, 1,1, 1,1, 6,1, 1,1, 0,5000,0)==0);
    assert(film_album_preempts_scan(0,1, 2,1, 1,1, 6,1, 1,1, 0,5000,0)==0);
    assert(film_album_preempts_scan(1,0, 2,1, 1,1, 6,1, 1,1, 0,5000,0)==0);
    assert(film_album_preempts_scan(1,1, 2,1, 1,1, 6,0, 3,0, 0,5000,0)==1);
    assert(film_album_preempts_scan(1,1, 2,1, 1,1, 6,1, 1,1, 2,1500,1000)==0);
    assert(film_album_preempts_scan(1,1, 2,1, 1,1, 6,1, 1,1, 2,2500,1000)==1);

    char path[]="album-index-test.idx";
    struct film_album_index loaded;
    film_album_index_reset(&loaded);
    assert(!film_album_index_write(path,&loaded));
    assert(!film_album_index_read(path,&loaded));
    assert(loaded.count==0 && !loaded.truncated);

    unlink(path);
    assert(!film_album_index_write(path,&idx));
    assert(!film_album_index_read(path,&loaded));
    assert(loaded.count==2 && !loaded.truncated);
    assert(!strcmp(loaded.names[0],"F0000001.JPG"));
    char name[13];
    assert(!film_album_index_name_at(path,1,name,sizeof(name)));
    assert(!strcmp(name,"F0000002.JPG"));
    unlink(path);

    film_album_index_reset(&idx);
    char item[13];
    for(unsigned i=0;i<A6400_ALBUM_INDEX_CAP+3u;i++) {
        snprintf(item,sizeof(item),"F%07u.JPG",i);
        int rc=film_album_index_add(&idx,item);
        if(i<A6400_ALBUM_INDEX_CAP)assert(rc==0);
        else assert(rc==2 && idx.truncated);
    }
    assert(idx.count==A6400_ALBUM_INDEX_CAP);
    assert(!strcmp(idx.names[0],"F0000000.JPG"));
    assert(!strcmp(idx.names[A6400_ALBUM_INDEX_CAP-1u],"F0001023.JPG"));
    puts("FILM_ALBUM_INDEX_OK");
    return 0;
}
