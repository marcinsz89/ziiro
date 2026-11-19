"""Rebuild game logo matches from saved AmiiboAPI usage and LaunchBox metadata."""

import argparse
import json
import unicodedata
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path
from zipfile import ZipFile

from sync_catalog import LAUNCHBOX_PNG, PLATFORMS, text_field, title_ids, write_atomically

LAUNCHBOX_PLATFORMS = {f"Nintendo {platform}": platform for platform in PLATFORMS.values()}
REGIONS = ("North America", "United States", "United Kingdom", "World", "Europe", "Australia", "Canada", "")


def normalized_title(name: str) -> str:
    """Compare names across spelling and punctuation conventions."""
    text = unicodedata.normalize("NFKD", name.casefold()).replace("&", " and ")
    return "".join(ch for ch in text if ch.isalnum())


def launchbox_logos(snapshot: Path, wanted: set[tuple[str, str]]) -> dict:
    """Read the XML in order and retain logo candidates for matched games."""
    titles = defaultdict(set)
    aliases = defaultdict(set)
    platforms = {}
    matches = set()
    logos = defaultdict(list)
    with ZipFile(snapshot) as archive, archive.open("Metadata.xml") as source:
        stack = []
        for event, node in ET.iterparse(source, events=("start", "end")):
            if event == "start":
                stack.append(node)
                continue
            if len(stack) == 2:
                record = node.findtext("DatabaseID")
                if node.tag == "Game" and record and (platform := LAUNCHBOX_PLATFORMS.get(node.findtext("Platform"))):
                    platforms[record] = platform
                    key = (platform, normalized_title(node.findtext("Name", "")))
                    if key in wanted:
                        titles[key].add(record)
                        matches.add(record)
                elif node.tag == "GameAlternateName" and record in platforms:
                    key = (platforms[record], normalized_title(node.findtext("AlternateName", "")))
                    if key in wanted:
                        aliases[key].add(record)
                        matches.add(record)
                elif node.tag == "GameImage" and record in matches and node.findtext("Type") == "Clear Logo":
                    name = node.findtext("FileName", "")
                    if LAUNCHBOX_PNG.fullmatch(name):
                        region = node.findtext("Region", "")
                        rank = REGIONS.index(region) if region in REGIONS else len(REGIONS)
                        logos[record].append((rank, name))
                stack[0].remove(node)
            stack.pop()

    result = {}
    for key in wanted:
        records = titles[key] or aliases[key]
        if len(records) != 1:
            continue
        record = next(iter(records))
        if logos[record]:
            result[key] = (record, sorted(set(logos[record])))
    return result


def group_entries(groups: dict[str, set[str]], prior: dict) -> list[dict]:
    """Retain curated logo rows and add every current series and franchise name."""
    existing = {(entry["folder"], entry["name"]): entry for entry in prior.get("groups", [])}
    result = []
    for folder, names in groups.items():
        for name in sorted(names):
            old = existing.get((folder, name), {})
            entry = {"folder": folder, "name": name}
            entry.update({key: value for key, value in old.items() if key not in ("folder", "name", "urls")})
            entry["urls"] = old.get("urls", [])
            result.append(entry)
    return result


def main() -> None:
    """Build a title and group index from AmiiboAPI usage and LaunchBox metadata."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("usage", type=Path, help="A saved AmiiboAPI ?showusage JSON response")
    parser.add_argument("launchbox", type=Path, help="An existing LaunchBox Metadata.zip snapshot")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    payload = json.loads(args.usage.read_text(encoding="utf-8"))
    games = defaultdict(set)
    group_names = {"series": set(), "games": set()}
    for figure in payload["amiibo"]:
        group_names["series"].add(text_field(figure["amiiboSeries"]))
        group_names["games"].add(text_field(figure["gameSeries"]))
        for field, platform in PLATFORMS.items():
            for game in figure.get(field) or []:
                games[(platform, text_field(game["gameName"]))].update(title_ids(game.get("gameID") or []))
    prior = json.loads(args.output.read_text(encoding="utf-8")) if args.output.is_file() else {}
    previous = {(entry["platform"], entry["title"]): entry for entry in prior.get("titles", [])}
    wanted_titles = {(platform, normalized_title(title)) for platform, title in games}
    logos = launchbox_logos(args.launchbox, wanted_titles)
    entries = []
    for (platform, title), ids in sorted(games.items()):
        curated = previous.get((platform, title), {})
        match = None if curated.get("urls") else logos.get((platform, normalized_title(title)))
        if match:
            record, candidates = match
            urls = [f"https://images.launchbox-app.com/{file}" for _, file in candidates[:2]]
        else:
            record = curated.get("launchbox")
            urls = curated.get("urls", [])
        entry = {"platform": platform, "title": title, "ids": sorted(ids)}
        entry.update({key: value for key, value in curated.items() if key not in ("platform", "title", "ids", "urls")})
        if record is not None:
            entry["launchbox"] = int(record)
        entry["urls"] = urls
        entries.append(entry)

    index = {"version": 1, "platforms": prior.get("platforms", []), "titles": entries,
             "groups": group_entries(group_names, prior)}
    write_atomically(args.output, (json.dumps(index, indent=2, ensure_ascii=False) + "\n").encode())
    matched_games = sum(bool(entry["urls"]) for entry in entries)
    matched_groups = sum(bool(entry["urls"]) for entry in index["groups"])
    print(f"Matched {matched_games} of {len(entries)} game-and-console entries")
    print(f"Matched {matched_groups} of {len(index['groups'])} series and franchise entries")


if __name__ == "__main__":
    main()
