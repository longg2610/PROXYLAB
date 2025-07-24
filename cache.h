#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define MAX_OBJECTID_LEN (256 + 6 + 2048 + 1)

/*Cache is a linked list of web objects (not contiguous)*/
typedef struct object_dat
{
    char* data;      /*points to heap memory containing web object data*/
    int size;          /*size of object*/
    char object_id [MAX_OBJECTID_LEN];  /*id the object by host+filename*/
    struct object_dat* prev;
    struct object_dat* next;
} object_dat;

/*for reads on cache: scan through object_dat queue, check if requested host+filename match any object_id
if matched -> use start-size to grab data, and move object_dat to front of queue*/
/*for write on cache: check if current used cache + object > max. If yes, evict last on queue, check again
and evict again if necessary until used cache + object < max. When used cache + object < max, malloc a new cache block and put
data on there.*/

extern void init_cache();
extern int cache_read(char* object_id, int connfd);
extern void cache_write(char* object, int size, char* object_id);
extern void evict();
extern int cacheFull();

#endif