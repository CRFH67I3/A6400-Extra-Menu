/* Author-default JPEG selection, fixed for the camera JOB worker.
 * Selection and 2x box reduction follow DevCrashDay/a6400 ritual_daemon.c,
 * de73e850044da4e5872be1ee93e4dc71684f2851 (SHA256 139ab443...54c22ce).
 * Keep this job's bounded I/O, existing codecs, exclusive output and metadata.
 * No SD mounts, original writes, UI options or runtime quality modes.
 */
#ifndef A6400_AUTHOR_FAST_JPEG_H
#define A6400_AUTHOR_FAST_JPEG_H

#define AUTHOR_SCAN_BYTES 1500000u
#define AUTHOR_RGB_BYTES (24u*1024u*1024u)

static unsigned film_clock_ms() {
#ifdef A6400_FILM_HOST
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC,&value)) return 0;
    return (unsigned)((unsigned long long)value.tv_sec*1000u+
                      (unsigned long)value.tv_nsec/1000000u);
#else
    return now_ms();
#endif
}

/* Find the codestream end, never a byte-pattern match inside APP metadata. */
static unsigned author_jpeg_length(const unsigned char *data,unsigned size,unsigned start) {
    if (start>=size || size-start<5 || data[start]!=0xff || data[start+1]!=0xd8) return 0;
    unsigned at=start+2;
    int scan=0;
    while (at+1<size) {
        if (scan) {
            if (data[at]==0xff && data[at+1]==0xd9) return at+2-start;
            ++at;
        } else {
            if (data[at++]!=0xff) continue;
            while (at<size && data[at]==0xff) ++at;
            if (at>=size) break;
            unsigned marker=data[at++];
            if (marker==0xd9) return at-start;
            if (marker==0xd8 || marker==0 || marker==1 ||
                (marker>=0xd0 && marker<=0xd7)) continue;
            if (size-at<2) break;
            unsigned length=((unsigned)data[at]<<8)|data[at+1];
            if (length<2 || length>size-at) break;
            at+=length;
            if (marker==0xda) scan=1;
        }
    }
    return 0;
}

/* JPEG fast path in the author's code scans only the final 1,500,000 bytes
 * and selects the largest complete nested JPEG above 40,000 bytes. */
static int author_preview(int fd,unsigned *offset,unsigned *length) {
    *offset=0; *length=0;
    long size=sys_lseek(fd,0,2);
    if (size<0 || (unsigned long)size>MAX_INPUT_BYTES) return -1;
    if ((unsigned long)size<AUTHOR_SCAN_BYTES) return 0;
    unsigned char *tail=(unsigned char *)xalloc(AUTHOR_SCAN_BYTES);
    if (!tail) return -1;
    long start=size-(long)AUTHOR_SCAN_BYTES;
    int complete=sys_lseek(fd,start,0)==start &&
        jpeg_read_exact(&fd,source_read,tail,AUTHOR_SCAN_BYTES);
    unsigned best=0,best_length=0;
    if (complete) for (unsigned i=0;i+16<AUTHOR_SCAN_BYTES;++i) {
        if (tail[i]==0xff && tail[i+1]==0xd8 && tail[i+2]==0xff &&
            (tail[i+3]==0xe1 || tail[i+3]==0xe0 || tail[i+3]==0xdb)) {
            unsigned bytes=author_jpeg_length(tail,AUTHOR_SCAN_BYTES,i);
            if (bytes>40000u && bytes>best_length) {
                best=i; best_length=bytes; i+=bytes-1;
            }
        }
    }
    xfree(tail,AUTHOR_SCAN_BYTES);
    if (!complete) return -1;
    if (best_length) {*offset=(unsigned)start+best;*length=best_length;}
    return best_length?1:0;
}

struct author_input {int fd,failed;unsigned left;};
static unsigned char author_read(unsigned char *buffer,unsigned char capacity,
                                 unsigned char *count,void *context) {
    author_input *input=(author_input *)context;
    unsigned wanted=input->left<capacity?input->left:capacity;
    long amount=wanted?sys_read(input->fd,buffer,wanted):0;
    if (amount<0 || (unsigned long)amount>wanted) {
        input->failed=1; *count=0; return PJPG_STREAM_READ_ERROR;
    }
    input->left-=(unsigned)amount; *count=(unsigned char)amount;
    return 0;
}

