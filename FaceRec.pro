QT += core gui
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

HEADERS += \
    mainwindow.h

SOURCES += \
    main.cpp \
    mainwindow.cpp

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# Include OpenCV
INCLUDEPATH += /opt/homebrew/opt/opencv/include/opencv4
LIBS += -L/opt/homebrew/opt/opencv/lib \
        -lopencv_core \
        -lopencv_highgui \
        -lopencv_imgproc \
        -lopencv_videoio \
        -lopencv_imgcodecs \
        -lopencv_video

# Include InspireFace
INCLUDEPATH += $$PWD/InspireFace/include
LIBS += -L$$PWD/InspireFace/lib -lInspireFace

# MacOS specific settings
macx {
    QMAKE_CXXFLAGS += -std=c++11
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 14.0
    
    # Set RPATH
    QMAKE_RPATHDIR += @executable_path/../Frameworks
    QMAKE_RPATHDIR += $$PWD/InspireFace/lib
    
    # Copy InspireFace library to app bundle
    INSPIREFACELIB.files = $$PWD/InspireFace/lib/libInspireFace.dylib
    INSPIREFACELIB.path = Contents/Frameworks
    QMAKE_BUNDLE_DATA += INSPIREFACELIB
}

# Copy streams.json to app bundle
RESOURCES += streams.json
STREAMSJSON.files = streams.json
STREAMSJSON.path = Contents/Resources
QMAKE_BUNDLE_DATA += STREAMSJSON
