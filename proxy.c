#include <stdio.h>
#include <string.h>
#include "csapp.h"
#include "sbuf.h"
#include "cache.h"

#define NTHREADS  4
#define SBUFSIZE  16    /*max number of connected fd in work pool*/

/* Maximum length of HTTP request fields*/
#define MAXHOSTLEN     128
#define MAXPORTLEN     6
#define MAXFILENAME    512
#define MAXHEADERLEN   128
#define MAXHEADER      32       

/* Maximum length of fields in an HTTP request line*/
#define MAXMETHODLEN      8
#define MAXURILEN         512
#define MAXVERSIONLEN     16
#define MAXPROTOCOLLEN    16     

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
sbuf_t sbuf;    /*shared buffer of connected descriptors*/

void* thread(void* vargp);
void doit(int connfd);
int parse(int connfd, char* host, char* port, char* filename, char request_headers [] [MAXHEADERLEN]);
void sigpipe_handler(int sig);

/*
main:
1. Start listening for a connection
2. Initialize work pool, threads, and cache
3. Accept a connection by putting the connected fd (connfd) to work pool
*/
int main(int argc, char** argv)
{
    printf("\nProxy launched\n");
    Signal(SIGPIPE, sigpipe_handler);   /*install SIGPIPE handler*/

    /*1. Establish proxy - client connection*/
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    listenfd = Open_listenfd(argv[1]);  /*listen on port of proxy, specified on command line*/

    /*2. Initialize shared buffer*/ 
    sbuf_init(&sbuf, SBUFSIZE);

    /*initialize threads*/
    pthread_t tid;          /*does not store tid so need not be an array of tid's*/
    for (int i = 0; i < NTHREADS; i++)
        Pthread_create(&tid, NULL, thread, NULL);

    /*initialize cache*/
    init_cache();

    /*3. Infinite loop: accept a client request and add the connected descriptor to work pool*/
    while (1)   
    {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA*) &clientaddr, &clientlen);
        sbuf_insert(&sbuf, connfd);         /*add into work pool*/
    }
    return 0;
}

/*
worker threads:
1. Take work (a connected descriptor) from work pool
2. Service the client specified by the descriptor
3. Close the connection to the client
*/
void* thread(void* vargp)
{
    Signal(SIGPIPE, sigpipe_handler);   /*install SIGPIPE handler*/
    Pthread_detach(Pthread_self());     /*run in detached mode -> no need for joining*/
    while(1)
    {
        /*take one connfd from work pool, service, and close connection*/
        int connfd = sbuf_remove(&sbuf);
        doit(connfd);        
        Close(connfd);
        printf("Connection closed\n\n");
    }
}

/*
doit:
1. Parse client's request
2. Try to find object in the cache. If found, send it to client
3. Object not found. Make request to be forwarded to server
4. Connect to server and forward the request
5. Receive response from server
6. Write object from server to cache if size permits

Note: char arrays should not request too much memory, might overflow thread's stack
*/
void doit(int connfd)
{
    /*1. Parse request from client*/
    char host [MAXHOSTLEN] = {'\0'};
    char port [MAXPORTLEN] = {'\0'};
    char filename [MAXFILENAME] = {'\0'}; 
    char request_headers [MAXHEADER] [MAXHEADERLEN] = {'\0'};

    if(!parse(connfd, host, port, filename, request_headers))   /*parse error -> move on to next request*/
        return;


    /*2. Cache read, if successful -> send to client and move on*/
    char object_id [MAXHOSTLEN + MAXPORTLEN + MAXFILENAME + 1] = {'\0'};    /*+1 for the colon in host:port*/
    sprintf(object_id, "%s:%s%s", host, port, filename);
    if(cache_read(object_id, connfd))
    {
        printf("Found object in cache, sent to client\n");
        return;
    }


    /*web object not found in cache*/
    printf("Making request to server...\n");
    
    /*3. Construct forwarded request to server*/
    char forward [MAXLINE] = {'\0'};
    /*first line: request line*/
    strcat(forward, "GET ");
    strcat(forward, filename);
    strcat(forward, " HTTP/1.0\r\n");
    /*second line: Host header (including port)*/
    strcat(forward, "Host: ");
    strcat(forward, host);
    strcat(forward, ":");
    strcat(forward, port);
    strcat(forward, "\r\n");
    /*third line: User-Agent header*/
    strcat(forward, user_agent_hdr);
    /*fourth line: Connection header*/
    strcat(forward, "Connection: close\r\n");
    /*fifth line: Proxy-Connection header*/
    strcat(forward, "Proxy-Connection: close\r\n");

    /*other headers forwarded as-is*/
    int i = 0;
    while(request_headers[i][0] != '\0')
    {
        if(strncmp(request_headers[i], "Host:", 5) &&               /*pre-specified headers not forwarded*/
            strncmp(request_headers[i], "Connection:", 11) && 
            strncmp(request_headers[i], "Proxy-Connection:", 17))
            strcat(forward, request_headers[i]);
        i += 1;
    }
    printf("Forwarded request is: \n===========================\n%s===========================\n", forward);


    /*4. Establish proxy -> server connection*/
    int clientfd = Open_clientfd(host, port);
    rio_t rio;
    Rio_readinitb(&rio, clientfd);                      /*all reads through clientfd now go through rio*/
    Rio_writen(clientfd, forward, strlen(forward));     /*forward request to server: simply write the HTTP request to clientfd*/
    

    /*5. Receive response from server*/
    char* backward = Calloc(MAXLINE, sizeof(char));   /*buffer for server response*/  
    int n = 0;                                        /*count number of bytes read*/

    /*buffer for writing object to cache*/
    char* cache_write_buf = Calloc(MAX_OBJECT_SIZE, sizeof(char));
    int size = 0;           /*metadata + content*/
    int actual_size = 0;    /*pure content*/
    
    int offset = 0;         /*to use memcpy, which works with binary data*/

    /*read metadata*/
    while((n = Rio_readlineb(&rio, backward, MAXLINE)) > 0)     
    {
        /*backward to client: write to connfd*/
        Rio_writen(connfd, backward, n);                    /*Note: only write back n characters read instead of MAXLINE*/ 

        /*append to cached buffer*/
        memcpy(cache_write_buf + offset, backward, n);      
        offset += n;
        size += n;
        if(!strcmp(backward, "\r\n"))       /*end of metadata*/
            break;
    }
    printf("Received metadata from server\n");

    /*read web content, updating actual (pure) size*/
    while((n = Rio_readlineb(&rio, backward, MAXLINE)) > 0)
    {
        /*backward to client: write to connfd*/
        Rio_writen(connfd, backward, n);
        if(offset + n < MAX_OBJECT_SIZE)  /*only append to cached buffer if MAX_OBJECT_SIZE not exceeded*/
        {
            memcpy(cache_write_buf + offset, backward, n);
            offset += n;
        }
        size += n;
        actual_size += n;       /*actual size only updated based on pure content*/
    }
    printf("Received content from server\n");


    /*6. Write to cache if actual size does not exceed MAX_OBJECT_SIZE*/
    if (actual_size > 0 && actual_size < MAX_OBJECT_SIZE)
    {
        printf("Writing object of size %d into cache\n", actual_size);
        cache_write(cache_write_buf, size, actual_size, object_id);
    }   
}

