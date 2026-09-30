TEMPLATE = app
TARGET = octool
include(common.pri)
OCTOOL_VERSION = $$cat($$PWD/../VERSION)
DEFINES += OCTOOL_VERSION=\\\"$$OCTOOL_VERSION\\\"
SOURCES += main.cpp
