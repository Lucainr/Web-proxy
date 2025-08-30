#include "csapp.h"
#include "echo.c"

// echo 함수의 프로토타입 선언
void echo(int connfd);

int main(int argc, char **argv) {
    int listenfd, connfd; // 듣기 소켓과 연결 소켓의 파일 디스크립터
    socklen_t clientlen; // 클라이언트 주소의 길이를 저장할 변수
    struct sockaddr_storage clientaddr; // 모든 종류의 소켓 주소를 저장할 수 있는 구조체
    char client_hostname[MAXLINE], client_port[MAXLINE]; // 클라이언트의 호스트 이름과 포트 번호를 저장할 배열

    // 프로그램 실행 시 인자가 2개가 아니면(프로그램 이름, 포트 번호) 사용법을 출력하고 종료
    if (argc != 2) {
        fprintf(stderr, "usage : %s <port>\n", argv[0]);
        exit(0);
    }

    // 지정된 포트 번호로 듣기 소켓을 열고 listenfd를 얻음
    // 이 소켓은 클라이언트의 연결 요청을 기다림
    listenfd = open_listenfd(argv[1]);

    // 무한 루프를 돌면서 클라이언트의 연결을 계속해서 기다림
    while (1) {
        // 클라이언트 주소 구조체의 크기를 설정합니다.
        clientlen = sizeof(struct sockaddr_storage);

        // accept 함수는 클라이언트의 연결 요청을 수락하고,
        // 서버와 클라이언트 간의 통신을 위한 새로운 소켓(연결 소켓)을 생성
        // connfd가 이 연결 소켓의 파일 디스크립터입니다.
        connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);

        // 클라이언트의 주소 정보를 호스트 이름과 포트 번호로 변환
        getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);

        // 어떤 클라이언트와 연결되었는지 호스트 이름과 포트 번호를 출력
        printf("Connect to (%s, %s)\n", client_hostname, client_port);

        // echo 함수를 호출하여 클라이언트와 데이터 송수신을 처리
        echo(connfd);

        // echo 함수 처리가 끝나면(클라이언트가 연결을 끊으면) 연결 소켓을 닫는다
        close(connfd);
    }
    // 프로그램이 정상적으로 종료됨을 나타냄 (실제로는 무한 루프로 인해 이 줄은 실행되지 않음)
    exit(0);
}