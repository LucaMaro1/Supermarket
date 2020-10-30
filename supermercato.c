#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include "data_structures.h"
#include "direttore.h"
#include "cliente.h"
#include "cassa.h"

#define FILE_CONF "config.txt"
#define FILE_CONF_NAME_MAXLEN 50
#define minT 10
#define minTperP 20
#define maxTperP 80
#define FILE_APERTURE_CASSE "tempiApertureCasse.txt"
#define FILE_TEMPO_SERVIZIO "tempoServizioClienti.txt"
#define FILE_INFO_CLIENTI "infoClienti.txt"

#define SYSCALL(r, c, e)\
    if((r = c) != 0) {  \
        errno = r;      \
        perror(e);      \
    }

static pthread_mutex_t mtx_dir_client = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtx_coda_in_cassa = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtx_comm_cassa = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtx_dir_apri_chiudi_cassa = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtx_clienti_senza_prodotti = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtx_invio_informazioni = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtx_file_tempi_aperture_casse = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtx_file_tempo_servizio_cliente = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtx_file_info_clienti = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtx_servizio_clienti = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtx_service_id_c = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t is_empty = PTHREAD_COND_INITIALIZER; //per sapere se tutti i clienti sono usciti dal supermercato
static pthread_cond_t attesa_autorizzazione = PTHREAD_COND_INITIALIZER;
static pthread_cond_t* cliente_servito;
static pthread_cond_t* cassa_aperta;
static pthread_cond_t* n_clienti_cambiati;
//variabili condivise
struct configurazione* conf;
int clienti_in = 0;
int clienti_vivi_generati = 0;
_Bool ciclo_dir = true;
long* is_in_client = NULL;
Cassa* is_open_cassa = NULL;
pthread_t tid_sig_hand_dir = 0;
CommQueue* testa = NULL;
CommQueue* coda = NULL;
pthread_t* tid_sig_hand_casse;
_Bool apri_chiudi_cassa = false;
Coda* testa_clienti_autorizzazione_uscire = NULL;
Coda* coda_clienti_autorizzazione_uscire = NULL;
_Bool sigquit = false;
_Bool fuori_da_ciclo_dir = false;
_Bool* casse_aperte_comm_k;
_Bool* service_id_available;
_Bool* servizio_effettuato;
//per il file di log
long index_client = 0;
int* chiusure_cassa;
int* clienti_serviti;
FILE* fd_tempi_aperture;
FILE* fd_tempo_servizio;
FILE* fd_info_clienti;
long prod_acquistati = 0;

void ordinaFile(char*);

static void* cassa_communication(void* arg) {
    int err;
    long t_sec = conf->int_comm_k_direttore / 1000;
    long t_nsec = (conf->int_comm_k_direttore % 1000) * 1000000;
    struct timespec t = {t_sec, t_nsec};
    long id_cassa = (long)arg;
    int clienti;
    while(ciclo_dir) {
        //la cassa non invia notifiche se è chiusa
        SYSCALL(err, pthread_mutex_lock(&mtx_invio_informazioni), "lock");
        while(!casse_aperte_comm_k[id_cassa] && ciclo_dir) {
            SYSCALL(err, pthread_cond_wait(&(cassa_aperta[id_cassa]), &mtx_invio_informazioni), "wait");
        }
        SYSCALL(err, pthread_mutex_unlock(&mtx_invio_informazioni), "unlock");

        SYSCALL(err, nanosleep(&t, NULL), "nanosleep");
        SYSCALL(err, pthread_mutex_lock(&mtx_comm_cassa), "lock");
        clienti = is_open_cassa[id_cassa].queue_len;
        enqueue(&testa, &coda, id_cassa, clienti);
        SYSCALL(err, pthread_mutex_unlock(&mtx_comm_cassa), "unlock");
        if(ciclo_dir && tid_sig_hand_dir != 0) {
            SYSCALL(err, pthread_kill(tid_sig_hand_dir, SIGALRM), "pthread_kill");
        }
    }
    return NULL;
}

