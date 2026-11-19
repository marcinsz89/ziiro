"""Fetch Ziiro's local catalog and figure artwork from AmiiboAPI."""

from __future__ import annotations

import argparse
import fcntl
import gzip
import json
import os
import re
import signal
import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from urllib.error import HTTPError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener, urlopen

API = "https://www.amiiboapi.org/api/"
HEADER = "#amiiboapi\t"
STAMP = re.compile(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(?:\.[0-9]{1,6})?")
MODEL_ID = re.compile(r"[0-9a-f]{16}")
LAUNCHBOX_PNG = re.compile(r"(?:r2_)?[a-f0-9-]{36}\.png", re.I)
IMAGE_SIZE_LIMIT = 512 * 1024
IMAGE_DIMENSION_LIMIT = 512
IMAGE_CACHE_LIMIT = 64 * 1024 * 1024
LOGO_SIZE_LIMIT = 8 * 1024 * 1024
LOGO_CACHE_LIMIT = 32 * 1024 * 1024
ARTWORK_WORKERS = 4
PLATFORMS = {
    "gamesSwitch2": "Switch 2",
    "gamesSwitch": "Switch",
    "games3DS": "3DS",
    "gamesWiiU": "Wii U",
}


def fetch_json(endpoint: str, limit: int) -> dict:
    """Read a bounded JSON document from AmiiboAPI."""
    request = Request(API + endpoint, headers={
        "Accept": "application/json",
        "Accept-Encoding": "gzip",
        "User-Agent": "Ziiro/0.1",
    })
    with urlopen(request, timeout=15) as response:
        if response.headers.get("Content-Encoding") == "gzip":
            with gzip.GzipFile(fileobj=response) as body:
                raw = body.read(limit + 1)
        else:
            raw = response.read(limit + 1)
    if len(raw) > limit:
        raise ValueError("AmiiboAPI response exceeded the download limit")
    result = json.loads(raw)
    if not isinstance(result, dict):
        raise ValueError("Expected a JSON object from AmiiboAPI")
    return result


def last_updated() -> str:
    """Read and validate the upstream catalog marker."""
    value = fetch_json("lastupdated", 4096).get("lastUpdated")
    if not isinstance(value, str) or STAMP.fullmatch(value) is None:
        raise ValueError("Invalid AmiiboAPI update timestamp")
    return value


def text_field(value: str) -> str:
    """Normalize a required text field before writing it as TSV."""
    if not isinstance(value, str) or "\0" in value:
        raise ValueError("Invalid AmiiboAPI text field")
    field = " ".join(value.split())
    if not field:
        raise ValueError("Empty AmiiboAPI text field")
    return field


def title_ids(value: list[str]) -> set[str]:
    """Use valid Nintendo title IDs with consistent casing."""
    return {item.lower() for item in value if isinstance(item, str) and MODEL_ID.fullmatch(item.lower())}


def catalog_lines(payload: dict, timestamp: str) -> list[str]:
    """Validate and normalize metadata and game effects for the offline loader."""
    figures = payload["amiibo"]
    if not isinstance(figures, list) or not figures:
        raise ValueError("AmiiboAPI returned an empty or invalid catalog")

    lines = [HEADER + timestamp]
    seen_ids = set()
    for figure in figures:
        identifier = (figure["head"] + figure["tail"]).lower()
        if MODEL_ID.fullmatch(identifier) is None or identifier in seen_ids:
            raise ValueError(f"Invalid or duplicate model ID: {identifier}")
        seen_ids.add(identifier)
        fields = [text_field(figure[key]) for key in ("name", "amiiboSeries", "gameSeries", "type")]
        lines.append("\t".join([identifier, *fields]))
        for key, platform in PLATFORMS.items():
            for game in figure.get(key) or []:
                name = text_field(game["gameName"])
                ids = ",".join(sorted(title_ids(game.get("gameID") or [])))
                uses = game.get("amiiboUsage") or []
                if not uses:
                    lines.append("\t".join(("game", platform, name, "Unknown", "No effect recorded.", ids)))
                for usage in uses:
                    effect = text_field(usage["Usage"])
                    write = usage.get("write")
                    access = "Read/write" if write is True else "Read only" if write is False else "Unknown"
                    lines.append("\t".join(("game", platform, name, access, effect, ids)))
    return lines


def sync(destination: Path, *, force: bool = False) -> int:
    """Keep the last good catalog and return 3 when it is already current."""
    timestamp = last_updated()
    if not force and destination.is_file():
        with destination.open(encoding="utf-8") as cached:
            if cached.readline().rstrip("\n") == HEADER + timestamp:
                first_game = next((line for line in cached if line.startswith("game\t")), "")
                if not first_game or first_game.count("\t") >= 5:
                    return 3

    lines = catalog_lines(fetch_json("amiibo/?showusage", 12 * 1024 * 1024), timestamp)
    if last_updated() != timestamp:
        raise ValueError("AmiiboAPI changed during the download. Check again.")

    write_atomically(destination, ("\n".join(lines) + "\n").encode())
    return 0


def write_atomically(destination: Path, data: bytes) -> None:
    """Replace a cache file after its complete contents are on disk."""
    with tempfile.NamedTemporaryFile(dir=destination.parent, prefix=".download-", delete=False) as temporary:
        staged = Path(temporary.name)
        try:
            temporary.write(data)
            temporary.flush()
            os.fsync(temporary.fileno())
            temporary.close()
            staged.replace(destination)
        finally:
            staged.unlink(missing_ok=True)


def allowed_image_url(url: str, model: str, extension: str) -> bool:
    """Accept known AmiiboAPI GitHub image paths for the requested model."""
    if not isinstance(url, str):
        return False
    origin = urlsplit(url)
    image = f"icon_{model[:8]}-{model[8:]}"
    folder = "webp" if extension == "webp" else "images"
    path = rf"/(?:8bitdream|n3evin)/amiiboapi/[^/]+/{folder}/{image}(?:_[0-9a-f]{{8}})?\.{extension}"
    return (origin.scheme == "https" and origin.netloc == "raw.githubusercontent.com" and
            not origin.query and not origin.fragment and re.fullmatch(path, origin.path, re.I) is not None)


def fetch_image(url: str, model: str, extension: str) -> bytes:
    """Read an approved upstream image within the encoded size limit."""
    request = Request(url, headers={"User-Agent": "Ziiro/0.1"})
    with urlopen(request, timeout=8) as response:
        if not allowed_image_url(response.geturl(), model, extension):
            raise ValueError("Artwork request redirected outside the image source")
        data = response.read(IMAGE_SIZE_LIMIT + 1)
    if len(data) > IMAGE_SIZE_LIMIT:
        raise ValueError("Artwork exceeded the download limit")
    return data


def valid_png(header: bytes, end: bytes, size: int) -> bool:
    """Check the PNG envelope and dimensions supported by the renderer."""
    if (not 45 <= size <= IMAGE_SIZE_LIMIT or len(header) < 24 or
            header[:16] != b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR" or
            end != b"\x00\x00\x00\x00IEND\xaeB\x60\x82"):
        return False
    width, height = struct.unpack_from(">II", header, 16)
    return 0 < width <= IMAGE_DIMENSION_LIMIT and 0 < height <= IMAGE_DIMENSION_LIMIT


def artwork_converter() -> Path:
    """Find the executable beside the desktop worker or in the device release."""
    share = Path(__file__).resolve().parent
    preview = share / "ziiro-preview"
    return preview if preview.is_file() else share.parent.parent / "bin" / "ziiro"


def read_artwork(model: str, figure: dict, converter: Path) -> bytes:
    """Prefer compact WebP transfers and retain compatible PNGs as a fallback."""
    webp = figure.get("imgwebp")
    if allowed_image_url(webp, model, "webp"):
        try:
            data = fetch_image(webp, model, "webp")
            converted = subprocess.run(
                [converter, "--convert-artwork"], input=data, stdout=subprocess.PIPE, timeout=5, check=True,
            ).stdout
            if valid_png(converted[:24], converted[-12:], len(converted)):
                return converted
        except HTTPError as error:
            if error.code not in (404, 410):
                raise
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired, ValueError):
            pass
    png = figure.get("image")
    if allowed_image_url(png, model, "png"):
        data = fetch_image(png, model, "png")
        if valid_png(data[:24], data[-12:], len(data)):
            return data
    raise ValueError("No compatible artwork available")


