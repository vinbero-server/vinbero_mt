#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vinbero_common/vinbero_common_Config.h>
#include <vinbero_common/vinbero_common_Log.h>
#include <vinbero_common/vinbero_common_Module.h>
#include <vinbero/vinbero_IModule.h>
#include <vinbero/vinbero_IBasic.h>
#include <vinbero/vinbero_ITLocal.h>
#include <vinbero/vinbero_ITlService.h>
#include <libgenc/genc_Tree.h>

struct vinbero_mt_Interface {
    VINBERO_ITLOCAL_FUNCTION_POINTERS;
    VINBERO_ITLSERVICE_FUNCTION_POINTERS;
};

struct vinbero_mt_LocalModule {
    struct vinbero_common_Config* config;
    int workerCount;
    pthread_attr_t workerThreadAttr;
    pthread_t* workerThreads;
    bool exit; // whether one of threads exited
    pthread_cond_t* exitCond;
    pthread_mutex_t* exitMutex;
};

VINBERO_IMODULE_FUNCTIONS;
VINBERO_IBASIC_FUNCTIONS;

int vinbero_IModule_init(struct vinbero_common_Module* module, struct vinbero_common_Config* config, void* args[]) {
    VINBERO_COMMON_LOG_TRACE("in %s(...)", __FUNCTION__);
    int ret;
    module->name = "vinbero_mt";
    module->version = "0.0.1";
    module->localModule.pointer = malloc(1 * sizeof(struct vinbero_mt_LocalModule));
    struct vinbero_mt_LocalModule* localModule = module->localModule.pointer;
    localModule->config = config;
    vinbero_common_Config_getInt(config, module, "vinbero_mt.workerCount", &(localModule->workerCount), 1);
    localModule->workerThreads = malloc(localModule->workerCount * sizeof(pthread_t));
    localModule->exit = false;
    localModule->exitMutex = malloc(1 * sizeof(pthread_mutex_t));
    pthread_mutex_init(localModule->exitMutex, NULL);
    localModule->exitCond = malloc(1 * sizeof(pthread_cond_t));
    pthread_cond_init(localModule->exitCond, NULL);
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_mt_Interface childInterface;
        VINBERO_ITLOCAL_DLSYM(&childInterface, &childModule->dlHandle, &ret); 
        if(ret < 0) {
            VINBERO_COMMON_LOG_ERROR("module %s doesn't satisfy ITLOCAL interface", childModule->id);
            return ret;
        }
        VINBERO_ITLSERVICE_DLSYM(&childInterface, &childModule->dlHandle, &ret); 
        if(ret < 0) {
            VINBERO_COMMON_LOG_ERROR("module %s doesn't satisfy ITLSERVICE interface", childModule->id);
            return ret;
        }
    }
/*
    if(vinbero_common_Config_getChildModuleCount(config, module->id) <= 0) {
        VINBERO_COMMON_LOG_ERROR("%s: %u: vinbero_mt requires child modules", __FILE__, __LINE__);
        return VINBERO_COMMON_EINVAL;
    } // this will be checked by vinbero core
*/

    return 0;
}

int vinbero_IModule_rInit(struct vinbero_common_Module* module, struct vinbero_common_Config* config, void* args[]) {
    VINBERO_COMMON_LOG_TRACE("in %s(...)", __FUNCTION__);
    return 0;
}

static int vinbero_mt_initChildTlModules(struct vinbero_common_Module* module, struct vinbero_common_Config* config) {
    VINBERO_COMMON_LOG_TRACE("in %s(...)", __FUNCTION__);
    int ret;
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_mt_Interface childInterface;
        VINBERO_ITLOCAL_DLSYM(&childInterface, &childModule->dlHandle, &ret);
        if(ret < 0)
            return ret;
        if((ret = childInterface.vinbero_ITLocal_init(childModule, config, (void*[]){NULL}) < 0)) {
            VINBERO_COMMON_LOG_ERROR("vinbero_ITLocal_init() failed at module %s", childModule->id);
            return ret;
        }
        if(vinbero_mt_initChildTlModules(childModule, config) == -1)
            return ret;
    }
    return 0;
}