static void* cassa(void* arg) {
    int err;
    long id = (long)arg;
    unsigned int rand_id = ((unsigned int)arg);
    long r = (rand_r(&rand_id) % (maxTperP - minTperP) + minTperP) * 1000000;
    _Bool statoprecedente = false;
    struct timespec apertura, chiusura;

    pthread_t tid_comm;
    //invia comunicazioni al direttore
    SYSCALL(err, pthread_create(&tid_comm, NULL, cassa_communication, (void*)id), "pthread_create");

    long prodotti;
    long id_cliente;
    int service_id_c;
    Lista* node;
    struct timespec t;
    while(ciclo_dir || (clienti_in + clienti_vivi_generati > 0)) {
        SYSCALL(err, pthread_mutex_lock(&mtx_coda_in_cassa), "lock");
        if(sigquit) {
            is_open_cassa[id].queue_len = -1;
        }
        while(is_open_cassa[id].queue_len == -1) {
            if(statoprecedente) {
                SYSCALL(err, clock_gettime(CLOCK_MONOTONIC, &chiusura), "clock_gettime");
                long sec = chiusura.tv_sec - apertura.tv_sec;
                long nsec = chiusura.tv_nsec - apertura.tv_nsec;
                if(nsec < 0) { //gestisco underflow dei nanosecondi
                    sec--;
                    nsec = nsec + 1000000000;
                }
                //printf("cassa %li in chiusura\n", id);
                SYSCALL(err, pthread_mutex_lock(&mtx_file_tempi_aperture_casse), "lock");
                fprintf(fd_tempi_aperture, "%li %li.%li\n", id, sec, (nsec / 1000000));  //precisione fino ai millisecondi
                SYSCALL(err, pthread_mutex_unlock(&mtx_file_tempi_aperture_casse), "unlock");
                statoprecedente = false;
                SYSCALL(err, pthread_mutex_lock(&mtx_invio_informazioni), "lock");
                casse_aperte_comm_k[id] = false;
                SYSCALL(err, pthread_mutex_unlock(&mtx_invio_informazioni), "unlock");
                chiusure_cassa[id]++;
            }
            //se la coda è vuota mi sospendo
            if(!ciclo_dir) {
                if(is_open_cassa[id].lPtr != NULL) {
                    //potrebbe essere arrivato SIGQUIT
                    if(sigquit) {
                        congedaTutti(is_open_cassa, id, &mtx_servizio_clienti, cliente_servito, servizio_effettuato);
                    } else { //oppure potrebbe essere stata chiusa dal direttore
                        while(is_open_cassa[id].lPtr != NULL) {
                            node = is_open_cassa[id].lPtr;
                            prodotti = node->prodotti;
                            id_cliente = node->id;
                            service_id_c = node->service_id;
                            t.tv_sec = 0;
                            t.tv_nsec = r;
                            //tempo fisso
                            SYSCALL(err, nanosleep(&t, NULL), "nanosleep");
                            t.tv_nsec = (prodotti * conf->t_per_prod) * 1000000;
                            t.tv_sec = t.tv_nsec / 1000000000;
                            t.tv_nsec = t.tv_nsec % 1000000000;
                            //tempo variabile
                            SYSCALL(err, nanosleep(&t, NULL), "nanosleep");
                            SYSCALL(err, pthread_mutex_lock(&mtx_file_tempo_servizio_cliente), "lock");
                            
                            //I TEMPI SONO SBAGLIATI

                            fprintf(fd_tempo_servizio, "%li %li %li\n", id, id_cliente, (r + t.tv_nsec) / 1000000);
                            SYSCALL(err, pthread_mutex_unlock(&mtx_file_tempo_servizio_cliente), "unlock");
                            congedaCliente(id, is_open_cassa, id_cliente);
                            clienti_serviti[id]++;

                            SYSCALL(err, pthread_mutex_lock(&mtx_servizio_clienti), "lock");
                            servizio_effettuato[service_id_c] = true;
                            SYSCALL(err, pthread_cond_signal(&(cliente_servito[service_id_c])), "signal");
                            SYSCALL(err, pthread_mutex_unlock(&mtx_servizio_clienti), "unlock");
                        }
                    }
                }
                SYSCALL(err, pthread_mutex_unlock(&mtx_coda_in_cassa), "unlock");
                SYSCALL(err, pthread_mutex_lock(&mtx_invio_informazioni), "lock");
                SYSCALL(err, pthread_cond_signal(&(cassa_aperta[id])), "signal");
                SYSCALL(err, pthread_mutex_unlock(&mtx_invio_informazioni), "unlock");
                SYSCALL(err, pthread_join(tid_comm, NULL), "join");
                return NULL;
            } else {
                if(is_open_cassa[id].lPtr != NULL) {
                    riposizionaTutti(is_open_cassa, id, conf->n_casse, n_clienti_cambiati);
                }
            }
            SYSCALL(err, pthread_cond_wait(&(n_clienti_cambiati[id]), &mtx_coda_in_cassa), "wait");
        }
        while(is_open_cassa[id].queue_len == 0) {
            if(!statoprecedente) {
                SYSCALL(err, clock_gettime(CLOCK_MONOTONIC, &apertura), "clock_gettime");
                //printf("cassa %li in apertura\n", id);
                statoprecedente = true;
                SYSCALL(err, pthread_mutex_lock(&mtx_invio_informazioni), "lock");
                casse_aperte_comm_k[id] = true;
                SYSCALL(err, pthread_cond_signal(&(cassa_aperta[id])), "signal");
                SYSCALL(err, pthread_mutex_unlock(&mtx_invio_informazioni), "unlock");
            }
            SYSCALL(err, pthread_cond_wait(&(n_clienti_cambiati[id]), &mtx_coda_in_cassa), "wait");
            if(fuori_da_ciclo_dir && is_open_cassa[id].lPtr == NULL && (casse_aperte(is_open_cassa, conf->n_casse) > 1 || (clienti_in + clienti_vivi_generati) == 0)) {
                is_open_cassa[id].queue_len = -1;
            }
        }
        while(is_open_cassa[id].lPtr != NULL && !sigquit) {
            if(!statoprecedente) {
                SYSCALL(err, clock_gettime(CLOCK_MONOTONIC, &apertura), "clock_gettime");
                //printf("cassa %li in apertura\n", id);
                statoprecedente = true;
                SYSCALL(err, pthread_mutex_lock(&mtx_invio_informazioni), "lock");
                casse_aperte_comm_k[id] = true;
                SYSCALL(err, pthread_cond_signal(&(cassa_aperta[id])), "signal");
                SYSCALL(err, pthread_mutex_unlock(&mtx_invio_informazioni), "unlock");
            }
            node = is_open_cassa[id].lPtr;
            prodotti = node->prodotti;
            id_cliente = node->id;
            service_id_c = node->service_id;
            t.tv_sec = 0;
            t.tv_nsec = r;
            //tempo fisso
            SYSCALL(err, nanosleep(&t, NULL), "nanosleep");
            t.tv_nsec = (prodotti * conf->t_per_prod) * 1000000;
            t.tv_sec = t.tv_nsec / 1000000000;
            t.tv_nsec = t.tv_nsec % 1000000000;
            //tempo variabile
            SYSCALL(err, nanosleep(&t, NULL), "nanosleep");
            SYSCALL(err, pthread_mutex_lock(&mtx_file_tempo_servizio_cliente), "lock");

            //I TEMPI SONO SBAGLIATI

            fprintf(fd_tempo_servizio, "%li %li %li\n", id, id_cliente, (r + t.tv_nsec) / 1000000);
            SYSCALL(err, pthread_mutex_unlock(&mtx_file_tempo_servizio_cliente), "unlock");
            congedaCliente(id, is_open_cassa, id_cliente);
            clienti_serviti[id]++;

            SYSCALL(err, pthread_mutex_lock(&mtx_servizio_clienti), "lock");
            servizio_effettuato[service_id_c] = true;
            SYSCALL(err, pthread_cond_signal(&(cliente_servito[service_id_c])), "signal");
            SYSCALL(err, pthread_mutex_unlock(&mtx_servizio_clienti), "unlock");
        }
        SYSCALL(err, pthread_mutex_unlock(&mtx_coda_in_cassa), "unlock");
    }

    if(statoprecedente) {
        SYSCALL(err, clock_gettime(CLOCK_MONOTONIC, &chiusura), "clock_gettime");
        long sec = chiusura.tv_sec - apertura.tv_sec;
        long nsec = chiusura.tv_nsec - apertura.tv_nsec;
        if(nsec < 0) { //gestisco underflow dei nanosecondi
            sec--;
            nsec = nsec + 1000000000;
        }
        //printf("cassa %li in chiusura\n", id);
        SYSCALL(err, pthread_mutex_lock(&mtx_file_tempi_aperture_casse), "lock");
        fprintf(fd_tempi_aperture, "%li %li.%li\n", id, sec, (nsec / 1000000));  //precisione fino ai millisecondi
        SYSCALL(err, pthread_mutex_unlock(&mtx_file_tempi_aperture_casse), "unlock");
        statoprecedente = false;
        is_open_cassa[id].queue_len = -1;
        chiusure_cassa[id]++;
    }
    SYSCALL(err, pthread_mutex_lock(&mtx_invio_informazioni), "lock");
    casse_aperte_comm_k[id] = false;
    SYSCALL(err, pthread_cond_signal(&(cassa_aperta[id])), "signal");
    SYSCALL(err, pthread_mutex_unlock(&mtx_invio_informazioni), "unlock");
    SYSCALL(err, pthread_join(tid_comm, NULL), "join");
    return NULL;
}

