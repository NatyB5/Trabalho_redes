/* 
   Compilar: gcc -o meu_navegador http_client.c
   executar: ./meu_navegador http://host[:port]/path
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#define BUF 8192

void parse_url(const char *url, char **host, char **port, char **path) {
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "https://", 8) == 0) {
        fprintf(stderr, "HTTPS not supported by this simple client\n");
        exit(1);
    }
    const char *slash = strchr(p, '/');
    if (!slash) {
        *path = strdup("/");
    } else {
        *path = strdup(slash);
    }
    size_t hostlen = (slash ? (size_t)(slash - p) : strlen(p));
    char *hostport = strndup(p, hostlen);
    char *colon = strchr(hostport, ':');
    if (colon) {
        *colon = 0;
        *host = strdup(hostport);
        *port = strdup(colon+1);
    } else {
        *host = strdup(hostport);
        *port = strdup("80");
    }
    free(hostport);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s http://host[:port]/path\n", argv[0]);
        return 1;
    }
    char *host=NULL, *port=NULL, *path=NULL;
    parse_url(argv[1], &host, &port, &path);

    struct addrinfo hints, *res, *rp;
    char portbuf[32];
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portbuf, sizeof(portbuf), "%s", port);
    if (getaddrinfo(host, portbuf, &hints, &res) != 0) {
        perror("getaddrinfo");
        return 1;
    }

    int sfd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd < 0) continue;
        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(sfd);
        sfd = -1;
    }
    if (sfd < 0) { perror("connect"); freeaddrinfo(res); return 1; }
    freeaddrinfo(res);

    char req[1024];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: meu_navegador/1.0\r\n\r\n",
             path, host);
    if (write(sfd, req, strlen(req)) < 0) { perror("write"); close(sfd); return 1; }

    
    FILE *sockf = fdopen(sfd, "rb");
    if (!sockf) { perror("fdopen"); close(sfd); return 1; }

    char line[BUF];
    int status_code = 0;
    size_t content_length = 0;
    int chunked = 0;

    if (!fgets(line, sizeof(line), sockf)) { fprintf(stderr,"erro lendo resposta\n"); fclose(sockf); return 1; }
    
    sscanf(line, "HTTP/%*s %d", &status_code);

    
    while (fgets(line, sizeof(line), sockf)) {
        if (strcmp(line, "\r\n") == 0 || strcmp(line, "\n") == 0) break;
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            content_length = strtoull(line+15, NULL, 10);
        } else if (strncasecmp(line, "Transfer-Encoding:", 18) == 0) {
            if (strcasestr(line, "chunked")) chunked = 1;
        }
    }

    if (status_code != 200) {
        fprintf(stderr, "HTTP error: %d\n", status_code);
        fclose(sockf);
        return 1;
    }

  
    char *slash = strrchr(path, '/');
    char fname[512];
    if (!slash || strlen(slash) == 1) strcpy(fname, "index.html");
    else snprintf(fname, sizeof(fname), "%s", slash+1);

    FILE *out = fopen(fname, "wb");
    if (!out) { perror("fopen out"); fclose(sockf); return 1; }

    if (content_length > 0) {
        size_t remaining = content_length;
        while (remaining) {
            char buf[BUF];
            size_t toread = remaining < sizeof(buf) ? remaining : sizeof(buf);
            size_t r = fread(buf,1,toread,sockf);
            if (r==0) break;
            fwrite(buf,1,r,out);
            remaining -= r;
        }
    } else if (chunked) {
        fprintf(stderr, "Chunked transfer not supported in this simple client\n");
    } else {

        char buf[BUF];
        size_t r;
        while ((r = fread(buf,1,sizeof(buf),sockf)) > 0) fwrite(buf,1,r,out);
    }

    printf("Salvo em %s\n", fname);
    fclose(out);
    fclose(sockf);
    free(host); free(port); free(path);
    return 0;
}
