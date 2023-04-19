#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_conn_hdr = "Proxy-Connection: close\r\n";  

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

void do_proxy(int fd, cache_list* cache);
void check_validHeader(int fd, rio_t *rp, char* hostname);
void parse_uri(char* uri, char* hostname, char* path, int* port);
void make_header(char* http_header, char* hostname, char* path, rio_t* client_rio);
int connect_server(char* hostname, int port);
void* start_thread(void *arg);

cache_list* cache = NULL; 
/* 
  Pt1. Sequential
  - GET처리
  1. 커맨드라인으로 받은 포트로부터의 요청을 listen
  2. Connection이 수립되면, 요청을 읽고 parse
  3. 해당 요청을 가지고, 적절한 웹서버와 connection설립하고 클라이언트가 특정한 object를 요청(이때 지켜야하는 헤더 규약 있다)
  4. 서버의 응답을 읽고 클라이언트에게 forward
*/
int main(int argc, char **argv) {
  int listenfd;
  int *connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  // size_t tid_p = 0;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    exit(1);
  }

  Signal(SIGPIPE, SIG_IGN); // 프로세스가 닫히거나, 끊긴 파이프에 쓰기 요청을 할 경우 발생하는 오류인 SIGPIPE를 무시하고 서버를 계속 동작

  listenfd = Open_listenfd(argv[1]);

  /* Pt2. Dealing with concurrent request 
    - 여러 요청을 동시에 처리할 수 있어야 함
    - 가능한 방안들
    a. 새 연결 요청 처리위해서 새 쓰레드 생성
    b. prethreaded server (12.5.5)

    - 메모리 leak을 막기 위해 쓰레드는 detached mode에서 동작해야 함: 쓰레드의 실행이 부모 프로세스와 분리 
      - 스레드가 실행을 마치면, 부모가 자동으로 스레드를 종료하는 것이 아니라, 스레드가 스스로를 종료함
  */

 /* Pt3. 캐싱 Web Object
  - 서버에서 web Object 받고
  - 클라이언트에 보냄과 동시에 메모리에 캐시 -> 일단은 host가 하나라고 가정하고..ㅋㅋ hostname을 따로 저장해주지 않고 구현해보자
  - 따라서 동일 요청시에는 서버에 reconnect할 필요 X
    1. 각 connection에 대한 버퍼 생성
    2. 여기에 데이터 수집
    3. 프록시가 수용할 수 있는 최대 데이터 사이즈 = MAX_CACHE_SIZE per 연결 + active connection 최대 개수 * MAX_OBJECT_SIZE
  - 캐시에서 나가는(퇴거) 정책 : LRU(에 근접하는?)

  - Synchronization:
    - 캐시로의 접근은 thread-safe 해야함, 즉 race condition으로부터 자유롭게.
    - 캐시에서 read는 여러 쓰레드가 동시에 할 수 있고
    - 한 쓰레드만이 캐시에 write 할 수 있다
    => partitioning, readers-writers-lock, semaphore 등을 고려해라
 */
  cache = init_cache(); /* 캐시: connection에서 쓸 캐시를 만듬 */

  while (1) {
    pthread_t tid;
    clientlen = sizeof(clientaddr);
    connfd = Malloc(sizeof(int));
    *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // 클라이언트와의 통신 수립, connection descriptor반환

    /* 여기서의 hostname과 port는 클라이언트 그 자체! 의 hostname과 port
       클라이언트 소켓 주소 구조체를 가지고 hostname과 port를 채움 
    */
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);

    printf("Accepted connection from (%s, %s)\n", hostname, port);

   /* Pthread_create의 세번째 인자인 루틴함수는, void* 만을 인자로 받는다
      근데, 내가 start_thread를 
      Pthread_create(&tid, NULL, start_thread, connfd);

      void* start_thread(void *arg, cache_list* cache)

      이런식으로 호출과 선언해주고, 그냥 전역변수인 cache값을 사용할거라 생각했더니, 매번 쓰레드가 돌고나면 캐시값이 임의의 값으로 바뀌는 문제가 발생했음
      따라서 void *인자 하나로 처리해주기 위해서, thread_args 구조체를 선언해서 이에 대한 포인터를 인자로 넘김 
      => CSAPP 12.3.2에 써있음 

      왜 위의 방법에서 문제가 발생했냐면,  start_thread함수내에서 cache변수는 함수 인자로 전달된 값을 사용하는데,
      Pthread_create에서 cache를 변수로 전달하지 않았기에, 함수 내에서 접근하는 cache 변수에는 쓰레드마다 임의의 값이 들어가게 돼서 그럼.
      실제로 전역변수 cache는 바뀌지 않았다만, 함수 내에서 임의의 값을 사용했던 것.

      왜냐, 내가 두번째 매개변수로 cache_list* cache를 적어두고, 이를 넘겨주지 않았기 때문
      두번째 인자를 아예 없애줬더라면 어련히 전역변수인 cache를 썼을것..
     */ 
    thread_args *args = Malloc(sizeof(thread_args));
    args->connfd = connfd;
    args->cache = cache;
    
    Pthread_create(&tid, NULL, start_thread, args);
  }
}

