#include <stdio.h>
#include <stdbool.h>

#ifndef _DATA_STRUCTURES_H_
#define _DATA_STRUCTURES_H_

	struct configurazione {
        char* log_file_name;
        int n_casse;
        int max_clienti;
        int E;
        int T;
        int max_prodotti_per_cliente;
        int k_init;
        int t_per_prod;
        int int_comm_k_direttore;
        int s1;
        int s2;
    };

    struct coda {
        long val;
        struct coda* next;
    };
    typedef struct coda Coda;

    struct lista {
        long id;
        long prodotti;
        int service_id;
        struct lista* next;
    };
    typedef struct lista Lista;

    struct cassa {
        int queue_len; //se la cassa è chiusa è settata a -1
        Lista* lPtr;
    };
    typedef struct cassa Cassa;

    struct comm_queue {
        long id_cassa;
        int num_clienti;
        struct comm_queue* next;
    };
    typedef struct comm_queue CommQueue;

    extern void enqueue(CommQueue**, CommQueue**, long, int);
    extern CommQueue dequeue(CommQueue**, CommQueue**);
    extern void deallocQueue(CommQueue**);
    extern void inserisci(Coda**, Coda**, long);
    extern long cancella(Coda** head, Coda** tail);
    extern _Bool cerca(Coda*, long);

#endif