struct author_image {
    unsigned char *pixels;
    unsigned bytes;
    int width,height,source_width,source_height,reduced;
    author_image():pixels(NULL),bytes(0),width(0),height(0),
        source_width(0),source_height(0),reduced(0) {}
    void clear() {if(pixels) xfree(pixels,bytes);pixels=NULL;bytes=0;}
    ~author_image() {clear();}
};

static inline int author_fast_memory_ok(const char *text,unsigned size,unsigned rgb_bytes,
                                        unsigned strip_bytes,unsigned mask_bytes,
                                        unsigned *available,unsigned *required) {
    const char *names[]={"MemFree:","Buffers:","Cached:","Shmem:"};
    unsigned values[4]={0,0,0,0},seen=0;
    *available=0;*required=0;
    if (rgb_bytes>AUTHOR_RGB_BYTES || strip_bytes>393216u || mask_bytes>1048576u) return 0;
    for (unsigned at=0;at<size;) {
        unsigned end=at;
        while (end<size && text[end]!='\n') ++end;
        if (end==size) return 0;
        for (unsigned key=0;key<4;++key) {
            unsigned n=(unsigned)strlen(names[key]);
            if (n>end-at || strncmp(text+at,names[key],n)) continue;
            if (seen&(1u<<key)) return 0;
            unsigned p=at+n,value=0,digits=0;
            while (p<end && (text[p]==' ' || text[p]=='\t')) ++p;
            while (p<end && text[p]>='0' && text[p]<='9') {
                if (++digits>9) return 0;
                value=value*10u+(unsigned)(text[p++]-'0');
            }
            if (!digits) return 0;
            while (p<end && (text[p]==' ' || text[p]=='\t')) ++p;
            if (end-p<2 || text[p]!='k' || text[p+1]!='B') return 0;
            p+=2;
            while (p<end && (text[p]==' ' || text[p]=='\t')) ++p;
            if (p!=end) return 0;
            values[key]=value;seen|=1u<<key;
        }
        at=end+1;
    }
    if (seen!=15u || values[0]+values[1]+values[2]<values[3]) return 0;
    *available=values[0]+values[1]+values[2]-values[3];
    *required=(rgb_bytes+strip_bytes+mask_bytes+MAX_HEAP_BLOCK+
               MAX_OUTPUT_BYTES+8u*1024u*1024u+1023u)/1024u;
    return *available>=*required;
}

static int author_memory_ready(unsigned rgb_bytes,unsigned strip_bytes,unsigned mask_bytes) {
#ifdef A6400_FILM_HOST
    (void)rgb_bytes;(void)strip_bytes;(void)mask_bytes;
    return 1;
#else
    int fd=sys_open("/proc/meminfo",O_RDONLY|O_NOFOLLOW,0);
    if (fd<0) return 0;
    char buffer[4096];
    long count=sys_read(fd,buffer,sizeof(buffer));
    int closed=sys_close(fd);
    unsigned available=0,required=0;
    int ok=count>0 && (unsigned long)count<sizeof(buffer) && !closed &&
        author_fast_memory_ok(buffer,(unsigned)count,rgb_bytes,strip_bytes,mask_bytes,&available,&required);
    printf("FILM_MEMORY available_kib=%u required_kib=%u rgb_bytes=%u ok=%d\n",
        available,required,rgb_bytes,ok);
    return ok;
#endif
}

