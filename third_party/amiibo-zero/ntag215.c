#include "ntag215.h"

#include <string.h>

bool ntag215_load(struct ntag215 *tag, const uint8_t dump[NTAG_DUMP_SIZE])
{
    if (dump[0] != 0x04 ||
        dump[3] != (0x88 ^ dump[0] ^ dump[1] ^ dump[2]) ||
        dump[8] != (dump[4] ^ dump[5] ^ dump[6] ^ dump[7])) {
        return false;
    }

    memset(tag, 0, sizeof(*tag));
    memcpy(tag->pages, dump, NTAG_DUMP_SIZE);
    memcpy(tag->uid, dump, 3);
    memcpy(tag->uid + 3, dump + 4, 4);

    // Both reference firmwares derive PWD from the active UID.
    tag->password[0] = 0xAA ^ tag->uid[1] ^ tag->uid[3];
    tag->password[1] = 0x55 ^ tag->uid[2] ^ tag->uid[4];
    tag->password[2] = 0xAA ^ tag->uid[3] ^ tag->uid[5];
    tag->password[3] = 0x55 ^ tag->uid[4] ^ tag->uid[6];
    tag->pack[0] = 0x80;
    tag->pack[1] = 0x80;
    return true;
}

static void read_pages(const struct ntag215 *tag, unsigned start, unsigned count,
                       struct ntag_reply *reply)
{
    for (unsigned i = 0; i < count; ++i) {
        unsigned page = (start + i) % NTAG_PAGE_COUNT;
        uint8_t *out = reply->data + i * 4;
        if (page < 0x85) {
            memcpy(out, tag->pages + page * 4, 4);
        } else {
            memset(out, 0, 4);
        }
    }
    reply->kind = NTAG_BYTES;
    reply->length = count * 4;
}

void ntag215_command(struct ntag215 *tag, const uint8_t *frame, size_t length,
                     struct ntag_reply *reply)
{
    static const uint8_t version[] = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x11, 0x03};
    reply->kind = NTAG_NIBBLE;
    reply->length = 0;
    reply->data[0] = 0x00;
    if (length == 0) {
        reply->kind = NTAG_SILENT;
        return;
    }

    switch (frame[0]) {
    case 0x30: // READ
        if (length == 2 && frame[1] < NTAG_PAGE_COUNT) {
            read_pages(tag, frame[1], 4, reply);
        }
        break;
    case 0x3A: // FAST_READ
        if (length == 3 && frame[1] <= frame[2] && frame[2] < NTAG_PAGE_COUNT) {
            read_pages(tag, frame[1], (unsigned)frame[2] - frame[1] + 1, reply);
        }
        break;
    case 0x60: // GET_VERSION
        if (length == 1) {
            memcpy(reply->data, version, sizeof(version));
            reply->kind = NTAG_BYTES;
            reply->length = sizeof(version);
        }
        break;
    case 0x1B: // PWD_AUTH
        if (length == 5 && memcmp(frame + 1, tag->password, sizeof(tag->password)) == 0) {
            memcpy(reply->data, tag->pack, sizeof(tag->pack));
            reply->kind = NTAG_BYTES;
            reply->length = sizeof(tag->pack);
        }
        break;
    case 0x3C: // READ_SIG
        if (length == 2 && frame[1] == 0) {
            memcpy(reply->data, tag->signature, sizeof(tag->signature));
            reply->kind = NTAG_BYTES;
            reply->length = sizeof(tag->signature);
        }
        break;
    case 0x39: // READ_CNT
        if (length == 2 && frame[1] <= 2) {
            memset(reply->data, 0, 3);
            reply->kind = NTAG_BYTES;
            reply->length = 3;
        }
        break;
    case 0x3E: // CHECK_TEARING_EVENT (ADV compatibility)
        if (length == 2 && frame[1] <= 2) {
            reply->data[0] = 0xBD;
            reply->kind = NTAG_BYTES;
            reply->length = 1;
        }
        break;
    case 0xA2: // WRITE (user memory)
        if (length == 6 && frame[1] >= 4 && frame[1] <= 129) {
            memcpy(tag->pages + frame[1] * 4, frame + 2, 4);
            reply->kind = NTAG_NIBBLE;
            reply->data[0] = 0x0A;
        }
        break;
    case 0x50: // HLTA
        if (length == 2 && frame[1] == 0) {
            reply->kind = NTAG_HALT;
        }
        break;
    }
}
