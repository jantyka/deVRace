#!/usr/bin/env bash
# deVRace setup: installs the deVRace AppImage for the current user and prepares the game data.
#
#   ./devrace-setup.sh                   install (AppImage next to this script, or downloaded)
#   ./devrace-setup.sh --game-dir DIR    use the Carmageddon data in DIR instead of searching
#   ./devrace-setup.sh --uninstall       remove the AppImage, menu entry and icon
#   ./devrace-setup.sh --uninstall --purge   also remove the copied game data and the config
#
# What it does (nothing needs root, nothing outside your home directory is changed):
#   1. Copies the AppImage to ~/Applications/deVRace.AppImage and links ~/.local/bin/devrace.
#   2. Finds Carmageddon (Steam Max Pack, GOG, Heroic or --game-dir) and copies the game data to
#      ~/.local/share/devrace/games/. The game writes save games and key maps into its data
#      folder, so it never runs from your Steam/GOG installation.
#   3. Creates ~/.local/share/devrace/dethrace.ini if there is none (an existing one is kept).
#   4. Adds "deVRace" to the application menu, with "Splat Pack" as a second action.
#
# Requirements it checks: Wayland session, an OpenXR runtime (SteamVR or Monado), FUSE 2 for
# AppImages (without it the menu entry starts the AppImage with APPIMAGE_EXTRACT_AND_RUN).

set -euo pipefail

VERSION="v1.1.0"
REPO="jantyka/deVRace"
STEAM_APPID=282010 # Carmageddon Max Pack

APP_DIR="${DEVRACE_APP_DIR:-$HOME/Applications}"
APP_PATH="$APP_DIR/deVRace.AppImage"
BIN_LINK="$HOME/.local/bin/devrace"
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
PREF_DIR="$DATA_HOME/devrace"
GAMES_DIR="$PREF_DIR/games"
INI="$PREF_DIR/dethrace.ini"
DESKTOP_FILE="$DATA_HOME/applications/deVRace.desktop"
ICON_FILE="$DATA_HOME/icons/hicolor/256x256/apps/devrace.png"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

APPIMAGE_SRC=""
GAME_DIR_ARG=""
UNINSTALL=0
PURGE=0
RECOPY=0

say() { printf '\033[1m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mWarning:\033[0m %s\n' "$*" >&2; }
die() {
    printf '\033[1;31mError:\033[0m %s\n' "$*" >&2
    exit 1
}

usage() {
    sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
    cat <<EOF

Options:
  --appimage FILE   install this AppImage (default: deVRace*.AppImage next to this script,
                    otherwise $VERSION is downloaded from github.com/$REPO)
  --game-dir DIR    Carmageddon data: the folder with CARMA/ and CARSPLAT/, or one game folder
                    (the one with DATA/GENERAL.TXT)
  --recopy          copy the game data again even if it is already there (save games are lost)
  --uninstall       remove what this script installed (game data and config are kept)
  --purge           with --uninstall: also remove $PREF_DIR
  -h, --help        show this help
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
    --appimage)
        APPIMAGE_SRC="${2:?--appimage needs a file}"
        shift 2
        ;;
    --game-dir)
        GAME_DIR_ARG="${2:?--game-dir needs a folder}"
        shift 2
        ;;
    --recopy)
        RECOPY=1
        shift
        ;;
    --uninstall)
        UNINSTALL=1
        shift
        ;;
    --purge)
        PURGE=1
        shift
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    *) die "unknown option '$1' (see --help)" ;;
    esac
done

[ "$(id -u)" -ne 0 ] || die "do not run this as root, deVRace is installed for your user"

# Escapes a value for the Exec key of a .desktop file and quotes it.
desktop_quote() {
    local s="$1"
    s="${s//\\/\\\\\\\\}"
    s="${s//\"/\\\\\"}"
    s="${s//\$/\\\\\$}"
    s="${s//\`/\\\\\`}"
    printf '"%s"' "$s"
}

