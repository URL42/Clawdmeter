#!/usr/bin/env bash
# Build the macOS app from this checkout, for this Mac's architecture only,
# and optionally install it over the released one.
#
#   tools/packaging/macos-app.sh            # build desktop/target/Clawdmeter.app
#   tools/packaging/macos-app.sh --install  # ...then replace /Applications/Clawdmeter.app and start it
#
# The bundle matches release.yml's (same Info.plist, ad-hoc signed), so an
# install keeps the path that Start at login and the Claude Code hooks point
# at. A new ad-hoc signature can make macOS ask for Bluetooth access again.
# Needs Rust (on PATH, or Homebrew's keg-only rustup) and the Command Line
# Tools.
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
install=false
[ "${1:-}" = "--install" ] && install=true

if ! command -v cargo >/dev/null && command -v brew >/dev/null; then
    PATH="$(brew --prefix rustup)/bin:$PATH"
fi

# `clawdmeter --version`, the logs and the settings sidebar report this.
# Not Apple's all-numeric form, which macOS tolerates for an app like this.
tag=$(git -C "$root" describe --tags --abbrev=0 --match 'v*' 2>/dev/null || echo v0.0.0)
ver="${tag#v}+fork.$(git -C "$root" rev-parse --short HEAD)"
[ -n "$(git -C "$root" status --porcelain)" ] && ver="$ver.dirty"
export CLAWDMETER_VERSION="$ver"
# runtime_shaders: GPUI compiles its Metal shaders when the app starts, so the
# build needs only the Command Line Tools, not Xcode's `metal` compiler.
(cd "$root/desktop" && cargo build --release --locked -p clawdmeter-app --features gpui/runtime_shaders)

# CoreBluetooth needs NSBluetoothAlwaysUsageDescription in the app bundle, or
# macOS kills the process under the LaunchAgent; LSUIElement keeps the tray app
# out of the Dock. Keep this Info.plist in step with release.yml's.
app="$root/desktop/target/Clawdmeter.app"
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$root/desktop/target/release/clawdmeter" "$app/Contents/MacOS/"
cp "$root/tools/packaging/icons/clawdmeter.icns" "$app/Contents/Resources/"
cat > "$app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleIconFile</key><string>clawdmeter</string>
  <key>CFBundleIdentifier</key><string>com.clawdmeter.app</string>
  <key>CFBundleName</key><string>Clawdmeter</string>
  <key>CFBundleExecutable</key><string>clawdmeter</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$ver</string>
  <key>CFBundleVersion</key><string>$ver</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSUIElement</key><true/>
  <key>NSBluetoothAlwaysUsageDescription</key><string>Clawdmeter sends your Claude Code usage to your Clawdmeter over Bluetooth.</string>
</dict></plist>
EOF
plutil -lint "$app/Contents/Info.plist" >/dev/null
codesign --force --sign - "$app"
codesign --verify "$app"
echo "built $app ($ver)"

$install || exit 0
dest=/Applications/Clawdmeter.app
# The tray app and any open window are the same binary. Wait for them to go:
# a new one started while the old one still holds the single-instance lock
# hands over to it and exits.
pkill -x clawdmeter 2>/dev/null || true
for _ in $(seq 50); do
    pgrep -x clawdmeter >/dev/null || break
    sleep 0.2
done
if pgrep -x clawdmeter >/dev/null; then
    echo "clawdmeter is still running; quit it and run this again" >&2
    exit 1
fi
rm -rf "$dest"
ditto "$app" "$dest"
open "$dest"
echo "installed $dest"
