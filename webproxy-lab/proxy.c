#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes
 * 과제에서 권장하는 전체 캐시 최대 크기와 단일 객체 최대 크기 상수
 * - MAX_CACHE_SIZE: 캐시 전체 용량 한도
 * - MAX_OBJECT_SIZE: 한 개의 응답 객체에 대해 캐시 가능한 최대 크기
 * 본 코드는 아직 캐시를 구현하지 않았지만, 상수는 유지
 */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* 프록시의 핵심 함수 프로토타입 선언
 * - doit: 클라이언트 1개 연결에 대한 전체 요청-응답 처리
 * - parse_uri: 클라이언트 요청의 URI를 host, port, path로 분해
 * - clienterror: 클라이언트에게 HTTP 에러 응답 생성 및 전송
 * - forward_request_headers: 클라이언트 요청 헤더를 서버로 전달하면서 필수 헤더를 정규화
 * - relay_response: 원서버의 응답을 클라이언트로 스트리밍 중계
 */
void doit(int fd);
int parse_uri(char *uri, char *hostname, char *path, char *port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void forward_request_headers(rio_t *client_rio, int serverfd, const char *hostname, const char *port, const char *method, const char *path);
void relay_response(int serverfd, int clientfd);

/* 과제에서 제공하는 고정 User-Agent 헤더 문자열
 * 프록시는 클라이언트의 User-Agent를 그대로 전달하지 않고
 * 아래 고정된 UA로 대체하여 서버에 전달해야 함
 * 끝에 \r\n 포함되어 있어 그대로 쓰면 됨
 */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv)
{
  int listenfd, clientfd;                         // 수신용 리스닝 소켓, 각 클라이언트 연결용 소켓
  char hostname[MAXLINE], port[MAXLINE];          // 접속한 클라이언트의 역방향 이름, 포트 문자열
  socklen_t clientlen;                            // 소켓 주소 구조체 크기
  struct sockaddr_storage clientaddr;             // 클라이언트 주소를 담을 범용 구조체

  /* 커맨드라인 인자 검사
   * 사용법: ./proxy <port>
   * 포트 문자열이 1개 있어야 함
   */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  /* 리스닝 소켓 생성
   * - Open_listenfd는 csapp의 래퍼로, 에러 시 내부에서 처리 후 적절히 종료
   * - 반환된 listenfd로 accept를 반복
   */
  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);

    /* 클라이언트 연결 수락
     * - Accept는 블로킹 호출
     * - 연결이 수락되면 통신할 전용 소켓 clientfd를 획득
     */
    clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // line:netp:tiny:accept

    /* 접속자의 호스트명과 포트를 문자열로 얻어 로그 출력
     * - 실패해도 프로그램 진행에는 큰 영향 없음
     */
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("PROXY : Accepted connection from (%s, %s)\n", hostname, port);

    /* 단일 연결 처리
     * - HTTP 요청 1개를 처리하고 응답 후 소켓을 닫음
     * - 프록시 과제의 기본 요건에서는 keep-alive 대신 Connection: close로 마무리
     */
    doit(clientfd);

    /* 클라이언트 소켓 종료
     * - 한 요청-응답 트랜잭션 완료 후 닫음
     */
    Close(clientfd); // line:netp:tiny:close
  }
}

/* 단일 클라이언트 요청을 처리하는 함수
 * 흐름
 * 1) 요청 라인 읽기 및 파싱 (메서드, URI, 버전)
 * 2) 메서드 허용 여부 검사 (GET, HEAD만 허용)
 * 3) URI를 host, port, path로 분해
 * 4) 원 서버와 TCP 연결
 * 5) 요청 헤더를 정규화하여 원 서버로 전달
 * 6) 원 서버의 응답을 읽어 클라이언트로 스트리밍 중계
 * 7) 원 서버 소켓 종료
 */
