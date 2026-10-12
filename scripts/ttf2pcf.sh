#!/bin/sh

if [ "$#" -ne 3 ]; then
    echo "Usage: $(basename "$0") <Size in pt> <infont.ttf> <outfont.pcf>" >&2
    exit 1
fi

PT_SIZE="$1"
SRC_OTF="$2"
DST_PCF="$3"

TMP_BDF="$(mktemp /tmp/otf2pcf.XXXXXX.bdf)"

trap 'rm -f "$TMP_BDF"' EXIT INT TERM

# include 0 to 383 characters
otf2bdf -pid 3 -eid 1 -p "${PT_SIZE}" -l "0_383" -o "$TMP_BDF" "${SRC_OTF}"
bdftopcf -m "$TMP_BDF" -o "${DST_PCF}"
