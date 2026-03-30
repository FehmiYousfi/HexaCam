#!/usr/bin/env bash
set -euxo pipefail

# 1) Build the Qt app
cmake \
  -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/qt6 \
  -DCMAKE_BUILD_TYPE=Release \
  ..
cmake --build . -- -j"$(nproc)"

# 2) Prepare AppDir
rm -rf AppDir
mkdir -p AppDir/usr/bin
cp JoystickIdentifier AppDir/usr/bin/

# 3) Install linuxdeploy core
#    We use the official continuous build from GitHub.
wget -qO /usr/local/bin/linuxdeploy \
     https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
chmod +x /usr/local/bin/linuxdeploy

# 4) Install the Qt6 plugin
wget -qO /usr/local/bin/linuxdeploy-plugin-qt6 \
     https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt.sh
chmod +x /usr/local/bin/linuxdeploy-plugin-qt6

# 5) Bundle everything
linuxdeploy \
  --appdir AppDir \
  --executable AppDir/usr/bin/JoystickIdentifier \
  --plugin qt6 \
  --output appimage

# 6) Rename for predictability
mv *.AppImage JoystickIdentifier.AppImage

echo "✅ Bundled AppImage: $(pwd)/JoystickIdentifier.AppImage"

