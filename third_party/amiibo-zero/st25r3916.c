#include "st25r3916.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/spi/spidev.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

enum {
    REG_IO_CONFIG = 0x00, REG_OPERATION = 0x02, REG_MODE = 0x03,
    REG_BIT_RATE = 0x04, REG_ISO_A = 0x05, REG_TARGET = 0x08, REG_AUX = 0x0A,
    REG_MASK_RX_TIMER = 0x0F, REG_TIMER_CONTROL = 0x12, REG_IRQ_MASK = 0x16,
    REG_IRQ_MAIN = 0x1A, REG_IRQ_ERROR = 0x1C, REG_IRQ_TARGET = 0x1D,
    REG_FIFO_STATUS = 0x1E, REG_TARGET_STATE = 0x21, REG_TX_BITS = 0x22,
    REG_AAT = 0x26, REG_TX_DRIVER = 0x28, REG_TARGET_MODULATION = 0x29,
    REG_FIELD_THRESHOLD = 0x2A, REG_REGULATOR = 0x2C, REG_AUX_DISPLAY = 0x31, REG_ID = 0x3F,

    SPI_READ = 0x40, SPI_FIFO_WRITE = 0x80, SPI_FIFO_READ = 0x9F,
    SPI_TARGET_A = 0xA0, SPI_SPACE_B = 0xFB, SPI_TEST = 0xFC,
    CMD_DEFAULT = 0xC1, CMD_STOP = 0xC2, CMD_TX_CRC = 0xC4, CMD_TX_RAW = 0xC5,
    CMD_SENSE = 0xCD, CMD_SLEEP = 0xCE, CMD_UNMASK_RX = 0xD1,
    CMD_ADJUST_REGULATORS = 0xD6, CMD_CLEAR_FIFO = 0xDB,

    OP_OSCILLATOR = 0x80, OP_RECEIVER = 0x40, OP_FIELD_DETECTOR = 0x03,
    OSCILLATOR_READY = 0x10, FIELD_PRESENT = 0x40,
    MODE_NFC_A_LISTEN = 0x88,
    TARGET_NFC_A_AUTO = 0x5C, TARGET_NFC_A_MANUAL = 0x5D,
    AUX_UID_7 = 0x10,

    IRQ_FIFO_WATER = 0x40, IRQ_RX_START = 0x20, IRQ_RX_END = 0x10, IRQ_TX_END = 0x08,
    IRQ_FIELD_OFF = 0x08, IRQ_FIELD_ON = 0x10,
    IRQ_RX_ERRORS = 0xF0, IRQ_AUTO_RX = 0x10, IRQ_SELECTED = 0x03,
    STATE_ACTIVE = 0x05, STATE_ACTIVE_WAKE = 0x0D,
    FIFO_SIZE = 512, NFC_IRQ_GPIO = 23, SPI_SPEED_HZ = 8000000, STOCK_SPI_SPEED_HZ = 5000000
};

enum { TRACE_LIMIT = 64 };
static uint32_t spi_speed_hz = SPI_SPEED_HZ;

struct command_trace {
    uint8_t opcode;
    uint8_t start_page;
    uint8_t end_page;
    bool has_page_range;
    unsigned reply_bits;
    bool tx_done;
};

struct listener {
    int spi;
    int irq;
    bool active;
    bool halted;
    bool selected_in_field;
    bool field;
    bool session_report;
    char pending_key;
    size_t variant;
    unsigned sessions;
    bool scan_attempted;
    bool auth_reply_sent;
    bool read_reply_sent;
    unsigned reads;
    unsigned auths;
    unsigned writes;
    uint8_t last_command;
    bool tx_pending;
    struct ntag_reply reply;
    size_t queued;
    unsigned fields;
    unsigned anticollision_frames;
    unsigned selections;
    unsigned rx_starts;
    unsigned rx_ends;
    unsigned replies;
    unsigned commands;
    unsigned rx_errors;
    unsigned tx_errors;
    uint8_t pending_rx_errors;
    bool trace_enabled;
    size_t trace_count;
    unsigned trace_dropped;
    struct command_trace trace[TRACE_LIMIT];
    struct command_trace *trace_reply;
};

