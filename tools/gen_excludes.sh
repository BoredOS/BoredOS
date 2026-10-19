#!/bin/bash
# tools/gen_excludes.sh
# Generates the excludes.txt file by parsing all .bup packages

INITRD_DIR="$1"
PACKAGES_DIR="$INITRD_DIR/usr/share/packages"
EXCLUDES_FILE="$PACKAGES_DIR/excludes.txt"

rm -f "$EXCLUDES_FILE"
touch "$EXCLUDES_FILE"

# Find all bup files
for bup in "$PACKAGES_DIR"/*.bup; do
    [ -f "$bup" ] || continue
    
    # Create temp dir
    TMP_DIR=$(mktemp -d)
    
    # Decompress and extract MANIFEST.toml and get list of files
    lz4 -d -q "$bup" "$TMP_DIR/pkg.tar"
    tar -xf "$TMP_DIR/pkg.tar" -C "$TMP_DIR"
    
    # Read destinations from MANIFEST.toml
    BIN_DEST="/usr/bin"
    ASSETS_DEST=""
    CONFIG_DEST="/Library/conf"
    
    if [ -f "$TMP_DIR/MANIFEST.toml" ]; then
        name_val=$(grep -E '^[[:space:]]*name[[:space:]]*=' "$TMP_DIR/MANIFEST.toml" | sed -E 's/.*=[[:space:]]*"([^"]*)".*/\1/')
        b_val=$(grep -E '^[[:space:]]*bin[[:space:]]*=' "$TMP_DIR/MANIFEST.toml" | sed -E 's/.*=[[:space:]]*"([^"]*)".*/\1/')
        a_val=$(grep -E '^[[:space:]]*assets[[:space:]]*=' "$TMP_DIR/MANIFEST.toml" | sed -E 's/.*=[[:space:]]*"([^"]*)".*/\1/')
        c_val=$(grep -E '^[[:space:]]*config[[:space:]]*=' "$TMP_DIR/MANIFEST.toml" | sed -E 's/.*=[[:space:]]*"([^"]*)".*/\1/')
        
        if [ ! -z "$name_val" ]; then CONFIG_DEST="/Library/AppData/$name_val"; fi
        if [ ! -z "$b_val" ]; then BIN_DEST="$b_val"; fi
        if [ ! -z "$a_val" ]; then ASSETS_DEST="$a_val"; fi
        if [ ! -z "$c_val" ]; then CONFIG_DEST="$c_val"; fi
    fi
    
    # List files in tar
    tar -tf "$TMP_DIR/pkg.tar" | while read -r line; do
        # Ignore directories
        if [[ "$line" == */ ]]; then
            continue
        fi
        # Ignore MANIFEST.toml, scripts/
        if [[ "$line" == "MANIFEST.toml" ]] || [[ "$line" == scripts* ]]; then
            continue
        fi
        
        # Remove trailing slash if any
        line_clean="${line%/}"
        [ -z "$line_clean" ] && continue
        
        # Determine target path
        if [[ "$line" == bin/* ]]; then
            rel="${line_clean#bin/}"
            [ -z "$rel" ] && continue
            echo "$BIN_DEST/$rel" >> "$EXCLUDES_FILE"
        elif [[ "$line" == assets/* ]]; then
            rel="${line_clean#assets/}"
            [ -z "$rel" ] && continue
            if [ ! -z "$ASSETS_DEST" ]; then
                echo "$ASSETS_DEST/$rel" >> "$EXCLUDES_FILE"
            fi
        elif [[ "$line" == config/* ]]; then
            rel="${line_clean#config/}"
            [ -z "$rel" ] && continue
            echo "$CONFIG_DEST/$rel" >> "$EXCLUDES_FILE"
        elif [[ "$line" == usr/share/applications/* ]]; then
            rel="${line_clean#usr/share/applications/}"
            [ -z "$rel" ] && continue
            echo "/Library/AppData/$name_val/$rel" >> "$EXCLUDES_FILE"
        fi
    done
    
    rm -rf "$TMP_DIR"
done

# Sort and make unique
if [ -f "$EXCLUDES_FILE" ]; then
    sort -u "$EXCLUDES_FILE" -o "$EXCLUDES_FILE"
fi

# Always exclude the packages folder itself, the excludes file itself, and the installer itself
echo "/usr/share/packages" >> "$EXCLUDES_FILE"
echo "/usr/share/packages/excludes.txt" >> "$EXCLUDES_FILE"
echo "/bin/boredos_install" >> "$EXCLUDES_FILE"
echo "/bin/boredos_install.elf" >> "$EXCLUDES_FILE"
sort -u "$EXCLUDES_FILE" -o "$EXCLUDES_FILE"
