#!/bin/bash
set -e

QT_URL="https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_612/qt.qt6.612.gcc_64"
QT_INSTALLER="qt-unified-linux-x64-4.6.1-online.run"

wget -q "$QT_URL/$QT_INSTALLER"
chmod +x $QT_INSTALLER
./$QT_INSTALLER install \
    --accept-licenses \
    --accept-obligations \
    --auto-answer \
    --default-answer \
    --minimal \
    --root "$QT_PATH" \
    qt6 \
    qt6.6.1 \
    $QT_MODULES
rm $QT_INSTALLER
