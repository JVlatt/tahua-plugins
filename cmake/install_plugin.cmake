# Copies a built plugin (.dll file or .bundle folder) into Resolume's plugin folder.
# Usage: cmake -DSRC=<plugin> -DDEST=<folder> -P install_plugin.cmake

get_filename_component(_name "${SRC}" NAME)
file(MAKE_DIRECTORY "${DEST}")

if(IS_DIRECTORY "${SRC}")
    file(REMOVE_RECURSE "${DEST}/${_name}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SRC}" "${DEST}/${_name}"
                    RESULT_VARIABLE _result)
else()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SRC}" "${DEST}"
                    RESULT_VARIABLE _result)
endif()

if(NOT _result EQUAL 0)
    message(FATAL_ERROR
        "Could not copy ${_name} to ${DEST}.\n"
        "Resolume locks loaded plugins: quit Arena, then build again.")
endif()
message(STATUS "Installed ${_name} -> ${DEST}")