/*
parse: 
1. Read request from client
2. Parse the request line
3. Read the headers
*/
int parse(int connfd, char* host, char* port, char* filename, char request_headers [] [MAXHEADERLEN])
{
    rio_t rio;
    Rio_readinitb(&rio, connfd);        /*all reads through connfd now go through rio*/


    /*1. Read request line from client*/
    char request_line[MAXLINE] = "";
    Rio_readlineb(&rio, request_line, MAXLINE);         /*read request from client (connfd) to request line buffer*/
    printf("Request line from client: %s\n", request_line);


    /*2. Parse the request line*/
    char method [MAXMETHODLEN] = "";        /*GET, POST, CONNECT, etc.*/
    char URI [MAXURILEN] = "";
    char protocol [MAXPROTOCOLLEN] = "";    /*http://, https://, etc*/
    char version [MAXVERSIONLEN] = "";      /*http/1.0, http/1.1, etc.*/

    sscanf(request_line, "%s %s %s", method, URI, version);
    if (strcmp (method, "GET") != 0)        /*only GET is implemented*/
    {
        printf("501 %s Not Implemented\n", method);
        return 0;
    }

    /*parse URI*/
    printf("URI from the request line: %s\n", URI);
    int i = 0;
    int j = 0;

    /*parse protocol (scheme)*/
    char* ptr = URI;
    if(strstr(ptr, "http://") == ptr)
    {
        i = strlen("http://");
        strcat(protocol, "http://");
    }
    else if(strstr(ptr, "https://") == ptr)
    {
        i = strlen("https://");
        strcat(protocol, "https://");
    }
    
    /*parse host*/
    while(URI[i] != '\0' && URI[i] != ':' && URI[i] != '/')
        host[j++] = URI[i++];
    
    /*parse port*/
    if(URI[i] == ':')   /*optional port number specified*/
    {
        i++;
        j = 0;
        while(URI[i] != '\0' && URI[i] != '/')
            port[j++] = URI[i++];
    }
    else                /*port number unspecified, use default port: 80*/
        strcat(port, "80");

    /*parse filename*/
    if(URI[i] == '\0')  /*filename unspecified, get root (/)*/
        strcat(filename, "/");  
    else
    {
        j = 0;
        while(URI[i] != '\0')
            filename[j++] = URI[i++];
    }
    

    /*3. Read request headers*/
    j = 0;
    Rio_readlineb(&rio, request_headers[j], MAXHEADERLEN);
    while(strcmp(request_headers[j], "\r\n"))
    {
        j++;
        Rio_readlineb(&rio, request_headers[j], MAXHEADERLEN);
    }
    printf("Number of request headers: %d\n", j-1);
    return 1;
}

void sigpipe_handler(int sig) 
{
    /*do nothing and return. implemented because the default handler will terminate the proxy*/
    return;
}