static int spi_transfer(int fd, const uint8_t *tx, uint8_t *rx, size_t length)
{
    struct spi_ioc_transfer message = {
        .tx_buf = (uintptr_t)tx,
        .rx_buf = (uintptr_t)rx,
        .len = (uint32_t)length,
        .speed_hz = spi_speed_hz,
        .bits_per_word = 8,
    };
    int count;
    do {
        count = ioctl(fd, SPI_IOC_MESSAGE(1), &message);
    } while (count < 0 && errno == EINTR);
    if (count >= 0 && (size_t)count != length) {
        errno = EIO;
        return -1;
    }
    return count < 0 ? -1 : 0;
}

static int send_bytes(int spi, uint8_t opcode, const uint8_t *data, size_t length)
{
    uint8_t tx[FIFO_SIZE + 1];
    tx[0] = opcode;
    if (length) {
        memcpy(tx + 1, data, length);
    }
    return spi_transfer(spi, tx, NULL, length + 1);
}

static int receive_bytes(int spi, uint8_t opcode, uint8_t *data, size_t length)
{
    uint8_t tx[65] = {opcode};
    uint8_t rx[sizeof(tx)];
    if (spi_transfer(spi, tx, rx, length + 1) < 0) {
        return -1;
    }
    memcpy(data, rx + 1, length);
    return 0;
}

static int write_reg(int spi, uint8_t address, uint8_t value)
{
    return send_bytes(spi, address, &value, 1);
}

static int command(int spi, uint8_t opcode)
{
    return send_bytes(spi, opcode, NULL, 0);
}

static int open_spi(const char *path, bool report_error)
{
    uint8_t mode = SPI_MODE_1;
    uint8_t bits = 8;
    uint8_t identity;
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        int error = errno;
        if (report_error) perror(path);
        errno = error;
        return -1;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) < 0 ||
        ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        int error = errno;
        if (report_error) perror("ST25R3916 SPI");
        close(fd);
        errno = error;
        return -1;
    }
    spi_speed_hz = SPI_SPEED_HZ;
    int read_result = receive_bytes(fd, SPI_READ | REG_ID, &identity, 1);
    if (read_result < 0 || (identity >> 3) != 0x05) {
        spi_speed_hz = STOCK_SPI_SPEED_HZ;
        read_result = receive_bytes(fd, SPI_READ | REG_ID, &identity, 1);
    }
    if (read_result < 0) {
        const int error = errno;
        if (report_error) perror("ST25R3916 SPI identity");
        close(fd);
        errno = error;
        return -1;
    }
    /* Accept all revisions of the ST25R3916, as the M5Stack Cap reader does. */
    if ((identity >> 3) != 0x05) {
        if (report_error) {
            fprintf(stderr, "Unexpected ST25R3916 identity 0x%02X. Check EXT 5V, POWER_EN and SPI CS.\n",
                    (unsigned)identity);
        }
        close(fd);
        errno = ENXIO;
        return -1;
    }
    printf("ST25R3916 detected (ID 0x%02X, SPI %u Hz).\n", (unsigned)identity, (unsigned)spi_speed_hz);
    return fd;
}

int cap_open_irq(int chip)
{
    struct gpio_v2_line_request request = {
        .offsets = {NFC_IRQ_GPIO},
        .consumer = "amiibo-zero",
        .config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING,
        .num_lines = 1,
    };
    if (ioctl(chip, GPIO_V2_GET_LINE_IOCTL, &request) < 0) {
        const int error = errno;
        perror("NFC IRQ GPIO23");
        errno = error;
        return -1;
    }
    return request.fd;
}

int cap_probe(const char *spi_device)
{
    // A freshly loaded overlay can expose spidev before permissions and the
    // powered NFC frontend are ready. Give both time to settle.
    for (unsigned attempt = 0; attempt < 12; ++attempt) {
        bool last_attempt = attempt == 11;
        int spi = open_spi(spi_device, last_attempt);
        if (spi >= 0) {
            close(spi);
            return CAP_PROBE_OK;
        }
        if (!last_attempt) {
            nanosleep(&(struct timespec){.tv_nsec = 250000000}, NULL);
            continue;
        }
        switch (errno) {
        case ENOENT:
        case ENODEV: return CAP_PROBE_NO_DEVICE;
        case EACCES:
        case EPERM: return CAP_PROBE_PERMISSION;
        case EBUSY:
        case EAGAIN: return CAP_PROBE_BUSY;
        case ENXIO: return CAP_PROBE_NO_CHIP;
        default: return CAP_PROBE_IO_ERROR;
        }
    }
    return CAP_PROBE_IO_ERROR;
}

