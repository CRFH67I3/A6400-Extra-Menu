/* Validate the first baseline JPEG codestream without rejecting MPO trailers. */
#ifndef A6400_JPEG_FRAME_H
#define A6400_JPEG_FRAME_H

typedef long (*jpeg_read_fn)(void *context, void *buffer, unsigned long count);
typedef long (*jpeg_seek_fn)(void *context, long offset, int origin);

static int jpeg_read_exact(void *context, jpeg_read_fn read_bytes,
                           unsigned char *buffer, unsigned long count) {
    while (count) {
        long amount = read_bytes(context, buffer, count);
        if (amount <= 0 || (unsigned long)amount > count) return 0;
        buffer += amount;
        count -= (unsigned long)amount;
    }
    return 1;
}

static int jpeg_first_frame_valid(void *context, jpeg_read_fn read_bytes,
                                  jpeg_seek_fn seek_bytes, long max_size,
                                  unsigned char *metadata,
                                  unsigned long metadata_capacity,
                                  unsigned long *metadata_length) {
    long size = seek_bytes(context, 0, 2);
    unsigned char pair[2];
    *metadata_length = 0;
    if (size < 4 || size > max_size || seek_bytes(context, 0, 0) != 0 ||
        !jpeg_read_exact(context, read_bytes, pair, 2) ||
        pair[0] != 0xff || pair[1] != 0xd8) return 0;

    for (;;) {
        unsigned char marker;
        if (!jpeg_read_exact(context, read_bytes, &marker, 1) || marker != 0xff)
            return 0;
        do {
            if (!jpeg_read_exact(context, read_bytes, &marker, 1)) return 0;
        } while (marker == 0xff);
        if (marker == 0 || marker == 0xd8 || marker == 0xd9 ||
            marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) return 0;
        if (!jpeg_read_exact(context, read_bytes, pair, 2)) return 0;
        unsigned length = ((unsigned)pair[0] << 8) | pair[1];
        long position = seek_bytes(context, 0, 1);
        if (length < 2 || position < 0 || position > size ||
            (long)(length - 2) > size - position ||
            !metadata) return 0;
        unsigned long payload = length - 2;
        int keep = marker == 0xe1;
        unsigned char prefix[12];
        unsigned long prefix_length = 0;
        if (marker == 0xe2 && payload >= sizeof(prefix)) {
            prefix_length = sizeof(prefix);
            if (!jpeg_read_exact(context, read_bytes, prefix, prefix_length)) return 0;
            static const unsigned char icc_signature[12] = {
                'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0
            };
            keep = 1;
            for (unsigned i = 0; i < sizeof(prefix); ++i)
                if (prefix[i] != icc_signature[i]) keep = 0;
        }
        if (keep) {
            if (payload + 4 > metadata_capacity - *metadata_length) return 0;
            unsigned char *target = metadata + *metadata_length;
            target[0] = 0xff;
            target[1] = marker;
            target[2] = pair[0];
            target[3] = pair[1];
            for (unsigned i = 0; i < prefix_length; ++i) target[4 + i] = prefix[i];
            if (!jpeg_read_exact(context, read_bytes, target + 4 + prefix_length,
                                 payload - prefix_length)) return 0;
            *metadata_length += payload + 4;
        } else if (seek_bytes(context, (long)(payload - prefix_length), 1) !=
                   position + (long)payload) {
            return 0;
        }
        if (marker == 0xda) break;
    }

    unsigned char chunk[4096];
    int after_ff = 0;
    for (;;) {
        long position = seek_bytes(context, 0, 1);
        if (position < 0 || position >= size) return 0;
        unsigned long count = (unsigned long)(size - position);
        if (count > sizeof(chunk)) count = sizeof(chunk);
        long amount = read_bytes(context, chunk, count);
        if (amount <= 0 || (unsigned long)amount > count) return 0;
        for (long i = 0; i < amount; ++i) {
            unsigned char value = chunk[i];
            if (!after_ff) {
                after_ff = value == 0xff;
            } else if (value == 0xff) {
                continue;
            } else {
                after_ff = 0;
                if (value == 0xd9) return seek_bytes(context, 0, 0) == 0;
                if (value != 0 && (value < 0xd0 || value > 0xd7)) return 0;
            }
        }
    }
}

#endif
