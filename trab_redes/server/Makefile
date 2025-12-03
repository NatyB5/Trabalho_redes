CC = gcc
CFLAGS = -O2 -Wall

all: meu_servidor

meu_servidor: src/http_server.c
	$(CC) $(CFLAGS) -o meu_servidor src/http_server.c

clean:
	rm -f meu_servidor