def cached_artwork(path: Path, size: int) -> bool:
    """Reuse supported cached PNGs, including downloads from earlier releases."""
    if not 45 <= size <= IMAGE_SIZE_LIMIT:
        return False
    with path.open("rb") as image:
        header = image.read(24)
        image.seek(-12, os.SEEK_END)
        return valid_png(header, image.read(12), size)


def cache_entries(directory: Path) -> list[tuple[int, int, Path]]:
    """Inspect only files managed by the downloaded artwork cache."""
    files = []
    for file in directory.iterdir():
        if file.suffix == ".png" and MODEL_ID.fullmatch(file.stem) and not file.is_symlink() and file.is_file():
            stat = file.stat()
            files.append((stat.st_mtime_ns, stat.st_size, file))
    return files


def download_figure_artwork(directory: Path, model: str, converter: Path) -> None:
    """Save one figure and make room if the on-demand cache is full."""
    if MODEL_ID.fullmatch(model) is None:
        raise ValueError("Invalid model ID")
    result = fetch_json(f"amiibo/?id={model}", 64 * 1024)["amiibo"]
    figures = result if isinstance(result, list) else [result]
    figure = next((item for item in figures if (item["head"] + item["tail"]).lower() == model), None)
    if figure is None:
        raise ValueError("AmiiboAPI did not return the requested figure")

    destination = directory / f"{model}.png"
    data = read_artwork(model, figure, converter)
    write_atomically(destination, data)
    files = cache_entries(directory)
    total = sum(size for _, size, _ in files)
    for _, size, file in sorted(files):
        if total <= IMAGE_CACHE_LIMIT:
            break
        if file != destination:
            file.unlink()
            total -= size