# ---------------------------------------------------------------------------------------------
# Uninstall
# ---------------------------------------------------------------------------------------------
if [ "$UNINSTALL" = 1 ]; then
    say "Removing deVRace"
    rm -fv "$APP_PATH" "$DESKTOP_FILE" "$ICON_FILE"
    if [ -L "$BIN_LINK" ]; then rm -fv "$BIN_LINK"; fi
    if [ "$PURGE" = 1 ]; then
        rm -rf "$PREF_DIR" && echo "removed '$PREF_DIR'"
    else
        echo "Kept your config and game data copy in $PREF_DIR (remove with --uninstall --purge)."
    fi
    command -v update-desktop-database > /dev/null && update-desktop-database -q "$DATA_HOME/applications" || true
    exit 0
fi

# ---------------------------------------------------------------------------------------------
# System checks (warnings only: the game also runs on the monitor without VR)
# ---------------------------------------------------------------------------------------------
say "Checking the system"
[ "$(uname -m)" = x86_64 ] || die "the AppImage is built for x86_64, this machine is $(uname -m)"

if [ -z "${WAYLAND_DISPLAY:-}" ]; then
    warn "no Wayland session found. deVRace is a native Wayland client and is only tested there."
fi

runtime_json=""
for dir in "${XDG_CONFIG_HOME:-$HOME/.config}" $(echo "${XDG_CONFIG_DIRS:-/etc/xdg}" | tr ':' ' ') /etc; do
    if [ -e "$dir/openxr/1/active_runtime.json" ]; then
        runtime_json="$dir/openxr/1/active_runtime.json"
        break
    fi
done
if [ -n "$runtime_json" ]; then
    echo "OpenXR runtime: $(readlink -f "$runtime_json")"
else
    warn "no active OpenXR runtime found. Start SteamVR once and set it as the OpenXR runtime" \
        "(SteamVR settings > Developer), or install Monado. Without one, the game runs on the monitor only."
fi

use_extract=0
if ! { ldconfig -p 2> /dev/null | grep -q 'libfuse\.so\.2'; } && ! [ -e /usr/lib/libfuse.so.2 ] && ! [ -e /usr/lib64/libfuse.so.2 ]; then
    warn "FUSE 2 (libfuse.so.2) not found. AppImages need it to mount themselves; the menu entry" \
        "will unpack the AppImage on each start instead (slower start). Install it with your" \
        "package manager (Arch: fuse2, Debian/Ubuntu: libfuse2t64 or libfuse2, Fedora: fuse-libs)."
    use_extract=1
fi

# ---------------------------------------------------------------------------------------------
# 1. AppImage
# ---------------------------------------------------------------------------------------------
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

if [ -z "$APPIMAGE_SRC" ]; then
    APPIMAGE_SRC="$(ls -t "$SCRIPT_DIR"/deVRace*.AppImage 2> /dev/null | head -n 1 || true)"
fi
if [ -z "$APPIMAGE_SRC" ]; then
    command -v curl > /dev/null || die "no AppImage next to this script and curl is missing to download one"
    say "Downloading deVRace $VERSION from github.com/$REPO"
    base="https://github.com/$REPO/releases/download/$VERSION"
    curl --fail --location --silent --show-error -o "$tmp/SHA256SUMS" "$base/SHA256SUMS"
    name="$(awk '/\.AppImage$/ { print $2 }' "$tmp/SHA256SUMS" | head -n 1)"
    [ -n "$name" ] || die "the release has no AppImage listed in SHA256SUMS"
    curl --fail --location --progress-bar -o "$tmp/$name" "$base/$name"
    (cd "$tmp" && grep " $name\$" SHA256SUMS | sha256sum --check --quiet) || die "checksum of $name does not match"
    APPIMAGE_SRC="$tmp/$name"
fi
[ -f "$APPIMAGE_SRC" ] || die "AppImage not found: $APPIMAGE_SRC"

