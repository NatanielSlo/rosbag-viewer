cmake_minimum_required(VERSION 3.22)
project(engine_cesiumjs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)

find_package(ament_cmake REQUIRED)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick WebEngineQuick WebEngineCore)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(engine_cesiumjs src/main.cpp)
qt_add_qml_module(engine_cesiumjs URI Proto VERSION 1.0 QML_FILES qml/Main.qml)
# The web files are served from the source tree (prototype only).
target_compile_definitions(engine_cesiumjs PRIVATE
    WEB_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(engine_cesiumjs PRIVATE
    Qt6::Gui Qt6::Quick Qt6::WebEngineQuick Qt6::WebEngineCore)

install(TARGETS engine_cesiumjs RUNTIME DESTINATION lib/${PROJECT_NAME})
ament_package()
