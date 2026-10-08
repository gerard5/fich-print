#!/bin/sh

if [ "$#" -ne 3 ]; then
    echo "Usage: $(basename "$0") <Size in pt> <infont.ttf> <outfont.bdf>" >&2
    exit 1
fi

PT_SIZE="$1"
SRC_OTF="$2"
DST_BDF="$3"

# include 0 to 383 characters
otf2bdf -pid 3 -eid 1 -p "${PT_SIZE}" -l "0_383" -o "${DST_BDF}" "${SRC_OTF}"
