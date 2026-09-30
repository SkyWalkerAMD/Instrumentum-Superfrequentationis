QT += widgets concurrent
CONFIG += c++11
QTPLUGIN += qxcb qoffscreen
QMAKE_CFLAGS += -std=gnu11
SOURCES += $$PWD/access.cpp $$PWD/registerpanel.cpp $$PWD/../port/hal/octool_hwio.c
HEADERS += $$PWD/access.h $$PWD/registerpanel.h $$PWD/../port/hal/octool_hwio.h
INCLUDEPATH += $$PWD
