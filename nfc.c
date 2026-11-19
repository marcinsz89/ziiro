#define _POSIX_C_SOURCE 200809L

#include "nfc.h"

#ifdef ZIIRO_DEVICE
#include "st25r3916.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;
static const char spi_device[] = "/dev/spidev0.2";
static const char gpio_chip[] = "/dev/gpiochip0";
static const char ext_mux[] = "/sys/class/leds/ext_usb_gpio_fun/brightness";
static const char ext_power[] = "/sys/class/leds/ext_5v_out/brightness";
static NfcState state;
static pid_t worker;
static int output = -1;
static char line[128];
static size_t line_size;
static volatile sig_atomic_t worker_stopping;

enum { CAP_TOP_USB = 30, CAP_POWER_OFF };
enum { CAP_POWER_GPIO = 26 };

static int read_brightness(const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    int brightness = -1;
    if (fscanf(file, "%d", &brightness) != 1) brightness = -1;
    fclose(file);
    return brightness;
}

static int cap_access_error(int error)
{
    if (error == EACCES || error == EPERM) return CAP_PROBE_PERMISSION;
    if (error == EBUSY || error == EAGAIN) return CAP_PROBE_BUSY;
    return CAP_PROBE_IO_ERROR;
}

static bool enable_ext_power(void)
{
    const int brightness = read_brightness(ext_power);
    if (brightness > 0) return true;
    if (brightness < 0) {
        fprintf(stderr, "Cannot read %s\n", ext_power);
        return false;
    }
    const int fd = open(ext_power, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        perror(ext_power);
        return false;
    }
    const ssize_t written = write(fd, "1\n", 2);
    if (written < 0) perror(ext_power);
    close(fd);
    if (written == 2 && read_brightness(ext_power) > 0) return true;
    fputs("EXT 5V did not turn on.\n", stderr);
    return false;
}

static int run_cap(const struct amiibo_images *images)
{
    const int mux = read_brightness(ext_mux);
    if (mux > 0) return CAP_TOP_USB;
    if (mux < 0) {
        fprintf(stderr, "Cannot verify top port routing at %s\n", ext_mux);
        return CAP_PROBE_IO_ERROR;
    }
    if (access(spi_device, R_OK | W_OK) < 0) {
        const int error = errno;
        perror(spi_device);
        return error == ENOENT || error == ENODEV ? CAP_PROBE_NO_DEVICE : cap_access_error(error);
    }

    const int chip = open(gpio_chip, O_RDONLY | O_CLOEXEC);
    if (chip < 0) {
        const int error = errno;
        perror(gpio_chip);
        return cap_access_error(error);
    }
    // Claim the IRQ before changing power or touching SPI. The stock reader also owns this line.
    const int irq = cap_open_irq(chip);
    if (irq < 0) {
        const int result = cap_access_error(errno);
        close(chip);
        return result;
    }
    struct gpio_v2_line_request power = {
        .offsets = {CAP_POWER_GPIO},
        .consumer = "ziiro-cap-power",
        .config = {
            .flags = GPIO_V2_LINE_FLAG_OUTPUT,
            .num_attrs = 1,
            .attrs = {{.attr = {.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES, .values = 1}, .mask = 1}},
        },
        .num_lines = 1,
    };
    int result = CAP_PROBE_IO_ERROR;
    const int requested = ioctl(chip, GPIO_V2_GET_LINE_IOCTL, &power);
    const int error = errno;
    close(chip);
    if (requested < 0) {
        result = cap_access_error(error);
        errno = error;
        perror("Cap POWER_EN GPIO26");
    } else {
        if (!enable_ext_power()) {
            result = CAP_POWER_OFF;
        } else {
            nanosleep(&(struct timespec){.tv_nsec = 100000000}, NULL);
            if (worker_stopping) result = 0;
            else if (images) result = cap_emulate(spi_device, irq, images, false, &worker_stopping);
            else result = cap_probe(spi_device);
        }
        // The stock BSP retains the output level on release. Keep both supplies on for the LCD's shared SPI bus.
        close(power.fd);
    }
    close(irq);
    return result;
}

static void stop_worker(int signal_number)
{
    (void)signal_number;
    worker_stopping = 1;
}