void doit(int clientfd) {
    int serverfd;                                     // 원 서버와의 연결 소켓
    char reqline[MAXLINE], method[MAXLINE],
         uri[MAXLINE], version[MAXLINE];              // 요청라인과 각 토큰
    char hostname[MAXLINE], port[16], path[MAXLINE];  // URI 분해 결과 저장
    rio_t c_rio;                                      // 클라이언트 입력 스트림용 RIO 버퍼

    /* 클라이언트 소켓을 RIO 버퍼에 바인딩
     * - 버퍼링된 안전한 입출력을 제공
     */
    Rio_readinitb(&c_rio, clientfd);

    /* 요청 라인 한 줄 읽기
     * - 예: "GET http://example.com/index.html HTTP/1.1\r\n"
     * - 0 이하면 클라이언트가 바로 끊었거나 에러이므로 조용히 반환
     */
    if (Rio_readlineb(&c_rio, reqline, MAXLINE) <= 0) return;

    /* 요청 라인 파싱
     * - 공백 기준 세 토큰 분리: method, uri, version
     * - 세 개가 정확히 나오지 않으면 잘못된 요청으로 간주
     */
    if (sscanf(reqline, "%s %s %s", method, uri, version) != 3) {
        clienterror(clientfd, reqline, "400", "Bad Request", "Malformed request line");
        return;
    }

    /* 메서드 제한
     * - GET, HEAD만 지원
     * - 이외 메서드는 501 Not Implemented로 응답
     */
    if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
        clienterror(clientfd, method, "501", "Not Implemented", "Proxy does not implement this method");
        return;
    }

    /* URI 분해
     * - "http://host[:port]/path" 형태를 hostname/port/path로 분리
     * - 실패 시 400 응답
     */
    if (parse_uri(uri, hostname, port, path) != 0) {
        clienterror(clientfd, uri, "400", "Bad Request", "Cannot parse URI");
        return;
    }

    /* 원 서버와 TCP 연결 시도
     * - hostname, port 사용
     * - 실패 시 502 Bad Gateway로 응답
     */
    if ((serverfd = Open_clientfd(hostname, port)) < 0) {
        clienterror(clientfd, hostname, "502", "Bad Gateway", "Could not connect to server");
        return;
    }

    /* 요청 헤더 전달
     * - 요청 라인을 HTTP/1.0으로 다운그레이드
     * - Host, User-Agent, Connection, Proxy-Connection을 표준화
     * - 그 외 클라이언트 헤더는 그대로 통과
     */
    forward_request_headers(&c_rio, serverfd, hostname, port, method, path);

    /* 응답 중계
     * - 상태줄과 헤더를 먼저 클라이언트에 전달
     * - 본문은 Content-Length, chunked, EOF 기반으로 안전하게 스트리밍
     */
    relay_response(serverfd, clientfd);

    /* 원 서버와의 연결 종료
     * - 클라이언트 소켓은 상위 함수에서 닫힘
     */
    Close(serverfd);
}

/* 에러 응답 생성기
 * - 간단한 HTML 본문을 만들어 HTTP/1.0 상태줄 + 헤더 + 본문을 전송
 * ⚠️ 주의: 현재 구현은 Content-length를 계산한 뒤 body를 두 번 쓰는 오류가 있음
 *    => 본문을 한 번만 작성하고, Content-length는 그 최종 본문 길이로 계산해야 맞음
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* HTML 본문 구성 시작
   * - body 버퍼에 누적 문자열을 만드는 방식
   * - sprintf(body, "..."); 로 시작한 뒤 이어서 "%s..." 패턴으로 연결
   */
  sprintf(body, "<html><title>Proxy Error</title>");
  sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Prox server</em>\r\n", body);
  /* 여기까지가 현재 body의 내용
   * 이 시점에서 Content-length를 계산하면 닫는 태그가 빠진 상태이므로 부정확
   * 아래에서 닫는 태그를 붙이고 나서 길이를 계산하는 것이 올바름
   */

  /* 상태줄 전송
   * - HTTP/1.0 <코드> <사유구절>
   */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));

  /* 헤더 전송
   * - Content-type: text/html
   * - Content-length: 본문 길이
   *
   * ⚠️ 현재 구현은 이 아래에서 body를 한 번 쓰고,
   *    그 다음 줄에서 body에 닫는 태그를 이어붙인 후 다시 쓰기 때문에
   *    본문이 두 번 전송되고 Content-length도 불일치가 됨
   *    => 실제 서비스에서는 일부 클라이언트가 응답을 비정상 처리할 수 있음
   */
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));

  /* 잘못된 Content-length 계산 지점
   * - 여기서 strlen(body)를 사용하면 아직 닫는 태그가 빠진 길이
   * - 그리고 바로 아래에서 body를 한 번 쓰고,
   *   또다시 body를 확장하여 두 번째로 쓰므로 총 두 번 쓰는 문제가 발생
   */
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));

  /* 본문 전송
   * - 닫는 태그 없이 전송됨
   */
  Rio_writen(fd, body, strlen(body));
}

