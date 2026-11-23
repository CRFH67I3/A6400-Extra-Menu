#define _POSIX_C_SOURCE 200809L
#include "film_album_index.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int film_album_name_ok(const char *name) {
    if(!name || strlen(name)!=12 || name[0]!='F' || strcmp(name+8,".JPG"))return 0;
    for(unsigned i=1;i<8;i++)if(name[i]<'0'||name[i]>'9')return 0;
    return 1;
}
void film_album_index_reset(struct film_album_index *idx) {
    if(!idx)return;
    idx->count=0;
    idx->truncated=0;
}
int film_album_index_add(struct film_album_index *idx, const char *name) {
    if(!idx || !film_album_name_ok(name))return -1;
    unsigned lo=0,hi=idx->count;
    while(lo<hi) {
        unsigned mid=lo+(hi-lo)/2u;
        int cmp=strcmp(idx->names[mid],name);
        if(!cmp)return 1;
        if(cmp<0)lo=mid+1u;
        else hi=mid;
    }
    if(idx->count>=A6400_ALBUM_INDEX_CAP) {
        idx->truncated=1;
        return 2;
    }
    for(unsigned i=idx->count;i>lo;i--)
        memcpy(idx->names[i],idx->names[i-1u],A6400_ALBUM_NAME_BYTES);
    memcpy(idx->names[lo],name,A6400_ALBUM_NAME_BYTES);
    idx->count++;
    return 0;
}
static int write_all(int fd,const char *text,int count) {
    int written=0;
    while(written<count) {
        ssize_t amount=write(fd,text+written,(size_t)(count-written));
        if(amount<0 && errno==EINTR)continue;
        if(amount<=0)return -1;
        written+=(int)amount;
    }
    return 0;
}
int film_album_index_write(const char *path, const struct film_album_index *idx) {
    char temporary[256];
    if(!path || !path[0] || !idx || strlen(path)>240)return -1;
    int n=snprintf(temporary,sizeof(temporary),"%s.new",path);
    if(n<1 || (unsigned)n>=sizeof(temporary))return -1;
    unlink(temporary);
    int fd=open(temporary,O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0)return -1;
    char line[64];
    int ok=1;
    n=snprintf(line,sizeof(line),"A6400ALBUM1\ncount=%u\ntruncated=%d\n",idx->count,idx->truncated?1:0);
    if(n<1 || write_all(fd,line,n))ok=0;
    for(unsigned i=0;ok && i<idx->count;i++) {
        n=snprintf(line,sizeof(line),"%s\n",idx->names[i]);
        if(n!=13 || write_all(fd,line,n))ok=0;
    }
    if(close(fd))ok=0;
    if(ok && !rename(temporary,path))return 0;
    unlink(temporary);
    return -1;
}
static int header_value(char *line,const char *key,unsigned *out) {
    size_t len=strlen(line);
    if(len && line[len-1]=='\n')line[len-1]=0;
    size_t n=strlen(key);
    if(strncmp(line,key,n) || line[n++]!='=')return -1;
    if(!line[n])return -1;
    unsigned v=0;
    for(const char *p=line+n;*p;p++) {
        if(*p<'0'||*p>'9')return -1;
        if(v>100000u)return -1;
        v=v*10u+(unsigned)(*p-'0');
    }
    *out=v;
    return 0;
}
int film_album_index_read(const char *path, struct film_album_index *idx) {
    if(!path || !idx)return -1;
    film_album_index_reset(idx);
    FILE *fp=fopen(path,"r");
    if(!fp)return -1;
    char line[64];
    if(!fgets(line,sizeof(line),fp) || strcmp(line,"A6400ALBUM1\n")){fclose(fp);return -1;}
    unsigned count=0,truncated=0;
    if(!fgets(line,sizeof(line),fp) || header_value(line,"count",&count) || count>A6400_ALBUM_INDEX_CAP) {
        fclose(fp);return -1;
    }
    if(!fgets(line,sizeof(line),fp) || header_value(line,"truncated",&truncated) || truncated>1u) {
        fclose(fp);return -1;
    }
    for(unsigned i=0;i<count;i++) {
        if(!fgets(line,sizeof(line),fp)){fclose(fp);return -1;}
        size_t n=strlen(line);
        if(n!=13 || line[12]!='\n'){fclose(fp);return -1;}
        line[12]=0;
        if(!film_album_name_ok(line)){fclose(fp);return -1;}
        if(i && strcmp(idx->names[i-1u],line)>=0){fclose(fp);return -1;}
        memcpy(idx->names[i],line,A6400_ALBUM_NAME_BYTES);
    }
    if(fgets(line,sizeof(line),fp)){fclose(fp);return -1;}
    fclose(fp);
    idx->count=count;
    idx->truncated=truncated?1:0;
    return 0;
}
int film_album_index_name_at(const char *path, unsigned index, char *out, unsigned out_cap) {
    static struct film_album_index idx;
    if(!out || out_cap<A6400_ALBUM_NAME_BYTES)return -1;
    if(film_album_index_read(path,&idx) || index>=idx.count)return -1;
    memcpy(out,idx.names[index],A6400_ALBUM_NAME_BYTES);
    return 0;
}