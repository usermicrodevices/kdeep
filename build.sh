#!/bin/bash
set -e

THIRDPARTY_DIR="thirdparty"

echo "=== Kate AI Plugin Build for KF6/Flatpak & Native Linux OS ==="

echo "Checking build dependencies..."

REQUIRED_PACKAGES=(
    cmake
    extra-cmake-modules
    build-essential
    pkg-config
    qt6-base-dev
    libkf6coreaddons-dev
    libkf6i18n-dev
    libkf6texteditor-dev
    libkf6config-dev
)

MISSING_PACKAGES=()
for pkg in "${REQUIRED_PACKAGES[@]}"; do
    if ! dpkg -s "$pkg" &>/dev/null; then
        MISSING_PACKAGES+=("$pkg")
    fi
done

if [ ${#MISSING_PACKAGES[@]} -gt 0 ]; then
    echo "Missing required packages: ${MISSING_PACKAGES[*]}"
    read -r -p "Install them now? [Y/n] " reply
    case "$reply" in
        [Nn])
            echo "Please install the missing packages and rerun the script."
            exit 1
            ;;
        *)
            sudo apt update
            sudo apt install -y "${MISSING_PACKAGES[@]}"
            ;;
    esac
else
    echo "All required packages are installed."
fi

if ! command -v git &>/dev/null; then
    echo "Error: git is required to fetch PicoLLM source." >&2
    exit 1
fi


PICOLM_DIR="$THIRDPARTY_DIR/picolm"
PICOLM_UPDATED=0
if [ ! -d "$PICOLM_DIR" ]; then
    echo "Cloning PicoLLM repository..."
    git clone git@github.com:RightNow-AI/picolm.git "$PICOLM_DIR"
else
    echo "PicoLLM repository already exists."
    read -r -p "Updating $PICOLM_DIR from Github...? [y/N] " reply
    echo
    case "$reply" in
        [Yy])
            (cd "$PICOLM_DIR" && git pull)
            PICOLM_UPDATED=1
            ;;
        *)
            ;;
    esac
fi

REQUIRED_FILES=(
    "picolm/picolm.c"
    "picolm/model.h"
    "picolm/model.c"
    "picolm/tensor.h"
    "picolm/tensor.c"
    "picolm/quant.h"
    "picolm/quant.c"
    "picolm/tokenizer.h"
    "picolm/tokenizer.c"
    "picolm/sampler.h"
    "picolm/sampler.c"
    "picolm/grammar.h"
    "picolm/grammar.c"
)
for f in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "$PICOLM_DIR/$f" ]; then
        echo "Error: Missing PicoLLM source file: $PICOLM_DIR/$f" >&2
        exit 1
    fi
done

BUILD_DIR="build_kf6"

if [ -d "$BUILD_DIR" ]; then
    echo "Directory $BUILD_DIR already exists."
    read -r -p "Clean and rebuild? [y/N] " reply
    echo
    case "$reply" in
        [Yy])
            if [ "$PICOLM_UPDATED" -eq 1 ]; then
                echo "Thirdparty updated – full clean rebuild..."
                rm -rf "$BUILD_DIR"
                mkdir -p "$BUILD_DIR"
            else
                echo "Cleaning kdeep build files only (keeping picolm cache)..."
                rm -f "$BUILD_DIR"/CMakeFiles/kdeep.dir/*.o
                rm -f "$BUILD_DIR"/CMakeFiles/kdeep.dir/**/*.o
                rm -f "$BUILD_DIR"/kdeep_autogen/*. moc_*
                rm -f "$BUILD_DIR"/kdeep.so
                rm -f "$BUILD_DIR"/CMakeCache.txt
            fi
            ;;
        *)
            echo "Keeping existing build directory."
            ;;
    esac
else
    mkdir -p "$BUILD_DIR"
fi

echo "Configuring CMake..."
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DQT_MAJOR_VERSION=6

echo "Building plugin..."
cmake --build "$BUILD_DIR"

PLUGIN_FILE=$(find "$BUILD_DIR" -name "kdeep.so" -type f | head -n1)
if [ -z "$PLUGIN_FILE" ]; then
    echo "Error: Build failed – plugin not found." >&2
    exit 1
fi

check_native_kate() {
    dpkg -l kate 2>/dev/null | grep -q "^ii"
}

if check_native_kate; then
    echo "Native Kate (via apt) detected."
    read -r -p "Install plugin system-wide (for native Kate) as well? [Y/n] " reply
    echo
    case "$reply" in
        [Nn])
            echo "Skipping system-wide installation."
            ;;
        *)
            echo "Removing any existing system-wide plugin..."
            sudo find /usr/lib /usr/local/lib -path "*/plugins/kf6/ktexteditor/kdeep.so" -type f -delete 2>/dev/null || true
            sudo find /usr/lib/x86_64-linux-gnu/qt6/plugins -name "kdeep.so" -type f -delete 2>/dev/null || true

            PLUGIN_DIR="/usr/lib/x86_64-linux-gnu/qt6/plugins/kf6/ktexteditor"
            echo "Installing to $PLUGIN_DIR..."
            sudo mkdir -p "$PLUGIN_DIR"
            sudo cp "$PLUGIN_FILE" "$PLUGIN_DIR/"
            sudo chmod 644 "$PLUGIN_DIR/kdeep.so"
            sudo cmake --install "$BUILD_DIR" --prefix /usr || true

            if [ -f "$PLUGIN_DIR/kdeep.so" ]; then
                echo "Successfully installed to: $PLUGIN_DIR/kdeep.so"
            else
                echo "Error: Failed to install plugin to system location." >&2
                exit 1
            fi
            ;;
    esac
