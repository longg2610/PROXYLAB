#include "cache.h"

sem_t mutex;       /*protects cache access*/
object_dat* head;
object_dat* tail;
int cache_used;

void print_cache()
{
    printf("Cache now contains the following objects: ");
    object_dat* p = head->next;
    while(p->size != 0)
    {
        printf("%s -> ", p->object_id);
        p = p->next;
    }
    printf("\n");
}

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
        printf("p->objectid: %s\n", p->object_id);
        if(!strcmp(object_id, p->object_id)) /*found in cache*/
        {
            /*printf("ID of object found in cache is: %s, with size of %d, actual size of %d, and contain data:\n\n%s. Length check: %ld\n", 
                p->object_id, p->size, p->actual_size, p->data, strlen(p->data));*/
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
    print_cache();
    return 0;
}

void cache_write(char* object, int size, int actual_size, char* object_id)
{
    P(&mutex);

    while(cacheFull(actual_size))
        evict();

    object_dat* new_object = Malloc(sizeof(struct object_dat));
    new_object->data = object;             
    new_object->size = size;
    new_object->actual_size = actual_size;
    strcpy(new_object->object_id, object_id);

    cache_used += actual_size;

    printf("Wrote object with id %s and size %d into cache\n", new_object->object_id, new_object->size);

    /*put to head of queue*/
    new_object->prev = head;
    new_object->next = head->next;
    new_object->next->prev = new_object;
    new_object->prev->next = new_object;

    V(&mutex);
    print_cache();
}

void evict()
{
    printf("EVICTING...\n");
    object_dat* p = tail->prev;
    cache_used -= p->actual_size;
    Free(p->data);
    p->prev->next = tail;
    tail->prev = p->prev;
    Free(p);
}

int cacheFull(int size)
{
    return ((cache_used + size) > MAX_CACHE_SIZE);
}

