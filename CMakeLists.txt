cmake_minimum_required(VERSION 3.16)
project(InvisibleOverlay VERSION 1.0.0 LANGUAGES CXX)

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows-specific settings
if(WIN32)
    # Target Windows 10 2004+ for WDA_EXCLUDEFROMCAPTURE
    add_compile_definitions(
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
        NTDDI_VERSION=0x0A000005
        UNICODE
        _UNICODE
    )
    
    # Disable min/max macros from Windows.h
    add_compile_definitions(NOMINMAX)
    
    # Enable Win32 lean and mean
    add_compile_definitions(WIN32_LEAN_AND_MEAN)
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/overlay_window.cpp
    src/audio_capture.cpp
    src/screen_capture.cpp
)

set(HEADERS
    src/utils.h
    src/overlay_window.h
    src/audio_capture.h
    src/screen_capture.h
)

# Create executable (WIN32 for Windows GUI app without console)
add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})

# Link Windows libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    user32
    gdi32
    dwmapi
    ole32
    uuid
)

# MSVC-specific settings
if(MSVC)
    # Enable all warnings, treat warnings as errors in Release
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4
        $<$<CONFIG:Release>:/WX>
        /permissive-
    )
    
    # Use static runtime for easier distribution
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    
    # Enable Link Time Optimization in Release
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )
endif()

# Output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
)

# Installation
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# CPack for creating installers
set(CPACK_PACKAGE_NAME "InvisibleOverlay")
set(CPACK_PACKAGE_VENDOR "Research")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Invisible Overlay Research Application")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
include(CPack)
