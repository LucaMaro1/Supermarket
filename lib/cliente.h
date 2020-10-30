#include <stdio.h>
#include "data_structures.h"

#ifndef _CLIENTE_H_
#define _CLIENTE_H_

    extern int inserisciCliente(long*, long, int);
    extern void cancellaCliente(long*, long, int);
    extern void accodaCliente(long, long, Cassa*, long, int);

#endif