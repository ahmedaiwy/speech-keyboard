cmake_minimum_required(VERSION 3.10)
project(VirtualKeyboard CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS Off)

# Find GTKMM
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED gtkmm-3.0)
include_directories(${GTKMM_INCLUDE_DIRS})

# Find X11 and XTest
find_package(X11 REQUIRED)
include_directories(${X11_INCLUDE_DIR})
find_library(XTEST_LIBRARY Xtst REQUIRED) # Corrected library name

# Find ALSA
find_package(ALSA REQUIRED)

# Find CURL
find_package(CURL REQUIRED)

# Find JSON-C (for nlohmann/json, which might use json-c internally or be a standalone header)
pkg_check_modules(JSONC REQUIRED json-c)

# Define paths to Vosk C++ API and Model
set(VOSK_API_BASE_DIR "${CMAKE_SOURCE_DIR}/vosk-linux-x86_64-0.3.45")
set(VOSK_INCLUDE_DIR "${VOSK_API_BASE_DIR}")
set(VOSK_LIB_DIR "${VOSK_API_BASE_DIR}") # This is the directory containing libvosk.so

# Explicitly tell find_library where to look for vosk
find_library(VOSK_LIBRARY vosk REQUIRED
    PATHS
    ${VOSK_LIB_DIR}
    ${VOSK_LIB_DIR}/lib # Often libraries are in a 'lib' subdirectory
    /usr/local/lib
    /usr/lib
)

# Add include directories for GTKMM, Vosk, AND the directory containing json.hpp
include_directories(${GTKMM_INCLUDE_DIRS})
include_directories(${VOSK_INCLUDE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/src) # For json.hpp

# Add link directories for Vosk library (though find_library with PATHS often makes this redundant for linking)
link_directories(${VOSK_LIB_DIR})
link_directories(${VOSK_LIB_DIR}/lib) # Also add lib subdirectory if it exists

# Define source files for your application
set(SOURCES
    src/main.cpp
    src/keyboard.cpp
    src/keyboardbutton.cpp
    src/speechtotextservice.cpp
)

# Add an executable target
add_executable(VirtualKeyboard ${SOURCES})

# Link libraries
target_link_libraries(VirtualKeyboard
    ${GTKMM_LIBRARIES}
    ${VOSK_LIBRARY} # Use the found VOSK_LIBRARY variable
    ${ALSA_LIBRARIES}
    pthread # For threading
    stdc++fs # For std::filesystem
    ${X11_LIBRARIES}
    ${XTEST_LIBRARY} # Use the found XTEST_LIBRARY variable
    ${CURL_LIBRARIES}
    ${JSONC_LIBRARIES} # Link JSON-C
)

# Set RPATH for Vosk if it's not in a standard system library path
# This helps the executable find the Vosk library at runtime if it's not globally installed
set_target_properties(VirtualKeyboard PROPERTIES
    BUILD_RPATH "$ORIGIN:${VOSK_LIB_DIR}:${VOSK_LIB_DIR}/lib"
    INSTALL_RPATH "$ORIGIN:${VOSK_LIB_DIR}:${VOSK_LIB_DIR}/lib"
)
