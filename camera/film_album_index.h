#ifndef A6400_FILM_ALBUM_INDEX_H
#define A6400_FILM_ALBUM_INDEX_H
/* Filename index for graded JPEGs in A:/A6400/TEST. Names only; no pixels. */
#define A6400_ALBUM_INDEX_CAP 1024u
#define A6400_ALBUM_NAME_BYTES 13u /* "F" + 7 digits + ".JPG" + NUL */

struct film_album_index {
    unsigned count;
    int truncated;
    char names[A6400_ALBUM_INDEX_CAP][A6400_ALBUM_NAME_BYTES];
};

int film_album_name_ok(const char *name);
void film_album_index_reset(struct film_album_index *idx);
/* 0=inserted, 1=duplicate, 2=past the cap (truncated set), -1=rejected name. */
int film_album_index_add(struct film_album_index *idx, const char *name);
int film_album_index_write(const char *path, const struct film_album_index *idx);
int film_album_index_read(const char *path, struct film_album_index *idx);
/* Read one sorted name. 0=ok. */
int film_album_index_name_at(const char *path, unsigned index, char *out, unsigned out_cap);
/* Browse count is the index length, never the old 16-slot ring. */
static inline unsigned film_album_browse_count(unsigned count) {
    return count>A6400_ALBUM_INDEX_CAP?A6400_ALBUM_INDEX_CAP:count;
}
/* 1 = decode this flip before the next DCIM inventory.
 * capture_idle is 0 while a shutter job is queued. A failed decode waits 1000 ms. */
static inline int film_album_preempts_scan(int capture_idle, int viewing,
    unsigned view_seq, unsigned view_slot, unsigned served_seq, unsigned served_slot,
    unsigned count, int scanned, unsigned scan_req, unsigned scan_done,
    unsigned fail_seq, unsigned now, unsigned fail_at) {
    if(!capture_idle || !viewing || !view_seq)return 0;
    if(!scanned || scan_req!=scan_done)return 1;
    if(view_slot>=count)return 0;
    if(view_seq==served_seq && view_slot==served_slot)return 0;
    if(fail_seq==view_seq && (unsigned)(now-fail_at)<1000u)return 0;
    return 1;
}
#endif
