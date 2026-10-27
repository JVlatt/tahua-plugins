# tahua_add_plugin(<Name>
#     ID      <4-char id, e.g. TE01>
#     TYPE    effect|source|mixer
#     VERSION <major.minor>
#     DESCRIPTION "<one line>"
#     SOURCES <files...>)
#
# Builds plugins/<Name> as a Resolume-loadable FFGL plugin:
#   Windows: TahuaName.dll      macOS: TahuaName.bundle (universal, ad-hoc signed)
# The plugin id, display name ("Tahua <Name>"), version and description are passed to the
# code as TAHUA_PLUGIN_* macros (see common/include/TahuaPlugin.h), so they live in one place.
# They are also written to <build>/manifest/<Name>.json, from which CI builds the release
# manifest.json read by Tahua Launcher. Bump VERSION whenever a plugin changes: the
# launcher only offers an update when the version differs.

option(TAHUA_INSTALL_AFTER_BUILD "Copy each plugin into Resolume's Extra Effects folder after build" OFF)

# Resolume Arena loads third-party plugins from <Documents>/Resolume Arena/Extra Effects.
# On Windows, Documents may be redirected (e.g. to another drive): ask the registry.
if(WIN32)
    cmake_host_system_information(RESULT _documents QUERY WINDOWS_REGISTRY
        "HKCU/Software/Microsoft/Windows/CurrentVersion/Explorer/Shell Folders" VALUE "Personal")
    if(NOT _documents)
        set(_documents "$ENV{USERPROFILE}/Documents")
    endif()
else()
    set(_documents "$ENV{HOME}/Documents")
endif()
file(TO_CMAKE_PATH "${_documents}/Resolume Arena/Extra Effects" _extra_effects)
set(TAHUA_EXTRA_EFFECTS_DIR "${_extra_effects}" CACHE PATH "Resolume Extra Effects folder")

set(_tahua_root "${CMAKE_CURRENT_LIST_DIR}/..")

function(tahua_add_plugin name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "ID;TYPE;VERSION;DESCRIPTION" "SOURCES")

    # --- validation -------------------------------------------------------------------
    set(_where "plugins/${name}/CMakeLists.txt")
    if(NOT ARG_TYPE MATCHES "^(effect|source|mixer)$")
        message(FATAL_ERROR "${_where}: TYPE must be effect, source or mixer (got '${ARG_TYPE}').")
    endif()
    string(SUBSTRING "${ARG_TYPE}" 0 1 _letter)
    string(TOUPPER "${_letter}" _letter)
    if(NOT ARG_ID MATCHES "^T${_letter}[0-9][0-9]$")
        message(FATAL_ERROR "${_where}: ID must look like T${_letter}01 for a ${ARG_TYPE} (got '${ARG_ID}').")
    endif()
    if(NOT ARG_VERSION MATCHES "^([0-9]+)\\.([0-9]+)$")
        message(FATAL_ERROR "${_where}: VERSION must be <major>.<minor> (got '${ARG_VERSION}').")
    endif()
    set(_major ${CMAKE_MATCH_1})
    set(_minor ${CMAKE_MATCH_2})
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "${_where}: SOURCES is empty.")
    endif()
    if(NOT ARG_DESCRIPTION OR ARG_DESCRIPTION MATCHES "[\"\\\\]")
        message(FATAL_ERROR "${_where}: DESCRIPTION is required and must not contain quotes or backslashes.")
    endif()

    # FFGL stores the plugin name in a 16-char field: longer names are truncated by the SDK.
    set(_display_name "Tahua ${name}")
    string(LENGTH "${_display_name}" _len)
    if(_len GREATER 16)
        message(FATAL_ERROR "${_where}: display name '${_display_name}' exceeds FFGL's 16 characters.")
    endif()

    # Resolume identifies plugins by their 4-char id: two plugins must never share one.
    get_property(_ids GLOBAL PROPERTY TAHUA_PLUGIN_IDS)
    foreach(_entry IN LISTS _ids)
        if(_entry MATCHES "^${ARG_ID}=(.*)$")
            message(FATAL_ERROR "Plugin id ${ARG_ID} is used by both '${CMAKE_MATCH_1}' and '${name}'.")
        endif()
    endforeach()
    set_property(GLOBAL APPEND PROPERTY TAHUA_PLUGIN_IDS "${ARG_ID}=${name}")
    set_property(GLOBAL APPEND PROPERTY TAHUA_PLUGIN_TARGETS ${name})

    # --- target -----------------------------------------------------------------------
    add_library(${name} MODULE ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE ffgl-sdk)
    target_include_directories(${name} PRIVATE "${_tahua_root}/common/include")
    target_compile_definitions(${name} PRIVATE
        TAHUA_PLUGIN_ID="${ARG_ID}"
        TAHUA_PLUGIN_NAME="${_display_name}"
        TAHUA_PLUGIN_DESCRIPTION="${ARG_DESCRIPTION}"
        TAHUA_PLUGIN_VERSION_MAJOR=${_major}
        TAHUA_PLUGIN_VERSION_MINOR=${_minor})

    # Manifest fragment (one per plugin and OS), merged by CI into the release manifest.json.
    if(APPLE)
        set(_file "Tahua${name}.bundle")
        set(_os "macos-universal")
    else()
        set(_file "Tahua${name}.dll")
        set(_os "windows-x64")
    endif()
    file(WRITE "${CMAKE_BINARY_DIR}/manifest/${name}.json" "{
  \"id\": \"${ARG_ID}\",
  \"name\": \"${name}\",
  \"displayName\": \"${_display_name}\",
  \"type\": \"${ARG_TYPE}\",
  \"version\": \"${ARG_VERSION}\",
  \"description\": \"${ARG_DESCRIPTION}\",
  \"minResolume\": \"7.3.1\",
  \"assets\": { \"${_os}\": { \"file\": \"${_file}\", \"archive\": \"Tahua${name}-${ARG_VERSION}-${_os}.zip\" } }
}
")

    # Prefixed file name so it cannot clash with other vendors' files in Extra Effects.
    set_target_properties(${name} PROPERTIES
        OUTPUT_NAME "Tahua${name}"
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

    if(MSVC)
        target_compile_options(${name} PRIVATE /W4 /permissive- /utf-8)
        # plugMain is also __declspec(dllexport)'ed by FFGL.h; the .def keeps parity with the SDK projects.
        target_sources(${name} PRIVATE "${FFGL_ROOT}/build/windows/FFGLPlugins.def")
        set(_artifact "$<TARGET_FILE:${name}>")
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wno-unused-parameter)
        set_target_properties(${name} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION bundle
            MACOSX_BUNDLE_INFO_PLIST "${_tahua_root}/cmake/Info.plist.in"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.jvlatt.tahua.${name}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${ARG_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${ARG_VERSION}")
        set(_artifact "$<TARGET_BUNDLE_DIR:${name}>")
        # Ad-hoc signature: required for arm64 code, and the minimum Gatekeeper accepts once
        # the quarantine attribute is removed.
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND codesign --force --sign - "${_artifact}"
            COMMENT "Ad-hoc signing Tahua${name}.bundle"
            VERBATIM)
    endif()

    if(TAHUA_INSTALL_AFTER_BUILD)
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" "-DSRC=${_artifact}" "-DDEST=${TAHUA_EXTRA_EFFECTS_DIR}"
                    -P "${_tahua_root}/cmake/install_plugin.cmake"
            VERBATIM)
    endif()

    # Used to stage release packages: cmake --install <build> --config Release --prefix dist/<os>
    install(TARGETS ${name} LIBRARY DESTINATION .)
endfunction()