// void* start_thread(void *arg, cache_list* cache) => 캐시 매번 초기화되는 선언 !
void* start_thread(void *arg) {
  thread_args *args = (thread_args*)arg;

  int connfd = *(args->connfd);
  cache_list *cache = args->cache; // 이게 없어도 알아서 전역변수인 cache를 사용함 

  Pthread_detach(Pthread_self());
  Free(arg);
  do_proxy(connfd, cache);
  Close(connfd);
  return NULL;
}

void do_proxy(int connfd, cache_list* cache) { // fd는 클라이언트와 수립된 descriptor
  int serverFd; // 엔드서버로의 descriptor
  char buf[MAXLINE] , method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE];
  int port;
  char server_header[MAXLINE]; // 서버에 전송할 헤더

  rio_t client_rio, server_rio;

  /* 2. Client로부터 request받기 */
  Rio_readinitb(&client_rio, connfd); // rio 초기화
  Rio_readlineb(&client_rio, buf, MAXLINE); // buf에 rio의 값을 씀(클라이언트의 request)

  /* 요청 헤더에서 method, uri, version을 가져옴 */
  sscanf(buf, "%s %s %s", method, uri, version); 

  /* GET 아닌 메소드에 대한 에러메시지 */
  if (strcasecmp(method, "GET")) { // 대소문자 구분X 스트링 비교
    clienterror(connfd, method, "501", "Not Implemented", "Proxy only supports GET method");
    return;
  }

  /* 프록시에서 서버로 보낼 정보 파싱 - uri에서 hostname, path, port를 꺼내서 채운다 */
  parse_uri(uri, hostname, path, &port);
  printf("호스트 : %s\n", hostname);
  printf("패스 : %s\n", path);
  printf("포트 : %d\n", port);

  /* 캐시: 캐시에 값이 있으면 그거를 그대로 돌려주면 된다 */
  if (cache->start != NULL) {
    printf("캐시 %c\n", cache->start->id);
  }
  else {
    printf("없음\n");
  }

  char obj_data[100000];
  unsigned int size;
  if (search_cache(cache, path, (void*)obj_data, &size) == 0) {
    Rio_writen(connfd, (void*)obj_data, size);

    return; // 밑의 과정 안해도 된다
  } 

  /* 서버로 보낼 요청 헤더 생성 - hostname, path, port를 가지고 만든다 */
  make_header(server_header, hostname, path,  &client_rio);

  /* 3. 웹서버와 Connection 설립 */
  serverFd = connect_server(hostname, port);
  
  // 서버에 요청 헤더 전송
  Rio_readinitb(&server_rio, serverFd);
  printf("server헤더 : %s\n", server_header);

  Rio_writen(serverFd, server_header, strlen(server_header));

  // 서버로부터 응답을 받아 클라이언트에 전송
  size_t n;
  size_t all_size = 0;
  char cache_buf[100000] = {0,}; /* 문제 해결 : malloc(size(100000)) 으로 했더니 이상한 값이(이전 메모리영역의 값) cache_buf에 들어가는 문제가..*/
  
  /* malloc을 사용하고자 했다면 이렇게 memset으로 메모리를 초기화했어야...
      char *cache_buf = malloc(sizeof(char) * 100000);
      if (cache_buf == NULL) {
        // 메모리 할당에 실패한 경우의 처리
      }
      memset(cache_buf, 0, sizeof(char) * 100000);
  */

  while((n= Rio_readlineb(&server_rio, buf, MAXLINE)) != 0)
  {
    printf("proxy received %d bytes, then send them to client\n", n);
    // 서버의 응답을 클라이언트에게 forward
    Rio_writen(connfd, buf, n); // client와의 연결 소켓인 connfd에 쓴다 
    all_size += n;
    // printf("버프 : %s \n", buf);
    sprintf(cache_buf, "%s%s", cache_buf, buf); // cache_buf에 전체 응답(헤더+본문)을 이어붙인다
  }

  /* 캐시: 캐시에 해당 값을 쓴다 - 위에서 캐시에서 해당값을 찾지 못했음 */
  if (all_size <= MAX_OBJECT_SIZE) {
    add_to_cache(cache, path, (cache_buf), all_size);
  }

  if (cache->start != NULL) {
    printf("저장 이후 캐시 %c\n", cache->start->id);
  }
  else {
    printf("저장 이후도 없음\n");
  }

  Close(serverFd);
}

