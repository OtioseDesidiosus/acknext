#-------------------------------------------------
#
# Project created by QtCreator 2017-08-12T12:01:33
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = MIEP
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which as been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

include(../../gl3w/gl3w.pri)

win32:CONFIG(release, debug|release): LIBS += -L$$OUT_PWD/../acknext/release/ -lacknext
else:win32:CONFIG(debug, debug|release): LIBS += -L$$OUT_PWD/../acknext/debug/ -lacknext
else:unix: LIBS += -L$$OUT_PWD/../acknext/ -lacknext

INCLUDEPATH += $$PWD/../acknext
DEPENDPATH += $$PWD/../acknext

INCLUDEPATH += $$PWD/../acknext/include

SOURCES += \
        main.cpp \
        mainwindow.cpp \
    qacknextwidget.cpp \
    daaang.cpp \
    materialeditor.cpp \
    ackcolorselector.cpp \
    imageview.cpp

HEADERS += \
        mainwindow.hpp \
    qacknextwidget.hpp \
    daaang.hpp \
    materialeditor.hpp \
    ackcolorselector.hpp \
    imageview.hpp

FORMS += \
        mainwindow.ui \
    materialeditor.ui \
    ackcolorselector.ui

RESOURCES += \
    ../resource/ackui.qrc
