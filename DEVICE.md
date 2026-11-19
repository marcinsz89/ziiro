# Ziiro on Cardputer Zero

Version 0.1.0~poc21 is a library and catalog PoC for the stock M5Stack ARM64 Linux image. It opens from HOME, scans local backups, and offers an optional catalog download on first run. It uses the built-in LCD and keyboard. The app supports experimental NFC presentation of a saved raw NTAG215 backup through the U219 Cap.

Use [the project handoff](HANDOFF.md) to see which release is active and what has been verified. For repeated installs from this workspace, use [restricted deployment](../DEPLOY.md).

## Install or update the workspace build

From the Mac workspace root, run the following after the deploy key is provisioned:

```sh
python3 deployment/deploy.py install ziiro \
  ziiro/dist/ziiro_0.1.0~poc21_arm64.deb
```

Open **Ziiro** from HOME. The welcome screen offers **Download catalog**, **Use local backups**, and **Settings**. The **U** key opens Settings from the browser. Copy raw `.bin` backups into `~/ziiro/amiibo_data/` or leave existing files there. For an existing installation, follow the [launcher name refresh](../DEPLOY.md#rename-an-existing-workspace-launcher). For the generated icon on HOME, follow the [one-time icon refresh](../DEPLOY.md#refresh-the-home-icon-on-an-existing-setup).

## Install directly with APT

From the Cardputer Zero workspace on your computer, replace `your-user@your-zero-ip` with your Zero SSH account and address. SSH can be enabled during setup. See the [workspace build guide](../BUILD.md) for SSH and ADB setup.

```sh
device='your-user@your-zero-ip'
scp ./ziiro/dist/ziiro_0.1.0~poc21_arm64.deb "$device":~/
ssh "$device"
```

On the Zero:

```sh
sudo apt install ./ziiro_0.1.0~poc21_arm64.deb
```

Open **Ziiro** from HOME. On first launch, it creates `~/ziiro/amiibo_data/` and `~/ziiro/artwork/` under the account running the launcher. You can create the same folders over SSH before opening HOME by running `ziiro --prepare-library` as that user.

## Copy your backups

On your computer, start with one personal backup in raw 540-byte NTAG215 format. Copy it to the directory created on the device:

```sh
scp '/path/to/my-amiibo.bin' "$device":ziiro/amiibo_data/
```

You can add more backups flat or in subfolders. Press **T** to rescan if Ziiro is already open. **Library** shows entries backed by files. **Tab** toggles the full offline catalog, and **G** groups by series or game franchise. Use **F/X** or **Up/Down** to move, **Enter** to open, and **Esc** to go back or return to HOME.

A small PNG alongside a backup can load directly if it has the same base name. For a collection with larger photos and logos in `Images/` folders, copy the corresponding series directories and a portable thumbnail cache. For example, from the workspace on your computer:

```sh
scp -r './ziiro/amiibo_data/The Legend of Zelda Amiibo' "$device":ziiro/amiibo_data/
scp -r ./ziiro/dist/local-art/.artwork-cache "$device":ziiro/
```

The workspace cache is prepared from the local collection. To regenerate it after adding artwork, run `python3 ziiro/tools/prepare_artwork.py ziiro/amiibo_data ziiro/dist/local-art/.artwork-cache --portable`, with Pillow installed. Press **T** in Ziiro after copying. The cache contains resized images, so copying the full-size originals to the Zero is optional. Keep `amiibo_data` as the folder name and preserve the series subfolders for filename-based artwork matches.

You can also use `~/ziiro/artwork/` with the same layout as the [desktop artwork pack](README.md#add-series-and-figure-art). The package includes a launcher icon and metadata. Your backups and artwork stay under your home directory.

## Bundle a thumbnail pack

An artwork build installs PNGs under `/usr/share/ziiro/artwork/`. Users only need to supply their backups. Figure thumbnails use the model ID, so an image still matches when its `.bin` file is renamed or placed at the top level of `amiibo_data/`.

Build the desktop tools, then export an existing local collection and cache to a new pack directory. Export reads the backup IDs for matching and copies only thumbnails. From the workspace root:

```sh
cmake --build ziiro/build --target ziiro-export-artwork
ziiro/build/ziiro-export-artwork ziiro/assets/catalog.tsv ziiro/amiibo_data \
  ziiro/.artwork-cache ziiro/dist/local-art/id-pack
python3 ziiro/tools/package_device.py ziiro/build-device ziiro/dist/local-art \
  --artwork-pack ziiro/dist/local-art/id-pack --local-test
```

The local test package uses the `+localart1` version suffix. Keep this package for personal testing. To distribute an artwork build, opt in with `--artwork-pack` and a pack cleared for redistribution. An optional `LICENSE.txt` in the pack is installed with the notices. Packaging with no artwork option continues to produce the metadata-only build.

## Test NFC presentation on the Zero

PoC 6 connected after the stock NFC app initialized the Cap. PoC 8 includes the Cap power-up from PoC 7 before detection and presentation. Cold-boot detection and console recognition still need physical verification.

1. With the U219 CC1101 + NFC Cap attached, power-cycle the Zero. Open **Ziiro** first, before opening the stock NFC app. The header shows **NFC checking**, then **NFC ready** or a setup error.
2. Open a figure with a saved backup. Press **Enter** on the figure screen. When the header says **NFC presenting**, three mint rings of different widths expand and fade behind the artwork, which sways by one pixel with each ring. A short cue plays once through the default ALSA output if `aplay` is available. Hold the Cap near the console's amiibo reader and verify the animation stays smooth during the read.
3. Press **Esc** to stop the ripple and release the Cap. Press **Esc** again to return to the list. The **B** screen reports that physical tag capture is unavailable.

Press **R** from the browser or figure screen to retry detection. **NFC top USB** means the top port is set to USB. Set EXT D+/D− to GPIO in **Settings → ExtPort**. **NFC busy** means another process owns a required GPIO line or the SPI device. Close other Cap apps and retry. **NFC no Cap** means SPI opened but returned an unexpected chip identity. **NFC no access** means the launcher user lacks SPI or GPIO access.

- **NFC SPI missing** means `/dev/spidev0.2` is absent. If it only appears after opening the stock app, add `dtoverlay=spi0-spidev2-gpio22-overlay` under `[all]` in `/boot/firmware/config.txt` on the Zero, then reboot. Check that the corresponding `.dtbo` is present in `/boot/firmware/overlays/` first. [M5Stack NFC overlay setup](https://github.com/CardputerZero/Cap-CC1101-NFC#hardware)
- **NFC power off** means Ziiro could not turn on EXT 5 V. For a one-session check, enable **EXT 5V** in **Settings → ExtPort**, then retry. For automatic power-up on each boot, the launcher user needs write access to `/sys/class/leds/ext_5v_out/brightness`. If the image lacks it and the launcher user belongs to `gpio`, install this udev rule on the Zero and reboot. The rule grants the group write access to that one power control. [M5Stack Cap LoRa package permission model](https://github.com/CardputerZero/Cap-LoRa-1262#hardware)

  ```sh
  sudo tee /etc/udev/rules.d/70-ziiro-ext5v.rules >/dev/null <<'EOF'
  ACTION=="add", SUBSYSTEM=="leds", KERNEL=="ext_5v_out", RUN+="/usr/bin/chgrp gpio /sys%p/brightness", RUN+="/usr/bin/chmod g+w /sys%p/brightness"
  EOF
  ```

Ziiro claims GPIO23 IRQ before driving GPIO26 `POWER_EN` high and enabling EXT 5 V. It waits 100 ms before SPI access and leaves the Cap supplies enabled on exit because the Cap and LCD share SPI0. [M5Stack NFC power sequence](https://github.com/CardputerZero/Cap-CC1101-NFC/blob/e0e0902caea8d46abedc8c6f97acfa23e7de92f1/src/hal/cardputerzero_cap_power.cpp)

The ARM64 build includes the MIT-licensed `ntag215.c` and `st25r3916.c` backend from [upstream commit `6f771684`](https://github.com/RiviaRammer/CardputerZeroApps/tree/6f771684388cbaebe3170fcc617760a99a73b0ec/amiibo_sim). The chip check accepts every ST25R3916 revision and retries at the [M5Stack reader's 5 MHz rate](https://github.com/CardputerZero/Cap-CC1101-NFC/blob/e0e0902caea8d46abedc8c6f97acfa23e7de92f1/src/nfc/linux_nfc_backend.cpp) if the first transfer fails or the identity is unexpected. It presents the original raw backup as supplied. The build is cross-compiled and the screen transitions are tested locally. Cap, RF, and console compatibility require a physical test.

## Build the device package from source

On an ARM64 Debian machine or on the Zero itself, install `build-essential`, `cmake`, `git`, and `python3`. Run from the Cardputer Zero workspace root.

```sh
cmake -S ziiro -B ziiro/build-device -DZIIRO_DEVICE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build ziiro/build-device --parallel 2
python3 ziiro/tools/package_device.py ziiro/build-device ziiro/dist
```

The first configure fetches pinned LVGL and libwebp sources. Use `-DLVGL_SOURCE_DIR=/path/to/lvgl` to point it at a local LVGL copy. The bundled WebP converter needs no additional packages on the Zero. The desktop simulator uses its own build directory.

To cross-build on macOS, install CMake, Python 3, and the Zig 0.15.2 toolchain. Use a fresh `build-device` directory if it was configured with another compiler. [Zig downloads](https://ziglang.org/download/)

```sh
cmake -S ziiro -B ziiro/build-device \
  -DZIIRO_DEVICE=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/ziiro/cmake/arm64-linux-zig.cmake"
cmake --build ziiro/build-device --parallel 8
python3 ziiro/tools/package_device.py ziiro/build-device ziiro/dist
```

## If the app exits on launch

Use `journalctl --user -u APPLaunch.service -n 80` on the Zero to look for display, keyboard, or path errors. The app expects a writable 320 × 170, 16-bit framebuffer, normally `/dev/fb_lcd`, and a readable internal keyboard input device. Stock image aliases can vary. The diagnostic overrides are `ZIIRO_FB` and `ZIIRO_KEYBOARD`. [M5Stack LCD and keyboard documentation](https://github.com/CardputerZero/Template/wiki/Peripheral-Development-Guide#lcd)
