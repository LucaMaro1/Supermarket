#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include "data_structures.h"
#include "cliente.h"

extern void congedaCliente(long id_cassa, Cassa* lista_casse, long id_cliente) {
    if(id_cliente == (lista_casse[id_cassa].lPtr)->id) {
        Lista* tempPtr = lista_casse[id_cassa].lPtr;
        lista_casse[id_cassa].lPtr = tempPtr->next;
        free(tempPtr);
    } else {
        Lista* precPtr = lista_casse[id_cassa].lPtr;
        Lista* curPtr = precPtr->next;
        while(curPtr != NULL && curPtr->id != id_cliente) {
            precPtr = curPtr;
            curPtr = curPtr->next;
        }
        if(curPtr != NULL) {
            Lista* tempPtr = curPtr;
            precPtr->next = curPtr->next;
            free(tempPtr);
        }
    }
    lista_casse[id_cassa].queue_len--;
}

extern void riposizionaTutti(Cassa* k_list, long id_cassa, int n_casse, pthread_cond_t* num_client_changed) {
    int err;
    int id_arr_c;
    long id_c;
    long p;
    Lista* cPtr = k_list[id_cassa].lPtr;
    unsigned int rand_id = (unsigned int)id_cassa;
    int cassa;
    //sposta tutti i clienti della coda in una cassa differente
    while(cPtr != NULL) {
        id_arr_c = cPtr->service_id;
        id_c = cPtr->id;
        p = cPtr->prodotti;
        k_list[id_cassa].lPtr = cPtr;
        Lista* tempPtr = k_list[id_cassa].lPtr;

        cassa = rand_r(&rand_id) % n_casse;
        while(k_list[cassa].queue_len == -1) {
            cassa = (cassa + 1) % n_casse;
        }
        accodaCliente(id_c, p, k_list, cassa, id_arr_c);
        if((err = pthread_cond_signal(&(num_client_changed[cassa]))) != 0) {
            errno = err;
            perror("signal");
        }

        cPtr = cPtr->next;
        free(tempPtr);
    }
    k_list[id_cassa].lPtr = cPtr;
}

extern void congedaTutti(Cassa* lista_casse, long id_cassa, pthread_mutex_t* mtx_client_service, pthread_cond_t* served_client, _Bool* service_done) {
    int err;
    int service_id_c;
    Lista* cPtr = lista_casse[id_cassa].lPtr;
    //segnala a tutti i clienti in coda che devono uscire, ma senza servirli
    while(cPtr != NULL) {
        service_id_c = cPtr->service_id;
        lista_casse[id_cassa].lPtr = cPtr;
        Lista* tempPtr = lista_casse[id_cassa].lPtr;
        cPtr = cPtr->next;
        free(tempPtr);
        
        if((err = pthread_mutex_lock(mtx_client_service)) != 0) {
            errno = err;
            perror("lock");
        }
        service_done[service_id_c] = true;
        if((err = pthread_cond_signal(&(served_client[service_id_c]))) != 0) {
            errno = err;
            perror("signal");
        }
        if((err = pthread_mutex_unlock(mtx_client_service)) != 0) {
            errno = err;
            perror("unlock");
        }
    }
    lista_casse[id_cassa].lPtr = NULL;
}