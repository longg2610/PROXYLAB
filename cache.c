#include "cache.h"

object_dat* head = NULL;
object_dat* tail = NULL;
int cache_used = 0;
void init_cache()
{
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
    object_dat* p = head->next;
    while(p->size != 0)
    {
        printf("p->objectid: %s\n", p->object_id);
        int length = strlen(object_id) > strlen(p->object_id) ? strlen(object_id) : strlen(p->object_id);
        if(!strncmp(object_id, p->object_id, length)) /*found in cache*/
        {
            printf("ID of object found in cache is: %s, with size of %d, and contain data:\n\n%s\n", p->object_id, p->size, p->data);
            Rio_writen(connfd, p->data, p->size);   /*send back to client*/

            /*relocate to head*/
            p->prev->next = p->next;
            p->next->prev = p->prev;

            p->prev = head;
            p->next = head->next;
            p->next->prev = p;
            p->prev->next = p;
            return 1;
        }
        p = p->next;
    }
    return 0;
}

void cache_write(char* object, int size, char* object_id)
{
    while(cacheFull(size))
        evict();

    object_dat* new_object = Malloc(sizeof(struct object_dat));
    new_object->data = object;              /*find a way to malloc here instead of in doit ?*/
    new_object->size = size;
    strcpy(new_object->object_id, object_id);

    cache_used += size;

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
}

void evict()
{
    printf("EVICTING...\n");
    object_dat* p = tail->prev;
    cache_used -= p->size;
    Free(p->data);
    p->prev->next = tail;
    tail->prev = p->prev;
    Free(p);
}

int cacheFull(int size)
{
    return ((cache_used + size) > MAX_CACHE_SIZE);
}
