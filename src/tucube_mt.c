#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <tucube/tucube_Module.h>
#include <tucube/tucube_IModule.h>
#include <tucube/tucube_ICore.h>
#include <libgenc/genc_cast.h>
#include <libgenc/genc_Tree.h>

struct tucube_mt_Interface {
    TUCUBE_ICORE_FUNCTION_POINTERS;
};

struct tucube_mt_LocalModule {
    int workerCount;
    int requiresNext;
};

TUCUBE_IMODULE_FUNCTIONS;
TUCUBE_ICORE_FUNCTIONS;

int tucube_IModule_init(struct tucube_Module* module, struct tucube_Config* config, void* args[]) {
    warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    module->localModule.pointer = malloc(1 * sizeof(struct tucube_mt_LocalModule));

    struct tucube_mt_LocalModule* localModule = module->localModule.pointer;
    TUCUBE_CONFIG_GET(config, module->id, "tucube_mt.workerCount", integer, &(localModule->workerCount), 2);
    TUCUBE_CONFIG_GET(config, module->id, "tucube_mt.requiresNext", integer, &(localModule->requiresNext), 0);
/*
    struct tucube_Module_Ids childModuleIds;
    GENC_ARRAY_LIST_INIT(&childModuleIds);
    TUCUBE_MODULE_GET_CHILD_MODULE_IDS(config, module->id, &childModuleIds);
    if(GENC_ARRAY_LIST_SIZE(&childModuleIds) == 0)
        errx(EXIT_FAILURE, "%s: %u: tucube_mt requires next modules", __FILE__, __LINE__);
*/
    return 0;
}

int tucube_ICore_service(struct tucube_Module* module, void* args[]) {
    struct tucube_mt_LocalModule* localModule = module->localModule.pointer;
    return 0;
}

int tucube_IModule_destroy(struct tucube_Module* module) {
struct tucube_mt_TlModule* localModule = module->localModule.pointer;
    warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
//    dlclose(module->dl_handle);
    free(module->tlModuleKey);
    free(module->localModule.pointer);
    free(module);
    return 0;
}
