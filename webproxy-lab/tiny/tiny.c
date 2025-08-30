/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd;
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
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

/* 한 개의 HTTP 트랜잭션을 처리한다. */
void doit(int fd) {
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* Request line과 Request header를 읽는 곳*/
  rio_readinitb(&rio, fd);
  rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers : \n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  
  /* main 루틴 */
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "적용되지 않음", "Tiny는 이 요청을 적용하지 않음");
    return;
  }
  read_requesthdrs(&rio); // request header를 읽는다

  /* GET Method 요청에서 URI를 분석 */
  is_static = parse_uri(uri, filename, cgiargs);
  
  if (stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "Not found 찾을 수 없음", "Tiny가 이 파일을 읽지 못합니다.");
    return;
  }

  if (is_static) {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden 금지됨", "Tiny가 이 파일을 읽지 못함");
      return;
    }
    serve_static(fd, filename, sbuf.st_size);
  } else {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden 제한됨", "Tiny가 CGI program을 실행할 수 없습니다. (CGI인자 String으로 분석할 수 없음.)");
      return;
    }
    serve_dynamic(fd, filename, cgiargs);
  }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* HTTP response(응답) body 설계 */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* HTTP response(응답) 출력 */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  // MIME type 출력
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  // 내용 길이(bytes)
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

/* Request(요청) header 읽기 / 무시한다. */
void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

/* HTTP URI 분석 */
int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  if (!strstr(uri, "cgi-bin")) {      // 만약 URI가 정적 컨텐츠를 위한 것일 때
    strcpy(cgiargs, "");              // CGI 인자 String을 지운다.
    strcpy(filename, ".");            // URI를 ./index.html 같은 상대 리눅스 경로이름으로 변환
    strcat(filename, uri);
    if (uri[strlen(uri)-1] == '/') {  // URI가 '/'문자열로 끝난다면
      strcat(filename, "home.html");  // 기본 파일(home.html) 이름을 추가한다.
    }
    return 1;
  } else {
    ptr = index(uri, '?');      // 모든 CGI 인자들을 추출
    if (ptr) {
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';              // NULL point
    } else {
      strcpy(cgiargs, "");
    }
    strcpy(filename, ".");      // 나머지 URI 부분을 상대 리눅스 파일 이름으로 변환한다.
    strcat(filename, uri);
    return 0;
  }
}

void serve_static(int fd, char *filename, int filesize) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* Response headers(응답 헤더)를 Client에게 전송 */
  get_filetype(filename, filetype);                               // 파일 이름의 접미어 부분을 검사해서 파일 타입을 결정
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers : \n");
  printf("%s", buf);

  /* Response body를 Client에게 보냄 */
  srcfd = open(filename, O_RDONLY, 0);                            // 읽기 위해서 filename을 open하고 식별자를 얻어온다.
  // mmap을 호출하면 파일 식별자 srcfd의 첫 번째 filesize 바이트를 주소 srcp에서 시작하는 사적 읽기-허용 가상메모리 영역으로 매핑함
  srcp = mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);     // 리눅스 mmap함수는 요청한 파일을 가상메모리 영역으로 매핑
  // 파일을 메모리로 매핑한 후에 더 이상 이 식별자는 필요 없음.
  close(srcfd);   // 그래서 이 파일을 닫음. 이렇게 하지 않으면 메모리 누수가 발생할 수 있다.
  // Rio_writen 함수는 주소 srcp에서 시작하는 filesize 바이트를 클라이언트의 연결 식별자(fd)로 복사한다.
  Rio_writen(fd, srcp, filesize); // 파일을 Client에게 전송
  Munmap(srcp, filesize); // 매핑된 가상메모리 주소를 반환한다. 이것도 메모리 누수를 피하는데 중요함
}

/* 파일 타입 반환 - 파일 이름의 접미어 부분을 검사함으로써 파일 타입을 반환함 */
void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html")) {
    strcpy(filetype, "text/html");
  } else if (strstr(filename, ".gif")) {
    strcpy(filetype, "image/gif");
  } else if (strstr(filename, ".png")) {
    strcpy(filetype, "image/png");
  } else if (strstr(filename, ".jpg")) {
    strcpy(filetype, "image/jpeg");
  } else {
    strcpy(filetype, "text/plain");
  }
}

/* 동적 컨텐츠를 Client에게 제공*/
void serve_dynamic(int fd, char *filename, char *cgiargs) {
  char buf[MAXLINE], *emptylist[] = {NULL};

  /* HTTP 응답의 첫번째 부분 return + 성공을 알려주는 응답 라인을 보냄 */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server : Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (fork() == 0) { // 응답의 첫 번째 부분을 보낸 후에 새로운 자식 프로세스를 fork한다.
    // CGI 프로그램은 응답의 나머지 부분을 보내야함
  setenv("QUERY_STRING", cgiargs, 1);     // 자식은 QUERY_STRING 환경변수를 요청한 URI의 CGI인자들로 초기화함
    dup2(fd, STDOUT_FILENO);              // 자식은 자식의 표준 출력을 연결 파일 식별자로 재지정함.
    execve(filename, emptylist, environ); // 그 후에 CGI프로그램을 로드하고 실행
  }
  // CGI 프로그램이 자식 컨텍스트에서 실행되기 때문에 execve함수를 호출하기 전에 존재하던 열린 파일들과 환경 변수들에도 동일하게 접근할수 있다.
  // 그래서 CGI 프로그램이 표춘 출력에 쓰는 모든 것은 직접 클라이언트 프로세스로 부모 프로세스의 어떤 간섭도 없이 진행됨
  wait(NULL); // 부모는 자식이 종료되어 정리되는 것을 기다리기 위해 wait함수에서 블록된다.
}