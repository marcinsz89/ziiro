# Ziiro 1.0 release checklist

Build the standard `ziiro_1.0.0-1_arm64.deb` using the [device build instructions](DEVICE.md#build-a-debian-package). Keep the Debian package name `ziiro` and use the personal GitHub account `marcinsz89` for the first submission. The first publishing account owns subsequent Store updates.

## Store listing draft

| Field | Value |
| --- | --- |
| App name | Ziiro |
| Summary | Browse your amiibo catalog and local backups on Cardputer Zero. |
| Description | Search figures by series, game franchise, or console and see recorded game effects. Download the catalog once to browse offline, or open a library of your own backups. Optional NFC presentation requires a U219 CC1101 + NFC Cap. |
| Category | Games |
| Author | Marcin Szydłowski |
| License | MIT proposed for original Ziiro code, pending maintainer approval |
| Public source URL | `https://github.com/marcinsz89/ziiro` once available |
| Share code | `ZIRO` proposed, subject to availability |
| Icon | [256 × 256 PNG](store/icon.png) |
| Screenshot | [320 × 170 PNG from the desktop preview](store/welcome.png) |

Set `network` and `additional_hardware` to true. Network access is for optional catalog and artwork downloads. The additional hardware is the optional NFC Cap. Set `camera`, `microphone`, `imu`, `background_service`, and `external_display` to false.

## Before submitting

1. Confirm rights to license Ziiro's original code, add the approved root `LICENSE`, and ship it in the Debian package. Keep the separate [third-party notices](THIRD_PARTY_NOTICES.md). Confirm provenance and redistribution terms for the launcher icon and sound cue.
2. Verify package name, listing title, and share code availability in the [Developer Portal](https://dev.cardputer.cc/). Run the portal package check with the final archive.
3. On an actual Zero, record the SKU, stock image, kernel, and Cap model. Test launch from HOME as the normal user, offline local browsing, catalog download, optional platform logo downloads and offline cache reuse, and exit and relaunch. Verify a cold boot, NFC detection, and console presentation with the U219. Check that an upgrade from the earlier test build preserves the user profile.
4. Record a short device video showing launch, primary features, and exit for the first Store review. A second screenshot showing a populated catalog would help users understand the listing.
5. Submit from the Developer Portal using the account above. Follow the generated [packages pull request](https://github.com/CardputerZero/packages/pulls) through review and registry deployment. Confirm installation and launch through the device Store after publication.

For later versions, keep the package name, owner, listing ID, and share code stable. Build a new archive with a higher Debian control version and verify the upgrade on the device.
