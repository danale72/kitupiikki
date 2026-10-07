QT += testlib widgets

CONFIG += qt console warn_on depend_includepath testcase
CONFIG -= app_bundle

TEMPLATE = app

INCLUDEPATH += $$PWD/../../kitsas
VPATH += $$PWD/../../kitsas

SOURCES += \
    tst_liitecachetest.cpp \
    liite/liitecache.cpp \
    liite/cacheliite.cpp

HEADERS += \
    liite/liitecache.h \
    liite/cacheliite.h \
    db/kpkysely.h \
    db/kitsasinterface.h
