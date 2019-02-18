#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <vinbero_com/vinbero_com_Status.h>
#include <vinbero_com/vinbero_com_Error.h>
#include <vinbero_com/vinbero_com_Call.h>
#include <vinbero_com/vinbero_com_Config.h>
#include <vinbero_com/vinbero_com_Log.h>
#include <vinbero_com/vinbero_com_Module.h>
#include <vinbero/vinbero_iface_MODULE.h>
#include <vinbero/vinbero_iface_BASIC.h>
#include <vinbero/vinbero_iface_TLOCAL.h>
#include <vinbero/vinbero_iface_TLSERVICE.h>
#include <libgenc/genc_Tree.h>
#include "config.h"

VINBERO_COM_MODULE_META_NAME("vinbero_mt")
VINBERO_COM_MODULE_META_LICENSE("MPL-2.0")
VINBERO_COM_MODULE_META_VERSION(
    VINBERO_MT_VERSION_MAJOR,
    VINBERO_MT_VERSION_MINOR,
    VINBERO_MT_VERSION_PATCH
)
VINBERO_COM_MODULE_META_IN_IFACES("BASIC")
VINBERO_COM_MODULE_META_OUT_IFACES("TLOCAL,TLSERVICE")
VINBERO_COM_MODULE_META_CHILD_COUNT(-1, -1)

VINBERO_IFACE_MODULE_FUNCS;
VINBERO_IFACE_BASIC_FUNCS;

struct vinbero_mt_workerMain_Arg { // You can replace this by tlModule
    struct vinbero_com_Module* module;
    int* exitEventFd;
};

struct vinbero_mt_LocalModule {
    int workerCount;
    pthread_attr_t workerThreadAttr;
    pthread_t* workerThreads;
    int* workerThreadExitEventFds;
    struct vinbero_mt_workerMain_Arg* workerThreadArgs; // You can replace this by tlModules
    int exitEventFd;
};


int vinbero_iface_MODULE_init(struct vinbero_com_Module* module) {
    VINBERO_COM_LOG_TRACE2();
    module->localModule.pointer = malloc(1 * sizeof(struct vinbero_mt_LocalModule));
    struct vinbero_mt_LocalModule* localModule = module->localModule.pointer;
    vinbero_com_Config_getInt(module->config, module, "vinbero_mt.workerCount", &(localModule->workerCount), 1);

    localModule->workerThreads = malloc(localModule->workerCount * sizeof(pthread_t));
    localModule->workerThreadExitEventFds = malloc(localModule->workerCount * sizeof(int));
    localModule->workerThreadArgs = malloc(localModule->workerCount * sizeof(struct vinbero_mt_workerMain_Arg));
    for(size_t index = 0; index != localModule->workerCount; ++index) {
        localModule->workerThreadExitEventFds[index] = eventfd(0, 0);
        localModule->workerThreadArgs[index].module = module;
        localModule->workerThreadArgs[index].exitEventFd = &localModule->workerThreadExitEventFds[index];
    }
    return VINBERO_COM_STATUS_SUCCESS;
}

int vinbero_iface_MODULE_rInit(struct vinbero_com_Module* module) {
    VINBERO_COM_LOG_TRACE2();
    return VINBERO_COM_STATUS_SUCCESS;
}

static int vinbero_mt_loadChildTlModules(struct vinbero_com_TlModule* tlModule) {
    int ret;
    GENC_TREE_NODE_INIT2(tlModule, GENC_TREE_NODE_SIZE(tlModule->module));
    GENC_TREE_NODE_FOREACH(tlModule->module, index) {
        struct vinbero_com_TlModule* childTlModule = malloc(sizeof(struct vinbero_com_TlModule));
        GENC_TREE_NODE_ADD(tlModule, childTlModule);
        childTlModule->module = GENC_TREE_NODE_RAW_GET(tlModule->module, index);
        childTlModule->localTlModule.pointer = NULL;
        childTlModule->arg = NULL;
        childTlModule->exitEventFd = tlModule->exitEventFd;
        ret = vinbero_mt_loadChildTlModules(childTlModule);
        if(ret < VINBERO_COM_STATUS_SUCCESS)
            return ret;
    }
    return VINBERO_COM_STATUS_SUCCESS;
}

