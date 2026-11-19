"""Package the ARM64 Linux build using the CMake install file list."""

import argparse
import re
import shutil
import struct
import subprocess
import tarfile
import tempfile
from pathlib import Path


def root_owner(entry: tarfile.TarInfo) -> tarfile.TarInfo:
    """Set the installed files and directories to root ownership."""
    entry.uid = entry.gid = 0
    entry.uname = entry.gname = "root"
    return entry


def write_debian_archive(output: Path, members: list[Path]) -> None:
    """Write the classic ar container required by Debian packages."""
    with output.open("wb") as archive:
        archive.write(b"!<arch>\n")
        for member in members:
            size = member.stat().st_size
            header = f"{member.name:<16}{0:<12}{0:<6}{0:<6}{0o100644:<8o}{size:<10}`\n"
            archive.write(header.encode("ascii"))
            with member.open("rb") as contents:
                shutil.copyfileobj(contents, archive)
            if size % 2:
                archive.write(b"\n")


def stage_artwork(pack: Path, prefix: Path) -> None:
    """Install an image-only artwork pack alongside the catalog."""
    if not pack.is_dir() or pack.is_symlink():
        raise ValueError("Artwork pack must be a regular directory.")

    output = prefix / "share" / "ziiro" / "artwork"
    staged = 0
    for kind in ("figures", "series", "games"):
        folder = pack / kind
        if folder.is_symlink():
            raise ValueError(f"Artwork folder cannot be a symlink: {folder}")
        if not folder.is_dir():
            continue
        pattern = r"[0-9a-f]{16}\.png" if kind == "figures" else r"[a-z0-9]+(?:-[a-z0-9]+)*\.png"
        for path in sorted(folder.iterdir()):
            if path.is_symlink() or not path.is_file() or not re.fullmatch(pattern, path.name):
                raise ValueError(f"Invalid artwork filename: {path}")
            with path.open("rb") as image:
                content = image.read(1024 * 1024 + 1)
            if (len(content) < 33 or len(content) > 1024 * 1024
                    or content[:16] != b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR"):
                raise ValueError(f"Artwork must be a PNG under 1 MiB: {path}")
            width, height = struct.unpack(">II", content[16:24])
            if not (0 < width <= 256 and 0 < height <= 256):
                raise ValueError(f"Artwork must fit within 256 x 256 pixels: {path}")
            (output / kind).mkdir(parents=True, exist_ok=True)
            (output / kind / path.name).write_bytes(content)
            staged += 1

    if not staged:
        raise ValueError("No thumbnails found under figures/, series/, or games/.")
    notice = pack / "LICENSE.txt"
    if notice.is_file() and not notice.is_symlink():
        shutil.copyfile(notice, prefix / "share" / "doc" / "ziiro" / "Artwork-LICENSE.txt")
    print(f"Staged {staged} thumbnails")


def package_device(
    build: Path, destination: Path, *, artwork_pack: Path | None = None, local_test: bool = False
) -> Path:
    """Build a Debian archive from a configured Ziiro device build."""
    build = build.resolve(strict=True)
    with (build / "ziiro").open("rb") as binary:
        header = binary.read(20)
    if header[:6] != b"\x7fELF\x02\x01" or header[18:20] != b"\xb7\x00":
        raise ValueError("The Ziiro executable must be a little-endian ARM64 ELF file.")

    control = (build / "debian-control").read_text()
    version_line = next(line for line in control.splitlines() if line.startswith("Version: "))
    version = version_line.split(": ", 1)[1]
    if artwork_pack:
        version += "+localart1" if local_test else "+art1"
        control = control.replace(version_line, f"Version: {version}", 1)
    destination = destination.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    output = destination / f"ziiro_{version}_arm64.deb"
    with tempfile.TemporaryDirectory(dir=destination) as scratch:
        work = Path(scratch)
        prefix = work / "data" / "usr"
        subprocess.run(["cmake", "--install", str(build), "--prefix", str(prefix)], check=True)
        if artwork_pack:
            stage_artwork(artwork_pack, prefix)
        (work / "debian-binary").write_bytes(b"2.0\n")
        (work / "control").write_text(control, encoding="utf-8")
        with tarfile.open(work / "control.tar.gz", "w:gz", format=tarfile.GNU_FORMAT) as archive:
            archive.add(work / "control", arcname="./control", filter=root_owner)
        with tarfile.open(work / "data.tar.gz", "w:gz", format=tarfile.GNU_FORMAT) as archive:
            archive.add(prefix, arcname="./usr", filter=root_owner)
        members = [work / name for name in ("debian-binary", "control.tar.gz", "data.tar.gz")]
        write_debian_archive(work / output.name, members)
        (work / output.name).replace(output)
    return output


def main() -> None:
    """Read the build and output paths from the command line."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--artwork-pack", type=Path, help="Bundle an ID-indexed thumbnail pack.")
    parser.add_argument("--local-test", action="store_true", help="Name an artwork package as a local build.")
    args = parser.parse_args()
    if args.local_test and not args.artwork_pack:
        parser.error("--local-test requires --artwork-pack")
    print(package_device(args.build, args.destination, artwork_pack=args.artwork_pack, local_test=args.local_test))


if __name__ == "__main__":
    main()
