#ifndef _TUCUBE_IMT_H
#define _TUCUBE_IMT_H

#include "tucube_Module.h"

#define TUCUBE_IMT_FUNCTIONS                                                                       \
int tucube_IModule_init(struct tucube_Module* module, struct tucube_Config* config, void* args[]); \
int tucube_IModule_destroy(struct tucube_Module* module);                                          \

#define TUCUBE_IMT_FUNCTION_POINTERS                                               \
int (*tucube_IModule_init)(struct tucube_Module*, struct tucube_Config*, void*[]); \
int (*tucube_IModule_destroy)(struct tucube_Module*);                              \

// SHOULD WE NEED ARRAYS OF VOID POINTERS ANYWAY?

#endif