static int vinbero_mt_initChildTlModules(struct vinbero_com_TlModule* tlModule) {
    VINBERO_COM_LOG_TRACE2();
    int ret;
    GENC_TREE_NODE_FOREACH(tlModule, index) {
        struct vinbero_com_TlModule* childTlModule = GENC_TREE_NODE_RAW_GET(tlModule, index);
        if(childTlModule->arg == NULL)
            childTlModule->arg = tlModule->arg;
        VINBERO_COM_CALL(TLOCAL, init, childTlModule->module, &ret, childTlModule);
        if(ret < VINBERO_COM_STATUS_SUCCESS)
            return ret;
        ret = vinbero_mt_initChildTlModules(childTlModule);
        if(ret < VINBERO_COM_STATUS_SUCCESS)
            return ret;
    }
    return VINBERO_COM_STATUS_SUCCESS;
}

static int vinbero_mt_rInitChildTlModules(struct vinbero_com_TlModule* tlModule) {
    VINBERO_COM_LOG_TRACE2();
    int ret;
    GENC_TREE_NODE_FOREACH(tlModule, index) {
        struct vinbero_com_TlModule* childTlModule = GENC_TREE_NODE_RAW_GET(tlModule, index);
        ret = vinbero_mt_rInitChildTlModules(childTlModule);
        if(ret < VINBERO_COM_STATUS_SUCCESS)
            return ret;
        VINBERO_COM_CALL(TLOCAL, rInit, childTlModule->module, &ret, childTlModule);
        if(ret < VINBERO_COM_STATUS_SUCCESS)
            return ret;
    }
    return VINBERO_COM_STATUS_SUCCESS;
}

static int vinbero_mt_destroyChildTlModules(struct vinbero_com_TlModule* tlModule) {
    VINBERO_COM_LOG_TRACE2();
    int ret;
    GENC_TREE_NODE_FOREACH(tlModule, index) {
        struct vinbero_com_TlModule* childTlModule = GENC_TREE_NODE_RAW_GET(tlModule, index);
        VINBERO_COM_CALL(TLOCAL, destroy, childTlModule->module, &ret, childTlModule);
        if(ret < VINBERO_COM_STATUS_SUCCESS)
            return ret;
        ret = vinbero_mt_destroyChildTlModules(childTlModule);
        if(ret < VINBERO_COM_STATUS_SUCCESS)
            return ret;
    }
    return VINBERO_COM_STATUS_SUCCESS;
}

static int vinbero_mt_rDestroyChildTlModules(struct vinbero_com_TlModule* tlModule) {
    VINBERO_COM_LOG_TRACE2();
    int ret;
    GENC_TREE_NODE_FOREACH(tlModule, index) {
        struct vinbero_com_TlModule* childTlModule = GENC_TREE_NODE_RAW_GET(tlModule, index);
        ret = vinbero_mt_rDestroyChildTlModules(childTlModule);
        if(ret < VINBERO_COM_STATUS_SUCCESS)
            return ret;
        VINBERO_COM_CALL(TLOCAL, rDestroy, childTlModule->module, &ret, childTlModule);
        if(ret < VINBERO_COM_STATUS_SUCCESS)
            return ret;
        GENC_TREE_NODE_FREE(childTlModule);
        free(childTlModule);
    }

    return VINBERO_COM_STATUS_SUCCESS;
}

static void vinbero_mt_pthreadCleanupHandler(void* arg) {
    VINBERO_COM_LOG_TRACE2();
    struct vinbero_com_TlModule* tlModule = arg;
    vinbero_mt_destroyChildTlModules(tlModule);
    vinbero_mt_rDestroyChildTlModules(tlModule);
    free(tlModule);
}

