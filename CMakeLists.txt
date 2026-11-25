cmake_minimum_required(VERSION 3.20)
project(OBSLink VERSION 1.0.1 LANGUAGES C CXX)

# OBSLink - dérivé de obs-multi-rtmp (SoraYuki, GPL v2). Voir LICENSE-GPL2.txt.
# Chemins fournis par build.ps1 : OBS_INC (libobs), OBS_FRONTEND_INC, OBS_LIB_DIR (obs.lib, obs-frontend-api.lib), Qt via CMAKE_PREFIX_PATH.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

add_library(OBSLink MODULE
  src/plugin-support.c
  src/obs-multi-rtmp.cpp
  src/edit-widget.cpp
  src/edit-targets-dialog.cpp
  src/push-widget.cpp
  src/output-config.cpp
  src/obs-properties-widget.cpp
  src/protocols.cpp
  src/helpers.cpp
  src/track-mixer.cpp
  src/chat-docks.cpp
  resources/OBSLink.rc
)

target_include_directories(OBSLink PRIVATE
  src
  dep/nlohmann-json
  ${OBS_INC}
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${OBS_FRONTEND_INC}
)

target_compile_definitions(OBSLink PRIVATE
  OBSLINK_VERSION="${PROJECT_VERSION}"
  _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
)
target_compile_options(OBSLink PRIVATE /utf-8 /Zc:__cplusplus /permissive- /wd4244 /wd4267 /wd4996)

target_link_libraries(OBSLink PRIVATE
  ${OBS_LIB_DIR}/obs.lib
  ${OBS_LIB_DIR}/obs-frontend-api.lib
  Qt6::Core Qt6::Widgets
)

set_target_properties(OBSLink PROPERTIES PREFIX "" OUTPUT_NAME "OBSLink")