static void* cliente(void* arg) {
    int err;
    struct timespec entrata, uscita, inizio_coda, fine_coda;
    SYSCALL(err, pthread_mutex_lock(&mtx_dir_client), "lock");
    clienti_vivi_generati--;
    clienti_in++;
    SYSCALL(err, clock_gettime(CLOCK_MONOTONIC, &entrata), "clock_gettime");
    SYSCALL(err, pthread_mutex_unlock(&mtx_dir_client), "unlock");
    unsigned int rand_id = ((unsigned int)arg);
    long id = (long)arg;
    //printf("cliente %li entra nel supermercato\n", id);

    //uscita immediata
    if(sigquit) {
        SYSCALL(err, pthread_mutex_lock(&mtx_dir_client), "lock");
        clienti_in--;
        SYSCALL(err, clock_gettime(CLOCK_MONOTONIC, &uscita), "clock_gettime");
        if(clienti_in == 0) {
            //notifico al direttore che non ci sono più clienti nel supermercato
            SYSCALL(err, pthread_cond_signal(&is_empty), "signal");
        }
        SYSCALL(err, pthread_mutex_unlock(&mtx_dir_client), "unlock");
        long sec = uscita.tv_sec - entrata.tv_sec;
        long nsec = uscita.tv_nsec - entrata.tv_nsec;
        if(nsec < 0) { //gestisco underflow dei nanosecondi
            sec--;
            nsec = nsec + 1000000000;
        }
        SYSCALL(err, pthread_mutex_lock(&mtx_file_info_clienti), "lock");
        fprintf(fd_info_clienti, "%li %li.%li 0.0 0\n", id, sec, (nsec / 1000000));
        SYSCALL(err, pthread_mutex_unlock(&mtx_file_info_clienti), "unlock");
        //printf("cliente %li esce dal supermercato, %d\n", id, (clienti_in + clienti_vivi_generati));
        return NULL;
    }

    SYSCALL(err, pthread_mutex_lock(&mtx_dir_client), "lock");
    inserisciCliente(is_in_client, id, conf->max_clienti);
    SYSCALL(err, pthread_mutex_unlock(&mtx_dir_client), "unlock");
    //FASE SCELTA PRODOTTI
    long r = (rand_r(&rand_id) % (conf->T - minT) + minT) * 1000000; //converto da ms a ns
    long p = rand_r(&rand_id) % conf->max_prodotti_per_cliente;
    long t_sec = r / 1000000000;
    long t_nsec = r % 1000000000;
    struct timespec t = {t_sec, t_nsec};
    //compra i prodotti
    SYSCALL(err, nanosleep(&t, NULL), "nanosleep");

    //uscita immediata dopo aver scelto i prodotti
    if(sigquit) {
        SYSCALL(err, pthread_mutex_lock(&mtx_dir_client), "lock");
        cancellaCliente(is_in_client, id, conf->max_clienti);
        clienti_in--;
        SYSCALL(err, clock_gettime(CLOCK_MONOTONIC, &uscita), "clock_gettime");
        if(clienti_in == 0) {
            //notifico al direttore che non ci sono più clienti nel supermercato
            SYSCALL(err, pthread_cond_signal(&is_empty), "signal");
        }
        SYSCALL(err, pthread_mutex_unlock(&mtx_dir_client), "unlock");
        long sec = uscita.tv_sec - entrata.tv_sec;
        long nsec = uscita.tv_nsec - entrata.tv_nsec;
        if(nsec < 0) { //gestisco underflow dei nanosecondi
            sec--;
            nsec = nsec + 1000000000;
        }
        SYSCALL(err, pthread_mutex_lock(&mtx_file_info_clienti), "lock");
        fprintf(fd_info_clienti, "%li %li.%li 0.0 0\n", id, sec, (nsec / 1000000));
        SYSCALL(err, pthread_mutex_unlock(&mtx_file_info_clienti), "unlock");
        //printf("cliente %li esce dal supermercato, %d\n", id, (clienti_in + clienti_vivi_generati));
        return NULL;
    }

    if(p == 0) {
        SYSCALL(err, pthread_mutex_lock(&mtx_clienti_senza_prodotti), "lock");
        inserisci(&testa_clienti_autorizzazione_uscire, &coda_clienti_autorizzazione_uscire, id);
        //manda segnale al direttore
        SYSCALL(err, pthread_kill(tid_sig_hand_dir, SIGINT), "pthread_kill");
        while(cerca(testa_clienti_autorizzazione_uscire, id)) {
            SYSCALL(err, pthread_cond_wait(&attesa_autorizzazione, &mtx_clienti_senza_prodotti), "wait");
        }
        SYSCALL(err, pthread_mutex_unlock(&mtx_clienti_senza_prodotti), "unlock");
        SYSCALL(err, pthread_mutex_lock(&mtx_dir_client), "lock");
        cancellaCliente(is_in_client, id, conf->max_clienti);
        clienti_in--;
        SYSCALL(err, clock_gettime(CLOCK_MONOTONIC, &uscita), "clock_gettime");
        if(clienti_in == 0) {
            //notifico al direttore che non ci sono più clienti nel supermercato
            SYSCALL(err, pthread_cond_signal(&is_empty), "signal");
        }
        SYSCALL(err, pthread_mutex_unlock(&mtx_dir_client), "unlock");
        long sec = uscita.tv_sec - entrata.tv_sec;
        long nsec = uscita.tv_nsec - entrata.tv_nsec;
        if(nsec < 0) { //gestisco underflow dei nanosecondi
            sec--;
            nsec = nsec + 1000000000;
        }
        SYSCALL(err, pthread_mutex_lock(&mtx_file_info_clienti), "lock");
        fprintf(fd_info_clienti, "%li %li.%li 0.0 0\n", id, sec, (nsec / 1000000));
        SYSCALL(err, pthread_mutex_unlock(&mtx_file_info_clienti), "unlock");
        //printf("cliente %li esce dal supermercato, %d\n", id, (clienti_in + clienti_vivi_generati));
        return NULL;
    }
    //ottengo l'identificatore di servizio
    SYSCALL(err, pthread_mutex_lock(&mtx_service_id_c), "lock");
    int service_id = id % conf->max_clienti;
    while(!service_id_available[service_id]) {
        service_id = (service_id + 1) % conf->max_clienti;
    }
    service_id_available[service_id] = false;
    SYSCALL(err, pthread_mutex_unlock(&mtx_service_id_c), "unlock");

    int cassa = rand_r(&rand_id) % conf->n_casse; //scelgo randomicamente la cassa a cui accodarmi
    SYSCALL(err, pthread_mutex_lock(&mtx_coda_in_cassa), "lock");
    //termina prima di mettersi in coda alla cassa
    if(sigquit) {
        SYSCALL(err, pthread_mutex_unlock(&mtx_coda_in_cassa), "unlock");
        SYSCALL(err, pthread_mutex_lock(&mtx_dir_client), "lock");
        cancellaCliente(is_in_client, id, conf->max_clienti);
        clienti_in--;
        SYSCALL(err, clock_gettime(CLOCK_MONOTONIC, &uscita), "clock_gettime");
        if(clienti_in == 0) {
            //notifico al direttore che non ci sono più clienti nel supermercato
            SYSCALL(err, pthread_cond_signal(&is_empty), "signal");
        }
        SYSCALL(err, pthread_mutex_unlock(&mtx_dir_client), "unlock");
        long sec = uscita.tv_sec - entrata.tv_sec;
        long nsec = uscita.tv_nsec - entrata.tv_nsec;
        if(nsec < 0) { //gestisco underflow dei nanosecondi
            sec--;
            nsec = nsec + 1000000000;
        }
        SYSCALL(err, pthread_mutex_lock(&mtx_file_info_clienti), "lock");
        fprintf(fd_info_clienti, "%li %li.%li 0.0 0\n", id, sec, (nsec / 1000000));
        SYSCALL(err, pthread_mutex_unlock(&mtx_file_info_clienti), "unlock");
        //printf("cliente %li esce dal supermercato, %d\n", id, (clienti_in + clienti_vivi_generati));
        return NULL;
    }
    int ind = -1;
    int factor;
    while(is_open_cassa[cassa].queue_len < 0) {
        //controllo sulle più vicine finchè non ne trovo una aperta
        factor = cassa + ind;
        factor = (factor >= 0) ? factor : conf->n_casse + factor; //per evitare di avere numeri sotto lo 0
        cassa = factor % conf->n_casse; //uso is_open_cassa come array circolare
        ind = (abs(ind) + 1) * (-1 * (ind / abs(ind))); //-1, 2, -3, 4, -5, 6, ...
    }
    accodaCliente(id, p, is_open_cassa, cassa, service_id);
    SYSCALL(err, clock_gettime(CLOCK_MONOTONIC, &inizio_coda), "clock_gettime");
    SYSCALL(err, pthread_cond_signal(&(n_clienti_cambiati[cassa])), "signal");
    SYSCALL(err, pthread_mutex_unlock(&mtx_coda_in_cassa), "unlock");

    SYSCALL(err, pthread_mutex_lock(&mtx_servizio_clienti), "lock");
    while(!servizio_effettuato[service_id]) {
        SYSCALL(err, pthread_cond_wait(&(cliente_servito[service_id]), &mtx_servizio_clienti), "wait");
    }
    servizio_effettuato[service_id] = false;
    SYSCALL(err, pthread_mutex_unlock(&mtx_servizio_clienti), "unlock");
    SYSCALL(err, pthread_mutex_lock(&mtx_service_id_c), "lock");
    service_id_available[service_id] = true;
    SYSCALL(err, pthread_mutex_unlock(&mtx_service_id_c), "unlock");
    //FASE CODA IN CASSA
    SYSCALL(err, clock_gettime(CLOCK_MONOTONIC, &fine_coda), "clock_gettime");
    prod_acquistati = prod_acquistati + p;

    SYSCALL(err, pthread_mutex_lock(&mtx_dir_client), "lock");
    cancellaCliente(is_in_client, id, conf->max_clienti);
    clienti_in--;
    SYSCALL(err, clock_gettime(CLOCK_MONOTONIC, &uscita), "clock_gettime");
    if(clienti_in == 0) {
        //notifico al direttore che non ci sono più clienti nel supermercato
        SYSCALL(err, pthread_cond_signal(&is_empty), "signal");
    }
    SYSCALL(err, pthread_mutex_unlock(&mtx_dir_client), "unlock");
    long sec = uscita.tv_sec - entrata.tv_sec;
    long nsec = uscita.tv_nsec - entrata.tv_nsec;
    long sec_coda = fine_coda.tv_sec - inizio_coda.tv_sec;
    long nsec_coda = fine_coda.tv_nsec - inizio_coda.tv_nsec;
    if(nsec < 0) { //gestisco underflow dei nanosecondi
        sec--;
        nsec = nsec + 1000000000;
    }
    if(nsec_coda < 0) {
        sec_coda--;
        nsec_coda = nsec_coda + 1000000000;
    }
    SYSCALL(err, pthread_mutex_lock(&mtx_file_info_clienti), "lock");
    fprintf(fd_info_clienti, "%li %li.%li %li.%li %li\n", id, sec, (nsec / 1000000), sec_coda, (nsec_coda/1000000), p);
    SYSCALL(err, pthread_mutex_unlock(&mtx_file_info_clienti), "unlock");
    //printf("cliente %li esce dal supermercato, %d\n", id, (clienti_in + clienti_vivi_generati));
    return NULL;
}

