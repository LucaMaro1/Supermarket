#!/bin/bash

exec 3<log.txt
read -u 3 clienti_serviti
read -u 3 prodotti_acquistati
i=-1
read -d ' ' -u 3 i_c
#stampo dati sui clienti
echo "Clienti:"
echo -e "ID\tP\tT_SUP\tT_CODA\tn_CODE"
while (( i_c>i )); do
	read -u 3 temp_line
	echo -e "$i_c\t${temp_line//' '/'\t'}"
	i=$i_c
	read -d ' ' -u 3 i_c
done
#stampo dati sulle casse
i_k=0
echo "Casse:"
echo -e "ID\tP\tC\tTtot[s]\tTmS[ms]\tn_CHIUSURE"
for ((i=0; i<$1 ; i++)); do
	tempo_tot_apertura=0
	tempo_tot_servizio=0
	if [[ $i>0 ]]; then #perché l'indice è stato già letto nel ciclo superiore
		read -d ' ' -u 3 i_k
	fi
	read -d ' ' -u 3 clienti_serviti
	read -d ' ' -u 3 n_chiusure
	read -u 3 prod_elaborati
	
	if [[ $n_chiusure>0 ]]; then
		#calcolo la somma dei tempi di apertura
		for ((j=0; j<$n_chiusure ; j++)); do
			read -d ' ' -u 3 t_apertura[$j]
			tempo_tot_apertura=$(echo $tempo_tot_apertura + ${t_apertura[$j]} | bc -l)
		done
		if [[ $clienti_serviti>0 ]]; then
			#calcolo la media dei tempi di servizio
			for ((j=0; j<$clienti_serviti ; j++)); do
				read -d ' ' -u 3 i_c
				read -u 3 t_servizio[$j]
				tempo_tot_servizio=$(echo "$tempo_tot_servizio + ${t_servizio[$j]}" | bc -l)
			done
			tempo_medio_servizio=$(echo "scale=3; $tempo_tot_servizio / $clienti_serviti" | bc -l)
		else
			tempo_medio_servizio=0
		fi
	else
		tempo_tot_apertura=0
		tempo_medio_servizio=0
	fi
	echo -e "$i_k\t$prod_elaborati\t$clienti_serviti\t$tempo_tot_apertura\t$tempo_medio_servizio\t$n_chiusure"
done
