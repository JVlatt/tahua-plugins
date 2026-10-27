# Zips each built plugin on its own and writes plugins-<os>.json (the per-OS part of the
# release manifest.json, merged by CI) with the archive's sha256 and size.
#
#   cmake -DBUILD_DIR=build/windows-ci -DOUT_DIR=dist/release -P scripts/package_plugins.cmake
#
# Each archive holds the plugin at its root: TahuaName.dll, or the TahuaName.bundle folder.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED BUILD_DIR OR NOT DEFINED OUT_DIR)
    message(FATAL_ERROR "Usage: cmake -DBUILD_DIR=<build dir> -DOUT_DIR=<dir> [-DCONFIG=Release] -P scripts/package_plugins.cmake")
endif()
if(NOT DEFINED CONFIG)
    set(CONFIG Release)
endif()
get_filename_component(BUILD_DIR "${BUILD_DIR}" ABSOLUTE)
get_filename_component(OUT_DIR "${OUT_DIR}" ABSOLUTE)
set(_bin "${BUILD_DIR}/bin/${CONFIG}")

file(GLOB _fragments "${BUILD_DIR}/manifest/*.json")
if(NOT _fragments)
    message(FATAL_ERROR "No manifest fragment in ${BUILD_DIR}/manifest: configure the project first.")
endif()
file(MAKE_DIRECTORY "${OUT_DIR}")

set(_entries "")
foreach(_fragment IN LISTS _fragments)
    file(READ "${_fragment}" _json)
    string(JSON _os MEMBER "${_json}" assets 0)
    string(JSON _file GET "${_json}" assets ${_os} file)
    string(JSON _archive GET "${_json}" assets ${_os} archive)

    if(NOT EXISTS "${_bin}/${_file}")
        message(FATAL_ERROR "${_bin}/${_file} not found: build the ${CONFIG} configuration first.")
    endif()
    file(REMOVE "${OUT_DIR}/${_archive}")
    if(CMAKE_HOST_APPLE)
        # ditto keeps bundle structure, permissions and code signature intact.
        execute_process(COMMAND ditto -c -k --keepParent "${_bin}/${_file}" "${OUT_DIR}/${_archive}"
                        RESULT_VARIABLE _result)
    else()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${OUT_DIR}/${_archive}" --format=zip "${_file}"
                        WORKING_DIRECTORY "${_bin}" RESULT_VARIABLE _result)
    endif()
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Could not create ${_archive}.")
    endif()

    file(SHA256 "${OUT_DIR}/${_archive}" _sha256)
    file(SIZE "${OUT_DIR}/${_archive}" _size)
    string(JSON _json SET "${_json}" assets ${_os} sha256 "\"${_sha256}\"")
    string(JSON _json SET "${_json}" assets ${_os} size "${_size}")
    list(APPEND _entries "${_json}")
    message(STATUS "${_archive}  ${_size} bytes  sha256 ${_sha256}")
endforeach()

list(JOIN _entries ",\n" _joined)
file(WRITE "${OUT_DIR}/plugins-${_os}.json" "[\n${_joined}\n]\n")
message(STATUS "Wrote ${OUT_DIR}/plugins-${_os}.json")
