add_library(tucube_mt MODULE tucube_mt.c)
set_target_properties(tucube_mt PROPERTIES PREFIX "")
install(TARGETS tucube_mt DESTINATION lib/tucube)
