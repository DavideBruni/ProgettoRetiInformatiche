all: dev serv
# make rule per il client
dev: device.o
	gcc -Wall device.c -o dev
# make rule per il server
serv: server.o
	gcc -Wall server.c -o serv
# pulizia dei file della compilazione (eseguito con ‘make clean’ da terminale)
clean:
	rm *o dev serv
