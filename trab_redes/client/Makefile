CC = gcc
CFLAGS = -O2 -Wall

all: meu_navegador

meu_navegador: src/http_client.c
	$(CC) $(CFLAGS) -o meu_navegador src/http_client.c

clean:
	rm -f meu_navegador
