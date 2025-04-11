QT += core gui widgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

TARGET = facerec
TEMPLATE = app

# Set build directories
OBJECTS_DIR = build/obj
MOC_DIR = build/moc
RCC_DIR = build/rcc
UI_DIR = build/ui

SOURCES += \
    main.cpp \
    src/controllers/facedetectioncontroller.cpp \
    src/models/modelmanager.cpp \
    src/models/settingsmanager.cpp \
    src/ui/mainwindow.cpp \
    src/ui/videowidget.cpp

HEADERS += \
    src/controllers/facedetectioncontroller.h \
    src/models/modelmanager.h \
    src/models/settingsmanager.h \
    src/ui/mainwindow.h \
    src/ui/videowidget.h

FORMS += \
    src/ui/mainwindow.ui \
    src/ui/videowidget.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# Include paths
INCLUDEPATH += \
    /opt/homebrew/include \
    /opt/homebrew/lib/QtCore.framework/Headers \
    /opt/homebrew/lib/QtGui.framework/Headers \
    /opt/homebrew/lib/QtWidgets.framework/Headers \
    $$PWD/InspireFace/include \
    /opt/homebrew/include/opencv4 \
    $$PWD/src

# Library paths
LIBS += \
    -F/opt/homebrew/lib \
    -framework QtCore \
    -framework QtGui \
    -framework QtWidgets \
    -L/opt/homebrew/lib \
    -lopencv_core \
    -lopencv_imgproc \
    -lopencv_highgui \
    -lopencv_videoio \
    -lopencv_objdetect \
    -lopencv_face \
    -L$$PWD/InspireFace/lib \
    -lInspireFace

# Mac specific configurations
macx {
    QMAKE_INFO_PLIST = Info.plist
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 14.0
    QMAKE_APPLE_DEVICE_ARCHS = arm64
}
