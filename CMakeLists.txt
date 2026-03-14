cmake_minimum_required(VERSION 3.15)
project(midirec VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
include(FetchContent)

# 1. RtMidi
# Try system first
find_package(PkgConfig REQUIRED)
pkg_check_modules(RTMIDI rtmidi)

if(NOT RTMIDI_FOUND)
    message(STATUS "RtMidi not found via pkg-config, attempting FetchContent...")
    FetchContent_Declare(
        rtmidi
        GIT_REPOSITORY https://github.com/thestk/rtmidi.git
        GIT_TAG        5.0.0 # Older version for better compatibility
    )
    set(BUILD_SHARED_LIBS OFF) # Force static libs
    FetchContent_MakeAvailable(rtmidi)
    set(RTMIDI_LIBRARIES rtmidi)
    set(RTMIDI_INCLUDE_DIRS ${rtmidi_SOURCE_DIR})
endif()

# 2. MidiFile (Craig Sapp)
FetchContent_Declare(
    midifile
    GIT_REPOSITORY https://github.com/craigsapp/midifile.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(midifile)

# Executable
add_executable(midirec 
    src/main.cpp
)

# Include directories
target_include_directories(midirec PRIVATE 
    ${RTMIDI_INCLUDE_DIRS}
    ${midifile_SOURCE_DIR}/include
)

# Link libraries
target_link_libraries(midirec PRIVATE 
    ${RTMIDI_LIBRARIES}
    midifile
)

# OS Specific adjustments
if(APPLE)
    target_link_libraries(midirec PRIVATE "-framework CoreMIDI" "-framework CoreAudio" "-framework CoreFoundation")
elseif(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA REQUIRED alsa)
    target_link_libraries(midirec PRIVATE ${ALSA_LIBRARIES} pthread)
elseif(WIN32)
    target_link_libraries(midirec PRIVATE winmm)
endif()

# Output directory
set_target_properties(midirec PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
