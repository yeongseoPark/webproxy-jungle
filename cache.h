#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct cache_object {
    struct cache_object* next;
    char* id; // 유저는 파일이름으로 찾자나....
    void* data;
    int length;
} cache_object;

typedef struct cache_list {
    cache_object* start;
    cache_object* end;
    unsigned int left_space;
    // reader개수, 세마포어 필요한데...나중에
} cache_list;

typedef struct thread_args { // Pthread_create가 void*만 인자로 받기때문에, 구조체 만들어서 얘에대한 포인터줘야함
// 안그러면 캐시가 막 바뀌어버리는ㄴ..
    int *connfd;
    cache_list *cache;
} thread_args;

cache_list *init_cache();

cache_object *init_object(char* id, unsigned int size);

void open_reader();

void close_reader();

int search_cache(cache_list* list, char* id, void* object, unsigned int* size) ;

void add_to_end(cache_object* obj, cache_list* list);

cache_object * delete_object(cache_list* cache, char* query_id);

int evict_object(cache_list * cache);

int add_to_cache();

void destory_cache(cache_list* list);

int add_to_cache(cache_list *cache, char *id, char *data, unsigned int length);

/* Cache header file for cache.c
 * Author: Aleksander Bapst (abapst)
 */
