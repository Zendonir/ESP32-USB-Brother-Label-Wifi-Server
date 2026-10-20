#!/usr/bin/env python3
"""Collects the flashable images of an ESP-IDF build into one directory.

Reads build/flasher_args.json (written by idf.py build), copies every image
with its flash offset, the merged image (if present) and writes:

  manifest.json   ESP Web Tools manifest (browser flasher)
  flash_args.txt  offsets for esptool
  SHA256SUMS      checksums

The browser flasher writes the individual parts instead of the merged image so
that the NVS partition (Wi-Fi credentials, setup AP password) survives an update.

usage: package_firmware.py <build_dir> <out_dir> <version>
"""
import hashlib
import json
import shutil
import sys
from pathlib import Path

PROJECT = "brother_printserver"
CHIP_FAMILY = "ESP32-S3"


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    build = Path(sys.argv[1])
    out = Path(sys.argv[2])
    version = sys.argv[3]
    out.mkdir(parents=True, exist_ok=True)

    args = json.loads((build / "flasher_args.json").read_text())
    if args.get("extra_esptool_args", {}).get("chip") not in (None, "esp32s3"):
        print("unexpected chip in flasher_args.json", file=sys.stderr)
        return 1

    parts = []
    lines = []
    for offset_str, rel in sorted(args["flash_files"].items(), key=lambda kv: int(kv[0], 16)):
        src = build / rel
        name = Path(rel).name
        shutil.copy2(src, out / name)
        offset = int(offset_str, 16)
        parts.append({"path": name, "offset": offset})
        lines.append(f"0x{offset:x} {name}")

    merged = build / "merged-binary.bin"
    files = [p["path"] for p in parts]
    if merged.exists():
        merged_name = f"{PROJECT}-{version}-esp32s3-merged.bin"
        shutil.copy2(merged, out / merged_name)
        files.append(merged_name)

    manifest = {
        "name": "Brother TD-2020A Print Server",
        "version": version,
        "new_install_prompt_erase": True,
        # The firmware does not implement Improv Wi-Fi; do not wait for it.
        "new_install_improv_wait_time": 0,
        "builds": [{"chipFamily": CHIP_FAMILY, "parts": parts}],
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    flash_settings = args.get("flash_settings", {})
    header = (
        f"# esptool --chip esp32s3 write_flash --flash-mode {flash_settings.get('flash_mode', 'dio')} "
        f"--flash-freq {flash_settings.get('flash_freq', '80m')} "
        f"--flash-size {flash_settings.get('flash_size', '16MB')} <offset file>...\n"
    )
    (out / "flash_args.txt").write_text(header + "\n".join(lines) + "\n")

    sums = []
    for name in files:
        digest = hashlib.sha256((out / name).read_bytes()).hexdigest()
        sums.append(f"{digest}  {name}")
    (out / "SHA256SUMS").write_text("\n".join(sums) + "\n")

    print(f"packaged {len(files)} images into {out}")
    for line in lines:
        print("  " + line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