static int arm_target(struct listener *dev)
{
    int spi = dev->spi;
    dev->active = false;
    dev->halted = false;
    dev->tx_pending = false;
    dev->queued = 0;
    dev->trace_reply = NULL;
    dev->pending_rx_errors = 0;

    if (command(spi, CMD_STOP) < 0 ||
        write_reg(spi, REG_TARGET, TARGET_NFC_A_AUTO) < 0 ||
        command(spi, CMD_SENSE) < 0 ||
        command(spi, CMD_UNMASK_RX) < 0) {
        return -1;
    }
    return 0;
}

static int load_target_uid(int spi, const struct ntag215 *tag)
{
    uint8_t memory[15] = {0};
    memcpy(memory, tag->uid, sizeof(tag->uid));
    memory[10] = 0x44; // ATQA 0x0044.
    memory[12] = 0x04; // Cascade bit for the first part of the 7-byte UID.
    return send_bytes(spi, SPI_TARGET_A, memory, sizeof(memory));
}

static int configure_target(struct listener *dev, struct ntag215 *tag)
{
    int spi = dev->spi;
    const uint8_t overheat_workaround[] = {0x04, 0x10};
    const uint8_t io_config[] = {0x07, 0xBC}; // Clock output off, 3.3 V, AAT, SPI.
    const uint8_t thresholds[] = {0x13, 0x02};
    const uint8_t tuning[] = {0x00, 0xFF};
    const uint8_t emd[] = {0x05, 0x40};
    const uint8_t protection[] = {0x30, 0, 0, 0, 0};
    const uint8_t irq_masks[] = {
        (uint8_t)~(IRQ_FIFO_WATER | IRQ_RX_START | IRQ_RX_END | IRQ_TX_END),
        (uint8_t)~(IRQ_FIELD_ON | IRQ_FIELD_OFF),
        0xFF, // RX error status is collected with RX_END.
        (uint8_t)~(IRQ_AUTO_RX | IRQ_SELECTED),
    };
    uint8_t aux;

    if (command(spi, CMD_DEFAULT) < 0 ||
        send_bytes(spi, SPI_TEST, overheat_workaround, sizeof(overheat_workaround)) < 0 ||
        send_bytes(spi, REG_IO_CONFIG, io_config, sizeof(io_config)) < 0 ||
        send_bytes(spi, REG_FIELD_THRESHOLD, thresholds, sizeof(thresholds)) < 0 ||
        write_reg(spi, REG_TX_DRIVER, 0xD0) < 0 ||
        write_reg(spi, REG_TARGET_MODULATION, 0x5F) < 0 ||
        send_bytes(spi, SPI_SPACE_B, emd, sizeof(emd)) < 0 ||
        write_reg(spi, REG_OPERATION, OP_OSCILLATOR | OP_FIELD_DETECTOR) < 0) {
        return -1;
    }
    for (unsigned attempt = 0; ; ++attempt) {
        if (receive_bytes(spi, SPI_READ | REG_AUX_DISPLAY, &aux, 1) < 0) {
            return -1;
        }
        if (aux & OSCILLATOR_READY) {
            break;
        }
        if (attempt == 49) {
            errno = ETIMEDOUT;
            return -1;
        }
        nanosleep(&(struct timespec){.tv_nsec = 1000000}, NULL);
    }
    if (write_reg(spi, REG_REGULATOR, 0x80) < 0 ||
        write_reg(spi, REG_REGULATOR, 0) < 0 ||
        command(spi, CMD_ADJUST_REGULATORS) < 0) {
        return -1;
    }
    nanosleep(&(struct timespec){.tv_nsec = 5000000}, NULL);

    // NTAG215 uses NFC-A at 106 kbit/s. Keep this mode through SELECT and RX.
    if (write_reg(spi, REG_MODE, MODE_NFC_A_LISTEN) < 0 ||
        write_reg(spi, REG_AUX, AUX_UID_7) < 0 ||
        load_target_uid(spi, tag) < 0 ||
        write_reg(spi, REG_BIT_RATE, 0) < 0 ||
        write_reg(spi, REG_ISO_A, 0) < 0 ||
        write_reg(spi, REG_TIMER_CONTROL, 0x08) < 0 ||
        write_reg(spi, REG_MASK_RX_TIMER, 3) < 0 ||
        send_bytes(spi, REG_AAT, tuning, sizeof(tuning)) < 0 ||
        send_bytes(spi, SPI_SPACE_B, protection, sizeof(protection)) < 0 ||
        send_bytes(spi, REG_IRQ_MASK, irq_masks, sizeof(irq_masks)) < 0 ||
        write_reg(spi, REG_OPERATION, OP_OSCILLATOR | OP_RECEIVER | OP_FIELD_DETECTOR) < 0) {
        return -1;
    }
    return arm_target(dev);
}

