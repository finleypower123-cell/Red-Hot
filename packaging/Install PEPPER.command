#!/bin/bash
# Double-click to install PEPPER. Installs every format found next to this script.
cd "$(dirname "$0")"
set -e

echo "Installing PEPPER..."
mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Library/Audio/Plug-Ins/Components"

if [ -d "PEPPER.vst3" ]; then
    rm -rf "$HOME/Library/Audio/Plug-Ins/VST3/PEPPER.vst3"
    cp -R "PEPPER.vst3" "$HOME/Library/Audio/Plug-Ins/VST3/"
    xattr -cr "$HOME/Library/Audio/Plug-Ins/VST3/PEPPER.vst3"
    echo "  VST3 -> ~/Library/Audio/Plug-Ins/VST3"
fi

if [ -d "PEPPER.component" ]; then
    rm -rf "$HOME/Library/Audio/Plug-Ins/Components/PEPPER.component"
    cp -R "PEPPER.component" "$HOME/Library/Audio/Plug-Ins/Components/"
    xattr -cr "$HOME/Library/Audio/Plug-Ins/Components/PEPPER.component"
    echo "  AU   -> ~/Library/Audio/Plug-Ins/Components"
fi

if [ -d "PEPPER.aaxplugin" ]; then
    AAX_DIR="/Library/Application Support/Avid/Audio/Plug-Ins"
    echo "  AAX needs your Mac password to install into the Pro Tools folder:"
    sudo mkdir -p "$AAX_DIR"
    sudo rm -rf "$AAX_DIR/PEPPER.aaxplugin"
    sudo cp -R "PEPPER.aaxplugin" "$AAX_DIR/"
    sudo xattr -cr "$AAX_DIR/PEPPER.aaxplugin"
    echo "  AAX  -> $AAX_DIR"
fi

killall -9 AudioComponentRegistrar 2>/dev/null || true
echo ""
echo "Done. Restart your DAW and look for PEPPER under Red Hot / Distortion."
