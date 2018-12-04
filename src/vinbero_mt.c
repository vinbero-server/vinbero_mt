#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vinbero_common/vinbero_common_Status.h>
#include <vinbero_common/vinbero_common_Error.h>
#include <vinbero_common/vinbero_common_Call.h>
#include <vinbero_common/vinbero_common_Config.h>
#include <vinbero_common/vinbero_common_Log.h>
#include <vinbero_common/vinbero_common_Module.h>
#include <vinbero/vinbero_interface_MODULE.h>
#include <vinbero/vinbero_interface_BASIC.h>
#include <vinbero/vinbero_interface_TLOCAL.h>
#include <vinbero/vinbero_interface_TLSERVICE.h>
#include <libgenc/genc_Tree.h>
#include "vinbero_mt_Version.h"

struct vinbero_mt_interface {
    VINBERO_INTERFACE_TLOCAL_FUNCTION_POINTERS;
    VINBERO_INTERFACE_TLSERVICE_FUNCTION_POINTERS;
};

struct vinbero_mt_LocalModule {
    int workerCount;
    pthread_attr_t workerThreadAttr;
    pthread_t* workerThreads;
    bool exit; // whether one of threads exited
    int exitPipe[2];
};

VINBERO_INTERFACE_MODULE_FUNCTIONS;
VINBERO_INTERFACE_BASIC_FUNCTIONS;

