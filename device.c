#define _POSIX_C_SOURCE 200809L

#include "studio.h"
#include "nfc.h"

#include <lvgl.h>

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define ZIIRO_SHARE "/usr/share/ziiro"

static volatile sig_atomic_t stopping;

static void stop(int signal_number)
{
    (void)signal_number;
    stopping = 1;
}

static bool join_path(char output[PATH_MAX], const char *base, const char *name)
{
    const int size = snprintf(output, PATH_MAX, "%s/%s", base, name);
    if (size >= 0 && size < PATH_MAX) return true;
    fprintf(stderr, "Ziiro path is too long.\n");
    return false;
}

static bool find_share(char share[PATH_MAX])
{
    char executable[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length > 0 && (size_t)length < sizeof(executable) - 1) {
        executable[length] = '\0';
        char *filename = strrchr(executable, '/');
        if (filename) {
            *filename = '\0';
            char *bin = strrchr(executable, '/');
            if (bin && strcmp(bin + 1, "bin") == 0) {
                *bin = '\0';
                return join_path(share, executable, "share/ziiro");
            }
        }
    }
    memcpy(share, ZIIRO_SHARE, sizeof(ZIIRO_SHARE));
    return true;
}

static bool ensure_directory(const char *path)
{
    if (mkdir(path, 0700) == 0) return true;
    if (errno == EEXIST) {
        struct stat info;
        if (lstat(path, &info) == 0 && S_ISDIR(info.st_mode)) return true;
        errno = ENOTDIR;
    }
    fprintf(stderr, "Cannot create %s: %s\n", path, strerror(errno));
    return false;
}

static bool prepare_library(char profile[PATH_MAX], char backups[PATH_MAX], char artwork[PATH_MAX])
{
    const char *home = getenv("HOME");
    if (!home || !*home) {
        const struct passwd *user = getpwuid(getuid());
        home = user ? user->pw_dir : NULL;
    }
    if (!home || home[0] != '/') {
        fprintf(stderr, "Cannot locate the current user's home directory.\n");
        return false;
    }
    return join_path(profile, home, "ziiro") && ensure_directory(profile) &&
        join_path(backups, profile, "amiibo_data") && ensure_directory(backups) &&
        join_path(artwork, profile, "artwork") && ensure_directory(artwork);
}

static bool internal_lcd(const char *path)
{
    const int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) return false;
    struct fb_var_screeninfo screen;
    const bool valid = ioctl(fd, FBIOGET_VSCREENINFO, &screen) == 0 &&
        screen.xres == STUDIO_WIDTH && screen.yres == STUDIO_HEIGHT &&
        screen.bits_per_pixel == 16 &&
        screen.red.offset == 11 && screen.red.length == 5 &&
        screen.green.offset == 5 && screen.green.length == 6 &&
        screen.blue.offset == 0 && screen.blue.length == 5;
    close(fd);
    return valid;
}

static const char *framebuffer(void)
{
    const char *preferred = getenv("ZIIRO_FB");
    if (!preferred || !*preferred) preferred = getenv("LV_LINUX_FBDEV_DEVICE");
    if (preferred && *preferred && internal_lcd(preferred)) return preferred;
    if (internal_lcd("/dev/fb_lcd")) return "/dev/fb_lcd";
    if (internal_lcd("/dev/fb0")) return "/dev/fb0";
    return NULL;
}

static int open_keyboard(void)
{
    const char *preferred = getenv("ZIIRO_KEYBOARD");
    if (!preferred || !*preferred) preferred = getenv("LV_LINUX_KEYBOARD_DEVICE");
    const int flags = O_RDONLY | O_NONBLOCK | O_CLOEXEC;
    if (preferred && *preferred) return open(preferred, flags);

    int fd = open("/dev/input/by-path/platform-3f804000.i2c-event", flags);
    if (fd >= 0) return fd;
    glob_t results = {0};
    if (glob("/dev/input/event*", 0, NULL, &results) == 0) {
        for (size_t i = 0; i < results.gl_pathc; i++) {
            fd = open(results.gl_pathv[i], flags);
            if (fd < 0) continue;
            char name[128] = {0};
            if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 &&
                strncmp(name, "tca8418", 7) == 0) break;
            close(fd);
            fd = -1;
        }
    }
    globfree(&results);
    return fd;
}