static void* signal_handler_dir(void* arg) {
    tid_sig_hand_dir = pthread_self();
    int err, sig;
    sigset_t* set = (sigset_t*)arg;
    while(ciclo_dir || (clienti_in + clienti_vivi_generati) > 0) {
        SYSCALL(err, sigwait(set, &sig), "sigwait");
        switch(sig) {
            case SIGQUIT:
                sigquit = true;
                ciclo_dir = false;
                break;
            case SIGHUP:
                ciclo_dir = false;
                break;
            case SIGINT:
                SYSCALL(err, pthread_mutex_lock(&mtx_clienti_senza_prodotti), "lock");
                cancella(&testa_clienti_autorizzazione_uscire, &coda_clienti_autorizzazione_uscire);
                SYSCALL(err, pthread_cond_signal(&attesa_autorizzazione), "signal");
                SYSCALL(err, pthread_mutex_unlock(&mtx_clienti_senza_prodotti), "unlock");
                break;
            case SIGALRM:
                SYSCALL(err, pthread_mutex_lock(&mtx_dir_apri_chiudi_cassa), "lock");
                apri_chiudi_cassa = true;
                SYSCALL(err, pthread_mutex_unlock(&mtx_dir_apri_chiudi_cassa), "unlock");
                break;
            default: ;
        }
    }
    SYSCALL(err, pthread_mutex_lock(&mtx_dir_apri_chiudi_cassa), "lock");
    tid_sig_hand_dir = 0;
    SYSCALL(err, pthread_mutex_unlock(&mtx_dir_apri_chiudi_cassa), "unlock");
    return NULL;
}

