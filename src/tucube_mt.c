#include <dlfcn.h>
#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <tucube/tucube_Module.h>
#include <tucube/tucube_IModule.h>
#include <tucube/tucube_ICore.h>
#include <libgenc/genc_Tree.h>

struct tucube_mt_Interface {
    TUCUBE_IMT_FUNCTION_POINTERS;
};

struct tucube_mt_LocalModule {
    int workerCount;
    bool exit;
    pthread_cond_t exitCond;
    pthread_mutex_t exitMutex;
};

TUCUBE_IMODULE_FUNCTIONS;
TUCUBE_ICORE_FUNCTIONS;
TUCUBE_IMT_FUNCTIONS;


static void tucube_Core_pthreadCleanupHandler(void* args) {
    struct tucube_Core* core = args;

    if(core->tucube_IBase_tlDestroy(GENC_LIST_HEAD(core->moduleList)) == -1)
        warnx("%s: %u: tucube_Module_tlDestroy() failed", __FILE__, __LINE__);
    pthread_mutex_lock(core->exitMutex);
    core->exit = true;
    pthread_cond_signal(core->exitCond);
    pthread_mutex_unlock(core->exitMutex);
}

int tucube_IModule_init(struct tucube_Module* module, struct tucube_Config* config, void* args[]) {
    warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    module->localModule.pointer = malloc(1 * sizeof(struct tucube_mt_LocalModule));

    struct tucube_mt_LocalModule* localModule = module->localModule.pointer;
    TUCUBE_CONFIG_GET(config, module->id, "tucube_mt.workerCount", integer, &(localModule->workerCount), 1);

    struct tucube_Module_Ids childModuleIds;
    GENC_ARRAY_LIST_INIT(&childModuleIds);
    TUCUBE_CONFIG_GET_CHILD_MODULE_IDS(config, module->id, &childModuleIds);
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct tucube_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        childModule->interface = malloc(1 * sizeof(struct tucube_mt_Interface));
        struct tucube_mt_Interface* moduleInterface = childModule->interface;
        if((moduleInterface->tucube_IMt_service = dlsym(childModule->dlHandle, "tucube_IMt_service")) == NULL) {
            warnx("%s: %u: Unable to find tucube_IMt_service()", __FILE__, __LINE__);
            return -1;
        }
    }
    return 0;
}

int tucube_ICore_service(struct tucube_Module* module, void* args[]) {
    struct tucube_mt_LocalModule* localModule = module->localModule.pointer;
    struct tucube_Module* parentModule = GENC_TREE_NODE_GET_PARENT(module);
    while(true) {
        warnx("Module message: %s", localModule->message);
        warnx("ID of my parent module is %s", parentModule->id); 
        GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
            struct tucube_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
            struct tucube_mt_Interface* moduleInterface = childModule->interface;
            moduleInterface->tucube_IMt_service(childModule);
        }
        sleep(localModule->workerCount);
    }
    return 0;
}

int tucube_IMt_service(struct tucube_Module* module) {
    struct tucube_mt_LocalModule* localModule = module->localModule.pointer;
    struct tucube_Module* parentModule = GENC_TREE_NODE_GET_PARENT(module);
    warnx("Module message: %s", localModule->message);
    warnx("ID of my parent module is %s", parentModule->id); 
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct tucube_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct tucube_mt_Interface* moduleInterface = childModule->interface;
        moduleInterface->tucube_IMt_service(childModule);
    }
    
    return 0;
}

int tucube_IModule_destroy(struct tucube_Module* module) {
struct tucube_mt_LocalModule* localModule = module->localModule.pointer;
    warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct tucube_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        free(childModule->interface);
    }
    free(module->localModule.pointer);
    if(module->tlModuleKey != NULL)
        free(module->tlModuleKey);
//    dlclose(module->dl_handle);
    return 0;
}
