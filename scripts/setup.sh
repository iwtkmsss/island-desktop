#!/usr/bin/env bash
set -euo pipefail
missing=false
for tool in cmake ninja c++ pkg-config dbus-run-session pipewire pipewire-pulse pactl pw-link wireplumber; do
    if ! command -v "$tool" > /dev/null; then printf 'Немає: %s\n' "$tool"; missing=true; fi
done
for library in Qt6Core Qt6Quick Qt6QuickControls2 Qt6Widgets Qt6Network Qt6DBus Qt6Test libpulse libpulse-simple xcb xcb-shape; do
    if ! pkg-config --exists "$library"; then printf 'Немає бібліотеки: %s\n' "$library"; missing=true; fi
done
if [[ "$missing" == true ]]; then
    printf '%s\n' 'Залежності Arch Linux:' 'sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-declarative qt6-wayland layer-shell-qt kglobalaccel kwindowsystem plasma-workspace kservice kio kirigami kiconthemes libpulse libxcb dbus pipewire pipewire-pulse wireplumber'
    exit 1
fi
printf '%s\n' 'Основні залежності знайдено. CMake додатково перевірить KDE Frameworks і LayerShellQt.'