static void* direttore(void* arg) {
    int err;
    sigset_t mask, oldmask;
    SYSCALL(err, sigemptyset(&mask), "sigemptyset");
    SYSCALL(err, sigaddset(&mask, SIGQUIT), "sigaddset");
    SYSCALL(err, sigaddset(&mask, SIGHUP), "sigaddset");
    SYSCALL(err, sigaddset(&mask, SIGALRM), "sigaddset");
    SYSCALL(err, sigaddset(&mask, SIGINT), "sigaddset");
    SYSCALL(err, pthread_sigmask(SIG_BLOCK, &mask, &oldmask), "pthread_sigmask");
    pthread_attr_t attr;
    pthread_t id_sig_handler;
    SYSCALL(err, pthread_attr_init(&attr), "attr_init");
    SYSCALL(err, pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED), "set_detach_state");
    SYSCALL(err, pthread_create(&id_sig_handler, NULL, signal_handler_dir, (void*)&mask), "pthread_create");
    SYSCALL(err, pthread_sigmask(SIG_SETMASK, &oldmask, NULL), "pthread_sigmask");

    pthread_t* id_cliente = malloc(sizeof(pthread_t) * conf->max_clienti);
    pthread_t* tid_casse = malloc(sizeof(pthread_t) * conf->n_casse);
    if(tid_casse == NULL || id_cliente == NULL) {
        fprintf(stderr, "memoria esaurita!\n");
    }
    //ordino alle prime casse di iniziare a lavorare
    for(long i = 0; i < conf->k_init; i++) {
        SYSCALL(err, pthread_create(&tid_casse[i], NULL, cassa, (void*)i), "pthread_create");
        SYSCALL(err, pthread_mutex_lock(&mtx_coda_in_cassa), "lock");
        is_open_cassa[i].queue_len = 0;
        SYSCALL(err, pthread_mutex_unlock(&mtx_coda_in_cassa), "unlock");
    }
    //creo i thread delle altre casse ma non entrano in funzione
    for(long i = conf->k_init ; i < conf->n_casse ; i++) {
        SYSCALL(err, pthread_create(&tid_casse[i], NULL, cassa, (void*)i), "pthread_create");
    }

    CommQueue nodo_comunicazione;
    int* clienti_in_coda = malloc(sizeof(int) * conf->n_casse);
    if(clienti_in_coda == NULL) {
        fprintf(stderr, "Memoria esaurita!\n");
        return NULL;
    }
    for(int i = 0 ; i < conf->k_init ; i++) {
        clienti_in_coda[i] = 0;
    }
    for(int i = conf->k_init ; i < conf->n_casse ; i++) {
        clienti_in_coda[i] = -1;
    }
    while(ciclo_dir) {
        //check casse
        SYSCALL(err, pthread_mutex_lock(&mtx_dir_apri_chiudi_cassa), "lock");
        if(apri_chiudi_cassa) {
            SYSCALL(err, pthread_mutex_unlock(&mtx_dir_apri_chiudi_cassa), "unlock");
            SYSCALL(err, pthread_mutex_lock(&mtx_comm_cassa), "lock");
            nodo_comunicazione = dequeue(&testa, &coda);
            SYSCALL(err, pthread_mutex_unlock(&mtx_comm_cassa), "unlock");
            clienti_in_coda[nodo_comunicazione.id_cassa] = nodo_comunicazione.num_clienti;
            if(casse_sotto_soglia(clienti_in_coda, conf->n_casse) >= conf->s1 && casse_aperte(is_open_cassa, conf->n_casse) > 1) {
                SYSCALL(err, pthread_mutex_lock(&mtx_coda_in_cassa), "lock");
                is_open_cassa[nodo_comunicazione.id_cassa].queue_len = -1;
                SYSCALL(err, pthread_cond_signal(&(n_clienti_cambiati[nodo_comunicazione.id_cassa])), "signal");
                SYSCALL(err, pthread_mutex_unlock(&mtx_coda_in_cassa), "unlock");
            } else if(nodo_comunicazione.num_clienti >= conf->s2 && casse_aperte(is_open_cassa, conf->n_casse) < conf->n_casse) {
                int cassa_da_aprire = conf->n_casse - 1;
                SYSCALL(err, pthread_mutex_lock(&mtx_coda_in_cassa), "lock");
                while(cassa_da_aprire >= 0 && is_open_cassa[cassa_da_aprire].queue_len >= 0) {
                    cassa_da_aprire--;
                }
                if(cassa_da_aprire >= 0) {
                    is_open_cassa[cassa_da_aprire].queue_len = 0;
                    SYSCALL(err, pthread_cond_signal(&(n_clienti_cambiati[cassa_da_aprire])), "signal");
                }
                SYSCALL(err, pthread_mutex_unlock(&mtx_coda_in_cassa), "unlock");
            }
            SYSCALL(err, pthread_mutex_lock(&mtx_dir_apri_chiudi_cassa), "lock");
            apri_chiudi_cassa = false;
        }
        SYSCALL(err, pthread_mutex_unlock(&mtx_dir_apri_chiudi_cassa), "unlock");

        SYSCALL(err, pthread_mutex_lock(&mtx_dir_client), "lock");
        //generazione clienti
        if((clienti_in + clienti_vivi_generati) <= conf->max_clienti - conf->E) {
            for(int i = 0; i < conf->E; i++) {
                SYSCALL(err, pthread_create(&id_cliente[clienti_in + i], &attr, cliente, (void*)index_client), "pthread_create");
                clienti_vivi_generati++;
                index_client++;
            }
        }
        SYSCALL(err, pthread_mutex_unlock(&mtx_dir_client), "unlock");
    }
    fuori_da_ciclo_dir = true;
    //puts("supermercato in chiusura!");

    //aspetto che tutti i clienti siano fuori dal supermercato
    SYSCALL(err, pthread_mutex_lock(&mtx_dir_client), "lock");
    while(clienti_in != 0) {
        SYSCALL(err, pthread_cond_wait(&is_empty, &mtx_dir_client), "wait");
    }
    SYSCALL(err, pthread_mutex_unlock(&mtx_dir_client), "unlock");

    //aspetto chiusura di tutte le casse
    for(int i = 0 ; i < conf->n_casse ; i++) {
        //sveglio la cassa e aspetto che finisca
        SYSCALL(err, pthread_mutex_lock(&mtx_coda_in_cassa), "lock");
        SYSCALL(err, pthread_cond_signal(&(n_clienti_cambiati[i])), "signal");
        SYSCALL(err, pthread_mutex_unlock(&mtx_coda_in_cassa), "unlock");
        SYSCALL(err, pthread_join(tid_casse[i], NULL), "join");
    }

    SYSCALL(err, pthread_mutex_lock(&mtx_dir_apri_chiudi_cassa), "lock");
    //se il signal handler rimane sospeso sulla sigwait lo "sveglio" per farlo terminare
    if(tid_sig_hand_dir != 0) {
        SYSCALL(err, pthread_kill(tid_sig_hand_dir, SIGHUP), "pthread_kill");
    }
    SYSCALL(err, pthread_mutex_unlock(&mtx_dir_apri_chiudi_cassa), "unlock");
    //join del signal handler
    SYSCALL(err, pthread_join(id_sig_handler, NULL), "join");
    free(clienti_in_coda);
    free(tid_casse);
    free(id_cliente);
    return NULL;
}

