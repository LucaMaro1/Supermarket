#include "data_structures.h"
#include <stdio.h>
#include <stdlib.h>

extern int inserisciCliente(long* arr, long val, int max_clienti) {
    for(int i = 0 ; i < max_clienti ; i++) {
        if(arr[i] == -1) {
            arr[i] = val;
            return i;
        }
    }
    return -1;
}

extern void cancellaCliente(long* arr, long val, int max_clienti) {
    int i = 0;
    while(arr[i] != val) { //mi aspetto di trovare sempre il valore che voglio cancellare
        i++;
    }
    //tratto l'array come una lista
    while(i < (max_clienti - 1) && arr[i + 1] != -1) {
        arr[i] = arr[i + 1];
        i++;
    }
    arr[i] = -1;
}

extern void accodaCliente(long id_cliente, long prodotti_acquistati, Cassa* lista_casse, long id_cassa, int id_servizio) {
    Lista* newPtr = malloc(sizeof(Lista));
    if(newPtr == NULL) {
        perror("malloc");
        return;
    }
    newPtr->id = id_cliente;
    newPtr->prodotti = prodotti_acquistati;
    newPtr->service_id = id_servizio;
    newPtr->next = lista_casse[id_cassa].lPtr;
    lista_casse[id_cassa].lPtr = newPtr;
    if(lista_casse[id_cassa].queue_len >= 0)
        lista_casse[id_cassa].queue_len++;
}