static int author_decode(int fd,unsigned offset,unsigned length,int fallback,author_image *image) {
    if (!length || sys_lseek(fd,(long)offset,0)!=(long)offset) return 4;
    author_input input={fd,0,length}; pjpeg_image_info_t info={};
    unsigned status=pjpeg_decode_init(&info,author_read,&input,0);
    if (status || input.failed || info.m_width<1 || info.m_height<1 ||
        (unsigned)info.m_width>MAX_IMAGE_DIMENSION ||
        (unsigned)info.m_height>MAX_IMAGE_DIMENSION ||
        (unsigned long)info.m_width*(unsigned long)info.m_height>MAX_PIXELS ||
        info.m_MCUHeight<1 || info.m_MCUHeight>16 ||
        (info.m_comps!=1 && info.m_comps!=3)) return 4;
    int scale=fallback && info.m_width>2400?2:1;
    int width=info.m_width/scale,height=info.m_height/scale;
    if (width<1 || height<1 ||
        (unsigned long)width*(unsigned long)height>AUTHOR_RGB_BYTES/3u) return 5;
    image->bytes=(unsigned)width*(unsigned)height*3u;
    unsigned strip_bytes=(unsigned)info.m_width*3u*(unsigned)info.m_MCUHeight;
    unsigned mask_bytes=(unsigned)(width/4)*(unsigned)(height/4)*2u;
    if (!author_memory_ready(image->bytes,strip_bytes,mask_bytes)) return 5;
    image->pixels=(unsigned char *)xalloc(image->bytes);
    if (!image->pixels) return 5;
    workspace work;
    work.strip_size=(size_t)info.m_width*3u*(unsigned)info.m_MCUHeight;
    work.strip=(unsigned char *)xalloc(work.strip_size);
    if (!work.strip) {image->clear();return 5;}
    unsigned pitch=(unsigned)info.m_width*3u;
    for (int row=0;row<info.m_MCUSPerCol;++row) {
        if (fill_strip(&info,work.strip) || input.failed) {image->clear();return 8;}
        int first_y=row*info.m_MCUHeight;
        int rows=info.m_height-first_y<info.m_MCUHeight?info.m_height-first_y:info.m_MCUHeight;
        if (scale==1) {
            memcpy(image->pixels+(unsigned long)first_y*pitch,work.strip,(size_t)rows*pitch);
        } else {
            for (int y=0;y+1<rows;y+=2) {
                unsigned char *out=image->pixels+(unsigned long)((first_y+y)/2)*(unsigned)width*3u;
                const unsigned char *a=work.strip+(unsigned)y*pitch,*b=a+pitch;
                for (int x=0;x<width;++x) for (unsigned c=0;c<3;++c) {
                    unsigned at=(unsigned)x*6u+c;
                    out[(unsigned)x*3u+c]=(unsigned char)((a[at]+a[at+3]+b[at]+b[at+3]+2)>>2);
                }
            }
        }
    }
    if (pjpeg_decode_mcu()!=PJPG_NO_MORE_BLOCKS || input.failed) {image->clear();return 8;}
    image->width=width;image->height=height;image->source_width=info.m_width;
    image->source_height=info.m_height;image->reduced=scale==2;
    return 0;
}

static unsigned author_u16(const unsigned char *p,int little) {
    return little?(unsigned)p[0]|((unsigned)p[1]<<8):((unsigned)p[0]<<8)|p[1];
}
static unsigned author_u32(const unsigned char *p,int little) {
    return little?author_u16(p,1)|(author_u16(p+2,1)<<16):
        (author_u16(p,0)<<16)|author_u16(p+2,0);
}
static void author_put(unsigned char *p,unsigned value,unsigned bytes,int little) {
    for (unsigned i=0;i<bytes;++i) p[little?i:bytes-1-i]=(unsigned char)(value>>(i*8u));
}
static unsigned author_ifd(unsigned char *tiff,unsigned size,unsigned at,int little,
                            unsigned width,unsigned height) {
    if (at<8 || at>size || size-at<2) return 0;
    unsigned count=author_u16(tiff+at,little),exif=0;
    if (count>(size-at-2)/12u) return 0;
    for (unsigned i=0;i<count;++i) {
        unsigned char *entry=tiff+at+2+i*12u;
        unsigned tag=author_u16(entry,little),type=author_u16(entry+2,little);
        if (author_u32(entry+4,little)!=1) continue;
        if (tag==34665 && type==4) exif=author_u32(entry+8,little);
        if ((tag==256 || tag==257 || tag==40962 || tag==40963) && (type==3 || type==4))
            author_put(entry+8,(tag==256 || tag==40962)?width:height,type==3?2u:4u,little);
    }
    return exif;
}
static void author_metadata_size(unsigned char *metadata,unsigned size,unsigned width,unsigned height) {
    for (unsigned at=0;at+4<=size;) {
        unsigned length=((unsigned)metadata[at+2]<<8)|metadata[at+3];
        if (length<2 || length>size-at-2) return;
        if (metadata[at]==0xff && metadata[at+1]==0xe1 && length>=16 &&
            metadata[at+4]=='E' && metadata[at+5]=='x' && metadata[at+6]=='i' &&
            metadata[at+7]=='f' && metadata[at+8]==0 && metadata[at+9]==0) {
            unsigned char *tiff=metadata+at+10;
            unsigned available=length-8;
            int little=tiff[0]=='I' && tiff[1]=='I';
            if ((little || (tiff[0]=='M' && tiff[1]=='M')) && author_u16(tiff+2,little)==42) {
                unsigned exif=author_ifd(tiff,available,author_u32(tiff+4,little),little,width,height);
                if (exif) author_ifd(tiff,available,exif,little,width,height);
            }
        }
        at+=length+2;
    }
}

