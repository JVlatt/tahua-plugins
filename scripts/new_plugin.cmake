# Creates plugins/<NAME>/ from templates/<TYPE>/ and assigns the next free plugin id.
#
#   cmake -DNAME=Tint -DTYPE=effect [-DDESCRIPTION="Hue shift and invert"] -P scripts/new_plugin.cmake
#
# NAME: PascalCase, at most 10 characters (Resolume shows "Tahua <NAME>", 16 chars max).
# TYPE: effect | source | mixer

set(_root "${CMAKE_CURRENT_LIST_DIR}/..")
cmake_path(NORMAL_PATH _root)

if(NOT DEFINED NAME OR NOT DEFINED TYPE)
    message(FATAL_ERROR "Usage: cmake -DNAME=<Name> -DTYPE=effect|source|mixer [-DDESCRIPTION=\"...\"] -P scripts/new_plugin.cmake")
endif()
if(NOT NAME MATCHES "^[A-Z][A-Za-z0-9]*$")
    message(FATAL_ERROR "NAME must be PascalCase letters/digits, e.g. Tint (got '${NAME}').")
endif()
string(LENGTH "${NAME}" _len)
if(_len GREATER 10)
    message(FATAL_ERROR "NAME '${NAME}' is too long: 'Tahua ${NAME}' must fit FFGL's 16 characters (max 10 for NAME).")
endif()
if(NOT TYPE MATCHES "^(effect|source|mixer)$")
    message(FATAL_ERROR "TYPE must be effect, source or mixer (got '${TYPE}').")
endif()
if(NOT DEFINED DESCRIPTION)
    set(DESCRIPTION "Tahua ${NAME} ${TYPE}")
endif()
if(DESCRIPTION MATCHES "[\"\\\\]")
    message(FATAL_ERROR "DESCRIPTION must not contain quotes or backslashes.")
endif()

set(_dest "${_root}/plugins/${NAME}")
if(EXISTS "${_dest}")
    message(FATAL_ERROR "${_dest} already exists.")
endif()

# Next free id for this type: T + E/S/M + two digits, scanning every plugins/*/CMakeLists.txt.
string(SUBSTRING "${TYPE}" 0 1 _letter)
string(TOUPPER "${_letter}" _letter)
set(_max 0)
file(GLOB _lists "${_root}/plugins/*/CMakeLists.txt")
foreach(_list IN LISTS _lists)
    file(STRINGS "${_list}" _lines REGEX "ID[ \t]+T${_letter}[0-9][0-9]")
    foreach(_line IN LISTS _lines)
        string(REGEX MATCH "T${_letter}([0-9][0-9])" _ "${_line}")
        math(EXPR _num "1${CMAKE_MATCH_1} - 100") # strip leading zero
        if(_num GREATER _max)
            set(_max ${_num})
        endif()
    endforeach()
endforeach()
math(EXPR _next "${_max} + 1")
if(_next GREATER 99)
    message(FATAL_ERROR "No free id left for type ${TYPE} (T${_letter}01..T${_letter}99).")
endif()
if(_next LESS 10)
    set(_next "0${_next}")
endif()
set(ID "T${_letter}${_next}")

string(TIMESTAMP DATE "%Y-%m-%d")
if(TYPE STREQUAL "effect")
    set(TYPE_LABEL "Effet")
elseif(TYPE STREQUAL "source")
    set(TYPE_LABEL "Source")
else()
    set(TYPE_LABEL "Mixer")
endif()

set(_tpl "${_root}/templates")
configure_file("${_tpl}/${TYPE}/CMakeLists.txt.in" "${_dest}/CMakeLists.txt" @ONLY NEWLINE_STYLE LF)
configure_file("${_tpl}/${TYPE}/Plugin.h.in" "${_dest}/${NAME}.h" @ONLY NEWLINE_STYLE LF)
configure_file("${_tpl}/${TYPE}/Plugin.cpp.in" "${_dest}/${NAME}.cpp" @ONLY NEWLINE_STYLE LF)
configure_file("${_tpl}/README.md.in" "${_dest}/README.md" @ONLY NEWLINE_STYLE LF)

message(STATUS "Created plugins/${NAME} (${TYPE}, id ${ID}).")
message(STATUS "Next: edit plugins/${NAME}/${NAME}.cpp and README.md, then build with the windows-dev or macos-dev workflow preset.")