/* URI 파서
 * 입력: "http://host[:port]/path" 혹은 "http://host" 형태
 * 출력: hostname, port, path 세 문자열
 * 동작
 * 1) scheme "http://" 제거
 * 2) host 시작 위치를 잡고, 첫 '/'와 ':' 위치를 찾아 host:port 구분
 * 3) port가 없으면 기본 "80"
 * 4) path가 없으면 "/" 부여, 있으면 슬래시 포함 전체 경로 복사
 * 반환값: 0 고정 (현재는 실패 케이스를 만들지 않음)
 *
 * 주의
 * - 이 구현은 절대경로가 없는 edge 케이스에 대비해 slash가 없으면 문자열 끝을 가리키게 처리
 * - uri를 수정하지 않고 읽기만 하므로 const 처리하는 편이 안전하지만
 *   현재 시그니처는 과제 관례에 맞춰 char* 유지
 */
int parse_uri(char *uri, char *hostname, char *port, char *path) {
    const char *p = uri;                       // 파싱 진행 포인터
    const char *scheme = "http://";            // 지원 스킴
    size_t slen = strlen(scheme);

    // "http://" 접두사가 있으면 건너뜀
    if (strncasecmp(p, scheme, slen) == 0) p += slen;

    // host 시작 지점
    const char *host_begin = p;
    const char *slash = strchr(host_begin, '/');   // 경로 시작 슬래시
    const char *colon = strchr(host_begin, ':');   // 포트 구분 콜론

    // 경로 슬래시가 없으면 문자열 끝을 경계로 사용
    if (!slash) {
        slash = uri + strlen(uri);
    }

    if (colon && colon < slash) {
        // host:port 형태
        size_t hlen = (size_t)(colon - host_begin);    // 호스트명 길이
        strncpy(hostname, host_begin, hlen);           // 호스트명 복사
        hostname[hlen] = '\0';                         // 널 종료

        size_t plen = (size_t)(slash - (colon + 1));   // 포트 문자열 길이
        strncpy(port, colon + 1, plen);                // 포트 복사
        port[plen] = '\0';                             // 널 종료
    } else {
        // 포트가 없는 host 단독
        size_t hlen = (size_t)(slash - host_begin);
        strncpy(hostname, host_begin, hlen);
        hostname[hlen] = '\0';
        strcpy(port, "80");                            // 기본 포트 80
    }

    // 경로 설정
    if (*slash == '\0') {
        // 슬래시가 문자열 끝이면 경로 없음 -> "/" 사용
        strcpy(path, "/");
    } else {
        // 슬래시 포함 경로 전체 복사
        strcpy(path, slash);
    }
    return 0;
}

/* 클라이언트 요청 헤더를 읽어 원 서버로 전달하는 함수
 * 동작
 * 1) 요청 라인을 HTTP/1.0으로 재작성하여 서버로 전송
 * 2) 클라이언트가 보낸 헤더들을 한 줄씩 읽되, 아래 규칙으로 필터링
 *    - Host: 있으면 그대로 전달, 없으면 나중에 추가
 *    - User-Agent:, Connection:, Proxy-Connection: 은 삭제하고 이후 고정값 삽입
 *    - Proxy-Authorization: 은 일반적으로 제거
 *    - 그 외 헤더는 그대로 전달
 * 3) 마지막으로 고정 UA/Connection/Proxy-Connection/Host 보정 후 빈 줄 전송
 * 주의
 * - 이 함수는 요청 바디가 있는 메서드(POST 등)를 고려하지 않음
 *   GET/HEAD만 다루므로 무방
 */
