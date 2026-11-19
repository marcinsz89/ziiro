# Ziiro on Cardputer Zero

Ziiro 1.0 targets the stock M5Stack ARM64 Linux image and uses the built-in 320 × 170 LCD and keyboard. Catalog and library browsing work without a Cap. NFC presentation requires the U219 CC1101 + NFC Cap and access to the device's GPIO and SPI interfaces.

## Install the release package

Once Ziiro is published, install it from the Cardputer Zero AppStore and launch **Ziiro** from HOME. To test the same package before publication, copy `ziiro_1.0.0-1_arm64.deb` to the Zero and run:

```sh
sudo apt install ./ziiro_1.0.0-1_arm64.deb
```

Run the app as the normal launcher user. On first launch, choose **Download catalog** or **Use local backups**. The [README](README.md) describes browsing, search, and controls.

## User files and upgrades

The device profile is `~/ziiro/` for the account that runs HOME. Launch once or run `ziiro --prepare-library` as that account to create the folders.

| Path under `~/ziiro/` | Contents |
| --- | --- |
| `amiibo_data/` | Backups you supply |
| `artwork/` | Optional custom PNGs |
| `catalog.tsv` | Downloaded catalog |
| `preferences.tsv` | Preview and artwork settings |
| `downloaded-artwork/`, `downloaded-logos/` | Cached images |

The package installs app files under `/usr` and keeps backups and settings in the existing profile. If you are moving from a development launcher entry, check for duplicate HOME entries before testing and back up the profile before changing installations.

## NFC setup and troubleshooting

Attach the U219 Cap and set EXT D+/D− to GPIO in **Settings → ExtPort**. Once **NFC ready** appears, open a figure with a backup and press **Enter**. Wait for **NFC presenting**, then try the console reader. Press **Esc** to stop. The animation reports the app's session state. If ALSA's `aplay` is available, a short cue also plays.

Press **R** to retry detection. If the app reports a setup issue:

- **NFC top USB**: switch the top port to GPIO.
- **NFC power off**: enable **EXT 5V** in system ExtPort settings for the current session.
- **NFC busy**: close other Cap apps and retry.
- **NFC SPI missing**: the SPI device is absent. See the boot overlay workaround below.
- **NFC no access**: the launcher user cannot access the SPI or GPIO interfaces. Check permissions for that user on the stock image.

If Ziiro exits on launch, check `journalctl --user -u APPLaunch.service -n 80` for display or keyboard errors. The app expects a writable 320 × 170 RGB565 framebuffer, usually `/dev/fb_lcd`, and a readable keyboard input device. The diagnostic overrides are `ZIIRO_FB` and `ZIIRO_KEYBOARD`.

### NFC SPI missing

If `/dev/spidev0.2` is absent after boot, check that `/boot/firmware/overlays/spi0-spidev2-gpio22-overlay.dtbo` exists. If the file is missing, use the [official M5Stack Cap setup](https://github.com/CardputerZero/Cap-CC1101-NFC#hardware) for your OS image.

If the overlay exists, add the following line under `[all]` in `/boot/firmware/config.txt` if it is not already present:

```ini
dtoverlay=spi0-spidev2-gpio22-overlay
```

Reboot, check that `/dev/spidev0.2` exists, then launch Ziiro from HOME and look for **NFC ready**. This also covers the case where the SPI device only appears after opening the stock NFC app.

## Build a Debian package

Install CMake 3.20+, Git, Python 3.10+, and a C/C++ toolchain on ARM64 Debian. Run from the repository root:

```sh
cmake -S . -B build-device -DZIIRO_DEVICE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-device --parallel 2
python3 tools/package_device.py build-device dist
```

On macOS, use Zig 0.15.2 and add `-DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/arm64-linux-zig.cmake"` to the configure command. Use a fresh build directory when changing toolchains. The package is written to `dist/ziiro_1.0.0-1_arm64.deb`. Package without an artwork pack for the Store.
