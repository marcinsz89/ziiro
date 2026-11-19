"""Resize a local amiibo collection's artwork and index it for Ziiro."""

import argparse
import hashlib
import io
import os
import tempfile
from pathlib import Path

from PIL import Image

IMAGE_FORMATS = ["PNG", "JPEG", "WEBP", "AVIF"]
IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp", ".avif"}


def prepare_artwork(collection: Path, cache: Path, *, portable: bool = False) -> None:
    """Build an image-only cache outside the original collection."""
    collection = collection.resolve(strict=True)
    cache = cache.resolve()
    if not collection.is_dir() or cache.is_relative_to(collection):
        raise ValueError("Choose an existing collection and a separate cache directory.")

    cache.mkdir(parents=True, exist_ok=True)
    thumbnails = cache / "thumbs"
    thumbnails.mkdir(exist_ok=True)
    collection_name = collection.name if portable else str(collection)
    if any(char in collection_name for char in "\t\r\n"):
        raise ValueError("The collection path contains an unsupported character.")
    rows = [f"portable\t{collection_name}" if portable else collection_name]
    skipped = 0
    for current, directories, names in os.walk(collection):
        directory = Path(current)
        directories[:] = sorted(
            name for name in directories
            if not name.startswith(".") and name.casefold() != "!essential files"
            and not (directory / name).is_symlink()
        )
        scope = directory.parent if directory.name.casefold() in {"image", "images"} else directory
        if not scope.is_relative_to(collection):
            scope = collection
        for name in sorted(names):
            path = directory / name
            if name.startswith(".") or path.is_symlink() or not path.is_file():
                continue
            if path.suffix.casefold() not in IMAGE_SUFFIXES:
                continue
            fields = (scope.relative_to(collection).as_posix(), path.stem)
            if any("\t" in field or "\n" in field or "\r" in field for field in fields):
                skipped += 1
                continue
            try:
                with Image.open(path, formats=IMAGE_FORMATS) as source:
                    if source.width * source.height > 16_000_000:
                        skipped += 1
                        continue
                    image = source.convert("RGBA")
                    bounds = image.getchannel("A").getbbox()
                    if bounds is None:
                        skipped += 1
                        continue
                    image = image.crop(bounds)
                    image.thumbnail((96, 96), Image.Resampling.LANCZOS)
                    output = io.BytesIO()
                    image.save(output, format="PNG", optimize=True)
            except (OSError, Image.DecompressionBombError):
                skipped += 1
                continue
            content = output.getvalue()
            digest = hashlib.sha256(content).hexdigest()
            thumbnail = thumbnails / f"{digest}.png"
            if not thumbnail.is_file():
                with tempfile.NamedTemporaryFile(dir=thumbnails, delete=False) as pending:
                    pending.write(content)
                Path(pending.name).replace(thumbnail)
            rows.append("\t".join((*fields, digest)))

    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=cache, delete=False) as index:
        index.write("\n".join(rows) + "\n")
    Path(index.name).replace(cache / "index.tsv")
    print(f"Prepared {len(rows) - 1} images ({skipped} skipped)")


def main() -> None:
    """Prepare local thumbnails from the paths provided on the command line."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("collection", type=Path)
    parser.add_argument("cache", type=Path)
    parser.add_argument("--portable", action="store_true", help="Allow copying the cache with the collection.")
    args = parser.parse_args()
    prepare_artwork(args.collection, args.cache, portable=args.portable)


if __name__ == "__main__":
    main()
