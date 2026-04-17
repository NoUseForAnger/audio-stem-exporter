cmake_minimum_required(VERSION 3.16)

# ─── obs-plugintemplate bootstrap ────────────────────────────────────────────
# Setup steps (one-time):
#   1. git clone https://github.com/obsproject/obs-plugintemplate.git tmp_tpl
#   2. xcopy /E tmp_tpl\cmake cmake\
#   3. copy  tmp_tpl\buildspec.json .
#   Then: cmake --preset windows-x64
# ─────────────────────────────────────────────────────────────────────────────
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(obs-mp3-writer VERSION 1.0.0)

option(ENABLE_FRONTEND_API "Hook into OBS recording start/stop events" ON)
option(ENABLE_QT           "Qt6 dock widget" ON)

include(compilerconfig)
include(defaults)
include(helpers)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/finders")

# ─── Dependencies ─────────────────────────────────────────────────────────────
find_package(libobs REQUIRED)
find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil swresample)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
endif()

# ─── Target ───────────────────────────────────────────────────────────────────
add_library(${CMAKE_PROJECT_NAME} MODULE)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
  src/plugin-main.cpp
  src/mp3-writer-filter.cpp)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src")

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
  CXX_STANDARD          17
  CXX_STANDARD_REQUIRED ON
  PREFIX                "")

if(WIN32)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE)
endif()

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
  OBS::libobs
  FFmpeg::avcodec
  FFmpeg::avformat
  FFmpeg::avutil
  FFmpeg::swresample)

if(WIN32)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Synchronization)
endif()

if(ENABLE_FRONTEND_API)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE MW_ENABLE_FRONTEND)
endif()

if(ENABLE_QT)
  find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

  target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    src/mw-dock.hpp
    src/mw-dock.cpp)

  set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON)

  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets)

  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE MW_ENABLE_QT)
endif()

# ─── Install / package ────────────────────────────────────────────────────────
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES FOLDER plugins)

install(TARGETS ${CMAKE_PROJECT_NAME}
  LIBRARY DESTINATION "obs-plugins/64bit"
  RUNTIME DESTINATION "obs-plugins/64bit")

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/"
  DESTINATION "data/obs-plugins/${CMAKE_PROJECT_NAME}")