say "Installing $(basename "$APPIMAGE_SRC") to $APP_PATH"
mkdir -p "$APP_DIR" "$(dirname "$BIN_LINK")"
if [ "$(readlink -f "$APPIMAGE_SRC")" != "$(readlink -f "$APP_PATH" 2> /dev/null || true)" ]; then
    cp -f "$APPIMAGE_SRC" "$APP_PATH.new"
    mv -f "$APP_PATH.new" "$APP_PATH"
fi
chmod +x "$APP_PATH"
if [ -e "$BIN_LINK" ] && ! [ -L "$BIN_LINK" ]; then
    warn "$BIN_LINK exists and is not a link, left alone"
else
    ln -sfn "$APP_PATH" "$BIN_LINK"
fi

# Icon out of the AppImage (unpacking works without FUSE).
icon_in_image="usr/share/icons/hicolor/256x256/apps/devrace.png"
(cd "$tmp" && "$APP_PATH" --appimage-extract "$icon_in_image" > /dev/null 2>&1) || true
if [ -f "$tmp/squashfs-root/$icon_in_image" ]; then
    mkdir -p "$(dirname "$ICON_FILE")"
    cp -f "$tmp/squashfs-root/$icon_in_image" "$ICON_FILE"
else
    warn "could not take the icon out of the AppImage"
fi

# ---------------------------------------------------------------------------------------------
# 2. Game data
# ---------------------------------------------------------------------------------------------
is_game_dir() { [ -f "$1/DATA/GENERAL.TXT" ] || [ -f "$1/data/general.txt" ]; }

# Prints the subfolder of $1 whose name matches $2 (case-insensitive) and holds game data.
find_sub() {
    local d
    while IFS= read -r d; do
        if is_game_dir "$d"; then
            printf '%s\n' "$d"
            return 0
        fi
    done < <(find "$1" -mindepth 1 -maxdepth 2 -type d -iname "$2" 2> /dev/null)
    return 1
}

steam_roots=("$HOME/.local/share/Steam" "$HOME/.steam/steam" "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam")
candidates=()
for root in "${steam_roots[@]}"; do
    vdf="$root/steamapps/libraryfolders.vdf"
    [ -f "$vdf" ] || continue
    while IFS= read -r lib; do
        acf="$lib/steamapps/appmanifest_$STEAM_APPID.acf"
        [ -f "$acf" ] || continue
        installdir="$(sed -n 's/^[[:space:]]*"installdir"[[:space:]]*"\(.*\)".*/\1/p' "$acf" | head -n 1)"
        [ -n "$installdir" ] && candidates+=("$lib/steamapps/common/$installdir")
    done < <(sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)".*/\1/p' "$vdf")
done
for pattern in "$HOME/GOG Games/"*armageddon* "$HOME/Games/Heroic/"*armageddon* "$HOME/Games/"*armageddon* "$HOME/Games/"*Carmageddon*; do
    [ -d "$pattern" ] && candidates+=("$pattern")
done

c1_src=""
sp_src=""
if [ -n "$GAME_DIR_ARG" ]; then
    [ -d "$GAME_DIR_ARG" ] || die "--game-dir: no such folder: $GAME_DIR_ARG"
    candidates=("$GAME_DIR_ARG")
    if is_game_dir "$GAME_DIR_ARG"; then
        case "$(basename "$GAME_DIR_ARG" | tr '[:upper:]' '[:lower:]')" in
        *splat*) sp_src="$GAME_DIR_ARG" ;;
        *) c1_src="$GAME_DIR_ARG" ;;
        esac
    fi
fi
for cand in "${candidates[@]}"; do
    [ -n "$c1_src" ] || c1_src="$(find_sub "$cand" CARMA || true)"
    [ -n "$sp_src" ] || sp_src="$(find_sub "$cand" CARSPLAT || true)"
done

copy_game() { # $1 = source, $2 = name (c1/sp)
    local dest="$GAMES_DIR/$2"
    if [ -d "$dest" ] && [ "$RECOPY" = 0 ]; then
        echo "$2: already there ($dest), kept (use --recopy to copy again)"
        return
    fi
    say "Copying $1 to $dest"
    rm -rf "$dest.tmp"
    mkdir -p "$GAMES_DIR"
    cp -r "$1" "$dest.tmp"
    chmod -R u+w "$dest.tmp"
    rm -rf "$dest"
    mv "$dest.tmp" "$dest"
}