static int begin_reply(struct listener *dev)
{
    struct ntag_reply *reply = &dev->reply;
    bool nibble = reply->kind == NTAG_NIBBLE;
    unsigned bits = nibble ? 4 : (unsigned)reply->length * 8;
    const uint8_t tx_bits[] = {(uint8_t)(bits >> 8), (uint8_t)bits};
    dev->queued = nibble ? 1 : reply->length;
    if (dev->queued > FIFO_SIZE) {
        dev->queued = FIFO_SIZE;
    }
    if (command(dev->spi, CMD_CLEAR_FIFO) < 0 ||
        send_bytes(dev->spi, SPI_FIFO_WRITE, reply->data, dev->queued) < 0 ||
        send_bytes(dev->spi, REG_TX_BITS, tx_bits, sizeof(tx_bits)) < 0 ||
        command(dev->spi, nibble ? CMD_TX_RAW : CMD_TX_CRC) < 0) {
        return -1;
    }
    dev->tx_pending = true;
    return 0;
}

static void trace_command(struct listener *dev, const uint8_t *frame, size_t length)
{
    dev->trace_reply = NULL;
    if (dev->trace_count == TRACE_LIMIT) {
        ++dev->trace_dropped;
        return;
    }

    struct command_trace *entry = &dev->trace[dev->trace_count++];
    *entry = (struct command_trace){.opcode = frame[0]};
    if (dev->reply.kind == NTAG_BYTES) entry->reply_bits = (unsigned)dev->reply.length * 8;
    else if (dev->reply.kind == NTAG_NIBBLE) entry->reply_bits = 4;
    if (frame[0] == 0x30 && length == 2 && dev->reply.kind == NTAG_BYTES) {
        entry->has_page_range = true;
        entry->start_page = frame[1];
        entry->end_page = (frame[1] + 3) % NTAG_PAGE_COUNT;
    } else if (frame[0] == 0x3A && length == 3) {
        entry->has_page_range = true;
        entry->start_page = frame[1];
        entry->end_page = frame[2];
    }
    dev->trace_reply = entry;
}

static int handle_frame(struct listener *dev, struct ntag215 *tag, uint8_t errors, bool just_selected)
{
    uint8_t fifo[2];
    uint8_t frame[64];
    int spi = dev->spi;
    if (receive_bytes(spi, SPI_READ | REG_FIFO_STATUS, fifo, sizeof(fifo)) < 0) {
        return -1;
    }
    size_t length = fifo[0] | ((unsigned)(fifo[1] & 0xC0) << 2);
    if (just_selected && length == 0) {
        return 0;
    }
    if (errors || (fifo[1] & 0x3F) || length <= 2 || length > sizeof(frame)) {
        ++dev->rx_errors;
        if (command(spi, CMD_CLEAR_FIFO) < 0) {
            return -1;
        }
        return command(spi, CMD_UNMASK_RX);
    }
    if (receive_bytes(spi, SPI_FIFO_READ, frame, length) < 0) {
        return -1;
    }
    // A SELECT interrupt and RX_END can be pending at the same time. A
    // command already in the FIFO after selection still needs its response.
    if (just_selected && length == 9 && frame[0] == 0x95 && frame[1] == 0x70) {
        return 0;
    }
    ++dev->commands;
    // A reader may select and halt just to inventory the tag.
    dev->scan_attempted |= frame[0] != 0x50;
    ntag215_command(tag, frame, length - 2, &dev->reply); // CRC-A checked by ST25R3916.
    if (dev->trace_enabled) trace_command(dev, frame, length - 2);

    dev->last_command = frame[0];
    dev->reads += (frame[0] == 0x30 || frame[0] == 0x3A) && dev->reply.kind == NTAG_BYTES;
    dev->auths += frame[0] == 0x1B && dev->reply.kind == NTAG_BYTES;
    dev->writes += frame[0] == 0xA2 && dev->reply.kind == NTAG_NIBBLE && dev->reply.data[0] == 0x0A;

    switch (dev->reply.kind) {
    case NTAG_BYTES:
    case NTAG_NIBBLE:
        return begin_reply(dev);
    case NTAG_HALT:
        dev->active = false;
        dev->halted = true;
        if (write_reg(spi, REG_TARGET, TARGET_NFC_A_AUTO) < 0 ||
            command(spi, CMD_SLEEP) < 0) {
            return -1;
        }
        return command(spi, CMD_UNMASK_RX);
    case NTAG_SILENT:
        return command(spi, CMD_UNMASK_RX);
    }
    return -1;
}

