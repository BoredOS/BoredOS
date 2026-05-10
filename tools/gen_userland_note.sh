#!/usr/bin/env sh
set -eu

if [ "$#" -ne 4 ]; then
    echo "usage: gen_userland_note.sh <app-name> <source-file> <icon-source-dir> <output-c>" >&2
    exit 1
fi

APP_NAME="$1"
SOURCE_PATH="$2"
ICON_SOURCE_DIR="$3"
OUT_PATH="$4"

MAX_APP_NAME=63
MAX_DESC=191
MAX_IMAGE_PATH=159
MAX_IMAGES=4

DEFAULT_ICON_PATH="/Library/images/icons/colloid/xterm.png"
DEFAULT_DESC="BoredOS userspace application."

escape_c_string() {
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

trim_spaces() {
    printf '%s' "$1" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//'
}

truncate_bytes() {
    value="$1"
    max_len="$2"
    printf '%s' "$value" | cut -c1-"$max_len"
}

if [ ! -f "$SOURCE_PATH" ]; then
    echo "error: source file '$SOURCE_PATH' not found for app '$APP_NAME'" >&2
    exit 1
fi

app_desc="$DEFAULT_DESC"
image_spec="$DEFAULT_ICON_PATH"

source_desc=$(sed -n 's@^[[:space:]]*//[[:space:]]*BOREDOS_APP_DESC:[[:space:]]*@@p' "$SOURCE_PATH" | head -n 1)
source_icons=$(sed -n 's@^[[:space:]]*//[[:space:]]*BOREDOS_APP_ICONS:[[:space:]]*@@p' "$SOURCE_PATH" | head -n 1)

if [ -n "$source_desc" ]; then
    app_desc="$source_desc"
fi
if [ -n "$source_icons" ]; then
    image_spec="$source_icons"
fi

app_desc=$(trim_spaces "$app_desc")
if [ -z "$app_desc" ]; then
    app_desc="$DEFAULT_DESC"
fi

image_spec=$(trim_spaces "$image_spec")
if [ -z "$image_spec" ]; then
    image_spec="$DEFAULT_ICON_PATH"
fi

app_name_value=$(truncate_bytes "$APP_NAME" "$MAX_APP_NAME")
app_desc=$(truncate_bytes "$app_desc" "$MAX_DESC")

IMAGE_1=""
IMAGE_2=""
IMAGE_3=""
IMAGE_4=""
IMAGE_COUNT=0

saved_ifs="$IFS"
IFS=';'
for raw_image in $image_spec; do
    image_path=$(trim_spaces "$raw_image")
    if [ -z "$image_path" ]; then
        continue
    fi
    image_path=$(truncate_bytes "$image_path" "$MAX_IMAGE_PATH")

    image_file="${image_path##*/}"
    if [ ! -f "$ICON_SOURCE_DIR/$image_file" ]; then
        echo "error: icon '$image_file' (from '$image_path') not found in $ICON_SOURCE_DIR for app '$APP_NAME'" >&2
        exit 1
    fi

    IMAGE_COUNT=$((IMAGE_COUNT + 1))
    if [ "$IMAGE_COUNT" -gt "$MAX_IMAGES" ]; then
        break
    fi

    case "$IMAGE_COUNT" in
        1) IMAGE_1="$image_path" ;;
        2) IMAGE_2="$image_path" ;;
        3) IMAGE_3="$image_path" ;;
        4) IMAGE_4="$image_path" ;;
    esac
done
IFS="$saved_ifs"

if [ "$IMAGE_COUNT" -eq 0 ]; then
    IMAGE_1="$DEFAULT_ICON_PATH"
    IMAGE_COUNT=1
fi

app_name_escaped=$(escape_c_string "$app_name_value")
app_desc_escaped=$(escape_c_string "$app_desc")
image_1_escaped=$(escape_c_string "$IMAGE_1")
image_2_escaped=$(escape_c_string "$IMAGE_2")
image_3_escaped=$(escape_c_string "$IMAGE_3")
image_4_escaped=$(escape_c_string "$IMAGE_4")

cat > "$OUT_PATH" <<EOF
#include <stdint.h>
#include "elf.h"

struct __attribute__((packed, aligned(4))) boredos_app_note_blob {
    Elf64_Word namesz;
    Elf64_Word descsz;
    Elf64_Word type;
    char name[sizeof(BOREDOS_APP_NOTE_NAME)];
    boredos_app_metadata_t metadata;
};

__attribute__((used, section(".note.boredos.app"), aligned(4)))
static const struct boredos_app_note_blob g_boredos_app_note = {
    .namesz = sizeof(BOREDOS_APP_NOTE_NAME),
    .descsz = sizeof(boredos_app_metadata_t),
    .type = BOREDOS_APP_NOTE_TYPE,
    .name = BOREDOS_APP_NOTE_NAME,
    .metadata = {
        .magic = BOREDOS_APP_METADATA_MAGIC,
        .version = BOREDOS_APP_METADATA_VERSION,
        .image_count = ${IMAGE_COUNT},
        .reserved = 0,
        .app_name = "${app_name_escaped}",
        .description = "${app_desc_escaped}",
        .images = {
            "${image_1_escaped}",
            "${image_2_escaped}",
            "${image_3_escaped}",
            "${image_4_escaped}",
        },
    },
};
EOF
