#ifndef ST25R3916_H
#define ST25R3916_H

#include "ntag215.h"
#include <signal.h>

enum { AMIIBO_POOL_LIMIT = 256 };
struct amiibo_images {
    const struct ntag215 *original;
    const struct ntag215 *variants;
    size_t count;
};

enum cap_probe_result {
    CAP_PROBE_OK = 0,
    CAP_PROBE_NO_DEVICE = 20,
    CAP_PROBE_PERMISSION,
    CAP_PROBE_BUSY,
    CAP_PROBE_NO_CHIP,
    CAP_PROBE_IO_ERROR,
};

int cap_open_irq(int gpio_chip);
int cap_probe(const char *spi_device);
/* The caller owns the IRQ descriptor for the session. */
int cap_emulate(const char *spi_device, int irq, const struct amiibo_images *images,
                bool trace, const volatile sig_atomic_t *stop);

#endif