static int end_session(struct listener *dev)
{
    bool needs_rearm = dev->selected_in_field;
    dev->session_report |= needs_rearm;
    dev->selected_in_field = false;
    dev->field = false;
    // Hardware already returns to its field-off state during discovery.
    return needs_rearm ? arm_target(dev) : 0;
}

static int handle_irq(struct listener *dev, struct ntag215 *tag)
{
    int spi = dev->spi;
    uint8_t status[4];
    // Each IRQ register clears on read. Collect the contiguous registers in
    // one SPI transaction and retain RX errors until the frame is handled.
    if (receive_bytes(spi, SPI_READ | REG_IRQ_MAIN, status, sizeof(status)) < 0) {
        return -1;
    }
    const uint8_t target = status[3];
    // TX_END can be latched together with field-off. Account for the reply
    // before re-arming, which clears the current transmission state.
    if (dev->tx_pending && (status[0] & IRQ_TX_END)) {
        dev->tx_pending = false;
        if (dev->reply.kind == NTAG_BYTES && dev->queued < dev->reply.length) {
            ++dev->tx_errors;
        } else {
            ++dev->replies;
            if (dev->trace_reply) dev->trace_reply->tx_done = true;
            if (dev->reply.kind == NTAG_BYTES) {
                dev->auth_reply_sent |= dev->last_command == 0x1B;
                dev->read_reply_sent |= dev->last_command == 0x30 || dev->last_command == 0x3A;
            }
        }
    }
    if (status[1] & IRQ_FIELD_OFF) {
        return end_session(dev);
    }
    dev->fields += (status[1] & IRQ_FIELD_ON) != 0;
    dev->anticollision_frames += (target & IRQ_AUTO_RX) != 0;
    dev->field |= (status[1] & IRQ_FIELD_ON) || (target & (IRQ_AUTO_RX | IRQ_SELECTED));
    bool just_selected = !dev->active && (target & IRQ_SELECTED);
    if (!dev->active && !just_selected && (status[0] & (IRQ_RX_START | IRQ_RX_END))) {
        uint8_t state;
        if (receive_bytes(spi, SPI_READ | REG_TARGET_STATE, &state, 1) < 0) {
            return -1;
        }
        state &= 0x0F;
        // Recover if a reader command and the selection arrive in separate
        // snapshots of the IRQ registers.
        just_selected = dev->halted ? state == STATE_ACTIVE_WAKE :
                                      state == STATE_ACTIVE || state == STATE_ACTIVE_WAKE;
    }
    if (just_selected) {
        dev->active = true;
        dev->halted = false;
        dev->field = true;
        dev->selected_in_field = true;
        ++dev->selections;
        if (write_reg(spi, REG_TARGET, TARGET_NFC_A_MANUAL) < 0) {
            return -1;
        }
        // Preserve any data already entering the FIFO. Otherwise explicitly
        // arm the receiver after handing off the automatic SAK response.
        if (!(status[0] & (IRQ_RX_START | IRQ_RX_END)) &&
            command(spi, CMD_UNMASK_RX) < 0) {
            return -1;
        }
    }
    if (dev->active) {
        dev->pending_rx_errors |= status[2] & IRQ_RX_ERRORS;
        dev->rx_starts += (status[0] & IRQ_RX_START) != 0;
        dev->rx_ends += (status[0] & IRQ_RX_END) != 0;
    }

    if (dev->tx_pending && (status[0] & IRQ_FIFO_WATER) &&
        dev->reply.kind == NTAG_BYTES && dev->queued < dev->reply.length) {
        size_t remaining = dev->reply.length - dev->queued;
        if (send_bytes(spi, SPI_FIFO_WRITE, dev->reply.data + dev->queued, remaining) < 0) {
            return -1;
        }
        dev->queued += remaining;
    }
    if (dev->active && (status[0] & IRQ_RX_END)) {
        uint8_t errors = dev->pending_rx_errors;
        dev->pending_rx_errors = 0;
        return handle_frame(dev, tag, errors, just_selected);
    }
    return 0;
}

