#ifndef ZIIRO_NFC_H
#define ZIIRO_NFC_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NFC_UNAVAILABLE,
    NFC_CHECKING,
    NFC_READY,
    NFC_TOP_USB,
    NFC_POWER_OFF,
    NFC_SPI_MISSING,
    NFC_NO_CAP,
    NFC_PERMISSION,
    NFC_BUSY,
    NFC_STARTING,
    NFC_PRESENTING,
    NFC_STOPPING,
    NFC_ERROR
} NfcState;

void nfc_probe(void);
void nfc_present(const char *backup);
void nfc_stop(void);
NfcState nfc_poll(void);
void nfc_shutdown(void);

#ifdef ZIIRO_DEVICE
int nfc_worker(int argc, char **argv);
#endif

#ifdef __cplusplus
}
#endif

#endif
