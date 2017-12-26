#include <dlfcn.h>
#include <err.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <tucube/tucube_Module.h>
#include <tucube/tucube_IModule.h>
#include <tucube/tucube_IBasic.h>
#include "tucube_IMt.h"
#include <libgenc/genc_Tree.h>

struct tucube_mt_Interface {
    TUCUBE_IMT_FUNCTION_POINTERS;
};

struct tucube_mt_LocalModule {
    struct tucube_Config* config;
    int workerCount;
    pthread_attr_t workerThreadAttr;
    pthread_t* workerThreads;
    bool exit;
    pthread_cond_t* exitCond;
    pthread_mutex_t* exitMutex;
};

TUCUBE_IMODULE_FUNCTIONS;
TUCUBE_IBASIC_FUNCTIONS;

int tucube_IModule_init(struct tucube_Module* module, struct tucube_Config* config, void* args[]) {
warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    module->localModule.pointer = malloc(1 * sizeof(struct tucube_mt_LocalModule));

    struct tucube_mt_LocalModule* localModule = module->localModule.pointer;
    localModule->config = config;
    TUCUBE_CONFIG_GET(config, module->id, "tucube_mt.workerCount", integer, &(localModule->workerCount), 1);
    localModule->workerThreads = malloc(localModule->workerCount * sizeof(pthread_t));
    localModule->exit = false;
    localModule->exitMutex = malloc(1 * sizeof(pthread_mutex_t));
    pthread_mutex_init(localModule->exitMutex, NULL);
    localModule->exitCond = malloc(1 * sizeof(pthread_cond_t));
    pthread_cond_init(localModule->exitCond, NULL);

    struct tucube_Module_Ids childModuleIds;
    GENC_ARRAY_LIST_INIT(&childModuleIds);
    TUCUBE_CONFIG_GET_CHILD_MODULE_IDS(config, module->id, &childModuleIds);
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct tucube_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        childModule->interface = malloc(1 * sizeof(struct tucube_mt_Interface));
        struct tucube_mt_Interface* moduleInterface = childModule->interface;
        if((moduleInterface->tucube_IMt_init = dlsym(childModule->dlHandle, "tucube_IMt_init")) == NULL) {
            warnx("%s: %u: Unable to find tucube_IMt_init()", __FILE__, __LINE__);
            return -1;
        }
        if((moduleInterface->tucube_IMt_service = dlsym(childModule->dlHandle, "tucube_IMt_service")) == NULL) {
            warnx("%s: %u: Unable to find tucube_IMt_service()", __FILE__, __LINE__);
            return -1;
        }
        if((moduleInterface->tucube_IMt_destroy = dlsym(childModule->dlHandle, "tucube_IMt_destroy")) == NULL) {
            warnx("%s: %u: Unable to find tucube_IMt_destroy()", __FILE__, __LINE__);
            return -1;
        }
    }
    return 0;
}

static void tucube_mt_pthreadCleanupHandler(void* args) {
warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    struct tucube_Module* module = args;
    struct tucube_mt_LocalModule* localModule = module->localModule.pointer;
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct tucube_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct tucube_mt_Interface* moduleInterface = childModule->interface;
        if(moduleInterface->tucube_IMt_destroy(childModule) == -1)
            warnx("%s: %u: tucube_IMt_destroy() failed", __FILE__, __LINE__);
    }
    pthread_mutex_lock(localModule->exitMutex);
    localModule->exit = true;
    pthread_cond_signal(localModule->exitCond);
    pthread_mutex_unlock(localModule->exitMutex);
}

