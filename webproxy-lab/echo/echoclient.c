#include "csapp.h"

int main(int argc, char **argv) {
    int clientfd; // 서버와 통신할 소켓의 파일 디스크립터
    char *host, *port, buf[MAXLINE]; // 호스트(서버 주소), 포트, 데이터 버퍼
    rio_t rio; // Robust I/O 구조체

    // 프로그램 실행 시 인자가 3개가 아니면(프로그램 이름, 호스트, 포트) 사용법을 출력하고 종료
    if (argc != 3) {
        fprintf(stderr, "usage : %s <host> <port>\n", argv[0]);
        exit(0);
    }
    host = argv[1]; // 첫 번째 인자를 호스트 이름으로 설정
    port = argv[2]; // 두 번째 인자를 포트 번호로 설정

    // 지정된 호스트와 포트로 서버에 연결을 시도하고, 연결된 소켓의 디스크립터를 얻음
    clientfd = open_clientfd(host, port);

    // rio 구조체를 초기화하고 clientfd와 연결
    rio_readinitb(&rio, clientfd);

    // 표준 입력(stdin)으로부터 한 줄씩 텍스트를 읽음
    // Fgets는 파일 끝(EOF)을 만나면 NULL을 반환하며 루프가 종료됨
    // 사용자가 터미널에서 Ctrl+D를 누르면 EOF가 입력됨
    while (Fgets(buf, MAXLINE, stdin) != NULL) {
        // 표준 입력에서 읽은 텍스트 라인을 서버로 전송
        rio_writen(clientfd, buf, strlen(buf));

        // 서버로부터 에코(echo)된 데이터를 한 줄 읽어옴
        rio_readlineb(&rio, buf, MAXLINE);

        // 서버로부터 받은 데이터를 표준 출력(stdout)에 출력
        Fputs(buf, stdout);
    }

    // 서버와의 연결을 닫음
    // 프로세스가 종료될 때 커널이 자동으로 열려있는 파일 디스크립터를 닫아주지만,
    // 명시적으로 닫아주는 것이 좋은 프로그래밍 습관
    close(clientfd);
    exit(0); // 프로그램을 정상적으로 종료
}
