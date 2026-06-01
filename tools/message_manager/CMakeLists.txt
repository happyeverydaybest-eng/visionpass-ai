# VisionPass 消息管理程序（Ubuntu x86端）
#
# 编译：
#   mkdir build && cd build
#   cmake ..
#   make
#
# 运行：
#   ./message_manager

cmake_minimum_required(VERSION 3.10)
project(message_manager VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Network)

add_executable(message_manager
    main.cpp
    MainWindow.cpp
    MessageServer.cpp
)

target_link_libraries(message_manager PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Network
)