static void print_session(struct listener *dev)
{
    ++dev->sessions;
    printf("Session %u: reads=%u auth=%u writes=%u last=0x%02X\n",
           dev->sessions, dev->reads, dev->auths, dev->writes, (unsigned)dev->last_command);
    if (dev->scan_attempted) {
        puts(dev->read_reply_sent && dev->auth_reply_sent ? "Scan result: success" : "Scan result: failed");
        if (dev->trace_enabled) {
            for (size_t i = 0; i < dev->trace_count; ++i) {
                const struct command_trace *entry = &dev->trace[i];
                printf("Trace: op=0x%02X", (unsigned)entry->opcode);
                if (entry->has_page_range) {
                    printf(" pages=0x%02X-0x%02X", (unsigned)entry->start_page, (unsigned)entry->end_page);
                }
                printf(" reply_bits=%u tx=%s\n", entry->reply_bits,
                       !entry->reply_bits ? "none" : entry->tx_done ? "done" : "unfinished");
            }
            if (dev->trace_dropped) printf("Trace: %u additional commands omitted\n", dev->trace_dropped);
        }
    }
    dev->trace_count = 0;
    dev->trace_dropped = 0;
    dev->trace_reply = NULL;
    dev->scan_attempted = false;
    dev->auth_reply_sent = false;
    dev->read_reply_sent = false;
    dev->reads = 0;
    dev->auths = 0;
    dev->writes = 0;
    dev->last_command = 0;
}

static void print_uid(const struct ntag215 *tag)
{
    fputs("UID ", stdout);
    for (size_t i = 0; i < sizeof(tag->uid); ++i) {
        printf("%s%02X", i ? ":" : "", (unsigned)tag->uid[i]);
    }
    putchar('\n');
}

static int switch_image(struct listener *dev, struct ntag215 *tag,
                        const struct amiibo_images *images, char key)
{
    if (key != 'o' && key != 'r') {
        return 0;
    }
    if (key == 'r' && !images->count) {
        puts("No UID pool loaded. Start emulate with --uid-pool FILE to use r.");
        return 0;
    }

    // The IRQ can arrive while handling terminal input. Recheck the RF detector
    // before issuing any command that stops the current NFC exchange.
    uint8_t aux;
    if (receive_bytes(dev->spi, SPI_READ | REG_AUX_DISPLAY, &aux, 1) < 0) {
        return -1;
    }
    if (aux & FIELD_PRESENT) {
        dev->field = true;
        dev->pending_key = key;
        return 0;
    }

    if (key == 'o') {
        dev->variant = images->count;
        *tag = *images->original;
    } else {
        uint32_t random;
        if (getrandom(&random, sizeof(random), 0) != (ssize_t)sizeof(random)) {
            return -1;
        }
        // Draw from the pool excluding the current variant, when possible.
        bool exclude_current = images->count > 1 && dev->variant < images->count;
        size_t index = random % (images->count - exclude_current);
        if (exclude_current && index >= dev->variant) {
            ++index;
        }
        dev->variant = index;
        *tag = images->variants[index];
    }
    if (load_target_uid(dev->spi, tag) < 0 || arm_target(dev) < 0) {
        return -1;
    }
    if (dev->variant == images->count) {
        puts("Ready: original dump restored.");
    } else {
        printf("Ready: UID variant %zu/%zu.\n", dev->variant + 1, images->count);
    }
    if (!dev->trace_enabled) print_uid(tag);
    return 0;
}

