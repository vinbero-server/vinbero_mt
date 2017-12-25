add_library(tucube_mt MODULE tucube_mt.c)
set_target_properties(tucube_mt PROPERTIES PREFIX "")
set_target_properties(tucube_mt PROPERTIES PUBLIC_HEADER "tucube_IMt.h")
install(TARGETS tucube_mt
        LIBRARY DESTINATION lib/tucube
        PUBLIC_HEADER DESTINATION include/tucube)
