#ifndef _TUCUBE_IMT_H
#define _TUCUBE_IMT_H

#include <tucube/tucube_Module.h>

#define TUCUBE_IMT_FUNCTIONS                                                                   \
int tucube_IMt_init(struct tucube_Module* module, struct tucube_Config* config, void* args[]); \
int tucube_IMt_service(struct tucube_Module* module, void* args[]);                            \
int tucube_IMt_destroy(struct tucube_Module* module);                                          \

#define TUCUBE_IMT_FUNCTION_POINTERS                                           \
int (*tucube_IMt_init)(struct tucube_Module*, struct tucube_Config*, void*[]); \
int (*tucube_IMt_service)(struct tucube_Module*, void*[]);                     \
int (*tucube_IMt_destroy)(struct tucube_Module*);                              \

// SHOULD WE NEED ARRAYS OF VOID POINTERS ANYWAY?

#endif
