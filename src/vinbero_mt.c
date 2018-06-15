#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vinbero_common/vinbero_common_Call.h>
#include <vinbero_common/vinbero_common_Config.h>
#include <vinbero_common/vinbero_common_Log.h>
#include <vinbero_common/vinbero_common_Module.h>
#include <vinbero/vinbero_interface_MODULE.h>
#include <vinbero/vinbero_interface_BASIC.h>
#include <vinbero/vinbero_interface_TLOCAL.h>
#include <vinbero/vinbero_interface_TLSERVICE.h>
#include <libgenc/genc_Tree.h>

struct vinbero_mt_interface {
    VINBERO_INTERFACE_TLOCAL_FUNCTION_POINTERS;
    VINBERO_INTERFACE_TLSERVICE_FUNCTION_POINTERS;
};

struct vinbero_mt_LocalModule {
    int workerCount;
    pthread_attr_t workerThreadAttr;
    pthread_t* workerThreads;
    bool exit; // whether one of threads exited
    pthread_cond_t* exitCond;
    pthread_mutex_t* exitMutex;
};

VINBERO_INTERFACE_MODULE_FUNCTIONS;
VINBERO_INTERFACE_BASIC_FUNCTIONS;

int vinbero_interface_MODULE_init(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
    int ret;
    module->name = "vinbero_mt";
    module->version = "0.0.1";
    module->localModule.pointer = malloc(1 * sizeof(struct vinbero_mt_LocalModule));
    struct vinbero_mt_LocalModule* localModule = module->localModule.pointer;
    vinbero_common_Config_getInt(module->config, module, "vinbero_mt.workerCount", &(localModule->workerCount), 1);
    localModule->workerThreads = malloc(localModule->workerCount * sizeof(pthread_t));
    localModule->exit = false;
    localModule->exitMutex = malloc(1 * sizeof(pthread_mutex_t));
    pthread_mutex_init(localModule->exitMutex, NULL);
    localModule->exitCond = malloc(1 * sizeof(pthread_cond_t));
    pthread_cond_init(localModule->exitCond, NULL);
/*
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_mt_interface childinterface;
        VINBERO_INTERFACE_TLOCAL_DLSYM(&childinterface, &childModule->dlHandle, &ret); 
        if(ret < 0) {
            VINBERO_COMMON_LOG_ERROR("module %s doesn't satisfy ITLOCAL interface", childModule->id);
            return ret;
        }
        VINBERO_INTERFACE_TLSERVICE_DLSYM(&childinterface, &childModule->dlHandle, &ret); 
        if(ret < 0) {
            VINBERO_COMMON_LOG_ERROR("module %s doesn't satisfy ITLSERVICE interface", childModule->id);
            return ret;
        }
    }
*/
/*
    if(vinbero_common_Config_getChildModuleCount(config, module->id) <= 0) {
        VINBERO_COMMON_LOG_ERROR("%s: %u: vinbero_mt requires child modules", __FILE__, __LINE__);
        return VINBERO_COMMON_EINVAL;
    } // this will be checked by vinbero core
*/

    return 0;
}

int vinbero_interface_MODULE_rInit(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
    return 0;
}

static int vinbero_mt_initChildTlModules(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
    int ret;
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        VINBERO_COMMON_CALL(TLOCAL, init, childModule, &ret, childModule);
        if(ret < 0)
            return ret;
        if(vinbero_mt_initChildTlModules(childModule) < 0)
            return ret;
    }
    return 0;
}

static int vinbero_mt_rInitChildTlModules(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
    int ret;
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        if((ret = vinbero_mt_rInitChildTlModules(childModule)) < 0)
            return ret;
        VINBERO_COMMON_CALL(TLOCAL, rInit, childModule, &ret, childModule);
        if(ret < 0)
            return ret;
    }
    return 0;
}

static int vinbero_mt_destroyChildTlModules(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
    int ret;
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        VINBERO_COMMON_CALL(TLOCAL, destroy, childModule, &ret, childModule);
        if(ret < 0)
            return ret;
        if((ret = vinbero_mt_destroyChildTlModules(childModule) < 0))
            return ret;
    }
    return 0;
}

static int vinbero_mt_rDestroyChildTlModules(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
    int ret;
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        if((ret = vinbero_mt_rDestroyChildTlModules(childModule) < 0))
            return ret;
        VINBERO_COMMON_CALL(TLOCAL, rDestroy, childModule, &ret, childModule);
        if(ret < 0)
            return ret;
    }
    return 0;
}

static void vinbero_mt_pthreadCleanupHandler(void* args) {
    VINBERO_COMMON_LOG_TRACE2();
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
    VINBERO_COMMON_LOG_TRACE2();
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
    vinbero_mt_initChildTlModules(module);
    vinbero_mt_rInitChildTlModules(module);
/*
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_mt_interface childinterface;
        VINBERO_COMMON_CALL(TLOCAL, init, childModule, &ret, childModule, localModule->config, argsToPass);
        if(ret < 0)
            return NULL;
    }
*/
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        VINBERO_COMMON_CALL(TLSERVICE, call, childModule, &ret, childModule, argsToPass);
        if(ret < 0)
            return NULL;
    }
    pthread_cleanup_pop(1);
    return NULL;
}

int vinbero_interface_BASIC_service(struct vinbero_common_Module* module, void* args[]) {
    VINBERO_COMMON_LOG_TRACE2();
    struct vinbero_mt_LocalModule* localModule = module->localModule.pointer;
    struct vinbero_common_Module* parentModule = GENC_TREE_NODE_GET_PARENT(module);
    pthread_attr_init(&localModule->workerThreadAttr);
    pthread_attr_setdetachstate(&localModule->workerThreadAttr, PTHREAD_CREATE_JOINABLE);

    for(size_t index = 0; index != localModule->workerCount; ++index) {
       if(pthread_create(localModule->workerThreads + index, &localModule->workerThreadAttr, vinbero_mt_workerMain, (void*[]){module, args}) != 0) {
            VINBERO_COMMON_LOG_ERROR("pthread_create() failed");
            return VINBERO_COMMON_ERROR_UNKNOWN;
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

int vinbero_interface_MODULE_destroy(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
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

int vinbero_interface_MODULE_rDestroy(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
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
