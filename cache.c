#include "cache.h"

sem_t mutex;       /*protects cache access*/

object_dat* head = NULL;
object_dat* tail = NULL;
int cache_used = 0;
void init_cache()
{
    Sem_init(&mutex, 0, 1);

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
        int length = strlen(object_id) > strlen(p->object_id) ? strlen(object_id) : strlen(p->object_id);
        if(!strncmp(object_id, p->object_id, length)) /*found in cache*/
        {
            /*printf("ID of object found in cache is: %s, with size of %d, actual size of %d, and contain data:\n\n%s. Length check: %ld\n", 
                p->object_id, p->size, p->actual_size, p->data, strlen(p->data));*/
            Rio_writen(connfd, p->data, p->size);   /*send back to client*/

            /*relocate to head*/
            p->prev->next = p->next;
            p->next->prev = p->prev;

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

    while(cacheFull(actual_size))
        evict();

    object_dat* new_object = Malloc(sizeof(struct object_dat));
    new_object->data = object;              /*find a way to malloc here instead of in doit ?*/
    new_object->size = size;
    new_object->actual_size = actual_size;
    cache_used += actual_size;

    strcpy(new_object->object_id, object_id);

    printf("Wrote object with id %s into cache\n", new_object->object_id);

    new_object->prev = head;
    new_object->next = head->next;
    new_object->next->prev = new_object;
    new_object->prev->next = new_object;

    printf("Cache now contains the following objects: ");
    object_dat* p = head->next;
    while(p->size != 0)
    {
        printf("%s -> ", p->object_id);
        p = p->next;
    }
    printf("\n");

    V(&mutex);
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

