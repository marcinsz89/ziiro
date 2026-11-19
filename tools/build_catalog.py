"""Build Ziiro's offline metadata catalog from an AmiiboAPI snapshot."""

import argparse
import json
import re
from pathlib import Path


def build_catalog(source: Path, destination: Path) -> None:
    """Write normalized IDs, names, release series, game franchises and types."""
    data = json.loads(source.read_text(encoding="utf-8"))
    rows = []
    for key, info in sorted(data["amiibos"].items()):
        if re.fullmatch(r"0x[0-9a-f]{16}", key) is None:
            raise ValueError(f"Invalid catalog ID: {key}")

        identifier = key[2:]
        series = data["amiibo_series"]["0x" + identifier[12:14]]
        franchise = data["game_series"]["0x" + identifier[:3]]
        kind = data["types"]["0x" + identifier[6:8]]
        fields = (identifier, info["name"], series, franchise, kind)
        rows.append("\t".join(" ".join(field.split()) for field in fields))

    destination.write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"Wrote {len(rows)} catalog entries")


def main() -> None:
    """Read two paths and build the catalog."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    build_catalog(args.source, args.destination)


if __name__ == "__main__":
    main()
