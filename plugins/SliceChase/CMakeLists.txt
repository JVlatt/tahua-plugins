tahua_add_plugin(SliceChase
    ID      TE02
    TYPE    effect
    VERSION 1.0
    DESCRIPTION "Lights Advanced Output slices from the layer as a mask"
    SOURCES SliceChase.h SliceChase.cpp SliceLoader.h SliceLoader.cpp)

# XML parsing, mask clipping and polygon triangulation (external/ submodules).
set(_external "${CMAKE_CURRENT_SOURCE_DIR}/../../external")
set(_clipper "${_external}/clipper2/CPP/Clipper2Lib")
target_sources(SliceChase PRIVATE
    "${_external}/pugixml/src/pugixml.cpp"
    "${_clipper}/src/clipper.engine.cpp")
target_include_directories(SliceChase PRIVATE
    "${_external}/pugixml/src"
    "${_external}/earcut/include"
    "${_clipper}/include")
if(MSVC)
    # C4702 (unreachable code) is raised by the optimizer inside Clipper2's headers.
    set_source_files_properties(SliceLoader.cpp PROPERTIES COMPILE_OPTIONS /wd4702)
endif()
