cmake_minimum_required(VERSION 3.14)

project(Klogg_KArchive VERSION 22.04)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Core Widgets Concurrent Network Xml REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core REQUIRED)

add_subdirectory(cmzlib)
add_subdirectory(cmbzip2)
add_subdirectory(cmliblzma)
add_subdirectory(karchive)
