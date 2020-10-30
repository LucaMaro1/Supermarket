#include <stdio.h>
#include "data_structures.h"

extern int casse_sotto_soglia(const int* arr, int n) {
    int count = 0;
    for(int i = 0 ; i < n ; i++) {
        if(arr[i] == 0 || arr[i] == 1) {
            count++;
        }
    }
    return count;
}

int casse_aperte(Cassa* k_list, int n_casse) {
    int count = 0;
    for(int i = 0 ; i < n_casse ; i++) {
        if(k_list[i].queue_len >= 0)
            count++;
    }
    return count;
}