int vinbero_interface_MODULE_init(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
    int ret;
    module->name = "vinbero_mt";
    module->version = VINBERO_MT_VERSION;
    module->localModule.pointer = malloc(1 * sizeof(struct vinbero_mt_LocalModule));
    struct vinbero_mt_LocalModule* localModule = module->localModule.pointer;
    vinbero_common_Config_getInt(module->config, module, "vinbero_mt.workerCount", &(localModule->workerCount), 1);
    localModule->workerThreads = malloc(localModule->workerCount * sizeof(pthread_t));
    localModule->exit = false;
    pipe(localModule->exitPipe);
/*
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_mt_interface childinterface;
        VINBERO_INTERFACE_TLOCAL_DLSYM(&childinterface, &childModule->dlHandle, &ret); 
        if(ret < VINBERO_COMMON_STATUS_SUCCESS) {
            VINBERO_COMMON_LOG_ERROR("module %s doesn't satisfy ITLOCAL interface", childModule->id);
            return ret;
        }
        VINBERO_INTERFACE_TLSERVICE_DLSYM(&childinterface, &childModule->dlHandle, &ret); 
        if(ret < VINBERO_COMMON_STATUS_SUCCESS) {
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

    return VINBERO_COMMON_STATUS_SUCCESS;
}

int vinbero_interface_MODULE_rInit(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
    return VINBERO_COMMON_STATUS_SUCCESS;
}

static int vinbero_mt_loadChildTlModules(struct vinbero_common_TlModule* tlModule) {
    int ret;
    GENC_TREE_NODE_INIT3(tlModule, GENC_TREE_NODE_GET_CHILD_COUNT(tlModule->module));
    GENC_TREE_NODE_FOR_EACH_CHILD(tlModule, index) {
        struct vinbero_common_TlModule* childTlModule = &GENC_TREE_NODE_GET_CHILD(tlModule, index);
        childTlModule->module = &GENC_TREE_NODE_GET_CHILD(tlModule->module, index);
        childTlModule->localTlModule.pointer = NULL;
        childTlModule->arg = NULL;
        ret = vinbero_mt_loadChildTlModules(childTlModule);
        if(ret < VINBERO_COMMON_STATUS_SUCCESS)
            return ret;
    }
    return VINBERO_COMMON_STATUS_SUCCESS;
}

static int vinbero_mt_initChildTlModules(struct vinbero_common_TlModule* tlModule) {
    VINBERO_COMMON_LOG_TRACE2();
    int ret;
    GENC_TREE_NODE_FOR_EACH_CHILD(tlModule, index) {
        struct vinbero_common_TlModule* childTlModule = &GENC_TREE_NODE_GET_CHILD(tlModule, index);
        if(childTlModule->arg == NULL)
            childTlModule->arg = tlModule->arg;
        VINBERO_COMMON_CALL(TLOCAL, init, childTlModule->module, &ret, childTlModule);
        if(ret < VINBERO_COMMON_STATUS_SUCCESS)
            return ret;
        ret = vinbero_mt_initChildTlModules(childTlModule);
        if(ret < VINBERO_COMMON_STATUS_SUCCESS)
            return ret;
    }
    return VINBERO_COMMON_STATUS_SUCCESS;
}

static int vinbero_mt_rInitChildTlModules(struct vinbero_common_TlModule* tlModule) {
    VINBERO_COMMON_LOG_TRACE2();
    int ret;
    GENC_TREE_NODE_FOR_EACH_CHILD(tlModule, index) {
        struct vinbero_common_TlModule* childTlModule = &GENC_TREE_NODE_GET_CHILD(tlModule, index);
        ret = vinbero_mt_rInitChildTlModules(childTlModule);
        if(ret < VINBERO_COMMON_STATUS_SUCCESS)
            return ret;
        VINBERO_COMMON_CALL(TLOCAL, rInit, childTlModule->module, &ret, childTlModule);
        if(ret < VINBERO_COMMON_STATUS_SUCCESS)
            return ret;
    }
    return VINBERO_COMMON_STATUS_SUCCESS;
}

static int vinbero_mt_destroyChildTlModules(struct vinbero_common_TlModule* tlModule) {
    VINBERO_COMMON_LOG_TRACE2();
    int ret;
    GENC_TREE_NODE_FOR_EACH_CHILD(tlModule, index) {
        struct vinbero_common_TlModule* childTlModule = &GENC_TREE_NODE_GET_CHILD(tlModule, index);
        VINBERO_COMMON_CALL(TLOCAL, destroy, childTlModule->module, &ret, childTlModule);
        if(ret < VINBERO_COMMON_STATUS_SUCCESS)
            return ret;
        ret = vinbero_mt_destroyChildTlModules(childTlModule);
        if(ret < VINBERO_COMMON_STATUS_SUCCESS)
            return ret;
    }
    return VINBERO_COMMON_STATUS_SUCCESS;
}

static int vinbero_mt_rDestroyChildTlModules(struct vinbero_common_TlModule* tlModule) {
    VINBERO_COMMON_LOG_TRACE2();
    int ret;
    GENC_TREE_NODE_FOR_EACH_CHILD(tlModule, index) {
        struct vinbero_common_TlModule* childTlModule = &GENC_TREE_NODE_GET_CHILD(tlModule, index);
        ret = vinbero_mt_rDestroyChildTlModules(childTlModule);
        if(ret < VINBERO_COMMON_STATUS_SUCCESS)
            return ret;
        VINBERO_COMMON_CALL(TLOCAL, rDestroy, childTlModule->module, &ret, childTlModule);
        if(ret < VINBERO_COMMON_STATUS_SUCCESS)
            return ret;
    }
    return VINBERO_COMMON_STATUS_SUCCESS;
}

static void vinbero_mt_pthreadCleanupHandler(void* arg) {
    VINBERO_COMMON_LOG_TRACE2();
    struct vinbero_common_TlModule* tlModule = arg;
    VINBERO_COMMON_LOG_TRACE2();

    struct vinbero_mt_LocalModule* localModule = tlModule->module->localModule.pointer;
    vinbero_mt_destroyChildTlModules(tlModule);
    vinbero_mt_rDestroyChildTlModules(tlModule);
    localModule->exit = true;
    write(localModule->exitPipe[1], "v", 1);

}

static void* vinbero_mt_workerMain(void* arg) {
    VINBERO_COMMON_LOG_TRACE2();
    int ret;
    struct vinbero_common_TlModule* tlModule = malloc(sizeof(struct vinbero_common_TlModule) * sizeof(char)); 
    tlModule->module = arg;
    tlModule->localTlModule.pointer = NULL;
    tlModule->arg = tlModule->module->arg;

    struct vinbero_mt_LocalModule* localModule = tlModule->module->localModule.pointer;


    pthread_cleanup_push(vinbero_mt_pthreadCleanupHandler, tlModule);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    sigset_t signalSet;
    sigemptyset(&signalSet);
    sigaddset(&signalSet, SIGINT);
    if(pthread_sigmask(SIG_BLOCK, &signalSet, (void*){NULL}) != 0) {
        VINBERO_COMMON_LOG_ERROR("pthread_sigmask() failed");
        pthread_exit(NULL);
    }

    ret = vinbero_mt_loadChildTlModules(tlModule);
    if(ret < VINBERO_COMMON_STATUS_SUCCESS) {
        VINBERO_COMMON_LOG_ERROR("vinbero_mt_loadChildTlModules() failed");
        pthread_exit(NULL);
    }
    ret = vinbero_mt_initChildTlModules(tlModule);
    if(ret < VINBERO_COMMON_STATUS_SUCCESS) {
        VINBERO_COMMON_LOG_ERROR("vinbero_mt_initChildTlModules() failed");
        pthread_exit(NULL);
    }
    ret = vinbero_mt_rInitChildTlModules(tlModule);
    if(ret < VINBERO_COMMON_STATUS_SUCCESS) {
        VINBERO_COMMON_LOG_ERROR("vinbero_mt_rInitChildTlModules() failed");
        pthread_exit(NULL);
    }

    GENC_TREE_NODE_FOR_EACH_CHILD(tlModule, index) {
        struct vinbero_common_TlModule* childTlModule = &GENC_TREE_NODE_GET_CHILD(tlModule, index);
        childTlModule->arg = tlModule->arg; 
        VINBERO_COMMON_CALL(TLSERVICE, call, childTlModule->module, &ret, childTlModule);
        if(ret < VINBERO_COMMON_STATUS_SUCCESS)
            pthread_exit(NULL);
    }
    pthread_cleanup_pop(1);
    return NULL;
}

int vinbero_interface_BASIC_service(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
    struct vinbero_mt_LocalModule* localModule = module->localModule.pointer;
    struct vinbero_common_Module* parentModule = GENC_TREE_NODE_GET_PARENT(module);

    pthread_attr_init(&localModule->workerThreadAttr);
    pthread_attr_setdetachstate(&localModule->workerThreadAttr, PTHREAD_CREATE_JOINABLE);

    for(size_t index = 0; index != localModule->workerCount; ++index) {
       if(pthread_create(localModule->workerThreads + index, &localModule->workerThreadAttr, vinbero_mt_workerMain, module) != 0) {
            VINBERO_COMMON_LOG_ERROR("pthread_create() failed");
            return VINBERO_COMMON_ERROR_UNKNOWN;
       }
    }

    char c;
    read(localModule->exitPipe[0], &c, 1);

    for(size_t index = 0; index != localModule->workerCount; ++index) {
        pthread_cancel(localModule->workerThreads[index]);
        pthread_join(localModule->workerThreads[index], NULL);
    }
    return VINBERO_COMMON_STATUS_SUCCESS;
}

int vinbero_interface_MODULE_destroy(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
    struct vinbero_mt_LocalModule* localModule = module->localModule.pointer;
    if(localModule->exit == false) {
        for(size_t index = 0; index != localModule->workerCount; ++index) {
            pthread_cancel(localModule->workerThreads[index]);
            char c;
            read(localModule->exitPipe[0], &c, 1);
            pthread_join(localModule->workerThreads[index], NULL);
            localModule->exit = false;
        }
        localModule->exit = true;
    }
    return VINBERO_COMMON_STATUS_SUCCESS;
}

int vinbero_interface_MODULE_rDestroy(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
    struct vinbero_mt_LocalModule* localModule = module->localModule.pointer;
    pthread_attr_destroy(&localModule->workerThreadAttr);
    free(localModule->workerThreads);
    close(localModule->exitPipe[0]);
    close(localModule->exitPipe[1]);
    free(module->localModule.pointer);
//    dlclose(module->dlHandle);
    return VINBERO_COMMON_STATUS_SUCCESS;
}
