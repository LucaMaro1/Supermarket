CC		= gcc
INCLUDES	= -I ./lib
AR		= ar rvs
LIB		= -L ./lib -lsupermercato -lpthread
LIBDIR		= ./lib
FP_LIBS		= ./lib/cassa.o		 \
		  ./lib/cliente.o	 \
		  ./lib/direttore.o	 \
		  ./lib/data_structures.o
LIBS		= $(FP_LIBS:./lib/=)
CONF_FILE	= conf.txt
TARGETS		= supermercato

.PHONY: all clean test
# .SILENT:

%: %.c lib%.a
	$(CC) -Wall $(INCLUDES) $< -o $@ $(LIB)

all		: $(TARGETS)

libsupermercato.a: $(LIBS)
	$(AR) $@ $(FP_LIBS)
	mv $@ $(LIBDIR)

cassa.o:
	$(CC) -Wall $(LIBDIR)/cassa.c -c -o $@
	mv $@ $(LIBDIR)

cliente.o:
	$(CC) -Wall $(LIBDIR)/cliente.c -c -o $@
	mv $@ $(LIBDIR)

direttore.o:
	$(CC) -Wall $(LIBDIR)/direttore.c -c -o $@
	mv $@ $(LIBDIR)

data_structures.o:
	$(CC) -Wall $(LIBDIR)/data_structures.c -c -o $@
	mv $@ $(LIBDIR)

test: all
	echo "log.txt\n6\n50\n3\n200\n100\n2\n5\n1000\n2\n10" >> conf.txt
	./supermercato -c conf.txt &
	sleep 25
	killall -w -s SIGHUP supermercato
	cat log.txt
	# ./script.sh 6 50

clean:
	rm -f $(TARGETS) $(CONF_FILE)
	rm -f $(FP_LIBS) $(LIBDIR)/libsupermercato.a
