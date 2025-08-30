#include "csapp.h"

/*
 * echo - 클라이언트로부터 받은 데이터를 그대로 다시 클라이언트에게 보내주는 함수
 * connfd: 통신에 사용할 연결 소켓의 파일 디스크립터
 */
void echo(int connfd) {
    size_t n; // 읽은 바이트 수를 저장할 변수
    char buf[MAXLINE]; // 데이터를 읽고 저장할 버퍼
    rio_t rio; // Robust I/O 구조체

    // rio 구조체를 초기화하고 connfd와 연결
    // 이제 rio_readlineb와 같은 함수를 사용하여 이 소켓에서 안전하게 읽을 수 있음
    rio_readinitb(&rio, connfd);

    // 클라이언트로부터 한 줄씩 데이터를 읽습니다.
    // rio_readlineb는 한 줄을 읽어 buf에 저장하고, 읽은 바이트 수(개행 문자 포함)를 반환
    // 클라이언트가 연결을 닫으면 0을 반환하여 루프가 종료
    while((n = rio_readlineb(&rio, buf, MAXLINE)) != 0) {
        // 서버가 몇 바이트를 받았는지 콘솔에 출력
        printf("서버가 %d bytes 를 전송받음\n", (int)n);

        // 받은 데이터를 그대로 클라이언트에게 다시 보냄
        // rio_writen은 buf에 있는 n바이트의 데이터를 connfd 소켓을 통해 전송
        rio_writen(connfd, buf, n);
    }
}
