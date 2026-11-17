#ifndef A6400_FILM_HOST_RT_H
#define A6400_FILM_HOST_RT_H
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
static inline long sys_read(int fd,void *p,size_t n) {return ::read(fd,p,n);}
static inline long sys_write(int fd,const void *p,size_t n) {return ::write(fd,p,n);}
static inline int sys_open(const char *p,int flags,int mode) {return ::open(p,flags,mode);}
static inline int sys_close(int fd) {return ::close(fd);}
static inline long sys_lseek(int fd,long off,int origin) {return ::lseek(fd,off,origin);}
static inline int sys_unlink(const char *p) {return ::unlink(p);}
static inline int film_fsync(int fd) {return ::fsync(fd);}
static inline void *xalloc(size_t n) {return ::malloc(n);}
static inline void xfree(void *p,size_t n) {(void)n;::free(p);}
#endif