static int vinbero_mt_rInitChildTlModules(struct vinbero_common_Module* module, struct vinbero_common_Config* config) {
    VINBERO_COMMON_LOG_TRACE("in %s(...)", __FUNCTION__);
    int ret;
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_mt_Interface childInterface;
        VINBERO_ITLOCAL_DLSYM(&childInterface, &childModule->dlHandle, &ret);
        if(ret < 0)
            return ret;
        if((ret = vinbero_mt_rInitChildTlModules(childModule, config)) < 0)
            return ret;
        if((ret = childInterface.vinbero_ITLocal_rInit(childModule, config, (void*[]){NULL})) < 0) {
            VINBERO_COMMON_LOG_ERROR("vinbero_ITLocal_rInit() failed at module %s", childModule->id);
            return ret;
        }
    }
    return 0;
}

static int vinbero_mt_destroyChildTlModules(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE("in %s(...)", __FUNCTION__);
    int ret;
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_mt_Interface childInterface;
        VINBERO_ITLOCAL_DLSYM(&childInterface, &childModule->dlHandle, &ret);
        if(ret < 0)
            return ret;
        if((ret = childInterface.vinbero_ITLocal_destroy(childModule)) < 0) {
            VINBERO_COMMON_LOG_ERROR("%s: %u: vinbero_ITLocal_destroy() failed", __FILE__, __LINE__);
            return ret;
        }
        if((ret = vinbero_mt_destroyChildTlModules(childModule) < 0))
            return ret;
    }
    return 0;
}

static int vinbero_mt_rDestroyChildTlModules(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE("in %s(...)", __FUNCTION__);
    int ret;
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_mt_Interface childInterface;
        if((ret = vinbero_mt_rDestroyChildTlModules(childModule) < 0))
            return ret;
        VINBERO_ITLOCAL_DLSYM(&childInterface, &childModule->dlHandle, &ret);
        if(ret < 0)
            return ret;
        if((ret = childInterface.vinbero_ITLocal_rDestroy(childModule)) < 0) {
            VINBERO_COMMON_LOG_ERROR("%s: %u: vinbero_ITLocal_rDestroy() failed", __FILE__, __LINE__);
            return ret;
        }
    }
    return 0;
}

static void vinbero_mt_pthreadCleanupHandler(void* args) {
    VINBERO_COMMON_LOG_TRACE("in %s(...)", __FUNCTION__);
    struct vinbero_common_Module* module = args;
    struct vinbero_mt_LocalModule* localModule = module->localModule.pointer;
    vinbero_mt_destroyChildTlModules(module);
    vinbero_mt_rDestroyChildTlModules(module);
    pthread_mutex_lock(localModule->exitMutex);
    localModule->exit = true;
    pthread_cond_signal(localModule->exitCond);
    pthread_mutex_unlock(localModule->exitMutex);
}