else
    echo "Native Kate not detected. Skipping system installation."
fi

if command -v flatpak &>/dev/null && flatpak list 2>/dev/null | grep -q org.kde.kate; then
    echo "Flatpak Kate detected."
    read -r -p "Install plugin for Flatpak Kate? [Y/n] " reply
    echo
    case "$reply" in
        [Nn])
            echo "Skipping Flatpak installation."
            ;;
        *)
            FLATPAK_PATH=$(flatpak info --show-location org.kde.kate)
            if [ -z "$FLATPAK_PATH" ]; then
                echo "Error: Could not determine Flatpak installation path." >&2
                exit 1
            else
                echo "USED FLATPAK PATH: $FLATPAK_PATH"
            fi

            RUNTIME=$(flatpak info --show-runtime org.kde.kate)
            echo "Detected runtime: $RUNTIME"
            case "$RUNTIME" in
                org.kde.Platform*6)
                    echo "Flatpak Kate is KF6-based. Installing..."
                    ;;
                *)
                    echo "Warning: Flatpak Kate might be KF5-based. The plugin may not load."
                    read -r -p "Continue anyway? [Y/n] " reply2
                    echo
                    case "$reply2" in
                        [Nn])
                            echo "Skipping Flatpak installation."
                            exit 0
                            ;;
                        *)
                            ;;
                    esac
                    ;;
            esac

            if [ "$FLATPAK_PATH" == /var/lib/flatpak/* ]; then
                USE_SUDO=1
            else
                USE_SUDO=0
            fi

            echo "Removing any existing Flatpak plugin..."
            sudo find /var/lib/flatpak/app/org.kde.kate -name "kdeep.so" -type f -delete 2>/dev/null || true
            find "$HOME/.local/share/flatpak/app/org.kde.kate" -name "kdeep.so" -type f -delete 2>/dev/null || true

            TARGET_DIR="$FLATPAK_PATH/files/lib/plugins/kf6/ktexteditor"
            echo "Installing to: $TARGET_DIR"
            if [ $USE_SUDO -eq 1 ]; then
                sudo mkdir -p "$TARGET_DIR"
                sudo cp "$PLUGIN_FILE" "$TARGET_DIR/kdeep.so"
                sudo chmod 644 "$TARGET_DIR/kdeep.so"
            else
                mkdir -p "$TARGET_DIR"
                cp "$PLUGIN_FILE" "$TARGET_DIR/kdeep.so"
                chmod 644 "$TARGET_DIR/kdeep.so"
            fi

            if [ -f "$TARGET_DIR/kdeep.so" ]; then
                echo "Successfully installed to: $TARGET_DIR/kdeep.so"
            else
                echo "Error: Failed to install plugin to Flatpak location." >&2
                exit 1
            fi

            flatpak override --user --share=network org.kde.kate
            ;;
    esac
else
    echo "Flatpak Kate not found. Skipping Flatpak installation."
fi

echo "Done."
echo
echo "IMPORTANT: After installation, you must enable the plugin in Kate:"
echo "  1. Open Kate"
echo "  2. Settings → Configure Kate → Plugins"
echo "  3. Check 'KDeep AI Assistant' and click Apply"
echo "  4. Restart Kate"
echo
echo "Then configure your API key in: Settings → Configure Kate → KDeep"
echo
echo "Test with:"
echo "  flatpak run --env=QT_DEBUG_PLUGINS=1 --env=QT_LOGGING_RULES=\"kate.deep.debug=true\" org.kde.kate"
echo "  or"
echo "  QT_LOGGING_RULES=\"kate.deep.debug=true\" kate"
