#include <stdio.h>
#include "csapp.h"
#include <string.h>

#define MAXHEADER 8192

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void doit(int connfd);
int parse(int connfd, char* host, char* port, char* filename, char request_headers [] [MAXLINE]);

int main(int argc, char** argv)
{
    printf("%s", user_agent_hdr);

    /*establish proxy - client connection*/
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char client_hostname [MAXLINE], client_port [MAXLINE];
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }


    listenfd = Open_listenfd(argv[1]);  /*port of proxy, specified on command line*/
    while (1)
    {
        clientlen = sizeof(struct sockaddr_storage);
        printf("Waiting for request from client(s)...\n");
        connfd = Accept(listenfd, (SA*) &clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);

        /*************************************************************************************************/ 
        
        /*parse request from client*/
        char host [MAXLINE] = "";
        char port [MAXLINE] = "";
        char filename [MAXLINE];  /*file name for GET requests*/
        char request_headers [MAXHEADER] [MAXLINE] = {'\0'}; 

        if(!parse(connfd, host, port, filename, request_headers)) 
        {
            printf("Malformed HTTP request\n");
            Close(connfd);
            continue;
        }

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
        Rio_writen(clientfd, forward, MAXLINE); 
        
        /*receive response from server*/
        char backward [MAXLINE] = {'\0'};
        while((Rio_readlineb(&rio, backward, MAXLINE)) > 0)    
        {
            /*backward to client: write to connfd*/
            Rio_writen(connfd, backward, MAXLINE);
        }
        
        printf("Backwarded to client, printing to terminal\n");
        /*Rio_readlineb(&rio, stdout, MAXLINE);*/
        

        /*************************************************************************************************/ 
        Close(connfd);
    }

    return 0;
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
        printf("Not a GET request\n");
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
        printf("%s\n", protocol);
        printf("Not an HTTP request\n");
        return 0;
    }

    printf("Done parsing protocol\n");

    j = 0;
    while(URI[i] != '/' && URI[i] != ':')
    {
        host[j] = URI[i];
        i += 1;
        j += 1;
    }
    
    printf("Done parsing Host: %s\n", host);

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

    printf("Done parsing Port: %s \n", port);

    j = 0;
    while(URI[i] != '\0')
    {
        filename[j] = URI[i];
        i += 1;
        j += 1;
    }

    printf("Done parsing filename: %s \n", filename);

    printf("Version: %s\n", version);

    j = 0;
    Rio_readlineb(&rio, request_headers[j], MAXLINE);
    while(strcmp(request_headers[j], "\r\n"))
    {
        j += 1;
        Rio_readlineb(&rio, request_headers[j], MAXLINE);
    }

    printf("Done parsing request headers\n");

    return 1;
    
}