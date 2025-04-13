QT += core gui widgets network

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
    src/controllers/facerecognitioncontroller.cpp \
    src/models/modelmanager.cpp \
    src/models/settingsmanager.cpp \
    src/models/faissmanager.cpp \
    src/ui/mainwindow.cpp \
    src/ui/videowidget.cpp

HEADERS += \
    src/controllers/facedetectioncontroller.h \
    src/controllers/facerecognitioncontroller.h \
    src/models/modelmanager.h \
    src/models/settingsmanager.h \
    src/models/faissmanager.h \
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
    $$PWD/InspireFace/include \
    /opt/homebrew/include/opencv4 \
    /opt/homebrew/include/postgresql@17 \
    /opt/homebrew/include/libpq \
    $$PWD/src

# Library paths
LIBS += \
    -F/opt/homebrew/lib \
    -framework QtNetwork \
    -framework QtWidgets \
    -framework QtGui \
    -framework QtCore \
    -L/opt/homebrew/lib \
    -lopencv_core \
    -lopencv_imgproc \
    -lopencv_highgui \
    -lopencv_videoio \
    -lopencv_objdetect \
    -lopencv_face \
    -L$$PWD/InspireFace/lib \
    -lInspireFace \
    -L/opt/homebrew/lib \
    -lpq \
    -lfaiss

# Mac specific configurations
macx {
    QMAKE_INFO_PLIST = Info.plist
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 14.0
    QMAKE_APPLE_DEVICE_ARCHS = arm64
    
    # PostgreSQL configuration for macOS
    INCLUDEPATH += \
        /opt/homebrew/Cellar/postgresql@17/17.4_1/include \
        /opt/homebrew/Cellar/faiss/1.10.0/include \
        /opt/homebrew/Cellar/libpq/17.4_1/include
    LIBS += -L/opt/homebrew/Cellar/libpq/17.4_1/lib
}