static int listen_for_reader(struct listener *dev, struct ntag215 *tag,
                             const struct amiibo_images *images,
                             const volatile sig_atomic_t *stop)
{
    struct pollfd events[] = {
        {.fd = dev->irq, .events = POLLIN},
        {.fd = isatty(STDIN_FILENO) ? STDIN_FILENO : -1, .events = POLLIN},
    };
    while (!*stop) {
        struct gpio_v2_line_values value = {.mask = 1};
        if (ioctl(dev->irq, GPIO_V2_LINE_GET_VALUES_IOCTL, &value) < 0) {
            return -1;
        }
        if (value.bits & 1) {
            if (handle_irq(dev, tag) < 0) {
                return -1;
            }
            // IRQ is level-high until the registers are drained. Recheck it
            // even if the GPIO edge queue was already consumed.
        } else if (!dev->field) {
            if (dev->session_report) {
                print_session(dev);
                dev->session_report = false;
            }
            if (dev->pending_key) {
                char key = dev->pending_key;
                dev->pending_key = 0;
                if (switch_image(dev, tag, images, key) < 0) {
                    return -1;
                }
            }
        }

        // Keep terminal I/O off the NFC response path while the field is on.
        int ready = poll(events, (dev->field || (value.bits & 1)) ? 1 : 2,
                         (value.bits & 1) ? 0 : 100);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready < 0) {
            return -1;
        }
        if (!ready) {
            if (dev->field) {
                uint8_t aux;
                if (receive_bytes(dev->spi, SPI_READ | REG_AUX_DISPLAY, &aux, 1) < 0) {
                    return -1;
                }
                if (!(aux & FIELD_PRESENT) && end_session(dev) < 0) {
                    return -1;
                }
            }
            continue;
        }
        if (events[0].revents & POLLIN) {
            struct gpio_v2_line_event event;
            ssize_t count = read(dev->irq, &event, sizeof(event));
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count != (ssize_t)sizeof(event)) {
                if (count >= 0) {
                    errno = EIO;
                }
                return -1;
            }
        } else if (events[0].revents) {
            errno = EIO;
            return -1;
        } else if (!dev->field && !(value.bits & 1) && events[1].revents) {
            char line[32];
            ssize_t count = read(events[1].fd, line, sizeof(line));
            if (count > 0) {
                for (ssize_t i = 0; i < count; ++i) {
                    if (line[i] == 'r' || line[i] == 'o') {
                        dev->pending_key = line[i];
                    }
                }
            } else if (count == 0 || errno != EINTR) {
                events[1].fd = -1;
            }
        }
    }
    return 0;
}

int cap_emulate(const char *spi_device, int irq, const struct amiibo_images *images,
                bool trace, const volatile sig_atomic_t *stop)
{
    struct ntag215 tag = *images->original;
    struct listener dev = {
        .spi = open_spi(spi_device, true), .irq = irq, .variant = images->count, .trace_enabled = trace,
    };
    if (dev.spi < 0) {
        return 1;
    }
    int result = 0;
    if (configure_target(&dev, &tag) < 0) {
        perror("ST25R3916 listener setup");
        result = 1;
    } else {
        if (!trace) print_uid(&tag);
        puts("Emulating. Hold the Cap near the amiibo reader. Ctrl+C to stop.");
        if (isatty(STDIN_FILENO)) {
            puts(images->count ? "Enter r for a random UID variant or o to restore the original." :
                                 "Using the original UID. Enter o to restore the original dump.");
        }
        if (listen_for_reader(&dev, &tag, images, stop) < 0) {
            perror("ST25R3916 listener");
            result = 1;
        }
        if (dev.selected_in_field || dev.session_report) {
            print_session(&dev);
        }
        printf("RF fields: %u, anticollision frames: %u, RX starts: %u, RX ends: %u, replies: %u\n",
               dev.fields, dev.anticollision_frames, dev.rx_starts, dev.rx_ends, dev.replies);
        printf("Selections: %u, commands: %u, RX errors: %u, TX errors: %u\n",
               dev.selections, dev.commands, dev.rx_errors, dev.tx_errors);
    }

    // Disable the NFC frontend while preserving Cap power on the shared LCD bus.
    int stopped = command(dev.spi, CMD_STOP);
    if (write_reg(dev.spi, REG_OPERATION, 0) < 0 || stopped < 0) {
        fputs("Could not stop the ST25R3916 cleanly.\n", stderr);
        result = 1;
    }
    close(dev.spi);
    return result;
}
