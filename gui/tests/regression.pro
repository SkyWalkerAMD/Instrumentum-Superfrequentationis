TEMPLATE = app
TARGET = gui-regression
include(../common.pri)
QT += testlib
CONFIG += console testcase
SOURCES += regression.cpp