def batch_artwork(directory: Path, catalog: Path, converter: Path) -> int:
    """Fill the cache for the offline catalog using one request for all image URLs."""
    with catalog.open(encoding="utf-8") as source:
        if not source.readline().startswith(HEADER):
            raise ValueError("Download the catalog before fetching all artwork")
        models = [line.split("\t", 1)[0] for line in source]
        models = list(dict.fromkeys(model for model in models if MODEL_ID.fullmatch(model)))
    if not models:
        raise ValueError("The catalog has no figures")

    sizes = {file.stem: size for _, size, file in cache_entries(directory)}
    used = sum(sizes.values())
    pending = [model for model in models if not cached_artwork(directory / f"{model}.png", sizes.get(model, 0))]
    done = len(models) - len(pending)
    failures = 0
    print(done, len(models), failures, flush=True)
    if not pending:
        return 0

    figures = fetch_json("amiibo/", 4 * 1024 * 1024)["amiibo"]
    by_model = {(figure["head"] + figure["tail"]).lower(): figure for figure in figures}

    def fetch(model: str) -> bytes | None:
        try:
            return read_artwork(model, by_model[model], converter)
        except (OSError, EOFError, KeyError, ValueError) as error:
            print(f"Artwork {model}: {error}", file=sys.stderr)
            return None

    consecutive_failures = 0
    with ThreadPoolExecutor(max_workers=ARTWORK_WORKERS) as workers:
        for start in range(0, len(pending), ARTWORK_WORKERS):
            chunk = pending[start:start + ARTWORK_WORKERS]
            for model, image in zip(chunk, workers.map(fetch, chunk)):
                if image is None:
                    failures += 1
                    consecutive_failures += 1
                else:
                    size = used - sizes.get(model, 0) + len(image)
                    if size > IMAGE_CACHE_LIMIT:
                        return 4
                    write_atomically(directory / f"{model}.png", image)
                    used = size
                    consecutive_failures = 0
                done += 1
                print(done, len(models), failures, flush=True)
                if consecutive_failures >= 2 * ARTWORK_WORKERS:
                    return 5
    return 5 if failures else 0