void forward_request_headers(rio_t *client_rio, int serverfd, 
                             const char *hostname, const char *port,
                             const char *method, const char *path) {
    char buf[MAXLINE], out[MAXLINE];                          // 입력/출력 라인 버퍼
    int has_host = 0, has_ua = 0, has_conn = 0, has_pconn = 0; // 존재 여부 플래그

    // 1) 요청 라인 재작성: HTTP/1.0 강제
    //   - 원 요청이 HTTP/1.1이어도 서버에는 1.0으로 보냄
    //   - keep-alive를 피하고 단순화를 위해 1.0 사용
    int n = snprintf(out, sizeof(out), "%s %s HTTP/1.0\r\n", method, path);
    Rio_writen(serverfd, out, n);

    // 2) 클라이언트 헤더 필터링 루프
    //   - 빈 줄(\r\n) 만날 때까지 반복
    //   - 각 헤더의 접두 키를 대소문자 무시 비교로 판별
    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
        if (!strcmp(buf, "\r\n")) break;  // 헤더 종료

        if (!strncasecmp(buf, "Host:", 5)) {
            has_host = 1;
            Rio_writen(serverfd, buf, strlen(buf));   // Host는 그대로 전달
        }
        else if (!strncasecmp(buf, "User-Agent:", 11)) {
            has_ua = 1;                               // 고정 UA를 쓸 예정이므로 스킵
        }
        else if (!strncasecmp(buf, "Connection:", 11)) {
            has_conn = 1;                             // close로 덮어쓸 예정이므로 스킵
        }
        else if (!strncasecmp(buf, "Proxy-Connection:", 17)) {
            has_pconn = 1;                            // close로 덮어쓸 예정이므로 스킵
        }
        else if (!strncasecmp(buf, "Proxy-Authorization:", 20)) {
            // 일반적으로 프록시 인증 헤더는 원 서버로 전달하지 않음
            // 이 구현에서는 제거
        }
        else {
            // 그 외 헤더는 변경 없이 전달
            Rio_writen(serverfd, buf, strlen(buf));
        }
    }

    // 3) Host 헤더가 없으면 추가
    if (!has_host) {
        if (!strcmp(port, "80"))
            n = snprintf(out, sizeof(out), "Host: %s\r\n", hostname);
        else
            n = snprintf(out, sizeof(out), "Host: %s:%s\r\n", hostname, port);
        Rio_writen(serverfd, out, n);
    }

    n = snprintf(out, sizeof(out), "%s", user_agent_hdr);
    Rio_writen(serverfd, out, n);

    n = snprintf(out, sizeof(out), "Connection: close\r\n");
    Rio_writen(serverfd, out, n);

    n = snprintf(out, sizeof(out), "Proxy-Connection: close\r\n");
    Rio_writen(serverfd, out, n);

    // 헤더 종료 빈 줄
    Rio_writen(serverfd, "\r\n", 2);
}

/* 응답 중계기
 * 목표
 * - 원 서버의 응답을 그대로 클라이언트에게 전달하되,
 *   본문 길이 방식을 안전하게 처리
 * 처리 케이스
 * 1) Transfer-Encoding: chunked
 * 2) Content-Length: N
 * 3) 길이 정보 없음 -> EOF까지
 * 구현 방식
 * - 먼저 상태줄 한 줄을 읽어 바로 전달
 * - 헤더를 한 줄씩 읽어 클라이언트로 전달하면서
 *   chunked 여부와 Content-Length를 파악
 * - 본문은 케이스별로 루프를 돌며 안전하게 스트리밍
 */
