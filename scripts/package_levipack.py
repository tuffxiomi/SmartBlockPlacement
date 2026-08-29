import argparse
import json
import re
import sys
import zipfile
from pathlib import Path

VALUE_PATTERN = re.compile(r'^\s*inline\s+constexpr\s+std::string_view\s+(Name|Author|Description|Version)\s*=\s*"((?:\\.|[^"\\])*)";\s*$')
REQUIRED = ("Name", "Author", "Description", "Version")


def parse_metadata(path: Path):
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        m = VALUE_PATTERN.match(line)
        if m:
            values[m.group(1)] = bytes(m.group(2), "utf-8").decode("unicode_escape")
    missing = [x for x in REQUIRED if not values.get(x)]
    if missing:
        raise ValueError("Missing metadata: " + ", ".join(missing))
    return values


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--library", required=True, type=Path)
    p.add_argument("--icon", required=True, type=Path)
    p.add_argument("--metadata", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    a = p.parse_args()

    if not a.library.is_file():
        raise FileNotFoundError(a.library)
    if not a.icon.is_file():
        raise FileNotFoundError(a.icon)
    if not a.metadata.is_file():
        raise FileNotFoundError(a.metadata)

    meta = parse_metadata(a.metadata)
    manifest = {
        "type": "preload-native",
        "name": meta["Name"],
        "author": meta["Author"],
        "description": meta["Description"],
        "version": meta["Version"],
        "entry": "libSmartBlockPlacement.so",
        "icon": "icon.png",
        "overwrite_files": ["icon.png"],
        "overwrite_folders": [],
    }

    a.output.parent.mkdir(parents=True, exist_ok=True)
    if a.output.exists():
        a.output.unlink()

    with zipfile.ZipFile(a.output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.writestr("manifest.json", json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
        z.write(a.library, "libSmartBlockPlacement.so")
        z.write(a.icon, "icon.png")

    with zipfile.ZipFile(a.output, "r") as z:
        names = set(z.namelist())
        expected = {"manifest.json", "libSmartBlockPlacement.so", "icon.png"}
        if names != expected:
            raise RuntimeError(f"Unexpected package entries: {sorted(names)}")
        parsed = json.loads(z.read("manifest.json"))
        if parsed != manifest:
            raise RuntimeError("Manifest verification failed")
        if z.getinfo("libSmartBlockPlacement.so").file_size != a.library.stat().st_size:
            raise RuntimeError("Library verification failed")
        if z.getinfo("icon.png").file_size != a.icon.stat().st_size:
            raise RuntimeError("Icon verification failed")

    print(a.output.resolve())


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(1)
