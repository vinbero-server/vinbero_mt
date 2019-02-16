add_library(vinbero_mt MODULE)
target_sources(vinbero_mt PRIVATE vinbero_mt.c)
target_compile_options(vinbero_mt PRIVATE -Wall)
set_target_properties(
    vinbero_mt
    PROPERTIES
    C_STANDARD 11
    PREFIX ""
    VERSION ${version}
    SOVERSION ${soversion}
    PUBLIC_HEADER ""
)

find_library(fastdl libfastdl.so REQUIRED)
find_library(gaio libgaio.so REQUIRED)
find_package(vinbero_com REQUIRED)
target_link_libraries(vinbero_mt ${vinbero_com} ${fastdl} ${gaio})

install(
    TARGETS vinbero_mt
    LIBRARY DESTINATION lib/vinbero
    PUBLIC_HEADER DESTINATION include/vinbero
)
