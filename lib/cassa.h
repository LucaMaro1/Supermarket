#include <stdio.h>
#include <pthread.h>
#include "data_structures.h"

#ifndef _CASSA_H_
#define _CASSA_H_

    extern void congedaCliente(long, Cassa*, long);
    extern void riposizionaTutti(Cassa*, long, int, pthread_cond_t*);
    extern void congedaTutti(Cassa*, long, pthread_mutex_t*, pthread_cond_t*, _Bool*);

#endif