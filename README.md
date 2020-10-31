# Supermarket
"Operating System" project, University of Pisa, A.A. 2018-2019

Realizzare la simulazione di un sistema che modella un supermercato con K casse e
frequentato da un certo numero di clienti. Il supermercato è diretto da un direttore che ha facoltà di aprire e
chiudere una o più casse delle K totali (lasciandone attiva almeno una). Il numero dei clienti nel supermercato è
contingentato: non ci possono essere più di C clienti che fanno acquisti (o che sono in coda alle casse) in ogni
istante. All’inizio, tutti i clienti entrano contemporaneamente nel supermercato, successivamente, non appena il
numero dei clienti scende a C-E (0<E<C), ne vengono fatti entrare altri E. Ogni cliente spende un tempo
variabile T all’interno del supermercato per fare acquisti, quindi si mette in fila in una delle casse che sono in
quel momento aperte ed aspetta il suo turno per “pagare” la merce acquistata. Periodicamente, ogni cliente in
coda, controlla se gli conviene cambiare coda per diminuire il suo tempo di attesa. Infine esce dal supermercato.
Un cliente acquista fino a P>=0 prodotti. Ogni cassa attiva ha un cassiere che serve i clienti in ordine FIFO con
un certo tempo di servizio. Il tempo di servizio del cassiere ha una parte costante (diversa per ogni cassiere) più
una parte variabile che dipende linearmente dal numero di prodotti acquistati dal cliente che sta servendo.
I clienti che non hanno acquistato prodotti (P=0), non si mettono in coda alle casse, ma prima di uscire dal
supermercato devono attendere il permesso di uscire dal direttore.
