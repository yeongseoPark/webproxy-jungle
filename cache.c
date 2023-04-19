#include "cache.h"

/* cache_list를 초기화 */
cache_list *init_cache() {
    cache_list* cur_list = (cache_list*)malloc(sizeof(cache_list));
    cur_list->start = NULL;
    cur_list->end   = NULL;
    cur_list->left_space = MAX_CACHE_SIZE;

    cur_list->readcnt = 0;
    Sem_init(&cur_list->r, 0, 1); // r과 w는 1로 초기화
    Sem_init(&cur_list->w, 0, 1);

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

/* 각 쓰레드는 동시에 cache에서 읽을 수 있다, 따라서 reader를 관리하는 readcnt변수에 대해서만 Mutual Exclusion 적용 */
void open_reader(cache_list* cache) {
    P(&cache->r); // a. readcnt를 사용하기 위해 잠그고
    cache->readcnt++;
    if (cache->readcnt == 1) { // 첫번째 reader야
        P(&cache->w); // reader가 한명이라도 존재하는 동안 writer는 못씀 
    }
    V(&cache->r); // b. 다시 풀어줌

    /* 이 뒤로는 reading이 일어나는 Critical section*/
}

/* 읽기를 완료 */
void close_reader(cache_list* cache) {
    P(&cache->r); 
    cache->readcnt--;
    if (cache->readcnt == 0) { // 마지막으로 나가는 reader가 writer를 열어줌
        V(&cache->w); // 이제 P를 호출했는데 w가 0이어서 멈춰있던 writer중 하나는 쓸 수 있음
    }
    V(&cache->r);    
}

/*  캐시에서 원하는 값을 찾는다
    값이 찾아지면, 내용과 길이는 object와 size에 써진다
    이 object는 연결리스트의 최근에 사용됐기에 마지막으로 간다
    그리고 0을 리턴한다

    못찾으면 -1을 리턴한다
 */
int search_cache(cache_list* list, char* id, void* object, unsigned int* size) {
    // 읽을거니까 크리티컬 섹션(close와 얘 사이)에 대한 writer의 접근을 lock해놓음 
    open_reader(list);

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
        close_reader(list);
        return -1;
    }

    // 다 읽음
    close_reader(list);
    
    P(&list->w); // 한번에 한 writer만 쓸 수 있다
    searcher = delete_object(list, id);
    if (searcher == NULL) {
        V(&list->w); // 이 경우에 대해서도 lock 풀어줘야 함, write가 끝난거니까
        return -1;
    }
    add_to_end(searcher, list);
    V(&list->w); // 정상적인 write 사이클이 다 끝난경우 lock풀어줌

    return 0;
}

/* LRU에 따라 연결리스트의 맨 끝에 삽입하고, 캐시 사이즈 키워주는 애 */
int add_to_cache(cache_list *cache, char *id, char *data, unsigned int length) {
    cache_object *obj = init_object(id, length);
    memcpy(obj->data, data, length);

    // 쓸거니까 write lock걸기
    P(&(cache->w));
    
        /* 캐시 사이즈 키우기 */
    while (cache->left_space < obj->length) {
        if (evict_object(cache) == -1) {  // 수용가능할때까지 LRU로 쫓아냄
            V(&cache->w); 
            return -1;
        }
    }

    add_to_end(obj, cache);

    // write lock 풀기
    V(&(cache->w));

    return 0;
}

/* 캐시에 object를 추가(맨끝에 ) */
void add_to_end(cache_object* obj, cache_list* list) { // LRU를 위해 연결리스트의 끝에 연결?

    if (list->start != NULL) { // list가 아예 빈값이 아니면
        list->left_space -= obj->length;
        list->end->next = obj;
        list->end = obj;
    }
    else {
        list->left_space -= obj->length;
        list->start = obj;
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
                free(follower);
            }
            else                 // 삭제하려는 값이 리스트의 중간또는 끝
            {
                follower->next = searcher->next;
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