/* uri에서 hostname, path, port 뽑아내기 */
void parse_uri(char* uri, char* hostname, char* path, int* port) {
  *port = 80; // 별 언급 없을시 default
  *path = '/'; // 기본 path
  char* hPos;
  char* pPos;
  
  /* host 부분부터 parse */
  /* http:// 가 달린 요청인경우 */
  hPos = strstr(uri, "//");
  if (hPos != NULL)
  {
    hPos = hPos + 2; /*  // 이거 두개 건너뛰어야함 */
  }
  else
  {
    hPos = uri;
  }

  /* port와 path를 parse */
  pPos = strstr(hPos, ":");
   // 여기서 strstr의 첫번째 인자를 uri로 두면, http://~~ 와 같은 요청이 들어왔을때
   // pPos를 http바로 다음 : 위치로 잡게됨
  if (pPos != NULL) {  // 따로 지정된 포트가 있음 
    *pPos = '\0';
    sscanf(hPos, "%s", hostname);
    sscanf(pPos+1, "%d%s" , port, path); // 여기서 segfault
  }
  else // 따로 지정된 포트가 없음
  { 
    pPos = strstr(hPos, "/");
    if (!pPos) { // 뒤의 path가 아예없음
      sscanf(hPos, "%s", hostname);
    }
    else { // 뒤의 path가 있음
      *pPos = '\0';
      sscanf(hPos, "%s", hostname);
      *pPos = '/'; // path부분 긁어야 하므로 다시 / 로 바꿔줌 
      sscanf(pPos, "%s" , path);
    }
  }
  return; 
}

/*
  request     : GET /path HTTP/1.0\r\n
  host        : Host: localhost:80
  Con         : Connection:
  Prox-con    : Proxy-Connection:
  user-agent  : User-Agent:
*/
void make_header(char* final_header, char* hostname, char* path, rio_t* client_rio)
{
  char request_header[MAXLINE], buf[MAXLINE], host_header[MAXLINE], other[MAXLINE];

  sprintf(request_header, "GET %s HTTP/1.0\r\n", path);
  sprintf(host_header, "Host: %s\r\n", hostname); // 호스트헤더는 기 요청받은 호스트네임으로

  /* 클라이언트의 나머지 요청 받아서 저장 */
  while (Rio_readlineb(client_rio, buf, MAXLINE)) {
    if (!(strcmp("\r\n", buf))) {
      break; // '\r\n' 이면 끝 
    }

    /* 얘네는 정해진 형식대로 채워줄거임 */
    if(strncasecmp(buf, "User-Agent", strlen("User-Agent")) 
      && strncasecmp(buf, "Connection", strlen("Connection"))
      && strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection"))) {
        strcat(other, buf); // 따라서 정해진 형식없는 애들만 other에 넣어줌(이어붙이기)
      }
  }

  /* 최종 요청 헤더의 모습 */
  sprintf(final_header, "%s%s%s%s%s%s%s",
    request_header,
    host_header,
    user_agent_hdr,
    conn_hdr,
    prox_conn_hdr,
    other,
    "\r\n"
  );
}


/* 유저가 요청한 hostname, port에 적합한 서버에 접속한다 */
int connect_server(char* hostname, int port) {
  int serverFd;
  char portc[MAXLINE];
  sprintf(portc, "%d", port); // int 인 port를 문자열로변경

  serverFd = open_clientfd(hostname, portc);

  return serverFd;
}


void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* Http response body */
  sprintf(body, "<html><title>Tiny Error</title>"); // 출력값을 변수(문자열)에 저장(string printf)
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* print Http response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}
