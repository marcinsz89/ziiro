# Ziiro project handoff

Updated November 17, 2026. Start with [the device guide](DEVICE.md) for builds and physical testing and [the deployment guide](../DEPLOY.md) for SSH access and rollback.

## Current release

**PoC 21** is installed on the Zero. Restricted SSH deployment status confirmed PoC 21 is active and PoC 20 is available for rollback. The published HOME entry is named **Ziiro**. Physical LCD verification of PoC 21 is pending. The user removed the earlier APT installation. The workspace app reads `~/ziiro/amiibo_data/`.

| Build | Package under the workspace root |
| --- | --- |
| Installed build with empty first-run catalog | [PoC 21 ARM64](dist/ziiro_0.1.0~poc21_arm64.deb) |

## What works

- The 320 × 170 LVGL app runs on the Zero's LCD and keyboard. The SDL preview runs on the Mac. The offline catalog has 954 metadata entries. Ziiro recursively indexes supported local backups by model ID, groups by series or game franchise, and toggles between the full catalog and backed-up entries.
- The selected figure's preview puts the backup checkmark at the right of the name row. Group previews show the backed-up count as `owned/total`. **F/X** or **Up/Down** browse the current list from the figure screen. **Esc** returns to the last selected figure.
- In PoC 9, the first **Enter** confirms a search query. **F/X** then browse results and a second **Enter** opens the selected figure. **Esc** exits search to the previous browser selection. The sequence passed in the desktop preview.
- PoC 12 starts with no bundled database or figure artwork. **U** opens Settings for catalog downloads and local rescans. The welcome screen also offers Settings. Local search shows source folders or model IDs and groups copies of the same model ID. The desktop preview passed the Settings, rescan, and search tests. **I** opens game details after downloading the catalog.
- PoC 13 opens Library after a scan and shows **G Group** in the browser. Platform browsing lists games on each console, then compatible figures and their effects. In NFC presentation, a scan band moves across the artwork. The desktop preview verified the platform flow and simulated scan band. The physical display still needs a check.
- PoC 14 puts the upstream catalog date at the top right of Settings and adds two saved controls. **Miniature preview** controls the browser artwork, and **Download artwork** fetches missing figure art when opened. The desktop preview verified both settings, a live download, and rendering from the cache offline. Physical LCD verification is pending.
- PoC 15 adds **Download all artwork** in Settings with progress, cancellation and resumption. New downloads use upstream WebP when available and are converted into PNGs for offline display. The desktop preview tested 12 live entries, cancellation, resumption and an offline rerun. Full-catalog timing on the Zero is pending.
- PoC 17 adds a bundled game logo index. **Download game & series logos** fills the same cache that preview downloads use. A desktop preview verified live requests to LaunchBox and Wikimedia and reuse of cached files offline.
- PoC 18 downloads missing artwork after a half-second pause on a highlighted figure, game, series, or franchise when **Download artwork** and **Miniature preview** are enabled. The desktop preview verified figure downloads in Animal Crossing, live game and series logo downloads, and rendering of the updated transparent 3DS, Switch, and Switch 2 symbols. The physical LCD still needs a check after reopening the app.
- PoC 19 reports logo download progress, missing index entries, and download failures in the highlighted preview. Returning to a row retries a failed download. A desktop test reproduced one failed attempt followed by a successful live Animal Crossing series download and verified the New Horizons game logo. It also checked the padded 3DS mark and red Switch marks. On-device download and LCD verification are pending.
- The logos1 update adds Metroid and Resident Evil mappings for Amiibo Series and Game Franchises. The desktop downloader verified both PNGs and reused them across the two views. The package replaces only the logo index in the PoC 19 app payload.
- PoC 21 expands the bundled logo index to 221 entries covering 141 game and console combinations, 32 amiibo series, and 48 game franchises. The generic **Others** series is the only entry without a logo URL. Downloads look up a game by its title and console, with the title ID as a fallback. Curated entries record artwork names and source attribution in the index.
- Host and ARM64 builds passed. The desktop preview downloaded and rendered logos when highlighting Fire Emblem, Chibi-Robo!, and Monster Hunter Stories 3, then reused the cached logos offline. The live source checks covered 47 new unique sources and 137 existing logo source sets. Six oversized logos convert with the 8,192-pixel edge limit and the existing eight-million-pixel image cap. The seven other oversized logos use verified alternatives. Five use LaunchBox thumbnails, Super Mario Galaxy 2 uses the Wii wordmark, and the Switch 2 Edition of The Legend of Zelda: Tears of the Kingdom uses the base Switch game's wordmark. The index records the source consoles for both reused wordmarks.
- The earlier PoC 10 private package includes 263 miniature PNGs matched by model ID or group. The device copy had 271 full-size images removed, freeing 201.6 MiB. The cleanup verified 1,081 unchanged `.bin` files. The original artwork is available in the Mac collection for regenerating thumbnails.
- The user confirmed the mint HOME icon renders after the [one-time launcher icon refresh](../DEPLOY.md#refresh-the-home-icon-on-an-existing-setup). PoC 10 ships a sharper 100 × 100 export to match APPLaunch's icon buffer. Sharpness on the physical LCD still needs a check. [APPLaunch icon buffer](https://github.com/CardputerZero/launcher/blob/master/projects/APPLaunch/main/ui/home_icon_buffer_pool.hpp)
- The ARM64 build can probe the U219 Cap and start or stop presentation of an existing raw NTAG215 backup. PoC 6 adds **R** to recheck the Cap from the browser, accepts all ST25R3916 revisions, retries at the stock reader's 5 MHz SPI rate, and distinguishes power, routing, SPI, permission, and busy errors.

## NFC verification

**Confirmed on this setup:** PoC 6 connects after the stock NFC app initializes the Cap. The stock app also read a physical amiibo. PoC 8 reported **NFC SPI Missing** before the stock app ran, so the boot-time SPI overlay needs verification.

**PoC 8** adds offline search from the browser and a pulsing figure border during NFC presentation. It includes the EXT 5 V and GPIO26 `POWER_EN` setup from PoC 7. See the [cold-boot test and one-time device prerequisites](DEVICE.md#test-nfc-presentation-on-the-zero). Physical verification is pending.

**Still to test:** a real console recognizing a saved amiibo presented by Ziiro. **NFC Ready** means the Cap probe succeeded. **NFC Presenting** means the backend is listening. Record console recognition separately.

1. Power-cycle the Zero with the U219 Cap attached and open **Ziiro** before the stock NFC app.
2. Check for **NFC Ready**. Press **R** to retry if needed and record the exact header text if the probe fails.
3. Open a figure with a known-good personal 540-byte NTAG215 backup. Press **Enter** to present it, and bring the Cap near the console when the header shows **NFC Presenting**.
4. Record the Zero model and OS build, game, backup tested, console response, and whether **Esc** stops the session. Start a second session to confirm the Cap can be released and acquired again.

If the Cap connects but the console rejects the scan, compare the same backup and hardware with upstream `amiibo_sim`. Use [the device troubleshooting notes](DEVICE.md#test-nfc-presentation-on-the-zero) and the APPLaunch journal to distinguish startup errors from console rejection.

## Resume development

Run from the Cardputer Zero workspace root on the Mac. The existing `.env` and restricted deploy key are already provisioned for this workspace. Keep those local files private. Use the regular `pi` login for SFTP or administrator changes such as the HOME icon refresh.

```sh
python3 deployment/deploy.py status ziiro
cmake --build ziiro/build-device --parallel 8
python3 ziiro/tools/package_device.py ziiro/build-device ziiro/dist
python3 deployment/deploy.py install ziiro \
  ziiro/dist/ziiro_0.1.0~poc21_arm64.deb
```

Deploy only when you want to install or update the device. For another build, use the filename printed by the packaging command. The current ARM64 CMake cache references temporary Zig, LVGL and libwebp paths. If they have disappeared, configure a fresh build directory using [the Zig 0.15.2 instructions](DEVICE.md#build-the-device-package-from-source). The CMake file pins LVGL and libwebp.

| Area | Start here |
| --- | --- |
| UI, key navigation, and NFC state labels | [studio.cpp](studio.cpp) |
| Backup indexing and artwork matching | [catalog.cpp](catalog.cpp), [archive_artwork.cpp](archive_artwork.cpp) |
| NFC worker, probe, and presentation | [nfc.c](nfc.c), [vendored amiibo backend](third_party/amiibo-zero/) |
| Device and simulator entry points | [device.c](device.c), [preview.c](preview.c) |
| Catalog, thumbnails, and Debian packaging tools | [tools/](tools/) |
| Restricted install, status, rollback, and cleanup | [deployment/](../deployment/) |

## Planned next

The requested favourites feature is scoped in [FAVOURITES_PLAN.md](FAVOURITES_PLAN.md): mark by model ID and add a flat alphabetical Favourites grouping. It is not implemented. Public release requirements and upgrade procedures are recorded in [APPSTORE.md](../APPSTORE.md).

1. Validate presentation, stop, and repeated sessions on the console. Fix any Cap or RF issues using the observed result and the upstream comparison.
2. Add physical tag capture in Ziiro using the stock reader's NFC-A and Type 2 read path as a starting point. First verify readable pages on a tag you own. Save a backup only after validating a complete supported dump. Test tag removal, incomplete reads, duplicate captures, and capture followed by presentation. [Official Cap reader source](https://github.com/CardputerZero/Cap-CC1101-NFC)
3. Finish release packaging and updates. The current restricted SSH workflow handles development builds. For a public build, keep metadata and license notices, clear any artwork intended for redistribution, and verify Store installation, upgrade, and removal before publishing.
