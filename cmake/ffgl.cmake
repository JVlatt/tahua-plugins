# Builds the FFGL SDK (external/ffgl) as a static library: target ffgl-sdk.
#
# The SDK's own CMakeLists.txt is not used: it builds its example plugins as static
# libraries (not loadable by Resolume) and requires GLEW through vcpkg on Windows,
# whereas the SDK already ships a prebuilt static GLEW in deps/.

set(FFGL_ROOT "${CMAKE_SOURCE_DIR}/external/ffgl")

if(NOT EXISTS "${FFGL_ROOT}/source/lib/FFGLSDK.cpp")
    message(FATAL_ERROR
        "FFGL SDK not found in ${FFGL_ROOT}.\n"
        "Fetch the submodule with: git submodule update --init --recursive")
endif()

# FFGLSDK.cpp is a unity build that #includes every SDK source file.
add_library(ffgl-sdk STATIC "${FFGL_ROOT}/source/lib/FFGLSDK.cpp")
target_include_directories(ffgl-sdk SYSTEM PUBLIC "${FFGL_ROOT}/source/lib")
target_compile_features(ffgl-sdk PUBLIC cxx_std_17)

if(WIN32)
    set(_glew "${FFGL_ROOT}/deps/glew-2.1.0")
    target_compile_definitions(ffgl-sdk PUBLIC GLEW_STATIC _CRT_SECURE_NO_WARNINGS)
    target_include_directories(ffgl-sdk SYSTEM PUBLIC "${_glew}/include")
    target_link_libraries(ffgl-sdk PUBLIC "${_glew}/lib/Release/x64/glew32s.lib" opengl32)
    # glew32s.lib asks for the release CRT (LIBCMT); Debug links LIBCMTD instead.
    # LNK4099: glew32s.lib ships without its PDB.
    target_link_options(ffgl-sdk INTERFACE "$<$<CONFIG:Debug>:/NODEFAULTLIB:LIBCMT;/ignore:4099>")
elseif(APPLE)
    target_compile_definitions(ffgl-sdk PUBLIC GL_SILENCE_DEPRECATION)
    # Same frameworks as the SDK's Xcode project (Carbon is used by ffglex/FFGLUtilities.cpp).
    target_link_libraries(ffgl-sdk PUBLIC
        "-framework OpenGL"
        "-framework Carbon"
        "-framework AppKit")
else()
    message(FATAL_ERROR "Resolume only runs on Windows and macOS.")
endif()
