# Copyright 2020 The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

cmake_minimum_required(VERSION 3.0)

set(version "1.0.0")

project(MumbleWebsocketPlugin
	VERSION "${version}"
	DESCRIPTION "Mumble Websocket Plugin"
	LANGUAGES "CXX"
)

# Cpp17 or later is required in order to use the wrapper
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# https://github.com/mumble-voip/mumble-plugin-cpp
set(MUMBLE_PLUGIN_CPP_DIR "../mumble-plugin-cpp" CACHE FILEPATH "Path to Mumble Plugin C++ Wrapper Directory")
# https://github.com/nlohmann/json
set(NLOHMANN_JSON_DIR "../json/include" CACHE FILEPATH "Path to nlohmann/json \"include\" Directory")
# https://github.com/eidheim/Simple-WebSocket-Server
set(SIMPLE_WEBSOCKET_SERVER_DIR "../Simple-Websocket-Server" CACHE FILEPATH "Path to Simple Websocket Server Directory")

# In order to use additional parts of the plugin interface, you have to enable the respective optiopn
# e.g. set(server-event-callbacks ON CACHE BOOL "" FORCE)
set(all-features ON CACHE BOOL "" FORCE)

# We need the wrapper as a depency
add_subdirectory("${MUMBLE_PLUGIN_CPP_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/wrapper")
add_subdirectory("${SIMPLE_WEBSOCKET_SERVER_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/Simple-WebSocket-Server-Build")

add_library(mumble_websocket_plugin SHARED plugin.cpp)

target_link_libraries(mumble_websocket_plugin PRIVATE mumble_plugin_cpp_wrapper)
target_link_libraries(mumble_websocket_plugin PRIVATE simple-websocket-server)
target_include_directories(mumble_websocket_plugin PRIVATE ${NLOHMANN_JSON_DIR})

if (WIN32)
	target_link_libraries(mumble_websocket_plugin PRIVATE crypt32)
endif()