static void* vinbero_mt_workerMain(void* arg) {
    VINBERO_COM_LOG_TRACE2();
    int ret;
    struct vinbero_com_TlModule* tlModule = malloc(1 * sizeof(struct vinbero_com_TlModule)); 
    struct vinbero_mt_workerMain_Arg* args = arg;
    tlModule->module = args->module;
    tlModule->exitEventFd = args->exitEventFd;

    tlModule->localTlModule.pointer = NULL;
    tlModule->arg = tlModule->module->arg;

    pthread_cleanup_push(vinbero_mt_pthreadCleanupHandler, tlModule);
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

    sigset_t signalSet;
    sigemptyset(&signalSet);
    sigaddset(&signalSet, SIGINT);
    if(pthread_sigmask(SIG_BLOCK, &signalSet, (void*){NULL}) != 0) {
        VINBERO_COM_LOG_ERROR("pthread_sigmask() failed");
        pthread_exit(NULL);
    }

    ret = vinbero_mt_loadChildTlModules(tlModule);
    if(ret < VINBERO_COM_STATUS_SUCCESS) {
        VINBERO_COM_LOG_ERROR("vinbero_mt_loadChildTlModules() failed");
        pthread_exit(NULL);
    }
    ret = vinbero_mt_initChildTlModules(tlModule);
    if(ret < VINBERO_COM_STATUS_SUCCESS) {
        VINBERO_COM_LOG_ERROR("vinbero_mt_initChildTlModules() failed");
        pthread_exit(NULL);
    }
    ret = vinbero_mt_rInitChildTlModules(tlModule);
    if(ret < VINBERO_COM_STATUS_SUCCESS) {
        VINBERO_COM_LOG_ERROR("vinbero_mt_rInitChildTlModules() failed");
        pthread_exit(NULL);
    }

    GENC_TREE_NODE_FOREACH(tlModule, index) {
        struct vinbero_com_TlModule* childTlModule = GENC_TREE_NODE_RAW_GET(tlModule, index);
        childTlModule->arg = tlModule->arg; 
        VINBERO_COM_CALL(TLSERVICE, call, childTlModule->module, &ret, childTlModule);
        if(ret < VINBERO_COM_STATUS_SUCCESS)
            pthread_exit(NULL);
    }
    pthread_cleanup_pop(1);
    return NULL;
}


int vinbero_iface_BASIC_service(struct vinbero_com_Module* module) {
    VINBERO_COM_LOG_TRACE2();
    struct vinbero_mt_LocalModule* localModule = module->localModule.pointer;

    pthread_attr_init(&localModule->workerThreadAttr);
    pthread_attr_setdetachstate(&localModule->workerThreadAttr, PTHREAD_CREATE_JOINABLE);

    for(size_t index = 0; index != localModule->workerCount; ++index) {
       if(pthread_create(localModule->workerThreads + index,
                         &localModule->workerThreadAttr,
                         vinbero_mt_workerMain,
                         &localModule->workerThreadArgs[index]) != 0) {
            VINBERO_COM_LOG_ERROR("pthread_create() failed");
            return VINBERO_COM_ERROR_UNKNOWN;
       }
    }

    for(size_t index = 0; index != localModule->workerCount; ++index) {
        pthread_join(localModule->workerThreads[index], NULL);
        localModule->workerThreads[index] = NULL;
    }

    return VINBERO_COM_STATUS_SUCCESS;
}

int vinbero_iface_MODULE_destroy(struct vinbero_com_Module* module) {
    VINBERO_COM_LOG_TRACE2();
    struct vinbero_mt_LocalModule* localModule = module->localModule.pointer;
    for(size_t index = 0; index != localModule->workerCount; ++index) {
        uint64_t counter;
        if(localModule->workerThreads[index] == NULL)
	    continue;
        counter = 1;
        write(localModule->workerThreadExitEventFds[index], &counter, sizeof(counter));
        pthread_join(localModule->workerThreads[index], NULL);
    }
    return VINBERO_COM_STATUS_SUCCESS;
}

int vinbero_iface_MODULE_rDestroy(struct vinbero_com_Module* module) {
    VINBERO_COM_LOG_TRACE2();
    struct vinbero_mt_LocalModule* localModule = module->localModule.pointer;
    pthread_attr_destroy(&localModule->workerThreadAttr);
    free(localModule->workerThreads);
    for(size_t index = 0; index != localModule->workerCount; ++index)
        close(localModule->workerThreadExitEventFds[index]);
    free(localModule->workerThreadExitEventFds);
    free(localModule->workerThreadArgs);
    free(module->localModule.pointer);
    return VINBERO_COM_STATUS_SUCCESS;
}
