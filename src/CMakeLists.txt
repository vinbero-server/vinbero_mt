add_library(vinbero_mt MODULE vinbero_mt.c)
set_target_properties(vinbero_mt PROPERTIES PREFIX "")
set_target_properties(vinbero_mt PROPERTIES
                                 VERSION ${version}
                                 SOVERSION ${soversion}
                                 PUBLIC_HEADER "")

find_library(fastdl libfastdl.so REQUIRED)
find_library(gaio libgaio.so.0.3 REQUIRED)
find_library(vinbero_com libvinbero_com.so REQUIRED)
target_link_libraries(vinbero_mt ${vinbero_com} ${fastdl} ${gaio})

install(TARGETS vinbero_mt
        LIBRARY DESTINATION lib/vinbero
        PUBLIC_HEADER DESTINATION include/vinbero)