if [ -z "$c1_src" ] && ! [ -d "$GAMES_DIR/c1" ]; then
    die "Carmageddon not found. Install the Carmageddon Max Pack (Steam or GOG) or pass the folder with --game-dir."
fi
[ -n "$c1_src" ] && copy_game "$c1_src" c1
[ -n "$sp_src" ] && copy_game "$sp_src" sp

if ! [ -f "$GAMES_DIR/c1/DATA/PIXELMAP/SMOKE.PIX" ]; then
    warn "$GAMES_DIR/c1 has no DATA/PIXELMAP/SMOKE.PIX (the 3dfx assets). VR needs them;" \
        "the Steam and GOG Max Pack include them."
fi

# ---------------------------------------------------------------------------------------------
# 3. Config
# ---------------------------------------------------------------------------------------------
games_section="[Games]
c1 = $GAMES_DIR/c1"
[ -d "$GAMES_DIR/sp" ] && games_section="$games_section
sp = $GAMES_DIR/sp"

if [ -f "$INI" ]; then
    if grep -q '^\[Games\]' "$INI"; then
        echo "Kept your $INI. Check that its [Games] section points to:"
        echo "$games_section" | sed 's/^/    /'
    else
        say "Adding [Games] to $INI (backup: $INI.bak)"
        cp -f "$INI" "$INI.bak"
        printf '\n%s\n' "$games_section" >> "$INI"
    fi
else
    say "Creating $INI"
    mkdir -p "$PREF_DIR"
    cat > "$INI" <<EOF
; deVRace configuration. All options: https://github.com/$REPO#configuration
[General]
; The OpenGL ("3dfx") renderer is required for VR
Emulate3DFX = 1
Hires = 1
PhysicsPerFrame = 1
; In VR the headset sets the frame rate
FPSLimit = 0
Windowed = 1
Cutscenes = 0
DefaultGame = c1

$games_section
EOF
fi

# ---------------------------------------------------------------------------------------------
# 4. Menu entry
# ---------------------------------------------------------------------------------------------
say "Adding deVRace to the application menu"
exec_prefix=""
[ "$use_extract" = 1 ] && exec_prefix="env APPIMAGE_EXTRACT_AND_RUN=1 "
app_q="$(desktop_quote "$APP_PATH")"
mkdir -p "$(dirname "$DESKTOP_FILE")"
{
    cat <<EOF
[Desktop Entry]
Type=Application
Name=deVRace
GenericName=Carmageddon in VR
Comment=Carmageddon (Dethrace) in VR through OpenXR. Start SteamVR first.
Exec=${exec_prefix}${app_q} --dir $(desktop_quote "$GAMES_DIR/c1")
Icon=devrace
Terminal=false
Categories=Game;ActionGame;
Keywords=Carmageddon;Dethrace;VR;OpenXR;
StartupWMClass=devrace
EOF
    if [ -d "$GAMES_DIR/sp" ]; then
        cat <<EOF
Actions=splatpack;

[Desktop Action splatpack]
Name=Splat Pack
Exec=${exec_prefix}${app_q} --dir $(desktop_quote "$GAMES_DIR/sp")
EOF
    fi
} > "$DESKTOP_FILE"
command -v update-desktop-database > /dev/null && update-desktop-database -q "$DATA_HOME/applications" || true

echo
say "Done."
cat <<EOF
Start SteamVR (or another OpenXR runtime), then start "deVRace" from the application menu,
or from a terminal:

    $APP_PATH --dir "$GAMES_DIR/c1"

Without a running OpenXR runtime the game runs on the monitor. Config: $INI
Uninstall: $0 --uninstall
EOF
case ":$PATH:" in
*":$HOME/.local/bin:"*) ;;
*) echo "(~/.local/bin is not in your PATH, so the 'devrace' command only works with its full path.)" ;;
esac
