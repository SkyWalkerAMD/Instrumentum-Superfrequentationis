QT += widgets concurrent
CONFIG += c++11
# Override the platform group: adding QTPLUGIN alone still auto-imports
# Wayland plugins built against EL8's newer Wayland protocol library.
# Desktop Wayland sessions use Xwayland; offscreen is for Qt regressions.
QTPLUGIN.platforms = qxcb qoffscreen
QMAKE_CFLAGS += -std=gnu11
SOURCES += $$PWD/access.cpp $$PWD/registerpanel.cpp
SOURCES += $$PWD/pstates.cpp
HEADERS += $$PWD/pstates.h
SOURCES += $$PWD/core/amd_pstates.cpp $$PWD/core/hardware.cpp
HEADERS += $$PWD/core/amd_pstates.h $$PWD/core/hardware.h
HEADERS += $$PWD/access.h $$PWD/registerpanel.h $$PWD/platform/hardware_factory.h
linux {
    SOURCES += $$PWD/platform/linux_hwio.cpp $$PWD/../port/hal/octool_hwio.c
    HEADERS += $$PWD/platform/linux_hwio.h $$PWD/../port/hal/octool_hwio.h
} else {
    error("The GUI requires an implemented OS backend. The portable core builds independently with CMake.")
}
INCLUDEPATH += $$PWD