static bool search_key_action(unsigned code, int value)
{
    switch (code) {
    case KEY_ENTER:
    case KEY_KPENTER: return value != 1 || studio_input(STUDIO_SELECT);
    case KEY_ESC: return value != 1 || studio_input(STUDIO_BACK);
    default: break;
    }

    static const char text[] = {
        [KEY_A] = 'a', [KEY_B] = 'b', [KEY_C] = 'c', [KEY_D] = 'd', [KEY_E] = 'e',
        [KEY_F] = 'f', [KEY_G] = 'g', [KEY_H] = 'h', [KEY_I] = 'i', [KEY_J] = 'j',
        [KEY_K] = 'k', [KEY_L] = 'l', [KEY_M] = 'm', [KEY_N] = 'n', [KEY_O] = 'o',
        [KEY_P] = 'p', [KEY_Q] = 'q', [KEY_R] = 'r', [KEY_S] = 's', [KEY_T] = 't',
        [KEY_U] = 'u', [KEY_V] = 'v', [KEY_W] = 'w', [KEY_X] = 'x', [KEY_Y] = 'y', [KEY_Z] = 'z',
        [KEY_0] = '0', [KEY_1] = '1', [KEY_2] = '2', [KEY_3] = '3', [KEY_4] = '4',
        [KEY_5] = '5', [KEY_6] = '6', [KEY_7] = '7', [KEY_8] = '8', [KEY_9] = '9',
        [KEY_SPACE] = ' ', [KEY_MINUS] = '-', [KEY_APOSTROPHE] = '\'', [KEY_BACKSPACE] = '\b',
    };
    if (code < sizeof(text) && text[code] && (value == 1 || code == KEY_BACKSPACE)) {
        studio_search_edit(text[code]);
    }
    return true;
}

static bool key_action(const struct input_event *event)
{
    if (event->type != EV_KEY || event->value == 0) return true;
    if (studio_search_editing()) return search_key_action(event->code, event->value);
    switch (event->code) {
    case KEY_UP:
    case KEY_F: return studio_input(STUDIO_UP);
    case KEY_DOWN:
    case KEY_X: return studio_input(STUDIO_DOWN);
    default: break;
    }
    if (event->value != 1) return true;

    switch (event->code) {
    case KEY_ENTER:
    case KEY_KPENTER: return studio_input(STUDIO_SELECT);
    case KEY_ESC: return studio_input(STUDIO_BACK);
    case KEY_TAB: return studio_input(STUDIO_VIEW);
    case KEY_G: return studio_input(STUDIO_GROUP);
    case KEY_T: return studio_input(STUDIO_REFRESH);
    case KEY_R: studio_retry_nfc(); return true;
    case KEY_U: return studio_input(STUDIO_SETTINGS);
    case KEY_S: return studio_input(STUDIO_SEARCH);
    case KEY_I: return studio_input(STUDIO_GAMES);
    default: return true;
    }
}

static int run_ui(int keyboard)
{
    struct pollfd input = {.fd = keyboard, .events = POLLIN};
    while (!stopping) {
        lv_timer_handler();
        const int ready = poll(&input, 1, 10);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0 || (input.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            fprintf(stderr, "Lost access to the Cardputer keyboard.\n");
            return 1;
        }
        if (input.revents & POLLIN) {
            struct input_event event;
            while (read(keyboard, &event, sizeof(event)) == sizeof(event)) {
                if (!key_action(&event)) return 0;
            }
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--convert-artwork") == 0) return studio_convert_artwork();
    if (argc >= 2 && strcmp(argv[1], "--nfc-worker") == 0) return nfc_worker(argc, argv);
    const bool prepare_only = argc == 2 && strcmp(argv[1], "--prepare-library") == 0;
    if (argc != 1 && !prepare_only) {
        fprintf(stderr, "Usage: ziiro [--prepare-library]\n");
        return 2;
    }
    char profile[PATH_MAX], backups[PATH_MAX], artwork[PATH_MAX];
    if (!prepare_library(profile, backups, artwork)) return 1;
    if (prepare_only) {
        puts(backups);
        return 0;
    }

    char share[PATH_MAX];
    if (!find_share(share)) return 1;

    const char *fb = framebuffer();
    if (!fb) {
        fprintf(stderr, "Cannot access a 320x170 RGB565 framebuffer. Check /dev/fb_lcd.\n");
        return 1;
    }
    const int keyboard = open_keyboard();
    if (keyboard < 0) {
        fprintf(stderr, "Cannot access the Cardputer keyboard. Check /dev/input or ZIIRO_KEYBOARD.\n");
        return 1;
    }
    struct sigaction action = {.sa_handler = stop};
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    lv_init();
    lv_display_t *display = lv_linux_fbdev_create();
    int result = 1;
    if (display && lv_linux_fbdev_set_file(display, fb) == LV_RESULT_OK) {
        if (studio_init(profile, share, backups, artwork)) {
            result = run_ui(keyboard);
        } else {
            fprintf(stderr, "Cannot open the Ziiro profile at %s.\n", profile);
        }
    }
    studio_shutdown();
    nfc_shutdown();
    lv_deinit();
    close(keyboard);
    return result;
}
