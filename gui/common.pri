QT += widgets concurrent
CONFIG += c++11
# Override the platform group: adding QTPLUGIN alone still auto-imports
# Wayland plugins built against EL8's newer Wayland protocol library.
# Desktop Wayland sessions use Xwayland; offscreen is for Qt regressions.
QTPLUGIN.platforms = qxcb qoffscreen
QMAKE_CFLAGS += -std=gnu11
SOURCES += $$PWD/access.cpp $$PWD/registerpanel.cpp $$PWD/../port/hal/octool_hwio.c
HEADERS += $$PWD/access.h $$PWD/registerpanel.h $$PWD/../port/hal/octool_hwio.h
INCLUDEPATH += $$PWD