int nfc_worker(int argc, char **argv)
{
    if (argc != 3 && argc != 4) return 2;
    struct sigaction action = {.sa_handler = stop_worker};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) < 0 || sigaction(SIGINT, &action, NULL) < 0 ||
        prctl(PR_SET_PDEATHSIG, SIGTERM) < 0) return 1;
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &action, NULL) < 0) return 1;
    char parent[32];
    snprintf(parent, sizeof(parent), "%ld", (long)getppid());
    if (strcmp(parent, argv[2]) != 0 || worker_stopping) return 1;
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc == 3) return run_cap(NULL);

    const int fd = open(argv[3], O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return 1;
    struct stat info;
    uint8_t dump[NTAG_DUMP_SIZE];
    FILE *file = fdopen(fd, "rb");
    if (!file) {
        close(fd);
        return 1;
    }
    const bool complete = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) &&
        info.st_size == NTAG_DUMP_SIZE && fread(dump, 1, sizeof(dump), file) == sizeof(dump) &&
        fgetc(file) == EOF && !ferror(file);
    fclose(file);
    struct ntag215 tag;
    if (!complete || !ntag215_load(&tag, dump) || worker_stopping) return 1;
    const struct amiibo_images images = {.original = &tag};
    return run_cap(&images);
}

static void spawn_worker(const char *backup)
{
    if (worker) return;
    state = NFC_ERROR;
    int pipe_fds[2];
    if (pipe(pipe_fds) < 0) return;
    if (fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK) < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return;
    }
    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    if (!error) {
        error = posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
        if (!error) error = posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
        if (!error) error = posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);
        if (!error) error = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
        char parent[32];
        snprintf(parent, sizeof(parent), "%ld", (long)getpid());
        char *args[] = {"ziiro", "--nfc-worker", parent, (char *)backup, NULL};
        if (!error) error = posix_spawn(&worker, "/proc/self/exe", &actions, NULL, args, environ);
        posix_spawn_file_actions_destroy(&actions);
    }
    close(pipe_fds[1]);
    if (error) {
        worker = 0;
        close(pipe_fds[0]);
        fprintf(stderr, "Cannot start Ziiro NFC worker: %s\n", strerror(error));
        return;
    }
    output = pipe_fds[0];
    line_size = 0;
    state = backup ? NFC_STARTING : NFC_CHECKING;
}

void nfc_probe(void) { spawn_worker(NULL); }
void nfc_present(const char *backup) { spawn_worker(backup); }

void nfc_stop(void)
{
    if (state == NFC_STARTING || state == NFC_PRESENTING) {
        kill(worker, SIGTERM);
        state = NFC_STOPPING;
    }
}

NfcState nfc_poll(void)
{
    if (!worker) return state;
    char buffer[256];
    ssize_t bytes;
    while ((bytes = read(output, buffer, sizeof(buffer))) > 0) {
        for (ssize_t index = 0; index < bytes; ++index) {
            if (buffer[index] == '\n') {
                if (state == NFC_STARTING && line_size >= 10 && memcmp(line, "Emulating.", 10) == 0)
                    state = NFC_PRESENTING;
                line_size = 0;
            } else if (line_size < sizeof(line)) {
                line[line_size++] = buffer[index];
            }
        }
    }
    int status = 0;
    const pid_t result = waitpid(worker, &status, WNOHANG);
    if (result == 0 || (result < 0 && errno == EINTR)) return state;
    const int code = result > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (state == NFC_CHECKING || state == NFC_STARTING) {
        switch (code) {
        case CAP_PROBE_OK: state = state == NFC_CHECKING ? NFC_READY : NFC_ERROR; break;
        case CAP_TOP_USB: state = NFC_TOP_USB; break;
        case CAP_POWER_OFF: state = NFC_POWER_OFF; break;
        case CAP_PROBE_NO_DEVICE: state = NFC_SPI_MISSING; break;
        case CAP_PROBE_NO_CHIP: state = NFC_NO_CAP; break;
        case CAP_PROBE_PERMISSION: state = NFC_PERMISSION; break;
        case CAP_PROBE_BUSY: state = NFC_BUSY; break;
        default: state = NFC_ERROR; break;
        }
    } else {
        state = (state == NFC_STOPPING && (code == 0 || (result > 0 && WIFSIGNALED(status) &&
                 WTERMSIG(status) == SIGTERM))) ? NFC_READY : NFC_ERROR;
    }
    close(output);
    output = -1;
    worker = 0;
    return state;
}

void nfc_shutdown(void)
{
    if (!worker) return;
    kill(worker, SIGTERM);
    for (int attempt = 0; worker && attempt < 100; ++attempt) {
        nfc_poll();
        if (worker) nanosleep(&(struct timespec){.tv_nsec = 20000000}, NULL);
    }
    if (worker) {
        kill(worker, SIGKILL);
        while (waitpid(worker, NULL, 0) < 0 && errno == EINTR) {}
        close(output);
        worker = 0;
        output = -1;
    }
}

#else
void nfc_probe(void) {}
void nfc_present(const char *backup) { (void)backup; }
void nfc_stop(void) {}
NfcState nfc_poll(void) { return NFC_UNAVAILABLE; }
void nfc_shutdown(void) {}
#endif