def artwork_slug(name: str) -> str:
    """Match the file naming convention in the browser."""
    return re.sub(r"[^a-zA-Z0-9]+", "-", name).strip("-").lower()


def logo_titles(index: dict, catalog: Path) -> dict[str, list[str]]:
    """Match the current catalog against the bundled titles and groups."""
    by_name = {(entry["platform"], entry["title"]): entry for entry in index["titles"]}
    by_id = {}
    for entry in index["titles"]:
        if not entry["urls"]:
            continue
        for identifier in entry["ids"]:
            key = (entry["platform"], identifier)
            if key not in by_id:
                by_id[key] = entry
            elif by_id[key] is not None and tuple(by_id[key]["urls"]) != tuple(entry["urls"]):
                by_id[key] = None
    groups = {(entry["folder"], entry["name"]): entry["urls"] for entry in index["groups"]}

    games = {}
    result = {f"platforms/{artwork_slug(entry['name'])}": entry["urls"]
              for entry in index.get("platforms", []) if entry["urls"]}
    with catalog.open(encoding="utf-8") as source:
        if not source.readline().startswith(HEADER):
            raise ValueError("Download the catalog before fetching logos")
        for line in source:
            fields = line.rstrip("\n").split("\t")
            if fields[0] == "game" and len(fields) >= 5:
                key = (fields[1], fields[2])
                ids = games.setdefault(key, set())
                if len(fields) > 5:
                    ids.update(title_ids(fields[5].split(",")))
            elif MODEL_ID.fullmatch(fields[0]) and len(fields) == 5:
                for folder, name in (("series", fields[2]), ("games", fields[3])):
                    urls = groups.get((folder, name))
                    if urls:
                        result[f"{folder}/{artwork_slug(name)}"] = urls

    for (platform, name), ids in games.items():
        matches = {tuple(entry["urls"]): entry for identifier in ids
                   if (entry := by_id.get((platform, identifier))) is not None}
        match = by_name.get((platform, name))
        if match is None and len(matches) == 1:
            match = next(iter(matches.values()))
        if match and match["urls"]:
            result[f"titles/{artwork_slug(platform)}/{artwork_slug(name)}"] = match["urls"]
    return result


def allowed_logo_url(url: str) -> bool:
    """Use only the image origins and paths in the packaged index."""
    origin = urlsplit(url)
    if origin.scheme != "https" or origin.query or origin.fragment:
        return False
    if origin.netloc == "images.launchbox-app.com":
        return LAUNCHBOX_PNG.fullmatch(origin.path.removeprefix("/")) is not None
    if (origin.netloc in ("upload.wikimedia.org", "thumb.wikimedia.org") and
            origin.path.startswith("/wikipedia/commons/") and origin.path.endswith(".png")):
        return True
    if origin.netloc == "www.konami.com":
        return origin.path == "/crossmedia/assets/images/products/Yugioh_logo.png"
    if origin.netloc == "img.konami.com":
        return origin.path == "/pawa/switch/s/images/logo_pawa_switch.png"
    return (origin.netloc == "www.smashbros.com" and
            re.fullmatch(r"/assets_v2/img/fighter/logo/[a-zA-Z0-9_-]+_en\.png", origin.path) is not None)


