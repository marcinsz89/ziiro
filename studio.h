#ifndef AMIIBO_STUDIO_H
#define AMIIBO_STUDIO_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { STUDIO_WIDTH = 320, STUDIO_HEIGHT = 170 };

typedef enum {
    STUDIO_UP,
    STUDIO_DOWN,
    STUDIO_SELECT,
    STUDIO_BACK,
    STUDIO_SETTINGS,
    STUDIO_VIEW,
    STUDIO_GROUP,
    STUDIO_REFRESH,
    STUDIO_SEARCH,
    STUDIO_GAMES,
    STUDIO_FAVOURITE
} StudioInput;

bool studio_init(const char *profile, const char *share, const char *backups, const char *custom_artwork);
int studio_convert_artwork(void);
void studio_shutdown(void);
bool studio_input(StudioInput input);
bool studio_search_editing(void);
void studio_search_edit(char key);
void studio_retry_nfc(void);

#ifdef __cplusplus
}
#endif

#endif
