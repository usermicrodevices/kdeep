#!/bin/bash
set -e

echo "=== Kate AI Plugin Build for KF6/Flatpak & Native (Ubuntu 25.10) ==="

# ... (all the initial package checks remain unchanged) ...

# ----------------------------------------------------------------------
# Build the plugin
# ----------------------------------------------------------------------
BUILD_DIR="build_kf6"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

echo "Configuring CMake..."
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DQT_MAJOR_VERSION=6

echo "Building plugin..."
cmake --build "$BUILD_DIR"

# Locate the built plugin
PLUGIN_FILE=$(find "$BUILD_DIR" -name "kdeepseek.so" -type f | head -n1)
if [ -z "$PLUGIN_FILE" ]; then
    echo "Error: Build failed – plugin not found." >&2
    exit 1
fi

# ----------------------------------------------------------------------
# Native (system) installation
# ----------------------------------------------------------------------
check_native_kate() {
    dpkg -l kate 2>/dev/null | grep -q "^ii"
}

if check_native_kate; then
    echo "Native Kate (via apt) detected."
    read -p "Install plugin system-wide (for native Kate) as well? (Y/n) " reply
    echo
    if [[ $reply =~ ^[Yy]$ ]] || [[ -z $reply ]]; then
        echo "Removing any existing system-wide plugin..."
        sudo find /usr/lib /usr/local/lib -path "*/plugins/kf6/ktexteditor/kdeepseek.so" -type f -delete 2>/dev/null || true
        sudo find /usr/lib/x86_64-linux-gnu/qt6/plugins -name "kdeepseek.so" -type f -delete 2>/dev/null || true

        PLUGIN_DIR="/usr/lib/x86_64-linux-gnu/qt6/plugins/kf6/ktexteditor"
        echo "Installing to $PLUGIN_DIR..."
        sudo mkdir -p "$PLUGIN_DIR"
        sudo cp "$PLUGIN_FILE" "$PLUGIN_DIR/"
        sudo chmod 644 "$PLUGIN_DIR/kdeepseek.so"
        sudo cmake --install "$BUILD_DIR" --prefix /usr || true

        if [ -f "$PLUGIN_DIR/kdeepseek.so" ]; then
            echo "Successfully installed to: $PLUGIN_DIR/kdeepseek.so"
        else
            echo "Error: Failed to install plugin to system location." >&2
            exit 1
        fi
    fi
else
    echo "Native Kate not detected. Skipping system installation."
fi

# ----------------------------------------------------------------------
# Flatpak installation (optional)
# ----------------------------------------------------------------------
if command -v flatpak &>/dev/null && flatpak list 2>/dev/null | grep -q org.kde.kate; then
    echo "Flatpak Kate detected."
    read -p "Install plugin for Flatpak Kate? (Y/n) " reply
    echo
    if [[ $reply =~ ^[Yy]$ ]] || [[ -z $reply ]]; then
        FLATPAK_PATH=$(flatpak info --show-location org.kde.kate)
        if [ -z "$FLATPAK_PATH" ]; then
            echo "Error: Could not determine Flatpak installation path." >&2
            exit 1
        else
            echo "USED FLATPAK PATH: $FLATPAK_PATH"
        fi

        # Check if Flatpak Kate uses KF6 runtime
        RUNTIME=$(flatpak info --show-runtime org.kde.kate)
        echo "Detected runtime: $RUNTIME"
        if [[ "$RUNTIME" =~ org\.kde\.Platform.*6 ]]; then
        #if flatpak info --show-runtime org.kde.kate | grep -E "org.kde.Platform/[^/]+/6"; then
            echo "Flatpak Kate is KF6-based. Installing..."
        else
            echo "Warning: Flatpak Kate might be KF5-based. The plugin may not load."
            read -p "Continue anyway? (Y/n) " reply2
            echo
            if [[ ! $reply2 =~ ^[Yy]$ ]] && [[ -n $reply2 ]]; then
                echo "Skipping Flatpak installation."
                exit 0
            fi
        fi

        # Determine if the flatpak is system-wide or user
        if [[ "$FLATPAK_PATH" == /var/lib/flatpak/* ]]; then
            USE_SUDO=1
        else
            USE_SUDO=0
        fi

        echo "Removing any existing Flatpak plugin..."
        sudo find /var/lib/flatpak/app/org.kde.kate -name "kdeepseek.so" -type f -delete 2>/dev/null || true
        find "$HOME/.local/share/flatpak/app/org.kde.kate" -name "kdeepseek.so" -type f -delete 2>/dev/null || true

        TARGET_DIR="$FLATPAK_PATH/files/lib/plugins/kf6/ktexteditor"
        echo "Installing to: $TARGET_DIR"
        if [ $USE_SUDO -eq 1 ]; then
            sudo mkdir -p "$TARGET_DIR"
            sudo cp "$PLUGIN_FILE" "$TARGET_DIR/kdeepseek.so"
            sudo chmod 644 "$TARGET_DIR/kdeepseek.so"
        else
            mkdir -p "$TARGET_DIR"
            cp "$PLUGIN_FILE" "$TARGET_DIR/kdeepseek.so"
            chmod 644 "$TARGET_DIR/kdeepseek.so"
        fi

        if [ -f "$TARGET_DIR/kdeepseek.so" ]; then
            echo "Successfully installed to: $TARGET_DIR/kdeepseek.so"
        else
            echo "Error: Failed to install plugin to Flatpak location." >&2
            exit 1
        fi

        #TODO: check it and if given Kate permanent permission to access the network not existing then do command below
        flatpak override --user --share=network org.kde.kate
    fi
else
    echo "Flatpak Kate not found. Skipping Flatpak installation."
fi

# ----------------------------------------------------------------------
# Done
# ----------------------------------------------------------------------
echo "Done."
echo
echo "IMPORTANT: After installation, you must enable the plugin in Kate:"
echo "  1. Open Kate"
echo "  2. Settings → Configure Kate → Plugins"
echo "  3. Check 'DeepSeek Assistant' and click Apply"
echo "  4. Restart Kate"
echo
echo "Then configure your API key in: Settings → Configure Kate → DeepSeek"
echo
echo "Test with:"
echo "  flatpak run --env=QT_DEBUG_PLUGINS=1 --env=QT_LOGGING_RULES=\"kate.deepseek.debug=true\" org.kde.kate"
echo "  or"
echo "  QT_LOGGING_RULES=\"kate.deepseek.debug=true\" kate"
