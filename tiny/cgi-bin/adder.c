/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
/* $begin adder */
#include "csapp.h"

int main(void) {
  char *buf, *p;
  char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
  int n1 = 0, n2 = 0;

  if ((buf = getenv("QUERY_STRING")) != NULL) {
    p = strchr(buf, '&');
    *p = '\0'; // 이게 없으면 ?3&4 로 인자가 들어왔을때, arg1에 3&4가 들어가게 됨(3이 아니라)
    strcpy(arg1, buf);
    strcpy(arg2, p+1);
    n1 = atoi(arg1);
    n2 = atoi(arg2);
  }

  /* response body 생성 */
  // sprintf(content, "QUERY_STRING=%s", buf);
  // sprintf(content, "Welocme to add.com: ");
  // sprintf(content, "%sTHE Internet addition portal.\r\n<p>", content);
  // sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>", content, n1, n2, n1 + n2);
  sprintf(content, "The answer is: %d + %d = %d\r\n<p>", n1, n2, n1 + n2);
  // sprintf(content, "%sThanks for visiting!\r\n", content);

  /* Generate the HTTP response */
  printf("Connection: close\r\n");
  printf("Content-length: %d\r\n", (int)strlen(content));
  printf("Content-type: text/html\r\n\r\n");
  /* 이 위까지가 응답 헤더 */
  printf("%s", content);
  fflush(stdout); // cgi 프로그램이 출력으로 쓰는것은 클라이언트에게 직접 간다
  
  exit(0);
}
/* $end adder */