static void* tucube_mt_workerMain(void* args) {
warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    struct tucube_Module* module = ((void**)args)[0];
    struct tucube_mt_LocalModule* localModule = module->localModule.pointer;
    pthread_cleanup_push(tucube_mt_pthreadCleanupHandler, module);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    sigset_t signalSet;
    sigemptyset(&signalSet);
    sigaddset(&signalSet, SIGINT);
    if(pthread_sigmask(SIG_BLOCK, &signalSet, (void*){NULL}) != 0) {
        warnx("%s: %u: pthread_sigmask() failed", __FILE__, __LINE__);
        return NULL;
    }
/*    if(GENC_TREE_NODE_CHILD_COUNT(module) == 0) {
        warnx("%s: %u: tucube_mt requires child modules", __FILE__, __LINE__);
        return NULL;
    }
*/
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct tucube_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct tucube_mt_Interface* moduleInterface = childModule->interface;
        if(moduleInterface->tucube_IMt_init(childModule, localModule->config, (void*[]){NULL}) == -1) {
            warnx("%s: %u: tucube_IMt_init() failed", __FILE__, __LINE__);
            return NULL;
        }
    }
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct tucube_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct tucube_mt_Interface* moduleInterface = childModule->interface;
        if(moduleInterface->tucube_IMt_service(childModule, (void*){NULL}) == -1) {
            warnx("%s: %u: tucube_IMt_service() failed", __FILE__, __LINE__);
            return NULL;
        }
    }
    pthread_cleanup_pop(1);
    return NULL;
}

int tucube_IBasic_service(struct tucube_Module* module, void* args[]) {
warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    struct tucube_mt_LocalModule* localModule = module->localModule.pointer;
    struct tucube_Module* parentModule = GENC_TREE_NODE_GET_PARENT(module);
    pthread_attr_init(&localModule->workerThreadAttr);
    pthread_attr_setdetachstate(&localModule->workerThreadAttr, PTHREAD_CREATE_JOINABLE);


    for(size_t index = 0; index != localModule->workerCount; ++index) {
       if(pthread_create(localModule->workerThreads + index, &localModule->workerThreadAttr, tucube_mt_workerMain, (void*[]){module}) != 0)
            err(EXIT_FAILURE, "%s: %u", __FILE__, __LINE__);
    }

    pthread_mutex_lock(localModule->exitMutex);
    while(localModule->exit != true) {
        pthread_cond_wait(localModule->exitCond,
             localModule->exitMutex);
    }
    pthread_mutex_unlock(localModule->exitMutex);

    for(size_t index = 0; index != localModule->workerCount; ++index) {
        pthread_cancel(localModule->workerThreads[index]);
        pthread_join(localModule->workerThreads[index], NULL);
    }
    return 0;
}

int tucube_IModule_destroy(struct tucube_Module* module) {
warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    struct tucube_mt_LocalModule* localModule = module->localModule.pointer;
       GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct tucube_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        free(childModule->interface);
    }
    free(module->localModule.pointer);
    if(module->tlModuleKey != NULL)
        free(module->tlModuleKey);

    localModule->exit = true;
    pthread_mutex_unlock(localModule->exitMutex);

    if(localModule->exit == false) {
        for(size_t index = 0; index != localModule->workerCount; ++index) {
            pthread_cancel(localModule->workerThreads[index]);
            pthread_mutex_lock(localModule->exitMutex);
            while(localModule->exit != true) {
                pthread_cond_wait(localModule->exitCond,
                     localModule->exitMutex);
            }
            pthread_mutex_unlock(localModule->exitMutex);
            pthread_join(localModule->workerThreads[index], NULL);
            localModule->exit = false;
        }
    }

    pthread_cond_destroy(localModule->exitCond);
    free(localModule->exitCond);
    pthread_mutex_destroy(localModule->exitMutex);
    free(localModule->exitMutex);


    pthread_attr_destroy(&localModule->workerThreadAttr);
    free(localModule->workerThreads);

    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct tucube_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct tucube_mt_Interface* moduleInterface = childModule->interface;
        if(moduleInterface->tucube_IMt_destroy(childModule) == -1) {
            warnx("%s: %u: tucube_IMt_destroy() failed", __FILE__, __LINE__);
            return -1;
        }
    }

//    dlclose(module->dlHandle);
    return 0;
}
