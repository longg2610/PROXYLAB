#include <stdio.h>
#include <string.h>
#include "csapp.h"
#include "sbuf.h"
#include "cache.h"

#define NTHREADS  4
#define SBUFSIZE  16

/* Maximum length of HTTP request fields*/
#define MAXHOSTLEN     256
#define MAXPORTLEN     6
#define MAXFILENAME    2048
#define MAXHEADER      64       /*max 64 headers in a request*/


/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

sbuf_t sbuf;    /*shared buffer of connected descriptors*/

void* thread(void* vargp);
void doit(int connfd);
int parse(int connfd, char* host, char* port, char* filename, char request_headers [] [MAXLINE]);


int main(int argc, char** argv)
{
    printf("%s", user_agent_hdr);

    /*establish proxy - client connection*/
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    listenfd = Open_listenfd(argv[1]);  /*port of proxy, specified on command line*/

    /*set up shared buffer and initialize threads*/
    pthread_t tid;  /*does not store tid so need not be an array of tid's*/
    sbuf_init(&sbuf, SBUFSIZE);
    for (int i = 0; i < NTHREADS; i++)
        Pthread_create(&tid, NULL, thread, NULL);

    /*initialize the cache*/
    init_cache();

    while (1)
    {
        clientlen = sizeof(struct sockaddr_storage);
        /*printf("Waiting for request from client(s)...\n");*/
        connfd = Accept(listenfd, (SA*) &clientaddr, &clientlen);
        sbuf_insert(&sbuf, connfd);  
    }

    return 0;
}

void* thread(void* vargp)
{
    Pthread_detach(Pthread_self());
    int connfd;
    while(1)
    {
        connfd = sbuf_remove(&sbuf);
        doit(connfd);        
        Close(connfd);
    }
}

void doit(int connfd)
{
    /*parse request from client. BUG: DO NOT REQUEST MAXLINE MEMORY, OVERFLOW THREAD'S STACK (KEEP THIS IN DOCUMENTATION)*/
    char host [MAXHOSTLEN] = "";
    char port [MAXPORTLEN] = "";
    char filename [MAXFILENAME] = "";  /*file name for GET requests (crazy how an uninitialized string had the shit out of me)*/
    char request_headers [MAXHEADER] [MAXLINE] = {'\0'}; 

    if(!parse(connfd, host, port, filename, request_headers)) 
    {
        /*printf("Malformed HTTP request\n");*/
        Close(connfd);
        return;
    }

    printf("Finding the object in cache...\n");

    /*do a cache read. if successful -> return*/
    char object_id [MAXHOSTLEN + MAXPORTLEN + MAXFILENAME + 1] = "";
    strcat(object_id, host);
    strcat(object_id, ":");
    strcat(object_id, port);
    strcat(object_id, filename);
    printf("objectid: %s\n", object_id);
    if(cache_read(object_id, connfd))
    {
        printf("Found object in cache, sent to client\n");
        return;
    }

    printf("Not found in cache, making request to server...\n");
    
    /*establish proxy -> server connection*/
    int clientfd;
    rio_t rio;
    clientfd = Open_clientfd(host, port);
    Rio_readinitb(&rio, clientfd);      /*all reads through clientfd now go through rio*/
    

    /*make forwarded request to server*/
    char forward [MAXLINE] = {'\0'};

    /*first line: request line*/
    strcat(forward, "GET ");
    strcat(forward, filename);
    strcat(forward, " HTTP/1.0\r\n");
    /*second line: Host header*/
    strcat(forward, "Host: ");
    strcat(forward, host);
    /*add port to host header*/
    strcat(forward, ":");
    strcat(forward, port);
    strcat(forward, "\r\n");

    /*third line: User-Agent header*/
    strcat(forward, user_agent_hdr);
    /*fourth line: Connection header*/
    strcat(forward, "Connection: close\r\n");
    /*fifth line: Proxy-Connection header*/
    strcat(forward, "Proxy-Connection: close\r\n");

    /*other headers*/
    int h = 0;
    while(request_headers[h][0] != '\0')
    {
        if(strncmp(request_headers[h], "Host:", 5) &&
            strncmp(request_headers[h], "Connection:", 11) && 
            strncmp(request_headers[h], "Proxy-Connection:", 17))
            strcat(forward, request_headers[h]);
        h += 1;
    }
    printf("Forwarded request is: \n%s", forward);

    /*forward request to server: write the HTTP request to clientfd*/
    Rio_writen(clientfd, forward, strlen(forward)); 
    
    /*receive response from server*/
    char* backward = Calloc(MAXLINE, sizeof(char));       /*this will need to be heap memory with cache*/
    int n = 0;

    char* cache_write_buf = Calloc(MAX_OBJECT_SIZE, sizeof(int));
    int size = 0; /*metadata+content*/
    int actual_size = 0;    /*content*/
    
    /*read metadata*/
    while((n = Rio_readlineb(&rio, backward, MAXLINE)) > 0)     /*BUG: only write back n characters read instead of MAXLINE*/ 
    {
        /*backward to client: write to connfd*/
        Rio_writen(connfd, backward, n);
        strncat(cache_write_buf, backward, n);
        size += n;
        if(!strcmp(backward, "\r\n"))
            break;
    }
    printf("Done reading metadata\n");

    /*only count cached web objects, not metadata*/
    while((n = Rio_readlineb(&rio, backward, MAXLINE)) > 0)
    {
        /*backward to client: write to connfd*/
        Rio_writen(connfd, backward, n);
        strncat(cache_write_buf, backward, n);
        size += n;
        actual_size += n;
    }

    /*write to the cache if object size does not exceed MAX_OBJECT_SIZE*/
    if (actual_size < MAX_OBJECT_SIZE)   
        cache_write(cache_write_buf, size, actual_size, object_id);

}

