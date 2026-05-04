cmake_minimum_required(VERSION 3.16...3.28)

project(st-pluginmanager VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_library(st-pluginmanager MODULE
    src/plugin-main.c
    src/auth.c
    src/downloader.c
    src/obfuscation.cpp
    src/manager-dialog.cpp
)

if(WIN32)
    set(OBS_SOURCE_DIR "" CACHE PATH "Path to obs-studio source tree")
    set(OBS_LIB_DIR    "" CACHE PATH "Path to OBS import libraries")
    set(QT6_DIR        "" CACHE PATH "Path to Qt6 dev package")

    add_library(obs_lib SHARED IMPORTED)
    set_target_properties(obs_lib PROPERTIES
        IMPORTED_IMPLIB "${OBS_LIB_DIR}/obs.lib"
    )

    add_library(obs_frontend SHARED IMPORTED)
    set_target_properties(obs_frontend PROPERTIES
        IMPORTED_IMPLIB "${OBS_LIB_DIR}/obs-frontend-api.lib"
    )

    add_library(w32_pthreads SHARED IMPORTED)
    set_target_properties(w32_pthreads PROPERTIES
        IMPORTED_IMPLIB "${OBS_LIB_DIR}/w32-pthreads.lib"
    )

    foreach(_qt_mod Core Gui Widgets)
        add_library(Qt6::${_qt_mod} SHARED IMPORTED)
        set_target_properties(Qt6::${_qt_mod} PROPERTIES
            IMPORTED_IMPLIB "${QT6_DIR}/lib/Qt6${_qt_mod}.lib"
        )
    endforeach()

    target_include_directories(st-pluginmanager PRIVATE
        "${OBS_SOURCE_DIR}/libobs"
        "${OBS_SOURCE_DIR}/frontend/api"
        "${OBS_SOURCE_DIR}/deps/w32-pthreads"
        "${QT6_DIR}/include"
        "${QT6_DIR}/include/QtCore"
        "${QT6_DIR}/include/QtGui"
        "${QT6_DIR}/include/QtWidgets"
    )

    # Find curl from obs-deps or system
    set(CURL_DIR "" CACHE PATH "Path to curl dev package (include + lib)")
    if(CURL_DIR)
        target_include_directories(st-pluginmanager PRIVATE "${CURL_DIR}/include")
        set(_CURL_LIB "${CURL_DIR}/lib/libcurl_imp.lib")
    else()
        set(_CURL_LIB "libcurl_imp.lib")
    endif()

    target_link_libraries(st-pluginmanager
        obs_lib
        obs_frontend
        w32_pthreads
        shell32
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        "${_CURL_LIB}"
    )

    target_compile_definitions(st-pluginmanager PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(st-pluginmanager PRIVATE /Zc:__cplusplus /permissive- /utf-8)

else()
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(LIBOBS REQUIRED IMPORTED_TARGET libobs)

    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
    find_package(CURL REQUIRED)

    pkg_check_modules(OBS_FRONTEND IMPORTED_TARGET obs-frontend-api)
    if(OBS_FRONTEND_FOUND)
        set(_OBS_FRONTEND_TARGET PkgConfig::OBS_FRONTEND)
    else()
        find_library(OBS_FRONTEND_LIB obs-frontend-api)
        if(OBS_FRONTEND_LIB)
            add_library(obs_frontend_imported SHARED IMPORTED)
            set_target_properties(obs_frontend_imported PROPERTIES
                IMPORTED_LOCATION "${OBS_FRONTEND_LIB}"
            )
            set(_OBS_FRONTEND_TARGET obs_frontend_imported)
        else()
            set(_OBS_FRONTEND_TARGET obs-frontend-api)
        endif()
    endif()

    target_link_libraries(st-pluginmanager
        PkgConfig::LIBOBS
        ${_OBS_FRONTEND_TARGET}
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        CURL::libcurl
        pthread
    )

    include(GNUInstallDirs)
    install(TARGETS st-pluginmanager
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-plugins")
endif()

if(DEFINED PLUGIN_VERSION_OVERRIDE)
    set(_PLUGIN_VER "${PLUGIN_VERSION_OVERRIDE}")
else()
    set(_PLUGIN_VER "${PROJECT_VERSION}")
endif()

option(DEBUG_BUILD "Enable debug logging" OFF)

target_compile_definitions(st-pluginmanager PRIVATE
    PLUGIN_VERSION="${_PLUGIN_VER}"
    $<$<BOOL:${DEBUG_BUILD}>:DEBUG_BUILD>
)

set_target_properties(st-pluginmanager PROPERTIES PREFIX "")
