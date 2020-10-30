#include "data_structures.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

extern void enqueue(CommQueue** head, CommQueue** tail, long id_cassa, int val) {
    CommQueue* newPtr = malloc(sizeof(CommQueue));
    newPtr->id_cassa = id_cassa;
    newPtr->num_clienti = val;
    newPtr->next = NULL;
    if(newPtr == NULL) {
        perror("malloc");
        return;
    }
    //inserisce in coda
    if(*head == NULL) {
        *head = newPtr;
        *tail = newPtr;
        (*head)->next = NULL;
        (*tail)->next = NULL;

    } else if((*head)->next == NULL) {
        (*tail)->next = newPtr;
        (*tail) = newPtr;
        (*head)->next = (*tail);
    } else {
        (*tail)->next = newPtr;
        (*tail) = newPtr;
    }
}

extern CommQueue dequeue(CommQueue** head, CommQueue** tail) {
    CommQueue returnPtr;
    returnPtr.num_clienti = -1;
    returnPtr.id_cassa = -1;
    if(head == NULL) {
        return returnPtr;
    } else if((*head)->next == NULL) {
        CommQueue* tempPtr = (*head);
        returnPtr = (**head);
        *head = NULL;
        *tail = NULL;
        free(tempPtr);
    } else {
        CommQueue* tempPtr = (*head);
        returnPtr = (**head);
        (*head) = (*head)->next;
        free(tempPtr);
    }
    return returnPtr;
}

extern void deallocQueue(CommQueue** head) {
    CommQueue* cPtr = *head;
    while(cPtr != NULL) {
        CommQueue* tempPtr = cPtr;
        cPtr = cPtr->next;
        free(tempPtr);
    }
}

extern void inserisci(Coda** head, Coda** tail, long val) {
    Coda* newPtr = malloc(sizeof(CommQueue));
    newPtr->val = val;
    newPtr->next = NULL;
    if(newPtr == NULL) {
        perror("malloc");
        return;
    }
    //inserisce in coda
    if(*head == NULL) {
        *head = newPtr;
        *tail = newPtr;
        (*head)->next = NULL;
        (*tail)->next = NULL;

    } else if((*head)->next == NULL) {
        (*tail)->next = newPtr;
        (*tail) = newPtr;
        (*head)->next = (*tail);
    } else {
        (*tail)->next = newPtr;
        (*tail) = newPtr;
    }
}

extern long cancella(Coda** head, Coda** tail) {
    long returnItem;
    if(head == NULL) {
        return -1;
    } else if((*head)->next == NULL) {
        Coda * tempPtr = (*head);
        returnItem = (*head)->val;
        *head = NULL;
        *tail = NULL;
        free(tempPtr);
    } else {
        Coda * tempPtr = (*head);
        returnItem = (*head)->val;
        (*head) = (*head)->next;
        free(tempPtr);
    }
    return returnItem;
}

extern _Bool cerca(Coda* cPtr, long val) {
    while(cPtr != NULL) {
        if(cPtr->val == val)
            return true;
        cPtr = cPtr->next;
    }
    return false;
}