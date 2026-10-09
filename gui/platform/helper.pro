TEMPLATE = app
TARGET = octool-hwio-helper
CONFIG += console c++11
CONFIG -= qt app_bundle
QMAKE_CFLAGS += -std=gnu11
LIBS += -lpthread
SOURCES += helper_main.cpp linux_helper.cpp linux_hwio.cpp linux_cpu.cpp \
    ../core/hardware.cpp ../core/helper_protocol.cpp ../../port/hal/octool_hwio.c
HEADERS += hardware_factory.h linux_helper.h linux_hwio.h \
    ../core/hardware.h ../core/helper_protocol.h ../../port/hal/octool_hwio.h
