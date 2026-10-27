cmake_minimum_required(VERSION 3.25)

# Must be set before project(): universal binary, same deployment target as the FFGL SDK xcconfig.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")

# Static MSVC runtime (/MT): plugins carry no VCRUNTIME/MSVCP dependency, and it matches
# the prebuilt glew32s.lib shipped with the SDK (built against LIBCMT).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(TahuaPlugins VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ffgl)
include(TahuaPlugin)

# Every plugins/<Name>/ folder that has a CMakeLists.txt is built: adding a plugin
# never requires touching this file. CONFIGURE_DEPENDS re-runs the glob on build.
file(GLOB _plugin_dirs CONFIGURE_DEPENDS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*")
foreach(_dir IN LISTS _plugin_dirs)
    if(EXISTS "${_dir}/CMakeLists.txt")
        add_subdirectory("${_dir}")
    endif()
endforeach()

get_property(_plugins GLOBAL PROPERTY TAHUA_PLUGIN_TARGETS)
list(LENGTH _plugins _count)
message(STATUS "Tahua plugins (${_count}): ${_plugins}")
if(TAHUA_INSTALL_AFTER_BUILD)
    message(STATUS "Plugins are copied after build to: ${TAHUA_EXTRA_EFFECTS_DIR}")
endif()