int parse(int connfd, char* host, char* port, char* filename, char request_headers [] [MAXLINE])
{
    char buf[MAXLINE];
    rio_t rio;

    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);  /*read request from client (in connfd) to request buffer*/

    char method [MAXLINE];
    char protocol [MAXLINE];
    char URI [MAXLINE];
    char version [MAXLINE];

    sscanf(buf, "%s %s %s", method, URI, version);

    if (strcmp (method, "GET") != 0) 
    {
        /*printf("Not a GET request\n");*/
        return 0;
    }

    /*parse URI*/
    int i = 0;
    int j = 0;

    while(j < 7)
    {
        protocol[j] = URI[i];
        i += 1;
        j += 1;
    }
    if(strcmp(protocol, "http://") != 0)
    {
        /*printf("%s\n", protocol);
        printf("Not an HTTP request\n");*/
        return 0;
    }

    /*printf("Done parsing protocol\n");*/

    j = 0;
    while(URI[i] != '/' && URI[i] != ':')
    {
        host[j] = URI[i];
        i += 1;
        j += 1;
    }
    
    /*printf("Done parsing Host: %s\n", host);*/

    /*optional port number specified*/
    j = 0;
    if(URI[i] == ':')
    {
        i += 1;
        while(URI[i] != '/')
        {
            port[j] = URI[i];
            i += 1;
            j += 1;
        }
    }
    /*use default port: 80*/
    else    
        strcat(port, "80");

    /*printf("Done parsing Port: %s \n", port);*/

    j = 0;
    while(URI[i] != '\0')
    {
        filename[j] = URI[i];
        i += 1;
        j += 1;
    }

    /*printf("Done parsing filename: %s \n", filename);*/

    /*printf("Version: %s\n", version);*/

    j = 0;
    Rio_readlineb(&rio, request_headers[j], MAXLINE);
    while(strcmp(request_headers[j], "\r\n"))
    {
        j += 1;
        Rio_readlineb(&rio, request_headers[j], MAXLINE);
    }

    /*printf("Done parsing request headers\n");*/

    return 1;
    
}