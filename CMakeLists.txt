cmake_minimum_required(VERSION 3.16)

project(wirecloak VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(QT_MAJOR_VERSION 6)

# === 1. COMPILER FLAGS (x86-64-v3) ===
if((CMAKE_CXX_COMPILER_ID MATCHES "GNU") OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
        add_compile_options(-march=x86-64-v3)
    endif()
endif()

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECompilerSettings)
include(ECMFindQmlModule)
include(ECMSetupVersion)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Svg DBus)
find_package(KF6 REQUIRED COMPONENTS CoreAddons I18n WindowSystem)
find_package(MauiKit4 REQUIRED)
find_package(MauiKitFileBrowsing4 REQUIRED)

ecm_find_qmlmodule(org.mauikit.controls 1.0)
ecm_find_qmlmodule(org.mauikit.filebrowsing 1.0)

# === 2. GIT VERSION INFO ===
find_package(Git)
if(Git_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    add_compile_definitions(GIT_BRANCH="${GIT_BRANCH}")
    add_compile_definitions(GIT_COMMIT_HASH="${GIT_COMMIT_HASH}")
endif()

# === 3. EXECUTABLE ===
add_executable(wirecloak
    src/main.cpp
    src/vpnbackend.cpp
    src/vpnbackend.h
    resources.qrc
)

target_link_libraries(wirecloak PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Svg
    Qt6::DBus
    KF6::CoreAddons
    KF6::I18n
    KF6::WindowSystem
    MauiKit4
    MauiKit4::FileBrowsing
)

install(TARGETS wirecloak ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# === 4. INSTALL ASSETS ===
install(FILES data/org.nitrux.wirecloak.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES dist/10-wirecloak.rules DESTINATION ${KDE_INSTALL_DATAROOTDIR}/polkit-1/rules.d)
