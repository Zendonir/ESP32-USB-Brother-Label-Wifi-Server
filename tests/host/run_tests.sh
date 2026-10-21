#!/usr/bin/env bash
# Host unit tests for the pure-C parts (descriptor parser, report/JSON formatter).
# Requires gcc (or clang) and python3. Runs with AddressSanitizer + UBSan.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
out="$here/build"
mkdir -p "$out"
CC="${CC:-gcc}"
"$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$root/components/common_util/include" -I"$root/components/usb_printer/include" \
    "$root/components/common_util/strbuf.c" "$root/components/usb_printer/usb_desc_report.c" \
    "$here/test_usb_desc_report.c" -o "$out/test_usb_desc_report"
"$out/test_usb_desc_report" "$out"
"$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$root/components/common_util/include" -I"$root/components/brother_protocol/include" \
    "$root/components/common_util/strbuf.c" "$root/components/brother_protocol/brother_status.c" \
    "$here/test_brother_status.c" -o "$out/test_brother_status"
"$out/test_brother_status" "$out"
for f in "$out"/*.json; do
    python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$f" || { echo "invalid JSON: $f"; exit 1; }
done
echo "JSON output valid ($(ls "$out"/*.json | wc -l) files)"