static int render_author_fast(int fd,const char *path,int preset,int grain,int halo,
                              unsigned char *metadata,unsigned metadata_size) {
    unsigned begin=film_clock_ms(),offset=0,length=0;
    int found=author_preview(fd,&offset,&length);
    if (found<0) return 4;
    unsigned selected=film_clock_ms(),attempts=1;
    author_image image;
    int result=found?author_decode(fd,offset,length,0,&image):4;
    if (result) {
        if (found) ++attempts;
        long size=sys_lseek(fd,0,2);
        if (size<4 || (unsigned long)size>MAX_INPUT_BYTES) return 4;
        result=author_decode(fd,0,(unsigned)size,1,&image);
        found=0;offset=0;length=(unsigned)size;
    }
    if (result) {printf("FILM_ERROR author_decode=%d\n",result);return result;}
    unsigned decoded=film_clock_ms();
    printf("FILM_SOURCE kind=%s source_width=%d source_height=%d width=%d height=%d offset=%u bytes=%u decode_passes=1 decode_attempts=%u\n",
        found?"embedded":image.reduced?"downsample2":"primary",image.source_width,image.source_height,
        image.width,image.height,offset,length,attempts);
    film_state film;film_init(&film,preset,grain);
    film_pixels(&film,image.pixels,(unsigned long)image.width*(unsigned)image.height);
    workspace work;
    int gw=image.width/4,gh=image.height/4;
    if (halo && preset!=4 && preset!=8 && gw>=8 && gh>=8) {
        work.mask_size=(size_t)gw*(unsigned)gh;
        work.mask=(unsigned char *)xalloc(work.mask_size);
        work.scratch=(unsigned char *)xalloc(work.mask_size);
        if (!work.mask || !work.scratch) return 5;
        film_mask_sample(work.mask,image.width,image.height,0,image.pixels,image.height);
        film_mask_blur(work.mask,work.scratch,gw,gh);
        xfree(work.scratch,work.mask_size);work.scratch=NULL;
        film_halo_apply(image.pixels,image.width,image.height,0,work.mask,gw,gh,preset);
        xfree(work.mask,work.mask_size);work.mask=NULL;
    }
    unsigned filtered=film_clock_ms();
    author_metadata_size(metadata,metadata_size,(unsigned)image.width,(unsigned)image.height);
    int out=sys_open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if (out<0) {printf("FILM_ERROR output_open=%d\n",out);return 6;}
    output_stream output(out,metadata,metadata_size);
    jpge::params settings;settings.m_quality=90;settings.m_subsampling=jpge::H2V2;
    jpge::jpeg_encoder encoder;
    result=encoder.init(&output,image.width,image.height,3,settings)?0:7;
    for (int y=0;!result && y<image.height;++y)
        if (!encoder.process_scanline(image.pixels+(unsigned long)y*(unsigned)image.width*3u)) result=10;
    if (!result && !encoder.process_scanline(NULL)) result=11;
    encoder.deinit();
    int flushed=film_fsync(out),closed=sys_close(out);
    if (result || output.failed || flushed || closed || !output.bytes) {
        sys_unlink(path);printf("FILM_ERROR render=%d output_failed=%d\n",result,output.failed);return 12;
    }
    unsigned finished=film_clock_ms();
    printf("FILM_TIMING select_ms=%u decode_ms=%u film_ms=%u encode_ms=%u total_ms=%u\n",
        selected-begin,decoded-selected,filtered-decoded,finished-filtered,finished-begin);
    printf("FILM_DONE width=%d height=%d bytes=%u rgb_kib=%u mask_kib=%u preset=%d grain=%d halation=%d\n",
        image.width,image.height,output.bytes,(image.bytes+1023u)/1024u,
        (unsigned)((work.mask_size+1023)/1024),preset,grain,halo);
    return 0;
}
#endif