static void* vinbero_mt_workerMain(void* args) {
    VINBERO_COMMON_LOG_TRACE("in %s(...)", __FUNCTION__);
    int ret;
    struct vinbero_common_Module* module = ((void**)args)[0];
    void** argsToPass = ((void**)args)[1];
    struct vinbero_mt_LocalModule* localModule = module->localModule.pointer;
    pthread_cleanup_push(vinbero_mt_pthreadCleanupHandler, module);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    sigset_t signalSet;
    sigemptyset(&signalSet);
    sigaddset(&signalSet, SIGINT);
    if(pthread_sigmask(SIG_BLOCK, &signalSet, (void*){NULL}) != 0) {
        VINBERO_COMMON_LOG_ERROR("%s: %u: pthread_sigmask() failed", __FILE__, __LINE__);
        return NULL;
    }
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_mt_Interface childInterface;
        VINBERO_ITLOCAL_DLSYM(&childInterface, &childModule->dlHandle, &ret);
        if(ret < 0)
            return NULL;
        if((ret = childInterface.vinbero_ITLocal_init(childModule, localModule->config, argsToPass)) < 0) {
            VINBERO_COMMON_LOG_ERROR("%s: %u: vinbero_ITLocal_init() failed", __FILE__, __LINE__);
            return NULL;
        }
    }
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_mt_Interface childInterface;
        VINBERO_ITLSERVICE_DLSYM(&childInterface, &childModule->dlHandle, &ret);
        if(ret < 0)
            return NULL;
        if((ret = childInterface.vinbero_ITlService_call(childModule, argsToPass)) < 0) {
            VINBERO_COMMON_LOG_ERROR("%s: %u: vinbero_ITlService_call() failed", __FILE__, __LINE__);
            return NULL;
        }
    }
    pthread_cleanup_pop(1);
    return NULL;
}

int vinbero_IBasic_service(struct vinbero_common_Module* module, void* args[]) {
    VINBERO_COMMON_LOG_TRACE("in %s(...)", __FUNCTION__);
    struct vinbero_mt_LocalModule* localModule = module->localModule.pointer;
    struct vinbero_common_Module* parentModule = GENC_TREE_NODE_GET_PARENT(module);
    pthread_attr_init(&localModule->workerThreadAttr);
    pthread_attr_setdetachstate(&localModule->workerThreadAttr, PTHREAD_CREATE_JOINABLE);

    for(size_t index = 0; index != localModule->workerCount; ++index) {
       if(pthread_create(localModule->workerThreads + index, &localModule->workerThreadAttr, vinbero_mt_workerMain, (void*[]){module, args}) != 0) {
            VINBERO_COMMON_LOG_ERROR("pthread_create() failed");
            return VINBERO_COMMON_EUNKNOWN;
       }
    }
    pthread_mutex_lock(localModule->exitMutex);
    while(localModule->exit != true) {
        if(pthread_cond_wait(localModule->exitCond, localModule->exitMutex) != 0)
            VINBERO_COMMON_LOG_ERROR("pthread_cond_wait() error %s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    }
    pthread_mutex_unlock(localModule->exitMutex);
    for(size_t index = 0; index != localModule->workerCount; ++index) {
        pthread_cancel(localModule->workerThreads[index]);
        pthread_join(localModule->workerThreads[index], NULL);
    }
    return 0;
}

int vinbero_IModule_destroy(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE("in %s(...)", __FUNCTION__);
    struct vinbero_mt_LocalModule* localModule = module->localModule.pointer;
    if(localModule->exit == false) {
        pthread_mutex_unlock(localModule->exitMutex);
        for(size_t index = 0; index != localModule->workerCount; ++index) {
            pthread_cancel(localModule->workerThreads[index]);
            pthread_mutex_lock(localModule->exitMutex);
            while(localModule->exit != true) {
                if(pthread_cond_wait(localModule->exitCond, localModule->exitMutex) != 0)
                    VINBERO_COMMON_LOG_ERROR("pthread_cond_wait() error");
            }
            pthread_mutex_unlock(localModule->exitMutex);
            pthread_join(localModule->workerThreads[index], NULL);
            localModule->exit = false;
        }
        localModule->exit = true;
    }
    return 0;
}

int vinbero_IModule_rDestroy(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE("in %s(...)", __FUNCTION__);
    struct vinbero_mt_LocalModule* localModule = module->localModule.pointer;
    free(module->localModule.pointer);
    if(module->tlModuleKey != NULL)
        free(module->tlModuleKey);
    pthread_cond_destroy(localModule->exitCond);
    free(localModule->exitCond);
    pthread_mutex_destroy(localModule->exitMutex);
    free(localModule->exitMutex);
    pthread_attr_destroy(&localModule->workerThreadAttr);

    free(localModule->workerThreads);
//    dlclose(module->dlHandle);
    return 0;
}
