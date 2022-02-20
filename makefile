all: client server
client: client.o 
		gcc -Wall client.o -o dev -lpthread
server: server.o
		gcc -Wall server.o -o serv -lpthread
clean:
		rm *o client server