void relay_response(int serverfd, int clientfd) {
    rio_t s_rio;
    Rio_readinitb(&s_rio, serverfd);              // 원 서버 소켓을 RIO 버퍼에 바인딩
    char buf[MAXLINE];                            // 라인/청크 버퍼

    int is_chunked = 0;                           // chunked 전송 여부
    long content_len = -1;                        // Content-Length 값, 없으면 -1

    // 1) 상태줄 읽기 및 전달
    //    예: "HTTP/1.1 200 OK\r\n"
    int n = Rio_readlineb(&s_rio, buf, MAXLINE);
    if (n <= 0) return;                           // 서버가 즉시 끊었거나 오류
    Rio_writen(clientfd, buf, n);

    // 2) 헤더 읽기 루프
    //    - 빈 줄까지 각 헤더를 즉시 클라이언트로 흘려보냄
    //    - Transfer-Encoding, Content-Length를 파악
    while ((n = Rio_readlineb(&s_rio, buf, MAXLINE)) > 0) {
        // chunked 인지 검사
        if (!strncasecmp(buf, "Transfer-Encoding:", 18) &&
            strstr(buf, "chunked")) is_chunked = 1;

        // Content-Length 파악
        if (!strncasecmp(buf, "Content-Length:", 15)) {
            // buf + 15 지점부터 정수 파싱
            content_len = strtol(buf + 15, NULL, 10);
        }

        // 현재 헤더 라인을 그대로 클라이언트로 전달
        Rio_writen(clientfd, buf, n);

        // 빈 줄 이면 헤더 종료
        if (!strcmp(buf, "\r\n")) break;
    }

    // 3) 본문 전달
    if (is_chunked) {
        /* 청크 전송 인코딩
         * - 구조: <hex 길이>\r\n <데이터...> \r\n [0\r\n 트레일러\r\n]\r\n
         * - 각 청크 크기 줄을 읽어 클라이언트로 전달한 뒤
         *   해당 크기만큼 데이터 블록을 읽어 그대로 전달
         * - 마지막 청크 크기는 0
         * - 마지막에는 선택적 트레일러 헤더들이 오고, 최종 빈 줄까지 전달
         */
        while ((n = Rio_readlineb(&s_rio, buf, MAXLINE)) > 0) {
            // 청크 크기 줄 자체를 먼저 그대로 전달
            Rio_writen(clientfd, buf, n);

            // 16진수 크기 파싱
            long chunk = strtol(buf, NULL, 16);

            if (chunk == 0) {
                // 마지막 청크: 뒤따르는 트레일러 헤더와 최종 CRLF까지 전달
                while ((n = Rio_readlineb(&s_rio, buf, MAXLINE)) > 0) {
                    Rio_writen(clientfd, buf, n);
                    if (!strcmp(buf, "\r\n")) break; // 트레일러 종료
                }
                break; // 전체 본문 종료
            }

            // 청크 데이터 본문 전달
            long togo = chunk;
            while (togo > 0) {
                // 남은 크기만큼 읽되 MAXLINE을 넘지 않도록 분할
                int m = Rio_readnb(&s_rio, buf, (togo > MAXLINE ? MAXLINE : togo));
                if (m <= 0) return;             // 조기 EOF는 비정상
                Rio_writen(clientfd, buf, m);
                togo -= m;
            }

            // 청크 데이터 뒤에 오는 CRLF 두 바이트를 그대로 중계
            n = Rio_readnb(&s_rio, buf, 2);
            if (n <= 0) return;
            Rio_writen(clientfd, buf, n);
        }
    } else if (content_len >= 0) {
        /* 고정 길이 본문
         * - Content-Length가 주어진 경우 정확히 그 바이트 수만큼 전달
         * - readnb는 요청한 크기보다 적게 줄 수 있으므로 누적으로 보냄
         */
        long togo = content_len;
        while (togo > 0) {
            int m = Rio_readnb(&s_rio, buf, (togo > MAXLINE ? MAXLINE : togo));
            if (m <= 0) break;                  // 비정상 조기 종료 가능
            Rio_writen(clientfd, buf, m);
            togo -= m;
        }
    } else {
        /* 길이 정보 없음
         * - Connection: close 기반의 HTTP/1.0 스타일 응답
         * - 서버가 소켓을 닫을 때까지 EOF까지 읽어서 전달
         */
        while ((n = Rio_readnb(&s_rio, buf, MAXLINE)) > 0) {
            Rio_writen(clientfd, buf, n);
        }
    }
}