int main(int argc, char* argv[]) {
    if(argc != 1 && argc != 3) {
        fprintf(stderr, "Use: %s\n", argv[0]);
        fprintf(stderr, "  or %s -c name_conf_file.txt\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //GESTIONE SEGNALI
    sigset_t mask;
    //creo maschera per il thread
    sigemptyset(&mask);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGHUP);
    if(pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0) {
        fprintf(stderr, "fatal error\n");
        exit(EXIT_FAILURE);
    }

    //PARTE DI CONFIGURAZIONE
    unsigned int size = strlen(FILE_CONF) + 1; //per sapere la quantità di memeria da allocare
    _Bool namedefined = false;
    if(argc == 3) { //è stato definito un file di configurazione
        if(strcmp("-c", argv[1]) != 0) {
            fprintf(stderr, "Unrecognized option %s\n", argv[1]);
            exit(EXIT_FAILURE);
        } else {
            size = strlen(argv[2]) + 1;
            namedefined = true;
        }
    }
    char* nomefileconf = malloc(sizeof(char) * size);
    if(nomefileconf == NULL) {
        perror("malloc");
    }
    if(!namedefined) {
        //nome del file di configurazione è quello di default
        strncpy(nomefileconf, FILE_CONF, size);
    } else {
        //acquisisco nome del file di configurazione
        strncpy(nomefileconf, argv[2], size);
    }

    FILE* fd_config = fopen(nomefileconf, "r");
    if(fd_config == NULL) {
        fprintf(stderr, "Configuration error\n");
        free(nomefileconf);
        exit(EXIT_FAILURE);
    }
    conf = malloc(sizeof(struct configurazione));
    conf->log_file_name = malloc(sizeof(char) * FILE_CONF_NAME_MAXLEN + 1);
    if(conf == NULL || conf->log_file_name == NULL) {
        perror("malloc");
        fclose(fd_config);
        free(nomefileconf);
        exit(errno);
    }
    //leggo tutti i valori di caonfigurazione
    fscanf(fd_config, "%s%d%d%d%d%d%d%d%d%d%d", conf->log_file_name, &conf->n_casse, &conf->max_clienti, &conf->E, &conf->T, &conf->max_prodotti_per_cliente, &conf->k_init, &conf->t_per_prod, &conf->int_comm_k_direttore, &conf->s1, &conf->s2);
    fclose(fd_config);

    fd_tempi_aperture = fopen(FILE_APERTURE_CASSE, "w+");
    if(fd_tempi_aperture == NULL){
        fprintf(stderr, "Errore apertura file\n");
        free(conf->log_file_name);
        free(conf);
        free(nomefileconf);
        exit(errno);
    }
    fd_tempo_servizio = fopen(FILE_TEMPO_SERVIZIO, "w+");
    if(fd_tempo_servizio == NULL) {
        fprintf(stderr, "Errore apertura file\n");
        free(conf->log_file_name);
        free(conf);
        free(nomefileconf);
        fclose(fd_tempi_aperture);
        exit(errno);
    }
    fd_info_clienti = fopen(FILE_INFO_CLIENTI, "w+");
    if(fd_info_clienti == NULL) {
        fprintf(stderr, "Errore apertura file\n");
        free(conf->log_file_name);
        free(conf);
        free(nomefileconf);
        fclose(fd_tempi_aperture);
        fclose(fd_tempo_servizio);
        exit(errno);
    }

    if((is_in_client = malloc(sizeof(long) * conf->max_clienti)) == NULL) {
        perror("malloc");
        free(conf->log_file_name);
        free(conf);
        free(nomefileconf);
        fclose(fd_tempi_aperture);
        fclose(fd_tempo_servizio);
        fclose(fd_info_clienti);
        exit(errno);
    }
    if((cliente_servito = malloc(sizeof(pthread_cond_t) * conf->max_clienti)) == NULL) {
        perror("malloc");
        free(conf->log_file_name);
        free(conf);
        free(nomefileconf);
        free(is_in_client);
        fclose(fd_tempi_aperture);
        fclose(fd_tempo_servizio);
        fclose(fd_info_clienti);
        exit(errno);
    }
    if((service_id_available = malloc(sizeof(_Bool) * conf->max_clienti)) == NULL) {
        perror("malloc");
        free(conf->log_file_name);
        free(conf);
        free(nomefileconf);
        free(is_in_client);
        free(cliente_servito);
        fclose(fd_tempi_aperture);
        fclose(fd_tempo_servizio);
        fclose(fd_info_clienti);
        exit(errno);
    }
    if((servizio_effettuato = malloc(sizeof(_Bool) * conf->max_clienti)) == NULL) {
        perror("malloc");
        free(conf->log_file_name);
        free(conf);
        free(nomefileconf);
        free(is_in_client);
        free(cliente_servito);
        free(service_id_available);
        fclose(fd_tempi_aperture);
        fclose(fd_tempo_servizio);
        fclose(fd_info_clienti);
        exit(errno);
    }
    for(int i = 0 ; i < conf->max_clienti ; i++) {
        is_in_client[i] = -1;
        service_id_available[i] = true;
        servizio_effettuato[i] = false;
        if((pthread_cond_init(&(cliente_servito[i]), NULL)) != 0) {
            perror("pthread_cond_init");
            free(conf->log_file_name);
            free(conf);
            free(nomefileconf);
            free(is_in_client);
            free(cliente_servito);
            free(service_id_available);
            free(servizio_effettuato);
            fclose(fd_tempi_aperture);
            fclose(fd_tempo_servizio);
            fclose(fd_info_clienti);
            exit(errno);
        }
    }
    if((is_open_cassa = malloc(sizeof(Cassa) * conf->n_casse)) == NULL) {
        perror("malloc");
        free(conf->log_file_name);
        free(conf);
        free(nomefileconf);
        free(is_in_client);
        free(cliente_servito);
        free(service_id_available);
        fclose(fd_tempi_aperture);
        fclose(fd_tempo_servizio);
        fclose(fd_info_clienti);
        exit(errno);
    }
    if((tid_sig_hand_casse = malloc(sizeof(pthread_t) * conf->n_casse)) == NULL) {
        perror("malloc");
        free(conf->log_file_name);
        free(conf);
        free(nomefileconf);
        free(is_in_client);
        free(cliente_servito);
        free(service_id_available);
        free(servizio_effettuato);
        free(is_open_cassa);
        fclose(fd_tempi_aperture);
        fclose(fd_tempo_servizio);
        fclose(fd_info_clienti);
        exit(errno);
    }
    if((casse_aperte_comm_k = malloc(sizeof(_Bool) * conf->n_casse)) == NULL) {
        perror("malloc");
        free(nomefileconf);
        free(conf->log_file_name);
        free(conf);
        free(is_in_client);
        free(cliente_servito);
        free(service_id_available);
        free(is_open_cassa);
        free(tid_sig_hand_casse);
        fclose(fd_tempi_aperture);
        fclose(fd_tempo_servizio);
        fclose(fd_info_clienti);
        exit(errno);
    }
    if((chiusure_cassa = malloc(sizeof(int) * conf->n_casse)) == NULL) {
        perror("malloc");
        free(nomefileconf);
        free(conf->log_file_name);
        free(conf);
        free(is_in_client);
        free(cliente_servito);
        free(service_id_available);
        free(servizio_effettuato);
        free(is_open_cassa);
        free(tid_sig_hand_casse);
        free(casse_aperte_comm_k);
        fclose(fd_tempi_aperture);
        fclose(fd_tempo_servizio);
        fclose(fd_info_clienti);
        exit(errno);
    }
    if((clienti_serviti = malloc(sizeof(int) * conf->n_casse)) == NULL) {
        perror("malloc");
        free(nomefileconf);
        free(conf->log_file_name);
        free(conf);
        free(is_in_client);
        free(cliente_servito);
        free(service_id_available);
        free(servizio_effettuato);
        free(is_open_cassa);
        free(tid_sig_hand_casse);
        free(casse_aperte_comm_k);
        free(chiusure_cassa);
        fclose(fd_tempi_aperture);
        fclose(fd_tempo_servizio);
        fclose(fd_info_clienti);
        exit(errno);
    }
    if((n_clienti_cambiati = malloc(sizeof(pthread_cond_t) * conf->n_casse)) == NULL) {
        perror("malloc");
        free(nomefileconf);
        free(conf->log_file_name);
        free(conf);
        free(is_in_client);
        free(cliente_servito);
        free(service_id_available);
        free(servizio_effettuato);
        free(is_open_cassa);
        free(tid_sig_hand_casse);
        free(casse_aperte_comm_k);
        free(chiusure_cassa);
        free(clienti_serviti);
        fclose(fd_tempi_aperture);
        fclose(fd_tempo_servizio);
        fclose(fd_info_clienti);
        exit(errno);
    }
    if((cassa_aperta = malloc(sizeof(pthread_cond_t) * conf->n_casse)) == NULL) {
        perror("malloc");
        free(nomefileconf);
        free(conf->log_file_name);
        free(conf);
        free(is_in_client);
        free(cliente_servito);
        free(service_id_available);
        free(servizio_effettuato);
        free(is_open_cassa);
        free(tid_sig_hand_casse);
        free(casse_aperte_comm_k);
        free(chiusure_cassa);
        free(clienti_serviti);
        free(n_clienti_cambiati);
        fclose(fd_tempi_aperture);
        fclose(fd_tempo_servizio);
        fclose(fd_info_clienti);
        exit(errno);
    }
    for(int i = 0 ; i < conf->n_casse ; i++) {
        is_open_cassa[i].queue_len = -1;
        is_open_cassa[i].lPtr = NULL;
        tid_sig_hand_casse[i] = 0;
        casse_aperte_comm_k[i] = false;
        chiusure_cassa[i] = 0;
        clienti_serviti[i] = 0;
        if((pthread_cond_init(&(n_clienti_cambiati[i]), NULL)) != 0) {
            perror("pthread_cond_init");
            free(nomefileconf);
            free(conf->log_file_name);
            free(conf);
            free(is_in_client);
            free(cliente_servito);
            free(service_id_available);
            free(servizio_effettuato);
            free(is_open_cassa);
            free(tid_sig_hand_casse);
            free(casse_aperte_comm_k);
            free(chiusure_cassa);
            free(clienti_serviti);
            free(n_clienti_cambiati);
            free(cassa_aperta);
            fclose(fd_tempi_aperture);
            fclose(fd_tempo_servizio);
            fclose(fd_info_clienti);
            exit(errno);
        }
        if((pthread_cond_init(&(cassa_aperta[i]), NULL)) != 0) {
            perror("pthread_cond_init");
            free(nomefileconf);
            free(conf->log_file_name);
            free(conf);
            free(is_in_client);
            free(cliente_servito);
            free(service_id_available);
            free(servizio_effettuato);
            free(is_open_cassa);
            free(tid_sig_hand_casse);
            free(casse_aperte_comm_k);
            free(chiusure_cassa);
            free(clienti_serviti);
            free(n_clienti_cambiati);
            free(cassa_aperta);
            fclose(fd_tempi_aperture);
            fclose(fd_tempo_servizio);
            fclose(fd_info_clienti);
            exit(errno);
        }
    }

    //PARTE DEI THREAD
    pthread_t id;
    int status;
    //faccio partire il direttore
    if(pthread_create(&id, NULL, direttore, (void*)&mask) != 0) {
        perror("creating thread");
        free(nomefileconf);
        free(conf->log_file_name);
        free(conf);
        free(is_in_client);
        free(cliente_servito);
        free(service_id_available);
        free(servizio_effettuato);
        free(is_open_cassa);
        free(tid_sig_hand_casse);
        free(casse_aperte_comm_k);
        free(chiusure_cassa);
        free(clienti_serviti);
        free(n_clienti_cambiati);
        free(cassa_aperta);
        fclose(fd_tempi_aperture);
        fclose(fd_tempo_servizio);
        fclose(fd_info_clienti);
        exit(errno);
    }

    if(pthread_join(id, (void*)&status) != 0){
        perror("pthread_join");
    }

    //PARTE FILE DI LOG
    fflush(fd_tempi_aperture);
    fflush(fd_tempo_servizio);
    fflush(fd_info_clienti);
    ordinaFile(FILE_TEMPO_SERVIZIO);
    ordinaFile(FILE_INFO_CLIENTI);
    int clienti_serviti_tot = 0;
    for(int i = 0 ; i < conf->n_casse ; i++) {
        clienti_serviti_tot = clienti_serviti_tot + clienti_serviti[i];
    }
    FILE* fd_log = fopen(conf->log_file_name, "w");
    if(fd_log == NULL) {
        fprintf(stderr, "Errore apertura file\n");
        deallocQueue(&testa); //se ci sono messaggi che non sono stati ricevuti
        free(nomefileconf);
        free(conf->log_file_name);
        free(conf);
        free(is_in_client);
        free(cliente_servito);
        free(service_id_available);
        free(servizio_effettuato);
        free(is_open_cassa);
        free(tid_sig_hand_casse);
        free(casse_aperte_comm_k);
        free(chiusure_cassa);
        free(clienti_serviti);
        free(n_clienti_cambiati);
        free(cassa_aperta);
        fclose(fd_tempi_aperture);
        fclose(fd_tempo_servizio);
        fclose(fd_info_clienti);
        exit(errno);
    }
    fprintf(fd_log, "%d\n", clienti_serviti_tot);
    fprintf(fd_log, "%li\n", prod_acquistati);
    int id_c, p_a;
    float time_in_sm, time_in_queue;
    lseek(fileno(fd_info_clienti), 0, SEEK_SET); //ok perchè ho fatto fflush prima
    for(int i = 0 ; i < index_client ; i++) {
        fscanf(fd_info_clienti, "%d %f %f %d", &id_c, &time_in_sm, &time_in_queue, &p_a);
        fprintf(fd_log, "%d %.3f %.3f %d\n", id_c, time_in_sm, time_in_queue, p_a);
    }
    lseek(fileno(fd_tempi_aperture), 0, SEEK_SET);
    lseek(fileno(fd_tempo_servizio), 0, SEEK_SET);
    int id_k_tempi, id_k_servizio, time_servizio;
    float time_open;
    int j = 0;
    fscanf(fd_tempo_servizio, "%d", &id_k_servizio);
    for(int i = 0 ; i < conf->n_casse ; i++) {
        fprintf(fd_log, "%d %d %d\n", i, clienti_serviti[i], chiusure_cassa[i]);
        if(chiusure_cassa[i] > 0) {
            while(fscanf(fd_tempi_aperture, "%d %f", &id_k_tempi, &time_open) == 2) {
                if(id_k_tempi == i) {
                    fprintf(fd_log, "%.3f ", time_open);
                }
            }
            fprintf(fd_log, "\n");
            lseek(fileno(fd_tempi_aperture), 0, SEEK_SET);

            while(j < clienti_serviti_tot && id_k_servizio <= i) {
                fscanf(fd_tempo_servizio, " %d %d", &id_c, &time_servizio);
                fprintf(fd_log, "%d %d\n", id_c, time_servizio);
                j++;
                fscanf(fd_tempo_servizio, "%d", &id_k_servizio);
            }
        }
    }

    deallocQueue(&testa); //se ci sono messaggi che non sono stati ricevuti
    free(nomefileconf);
    free(conf->log_file_name);
    free(conf);
    free(is_in_client);
    free(cliente_servito);
    free(service_id_available);
    free(servizio_effettuato);
    free(is_open_cassa);
    free(tid_sig_hand_casse);
    free(casse_aperte_comm_k);
    free(chiusure_cassa);
    free(clienti_serviti);
    free(n_clienti_cambiati);
    free(cassa_aperta);
    fclose(fd_tempi_aperture);
    fclose(fd_tempo_servizio);
    fclose(fd_info_clienti);
    fclose(fd_log);
    //elimino i file temporanei
    if((remove(FILE_APERTURE_CASSE)) != 0) {
        perror("remove");
    }
    if((remove(FILE_TEMPO_SERVIZIO)) != 0) {
        perror("remove");
    }
    if((remove(FILE_INFO_CLIENTI)) != 0) {
        perror("remove");
    }
    return 0;
}

void ordinaFile(char* filename) {
    //puts(filename);
    int pid = fork();
    switch(pid) {
        case -1:
            perror("fork");
            exit(errno);
        case 0:
            execlp("sort", "sort", "-k", "1", "-n", "-o", filename, filename, (char*)NULL);
            perror("exec");
            exit(errno);
        default:
            if(waitpid(pid, NULL, 0) == -1) {
                perror("waitpid");
                return;
            }
            break;
    }
}