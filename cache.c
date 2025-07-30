#include "cache.h"

sem_t mutex;       /*protects cache access*/
object_dat* head;  /*head sentinel node*/
object_dat* tail;  /*tail sentinel node*/
int cache_used;

/*private helper functions*/
void evict();
int cacheFull(int size);
void print_cache();

void init_cache()
{
    Sem_init(&mutex, 0, 1);
    cache_used = 0;

    /*head and tail points to the SENTINEL nodes*/
    head = Malloc(sizeof(struct object_dat));
    tail = Malloc(sizeof(struct object_dat));
    head->size = 0;
    tail->size = 0;
    
    head->prev = NULL;
    head->next = tail;
    
    tail->prev = head;
    tail->next = NULL;
}

int cache_read(char* object_id, int connfd)
{
    P(&mutex);

    object_dat* p = head->next;
    while(p->size != 0)
    {
        if(!strcmp(object_id, p->object_id))        /*found in cache*/
        {
            Rio_writen(connfd, p->data, p->size);   /*send back to client*/

            /*splice*/
            p->prev->next = p->next;
            p->next->prev = p->prev;
            
            /*relocate to head*/
            p->prev = head;
            p->next = head->next;
            p->next->prev = p;
            p->prev->next = p;

            V(&mutex);
            return 1;
        }
        p = p->next;
    }

    V(&mutex);
    return 0;
}

void cache_write(char* object, int size, int actual_size, char* object_id)
{
    P(&mutex);

    /*evict until there is space*/
    while(cacheFull(actual_size))   
        evict();

    /*make new cache block*/
    object_dat* new_object = Malloc(sizeof(struct object_dat));
    new_object->data = object;             
    new_object->size = size;
    new_object->actual_size = actual_size;
    strcpy(new_object->object_id, object_id);

    cache_used += actual_size;  /*update cache size*/

    /*put new cache block to head of queue*/
    new_object->prev = head;
    new_object->next = head->next;
    new_object->next->prev = new_object;
    new_object->prev->next = new_object;

    V(&mutex);
}

void evict()
{
    printf("Cache full, evicting...\n");
    object_dat* p = tail->prev;

    /*update cache size and free data*/
    cache_used -= p->actual_size;   
    Free(p->data);

    /*reconnect pointers and free cache block*/
    p->prev->next = tail;
    tail->prev = p->prev;
    Free(p);
}

int cacheFull(int size)
{
    return ((cache_used + size) > MAX_CACHE_SIZE);
}

void print_cache()
{
    printf("Cache currently contains the following objects: ");
    object_dat* p = head->next;
    while(p->size != 0)
    {
        printf("%s -> ", p->object_id);
        p = p->next;
    }
    printf("\n");
    printf("Size of cache is now %d, remaining: %d\n", cache_used, MAX_CACHE_SIZE - cache_used);
}
