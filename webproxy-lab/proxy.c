#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void doit(int fd);
void parse_uri(char *uri, char *hostname, char *path, char *port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void read_requesthdrs(rio_t *rio);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

static const int is_local_test = 1; // test 환경에 따른 domain&port 지정을 위한 상수 (0 할당시 domain&port가 고정되어 외부에서 접속 가능)

int main(int argc, char **argv)
{
  int listenfd, clientfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("PROXY : Accepted connection from (%s, %s)\n", hostname, port);

    doit(clientfd);
    Close(clientfd); // line:netp:tiny:close
  }
}

void doit(int clientfd) {
  int serverfd; // 서버 소켓 디스크립터
  char request_buf[MAXLINE], response_buf[MAX_OBJECT_SIZE]; // 요청과 응답을 저장할 버퍼
  char method[MAXLINE], uri[MAXLINE], path[MAXLINE]; // 요청 메소드, URI, 경로
  char hostname[MAXLINE], port[MAXLINE]; // 호스트명, 포트
  rio_t request_rio, response_rio; // 클라이언트로부터의 요청과 서버로부터의 응답을 처리하기 위한 리오 버퍼

  /* 클라이언트의 요청 읽기 */
  Rio_readinitb(&request_rio, clientfd); // 클라이언트 소켓 디스크립터를 리오 버퍼에 연결
  Rio_readlineb(&request_rio, request_buf, MAXLINE); // 클라이언트로부터 요청 라인을 읽음
  printf("Request header: %s\n", request_buf); // 디버깅용 요청 헤더 출력

  /* 요청 메소드, URI 읽기 */
  sscanf(request_buf, "%s %s", method, uri); // 요청 라인에서 메소드와 URI를 추출

  /* URI 파싱하여 호스트명, 포트, 경로 추출 */
  parse_uri(uri, hostname, port, path); // URI에서 호스트명, 포트, 경로를 파싱하여 변수에 저장

  printf("uri: %s\n", uri); // 디버깅용 URI 출력

  /* 요청 수정: 새로운 요청 구성 */
  sprintf(request_buf, "%s /%s %s\r\n", method, path, "HTTP/1.0"); // 요청 라인 재구성
  printf("%s\n", request_buf); // 재구성된 요청 헤더 출력
  sprintf(request_buf, "%sConnection: close\r\n", request_buf); // Connection 헤더 추가
  sprintf(request_buf, "%sProxy-Connection: close\r\n", request_buf); // Proxy-Connection 헤더 추가
  sprintf(request_buf, "%s%s\r\n", request_buf, user_agent_hdr); // User-Agent 헤더 추가

  /* 요청 메소드가 GET 또는 HEAD가 아닌 경우 오류 응답 전송 */
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
      clienterror(clientfd, method, "501", "Not Implemented", "Proxy does not implement this method");
      return;
  }

  /* 원격 서버에 클라이언트의 요청 전송 */
  serverfd = Open_clientfd(hostname, port); // 서버로의 연결 생성
  if (serverfd < 0) { // 연결 실패 시
      clienterror(clientfd, hostname, "404", "Not found", "Proxy couldn't connect to the server"); // 클라이언트에게 오류 응답 전송
      return;
  }
  printf("%s\n", request_buf); // 전송할 요청 헤더 출력
  Rio_writen(serverfd, request_buf, strlen(request_buf)); // 서버에 요청 전송

  /* 서버로부터 응답 받아 클라이언트에 전송 */
  ssize_t n;
  n = Rio_readn(serverfd, response_buf, MAX_OBJECT_SIZE); // 서버로부터 응답을 읽음
  Rio_writen(clientfd, response_buf, n); // 클라이언트에게 응답을 전송

  Close(serverfd); // 서버 연결 종료
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* HTTP response(응답) body 설계 */
  sprintf(body, "<html><title>Proxy Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Prox server</em>\r\n", body);

  /* HTTP response(응답) 출력 */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  // MIME type 출력
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  // 내용 길이(bytes)
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  sprintf(body, "%s<hr><em>The Prox server</em>\r\n</body></html>", body);
  Rio_writen(fd, body, strlen(body));
}

// uri를 `hostname`, `port`, `path`로 파싱하는 함수
// uri 형태: `http://hostname:port/path` 혹은 `http://hostname/path` (port는 optional)
void parse_uri(char *uri, char *hostname, char *port, char *path) {
    printf("---parse_uri: %s\n", uri); // 디버깅용 메시지 출력
    // 호스트명을 가리키는 포인터 설정
    char *hostname_ptr = strstr(uri, "//") != NULL ? strstr(uri, "//") + 2 : uri + 1;
    // 포트를 가리키는 포인터 설정
    char *port_ptr = strstr(hostname_ptr, ":");
    // 경로를 가리키는 포인터 설정
    char *path_ptr = strstr(hostname_ptr, "/");
    
    // 경로가 존재한다면
    if (path_ptr > 0) {
        *path_ptr = '\0'; // 경로 부분을 끝낼 문자('\0')로 대체
        strcpy(path, path_ptr+1); // 경로를 path 버퍼에 복사
    }
    // 포트가 존재한다면
    if (port_ptr > 0) {
        *port_ptr = '\0'; // 포트 부분을 끝낼 문자('\0')로 대체
        strcpy(port, port_ptr + 1); // 포트를 port 버퍼에 복사
    }

    strcpy(hostname, hostname_ptr); // 호스트명을 hostname 버퍼에 복사
    printf("---parse_uri host: %s, port: %s, path: %s\n", hostname, port, path); // 호스트명, 포트, 경로를 출력
}

/* 클라이언트로부터의 HTTP 요청 헤더를 읽어들이는 함수 */
void read_requesthdrs(rio_t *rp) {
    char buf[MAXLINE];

    // HTTP 헤더를 읽어들임
    Rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n")) { // 빈 줄이 나올 때까지 반복
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf); // 헤더를 화면에 출력하거나 다른 작업을 수행할 수 있음
    }
    return;
}