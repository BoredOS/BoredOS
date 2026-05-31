#!/usr/bin/env sh
# Copyright (c) 2026 Christiaan (chris@boreddev.nl)

set -eu

EXTERNAL_DIR="external"
mkdir -p "$EXTERNAL_DIR"

# List of repositories to fetch.
# Format: repo_name | git_url | commit_hash_or_branch
REPOS="
libc|https://github.com/boredos/libc.git|main
bsh|https://github.com/boredos/bsh.git|main
nova|https://github.com/boredos/nova.git|main
coreutils|https://github.com/boredos/coreutils.git|main
kilo|https://github.com/boredos/kilo.git|main
boredos_install|https://github.com/boredos/boredos_install.git|main
lua|https://github.com/boredos/lua.git|main
tcc|https://github.com/boredos/tcc.git|main
netutils|https://github.com/boredos/netutils.git|main
bart|https://github.com/boredos/bart.git|main
colloid|https://github.com/boredos/colloid.git|main
bfonts|https://github.com/boredos/bfonts.git|main
doomgeneric|https://github.com/boredos/doomgeneric.git|main
"

LOCKDIR="build/fetch.lock"
if ! mkdir "$LOCKDIR" 2>/dev/null; then
    while [ -d "$LOCKDIR" ]; do
        sleep 0.1
    done
    exit 0
fi

trap 'rm -rf "$LOCKDIR"' EXIT INT TERM

fetch_repo() {
    name="$1"
    url="$2"
    target_ref="$3"
    repo_path="$EXTERNAL_DIR/$name"

    echo "============================================================"
    echo "Processing external repository: $name"
    echo "============================================================"

    if [ -d "$repo_path/.git" ]; then
        current_hash=$(cd "$repo_path" && git rev-parse HEAD 2>/dev/null || echo "")
        
        remote_hash=$(git ls-remote "$url" "refs/heads/$target_ref" 2>/dev/null | cut -f1 || echo "")
        
        if [ -z "$remote_hash" ]; then
            echo "[WARNING] Could not query remote branch '$target_ref' for $name. Staying at current local commits."
            return 0
        fi

        if [ "$current_hash" = "$remote_hash" ]; then
            echo "[OK] $name is up-to-date at commit $current_hash (Skipping pull)"
            return 0
        fi

        echo "[PULL] Local hash $current_hash differs from remote $remote_hash. Pulling updates..."
        if (cd "$repo_path" && git pull -q origin "$target_ref" 2>/dev/null); then
            echo "[SUCCESS] $name updated successfully to commit $remote_hash!"
        else
            echo "[WARNING] Failed to pull updates for $name. Staying at current commits."
        fi
    else
        echo "[CLONE] Cloning $name from $url..."
        if git clone --filter=blob:none "$url" "$repo_path"; then
            echo "[CHECKOUT] Switching to $target_ref..."
            (cd "$repo_path" && git checkout -q "$target_ref")
        else
            echo "[ERROR] Failed to clone $name from $url." >&2
            exit 1
        fi
    fi
}

printf '%s\n' "$REPOS" | while read -r line; do
    [ -z "$line" ] && continue
    case "$line" in
        \#*) continue ;;
    esac

    repo_name=$(echo "$line" | cut -d'|' -f1)
    repo_url=$(echo "$line" | cut -d'|' -f2)
    repo_ref=$(echo "$line" | cut -d'|' -f3)

    fetch_repo "$repo_name" "$repo_url" "$repo_ref"
done
