add_library(vinbero_mt MODULE vinbero_mt.c)
set_target_properties(vinbero_mt PROPERTIES PREFIX "")
set_target_properties(vinbero_mt PROPERTIES
                                 VERSION 0.1.0
                                 SOVERSION 0.1
                                 PUBLIC_HEADER "")
install(TARGETS vinbero_mt
        LIBRARY DESTINATION lib/vinbero
        PUBLIC_HEADER DESTINATION include/vinbero)
