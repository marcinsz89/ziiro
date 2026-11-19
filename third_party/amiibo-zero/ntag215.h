#ifndef NTAG215_H
#define NTAG215_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { NTAG_PAGE_COUNT = 135, NTAG_DUMP_SIZE = 4 * NTAG_PAGE_COUNT };

struct ntag215 {
    uint8_t pages[NTAG_DUMP_SIZE];
    uint8_t uid[7];
    uint8_t password[4];
    uint8_t pack[2];
    uint8_t signature[32];
};

enum ntag_reply_kind { NTAG_SILENT, NTAG_BYTES, NTAG_NIBBLE, NTAG_HALT };
struct ntag_reply {
    enum ntag_reply_kind kind;
    size_t length;
    uint8_t data[NTAG_DUMP_SIZE];
};

bool ntag215_load(struct ntag215 *tag, const uint8_t dump[NTAG_DUMP_SIZE]);
void ntag215_command(struct ntag215 *tag, const uint8_t *frame, size_t length,
                     struct ntag_reply *reply);

#endif
