# Ziiro for Cardputer Zero

The desktop preview renders an offline amiibo catalog and a keyboard-driven backup library at the Zero's native 320 × 170 resolution using LVGL 9.5. A new profile starts with an empty catalog. Ziiro can download catalog data and game compatibility from AmiiboAPI or browse your local raw backups.

For the current release, verification status, and next steps, start with the [project handoff](HANDOFF.md). For setup and testing, see the [device guide](DEVICE.md).

## Run the preview

Install CMake, a C/C++ compiler, pkg-config, SDL2 development files and Python 3.9 or newer. For example, on macOS use `brew install cmake pkg-config sdl2 python`. CMake fetches pinned LVGL 9.5.0 and libwebp 1.6.0 sources on the first build. Pass `-DLVGL_SOURCE_DIR=/path/to/lvgl` to use an existing LVGL checkout.

Run these commands from the Cardputer Zero root.

```sh
cmake -S ziiro -B ziiro/build
cmake --build ziiro/build --parallel
./ziiro/build/ziiro-preview --profile "$(mktemp -d /tmp/ziiro-first-run.XXXXXX)"
```

Reuse the same profile path to test an offline reopen. Without `--profile`, the preview saves state in `build/profile/`. On the welcome screen, use **F/X** to choose **Download catalog**, **Use local backups**, or **Settings**, then **Enter** to confirm. Press **Esc** to cancel an in-progress download.

Move with **F/X** or **Up/Down**, open an entry with **Enter**, and use **Esc** to return or exit. **T** rescans backups and returns to **Library**. **Tab** switches between Library and the full Catalog after downloading it. **G** cycles through amiibo series, game franchises, and platforms. **B** previews Backup, and **U** opens Settings for catalog downloads, update checks, and local rescans. Add `--screenshot /tmp/ziiro.bmp` to render a single frame and exit.

