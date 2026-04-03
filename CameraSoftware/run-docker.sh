#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(pwd)"
BUILD_DIR="$PROJECT_DIR/build"
IMAGE_NAME="joystick-builder"
DOCKERFILE="deploy/docker/Dockerfile-build-ubuntu"

# Clean & prepare
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Build Docker image
docker build -f "$DOCKERFILE" -t "$IMAGE_NAME" .

# Run build
docker run --rm --privileged \
    -v "$PROJECT_DIR":/project \
    -v "$BUILD_DIR":/project/build \
    -e DISPLAY="$DISPLAY" \
    -e QT_X11_NO_MITSHM=1 \
    "$IMAGE_NAME" \
    bash -lc "
      set -eux;
      cd /project/build;
      
      cmake -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/qt6 \
            -DCMAKE_BUILD_TYPE=Release \
            /project;
      cmake --build . -- -j\$(nproc);

      # Prepare AppDir
      rm -rf AppDir;
      mkdir -p AppDir/usr/bin \
               AppDir/usr/share/applications \
               AppDir/usr/share/icons/hicolor/64x64/apps \
               AppDir/usr/share/videos \
               AppDir/usr/lib;

      # Copy application files
      cp JoystickIdentifier AppDir/usr/bin/;
      cp /project/deploy/JoystickIdentifier.desktop AppDir/usr/share/applications/;
      cp /project/deploy/icons/joystickidentifier.png AppDir/usr/share/icons/hicolor/64x64/apps/;
      
      # Copy videos
      cp -r /project/videos/* AppDir/usr/share/videos/;
      
      # Copy AppRun script
      cp /project/AppRun AppDir/;
      chmod +x AppDir/AppRun;
      
      # Bundle GStreamer plugins
        mkdir -p AppDir/usr/lib/gstreamer-1.0
	cp -r /usr/lib/x86_64-linux-gnu/gstreamer-1.0/* AppDir/usr/lib/gstreamer-1.0/

	# Also bundle essential GST libraries
	mkdir -p AppDir/usr/lib/x86_64-linux-gnu/
	cp -r /usr/lib/x86_64-linux-gnu/libgst* AppDir/usr/lib/x86_64-linux-gnu/
	cp -r /usr/lib/x86_64-linux-gnu/libglib* AppDir/usr/lib/x86_64-linux-gnu/
      
      # Bundle AppImage
	linuxdeploy \
	  --appdir AppDir \
	  --executable AppDir/usr/bin/JoystickIdentifier \
	  --desktop-file AppDir/usr/share/applications/JoystickIdentifier.desktop \
	  --icon-file    AppDir/usr/share/icons/hicolor/64x64/apps/joystickidentifier.png \
	  --plugin qt \
	  --output appimage
      
      mv *.AppImage /project/build/JoystickIdentifier.AppImage
    "

echo "✅ Done! AppImage: $BUILD_DIR/JoystickIdentifier.AppImage"
