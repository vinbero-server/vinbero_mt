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
#include "tucube_IMt.h"
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


static void tucube_mt_pthreadCleanupHandler(void* args) {
warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    struct tucube_Module* module = args;
    struct tucube_mt_LocalModule* localModule = module->localModule.pointer;
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct tucube_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        if(childModule->tucube_IModule_tlDestroy(childModule) == -1)
            warnx("%s: %u: tucube_Module_tlDestroy() failed", __FILE__, __LINE__);
    }
    pthread_mutex_lock(localModule->exitMutex);
    localModule->exit = true;
    pthread_cond_signal(localModule->exitCond);
    pthread_mutex_unlock(localModule->exitMutex);
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

/*
static void* tucube_mt_startWorker(void* args) {
    struct tucube_Core* core = ((void**)args)[0];
    struct tucube_Module_ConfigList* moduleConfigList = ((void**)args)[1];
    pthread_cleanup_push(tucube_Core_pthreadCleanupHandler, core);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    if(core->tucube_IBase_tlInit(GENC_LIST_HEAD(core->moduleList), GENC_LIST_HEAD(moduleConfigList), (void*[]){NULL}) == -1)
        errx(EXIT_FAILURE, "%s: %u: tucube_Module_tlInit() failed", __FILE__, __LINE__);

    sigset_t signalSet;
    sigemptyset(&signalSet);
    sigaddset(&signalSet, SIGINT);
    if(pthread_sigmask(SIG_BLOCK, &signalSet, NULL) != 0)
        errx(EXIT_FAILURE, "%s: %u: pthread_sigmask() failed", __FILE__, __LINE__);

    if(core->tucube_ITlService_call(GENC_LIST_HEAD(core->moduleList), (void*[]){&core->serverSocket, NULL}) == -1)
        errx(EXIT_FAILURE, "%s: %u: tucube_ITlService_call() failed", __FILE__, __LINE__);

    pthread_cleanup_pop(1);

    return NULL;
}

int tucube_mt_start(struct tucube_Core* core, struct tucube_Core_Config* coreConfig, struct tucube_Module_ConfigList* moduleConfigList) {
    tucube_Core_init(core, coreConfig, moduleConfigList);
    tucube_Core_registerSignalHandlers();
    pthread_t* workerThreads;
    pthread_attr_t coreThreadAttr;
    jmp_buf* jumpBuffer = malloc(1 * sizeof(jmp_buf));
    if(setjmp(*jumpBuffer) == 0) {
        pthread_key_create(&tucube_Core_tlKey, NULL);
        pthread_setspecific(tucube_Core_tlKey, jumpBuffer);

        pthread_attr_init(&coreThreadAttr);
        pthread_attr_setdetachstate(&coreThreadAttr, PTHREAD_CREATE_JOINABLE);

        workerThreads = malloc(core->workerCount * sizeof(pthread_t));

        atexit(tucube_Core_exitHandler);

        void* workerArgs[2] = {core, moduleConfigList};
        for(size_t index = 0; index != core->workerCount; ++index) {
           if(pthread_create(workerThreads + index, &coreThreadAttr, tucube_Core_startWorker, workerArgs) != 0)
                err(EXIT_FAILURE, "%s: %u", __FILE__, __LINE__);
        }

        pthread_mutex_lock(core->exitMutex);
        while(core->exit != true) {
            pthread_cond_wait(core->exitCond,
                 core->exitMutex);
        }
        pthread_mutex_unlock(core->exitMutex);

        for(size_t index = 0; index != core->workerCount; ++index) {
            pthread_cancel(workerThreads[index]);
            pthread_join(workerThreads[index], NULL);
        }
        core->exit = true;
    }
    free(jumpBuffer);
    pthread_key_delete(tucube_Core_tlKey);
    pthread_mutex_unlock(core->exitMutex);

    if(core->exit == false) {
        for(size_t index = 0; index != core->workerCount; ++index) {
            pthread_cancel(workerThreads[index]);
            pthread_mutex_lock(core->exitMutex);
            while(core->exit != true) {
                pthread_cond_wait(core->exitCond,
                     core->exitMutex);
            }
            pthread_mutex_unlock(core->exitMutex);
            pthread_join(workerThreads[index], NULL);
            core->exit = false;
        }
    }

    pthread_cond_destroy(core->exitCond);
    free(core->exitCond);
    pthread_mutex_destroy(core->exitMutex);
    free(core->exitMutex);

    close(core->serverSocket);

    pthread_attr_destroy(&coreThreadAttr);
    free(workerThreads);

    if(core->tucube_IBase_destroy(GENC_LIST_HEAD(core->moduleList)) == -1)
        warn("%s: %u", __FILE__, __LINE__);
    free(core->moduleList);

//    dlclose(core->dlHandle);
    return 0;
}
*/

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
