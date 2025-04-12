# Add source files
set(SOURCES
    src/main.cpp
    src/ui/mainwindow.cpp
    src/ui/camerawidget.cpp
    src/models/modelmanager.cpp
    src/models/settingsmanager.cpp
    src/models/faissmanager.cpp
    src/controllers/facerecognitioncontroller.cpp
) 

# Find required packages
find_package(Qt6 COMPONENTS Core Gui Widgets Network REQUIRED)
find_package(OpenCV REQUIRED)

# Add FAISS library
find_library(FAISS_LIBRARY NAMES faiss PATHS /opt/homebrew/lib)
if(NOT FAISS_LIBRARY)
    message(FATAL_ERROR "FAISS library not found")
endif()

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    ${OpenCV_LIBS}
    ${FAISS_LIBRARY}
    ${INSPIRE_FACE_LIBRARY}
) 