In platform view, select a console, then a game to list the amiibo with recorded effects for that title. Press **I** on a figure to read its effects for the selected game. A figure opened from search, series, or franchise shows its full compatibility list. [AmiiboAPI usage fields](https://www.amiiboapi.org/docs/#show-usage)

**Miniature preview** in Settings controls the browser thumbnails and preview card. Turning it off gives the list the full width and keeps the figure screen available. **Download artwork** starts off. Pause on a highlighted figure, game, series, or franchise to fetch missing artwork for the right-hand preview. Opening a figure also fetches missing artwork. Local artwork takes precedence, and cached downloads work offline.

After downloading the catalog, use **Settings → Download all figure artwork** for missing figure images or **Download game & series logos** for missing logos. **Esc** cancels the run and keeps completed files. Select the action again to resume. Figure images use WebP when available and are converted once to PNGs with a longest edge of 128 pixels. Downloads use the profile's `downloaded-artwork/` and `downloaded-logos/` directories. The logo downloader uses the bundled index and fetches individual PNGs without an account or API key.

Press **S** in the browser to search the current Library or Catalog offline by name, series, or franchise. Type to filter and press **Enter** to confirm. Use **F/X** or **Up/Down** to browse the results, and press **Enter** again to open the selected figure. **Esc** exits search to your previous browser selection. From an open figure, **Esc** first returns to the results.

## Open a folder of backups

By default the desktop preview scans the profile's `amiibo_data/` folder. Use `--library` to browse a different collection. You can drop backups directly into that folder or organize them in subfolders. **Library** only displays entries with a local backup.

```sh
./ziiro/build/ziiro-preview --library "/path/to/my backups"
```

Ziiro searches subdirectories for raw NTAG215 `.bin` files (540 bytes, with valid UID check bytes). It reads the public amiibo model ID and joins it to the offline catalog. A Smash Bros. Link backup appears under Super Smash Bros. in series view and under The Legend of Zelda in franchise view. The name and directory of the file can change without affecting that match. The model ID match identifies the catalog entry. The console still determines whether a backup is usable in a game.

Copies of the same model ID appear under one entry. Unrecognized IDs appear in **Unidentified** with the original filename and a subtitle showing the source folder or model ID. The importer skips symlinks, hidden folders, `!Essential Files`, and files outside the supported raw layout. It reads the original paths in place. Press **T** after adding files. The terminal reports matched, unidentified, and skipped file counts.

## Add series and figure art

The local `amiibo_data/` layout can use `Image` or `Images` directories. Prepare its image cache once, then run the preview as usual. Run the same command again after adding or replacing artwork. The tool requires Python 3.10 or newer and Pillow. For AVIF images, use a Pillow build with AVIF support.

```sh
python3 ziiro/tools/prepare_artwork.py ziiro/amiibo_data ziiro/build/profile/.artwork-cache
./ziiro/build/ziiro-preview --library ziiro/amiibo_data
```

The tool writes 96-pixel thumbnails and a small index to `.artwork-cache/`. Ziiro matches logos by series or franchise name and looks for figure art by backup filename or catalog name. If a backup is in a flat folder, Ziiro can match art with an unambiguous name in the catalog's series or franchise. The original collection stays in place. Both the collection and generated cache are ignored by Git.

Ziiro uses color and initials when artwork is missing. Supply a separate artwork directory with `--artwork "/path/to/art"` to use your own pack. Use PNGs up to 256 × 256, preferably square and transparent. The small screen scales them for the row and selected card.

| Artwork | Example path under the artwork directory |
| --- | --- |
| Release series | `series/animal-crossing.png` |
| Game franchise | `games/super-mario.png` |
| Game title in platform view | `titles/switch/animal-crossing-new-horizons.png` |
| Console | `platforms/switch-2.png` |
| Individual figure, by exact 16-digit model ID | `figures/0000000000340102.png` |

Series and game names use lowercase ASCII letters, digits and hyphens. An image directly under `titles/` can be shared across consoles. A console-specific title image takes priority. If `my-mario.bin` has a neighboring `my-mario.png`, Ziiro displays that image ahead of ID-based artwork. The four bundled [platform logos](assets/Platform-logos.txt) can be overridden with matching PNGs in the user's artwork directory. Images are read locally. Figure artwork can come from your local library or be downloaded from the catalog.

## Offline catalog

The catalog download stores IDs, names, release series, game franchises, item types, game effects, and Nintendo title IDs in the profile's `catalog.tsv` with the upstream timestamp. The file can contain entries beyond the formats supported by the NTAG215 importer. Figures become backed up in the library when a matching local file appears. A manual check reuses an up-to-date cache and upgrades older catalog files to include title IDs.

The bundled logo index covers the 2026-11-17 AmiiboAPI snapshot of 954 figures. It includes logo sources for all 141 game-and-console entries, all 48 game franchises, and 31 of 32 amiibo release series. The generic **Others** series has an empty URL list. Game downloads match by name and console, with an unambiguous Nintendo title ID match available for renamed titles. Sources include LaunchBox Clear Logos, Wikimedia Commons, Nintendo's Smash Bros. website, and Konami. Some LaunchBox entries use the database's smaller PNG thumbnails. When a catalog entry uses artwork from a related edition, platform, or franchise, the index records the artwork's name or source platform. [Bundled logo index](assets/game-logos.json)

To refresh the index, save the AmiiboAPI `?showusage` JSON response and a LaunchBox `Metadata.zip` snapshot, then run from the workspace root.

```sh
python3 ziiro/tools/build_game_logos.py /path/to/showusage.json /path/to/Metadata.zip ziiro/assets/game-logos.json
```

The generator keeps curated artwork for entries still in the catalog and adds rows for new entries. A row with an empty URL list stays visible in the index and is skipped during downloads. [LaunchBox media guide](https://feedback.launchbox-app.com/en/help/articles/7997304-using-games-database-media-in-launchbox) · [AmiiboAPI usage fields](https://www.amiiboapi.org/docs/#show-usage)

The [AmiiboAPI](https://amiiboapi.org/) also publishes figure image URLs keyed by the two halves of the model ID. A local image named `icon_00000000-00340102.png` can go in `amiibo_data/Images/` for the cache tool to match by exact ID. It can also be saved as `figures/0000000000340102.png` in a custom art pack. The metadata catalog and local art work offline.

The earlier pinned snapshot remains available under `assets/catalog.tsv` for development tools. [Pinned metadata source](https://github.com/8bitDream/AmiiboAPI/blob/855a0ff4308278de756bed01de1eca89936f456b/database/amiibo.json) · [AmiiboAPI MIT license](assets/AmiiboAPI-LICENSE.txt) · [Wire layout reference](https://github.com/arnaldolima1/totk-recomp/blob/main/runtime/amiibo.c)

## Stage a preview for distribution

```sh
cmake --install ziiro/build --prefix /tmp/ziiro-stage
```

The staged desktop preview contains the executable, catalog downloader, and dependency notices. Tag backups, keys, and the local catalog and artwork caches belong in the user profile.

## What the existing app supports

| Feature | Upstream `amiibo_sim` 0.1.9-1 |
| --- | --- |
| Library | Browses local 540-byte NTAG215 backups and subfolders on the Zero. Refreshes the current directory and scrolls long selected names. |
| Hardware | Detects the U219 Cap, checks SPI access and retries initialization. Provides a separate administrator-run setup helper for the overlay and device permissions. |
| Presentation | Presents a selected backup to a reader, reports incomplete reads or replies sent, and stops on demand. The console determines whether a game accepted the scan. |
| File validation | The CLI's `check` validates file length and UID check bytes. Its `probe` checks the chip. The app also documents optional UID rotation with user-provided inputs. |
| Capture and updates | The CLI only exposes `check`, `probe` and `emulate`. The app imports existing files. Store handles released package upgrades. |

[Upstream app documentation](https://github.com/RiviaRammer/CardputerZeroApps/blob/main/amiibo_sim/README.md) · [CLI source](https://github.com/RiviaRammer/CardputerZeroApps/blob/main/amiibo_sim/src/main.c) · [Device Store update mechanism](https://github.com/CardputerZero/Store#backend-commands)

## NFC backend and capture plan

Ziiro builds the MIT-licensed NTAG215 target and Linux SPI/GPIO code from `amiibo-zero` into its ARM64 executable. It runs those operations in a child process, watches for backend readiness, and stops the worker when leaving an active session. The [device guide](DEVICE.md#test-nfc-presentation-on-the-zero) lists the supported Cap, SPI rates, and pinned source revision.

For Backup, start from the official Cap NFC reader's polling and Type 2 read path. Its documented app shows NDEF records. A complete NTAG215 backup will need its own read and validation workflow. Test first with a generic tag you control. Detect the tag type, collect the pages the tag makes readable, and record failures as incomplete reads. Only put a validated, complete backup into the emulation library. Save new files atomically and keep existing copies when a capture fails or repeats. The [NTAG215 datasheet](https://www.nxp.com/docs/en/data-sheet/NTAG213_215_216.pdf) distinguishes the 504 user bytes from the 540-byte full memory layout. [Official Cap reader source](https://github.com/CardputerZero/Cap-CC1101-NFC)

Development builds use [restricted SSH deployment](../DEPLOY.md). Settings checks catalog data. Store installs and app updates are planned for public releases. [Store upgrade flow](https://github.com/CardputerZero/Store#backend-commands)