class LogoRedirects(HTTPRedirectHandler):
    """Keep redirected requests on the configured image hosts."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if not allowed_logo_url(newurl):
            raise HTTPError(newurl, code, "Logo redirected outside its source", headers, fp)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def fetch_logo(urls: list[str], converter: Path) -> bytes:
    """Download and resize one logo, with a second candidate when provided."""
    opener = build_opener(LogoRedirects())
    reason = "No supported image URL"
    for url in urls:
        if not allowed_logo_url(url):
            continue
        try:
            request = Request(url, headers={"User-Agent": "Ziiro/1.0 (Cardputer Zero artwork downloader)"})
            with opener.open(request, timeout=8) as response:
                if not allowed_logo_url(response.geturl()):
                    continue
                data = response.read(LOGO_SIZE_LIMIT + 1)
            if len(data) > LOGO_SIZE_LIMIT or not data.startswith(b"\x89PNG\r\n\x1a\n"):
                reason = "Source did not return a supported PNG"
                continue
            output = subprocess.run([converter, "--convert-artwork"], input=data, stdout=subprocess.PIPE,
                                    check=True, timeout=8).stdout
            if valid_png(output[:24], output[-12:], len(output)):
                return output
            reason = "Converter did not produce a supported PNG"
        except InterruptedError:
            raise
        except (OSError, EOFError, subprocess.SubprocessError) as error:
            reason = str(error)
    raise ValueError(reason)


def run_logos(directory: Path, catalog: Path, converter: Path, selection: str | None = None) -> int:
    """Cache one focused logo or all indexed logos for this profile."""
    index = json.loads(Path(__file__).with_name("game-logos.json").read_text(encoding="utf-8"))
    if index.get("version") != 1:
        raise ValueError("Unsupported logo index")
    urls = logo_titles(index, catalog)
    if not urls or (selection is not None and selection not in urls):
        return 3
    entries = [selection] if selection is not None else list(urls)
    pending = {}
    available = {}
    for key, candidates in urls.items():
        relative = Path(key + ".png")
        path = directory / relative
        if any((directory / parent).is_symlink() for parent in relative.parents if parent != Path(".")):
            raise ValueError("Logo cache folders must be regular directories")
        if path.is_file() and not path.is_symlink() and cached_artwork(path, path.stat().st_size):
            available[tuple(candidates)] = path
        elif selection is None or selection == key:
            pending[key] = path
    checked = len(entries) - len(pending)
    errors = consecutive = 0
    if selection is None:
        print(checked, len(entries), errors, flush=True)
    if not pending:
        return 0

    used = sum(path.stat().st_size for path in directory.glob("**/*.png") if path.is_file() and not path.is_symlink())
    for key, path in pending.items():
        candidates = tuple(urls[key])
        try:
            cached = available.get(candidates)
            image = cached.read_bytes() if cached else fetch_logo(urls[key], converter)
        except ValueError as error:
            print(f"Could not fetch logo for {key}: {error}", file=sys.stderr)
            errors += 1
            consecutive += 1
        else:
            prior = path.stat().st_size if path.is_file() and not path.is_symlink() else 0
            if used - prior + len(image) > LOGO_CACHE_LIMIT:
                return 4
            path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
            write_atomically(path, image)
            available[candidates] = path
            used += len(image) - prior
            consecutive = 0
        checked += 1
        if selection is None:
            print(checked, len(entries), errors, flush=True)
        if consecutive >= 4:
            break
    return 5 if errors else 0


def cancel_sync(_signal: int, _frame: object) -> None:
    """Interrupt the worker and let it clean up any staging file."""
    raise InterruptedError("Cancelled")


def main() -> int:
    """Run a catalog sync or artwork job for the app."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--artwork", metavar="MODEL_ID")
    parser.add_argument("--batch-artwork", type=Path, metavar="CATALOG")
    parser.add_argument("--logo", metavar="KEY")
    parser.add_argument("--batch-logos", type=Path, metavar="CATALOG")
    args = parser.parse_args()
    signal.signal(signal.SIGTERM, cancel_sync)
    try:
        if args.artwork or args.batch_artwork or args.logo or args.batch_logos:
            if args.destination.is_symlink():
                raise ValueError("Artwork cache must be a local directory")
            args.destination.mkdir(mode=0o700, exist_ok=True)
            with (args.destination / ".lock").open("a") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                converter = artwork_converter()
                if args.batch_artwork:
                    return batch_artwork(args.destination, args.batch_artwork, converter)
                if args.logo or args.batch_logos:
                    catalog = args.batch_logos or args.destination.parent / "catalog.tsv"
                    return run_logos(args.destination, catalog, converter, args.logo)
                download_figure_artwork(args.destination, args.artwork, converter)
                return 0
        with (args.destination.parent / ".catalog-sync.lock").open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return sync(args.destination, force=args.force)
    except InterruptedError:
        return 130
    except (OSError, EOFError, KeyError, TypeError, ValueError) as error:
        operation = "Artwork download" if args.artwork or args.batch_artwork or args.logo or args.batch_logos else "Catalog sync"
        print(f"{operation} failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
