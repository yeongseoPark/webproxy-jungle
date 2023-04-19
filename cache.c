#include "cache.h"

/* cache_list를 초기화 */
cache_list *init_cache() {
    cache_list* cur_list = (cache_list*)malloc(sizeof(cache_list));
    cur_list->start = NULL;
    cur_list->end   = NULL;
    cur_list->left_space = MAX_CACHE_SIZE;

    return cur_list;
}

/* cache로 쓸 object를 초기화 */
cache_object *init_object(char* id, unsigned int size) {
    cache_object* cur_object = (cache_object*)malloc(sizeof(cache_object));

    cur_object->id = (char*)malloc(strlen(id)+1);
    strcpy(cur_object->id, id); // id가 char배열 지역변수로 들어오기 때문에, 이렇게 해줘야만 소멸 방지 가능 

    cur_object->length = size; 
    cur_object->data = malloc(size);
    cur_object->next = NULL;
    
    return cur_object;
}

/* 각 쓰레드는 동시에 cache에서 읽을 수 있다 */
void open_reader() {

}

/* 읽기를 완료 */
void close_reader() {

}

/* 캐시에서 원하는 값을 찾는다
    값이 찾아지면, 내용과 길이는 object와 size에 써진다
    이 object는 연결리스트의 최근에 사용됐기에 마지막으로 간다
    그리고 0을 리턴한다

    못찾으면 -1을 리턴한다
 */
int search_cache(cache_list* list, char* id, void* object, unsigned int* size) {
    cache_object* searcher = list->start;
    while(searcher != NULL) {
        if (!strcmp(searcher->id, id))
            break;
        searcher = searcher->next;
    }

    if (searcher) { // cache hit
        *size = searcher->length;
        memcpy(object, searcher->data, *size);
    }
    else { // cache miss
        return -1;
    }
    
    searcher = delete_object(list, id);
    if (searcher == NULL) {
        return -1;
    }
    add_to_end(searcher, list);
    return 0;
}

/*  */
int add_to_cache(cache_list *cache, char *id, char *data, unsigned int length) {
    cache_object *obj = init_object(id, length);
    memcpy(obj->data, data, length);
    
        /* 캐시 사이즈 키우기 */
    while (cache->left_space < obj->length) {
        if (evict_object(cache) == -1) {  // 수용가능할때까지 LRU로 쫓아냄
            return -1;
        }
    }

    add_to_end(obj, cache);

    return 0;
}

/* 캐시에 object를 추가(맨끝에 ) */
void add_to_end(cache_object* obj, cache_list* list) { // LRU를 위해 연결리스트의 끝에 연결?
    if (list->start != NULL) { // list가 아예 빈값이 아니면
        list->left_space -= obj->length;
        // memmove(list->end->next, obj, sizeof(obj)+1);
        list->end->next = obj;
        list->end = obj;
        // memmove(list->end, obj, sizeof(obj)+1);
    }
    else {
        list->left_space -= obj->length;
        // memmove(list->start, obj, sizeof(obj)+1);
        list->start = obj;
        // memmove(list->end, obj, sizeof(obj)+1);
        list->end = obj;
    }
}

/* id가 같은애를 찾아서 없애주면 됨 */
cache_object * delete_object(cache_list* cache, char* query_id) {
    cache_object* searcher = cache->start;
    cache_object* follower = NULL;
    while (searcher != NULL) {

        if (!strcmp(searcher->id, query_id)) { // 같은거 찾음
            if (follower == NULL)  // 삭제하려는 값이 리스트의 시작
            {
                cache->start = searcher->next;
                // free(searcher);
                free(follower);
            }
            // else if (searcher->next == NULL) { // 삭제하려는 값이 리스트의 끝
            //     follower->next = NULL;
            //     free(sear)
            // }
            else                 // 삭제하려는 값이 리스트의 중간또는 끝
            {
                follower->next = searcher->next;
                // free(searcher);
                free(follower);
            }

            /* 캐시 사이즈 늘리기 */
            if (cache->left_space + searcher->length > MAX_CACHE_SIZE) {
                cache->left_space = MAX_CACHE_SIZE;
            } else {
                cache->left_space += searcher->length;
            }

            return searcher;
        }
        
        follower = searcher;
        searcher = searcher->next;
    }
    return NULL;
}

/* LRU원칙에 따라 연결리스트의 첫번째 인자를 삭제 */
int evict_object(cache_list * cache) {
    cache_object* obj = cache->start;
    if (obj == NULL) 
        return -1;
    
    if (obj == cache->end)
        cache->end = NULL;
    
    cache->start = obj->next;
    cache->left_space += obj->length;

    Free(obj->id);
    Free(obj->data);
    Free(obj);
    return 0;
}

// /* cache를 삭제..? */
// void destory_cache(cache_list* list) {
//     free(list);
// }


/* Cache for Proxylab, CMU 15-213/513, Fall 2015
 * Author: Aleksander Bapst (abapst)
 *
 * Implements a linked-list cache for the proxy server using a LRU
 * cache eviction policy. LRU is approximated by moving recently read objects
 * to the end of the list, and evicting objects from the front of list.
 * Thread safety is implemented using semaphores to block writing to the cache
 * until no readers are present.
 */
