echoclient.c

int main(int argc, char **argv)
    서버와 통신할 socket의 file discripter (int)
    host(server address), port, data buffer[MAXLINE]
    Robust I/O 구조체

    프로그램 실행 시 인자가 3개가 아니라면(프로그램 이름, host, port) 사용법 출력하고 종료
    if (인자가 3개가 아니라면)
        사용법 출력 fprintf(stderr, "host, port", argv[0])
        종료
    
    첫 번째 인자를 호스트 이름으로 설정
    두 번째 인자를 port 번호로 설정

    지정된 host와 port로 서버에 연결을 시도, 연결된 socket의 discripter를 얻음
    open_clientfd(host, port)

    rio구조체를 초기화 하고 clientfd와 연결

    표준 입력(stdin)으로부터 한 줄씩 텍스트를 읽음
    Fgets는 파일 끝(EOF)을 만나면 NULL을 반환하며 루프가 종료됨
    사용자가 터미널에서 Ctrl+D를 누르면 EOF가 입력됨
    while (Fgets(data buffer, 사이즈, stdin이 비어있을 동안)

        표준입력에서 읽은 텍스트 라인을 서버로 전송

        서버로부터 에코(echo)된 데이터를 한 줄 읽어옴

        서버로부터 받은 데이터를 표준 출력(stdout)에 출력

    서버와의 연결을 닫음
    
    프로그램을 정상적으로 종료


echoserveri.c

ehco 함수의 프로토 타입 선언
void echo(connection file discripter)

int main(int argc, char **argv)
    listen socket과 connect socket의 file discripter 선언
    int

    클라이언트 주소의 길이를 저장할 변수
    socketlen_t clientlen

    모든 종류의 socket주소를 저장할 수 있는 구조체 선언
    struct sockaddr_storage clientaddr;

    클라이언트의 호스트 이름과 포트 번호를 저장할 배열
    char 호스트 이름 배열[], 포트 번호 배열[]

    프로그램 실행 시 인자가 2개가 아니면(프로그램 이름, 포트 번호) 사용법 출력하고 종료
    if (argc 2개가 아니라면)
        fprintf 사용법 출력(stderr,"~~", argv[0])
        종료
    
    지정된 포트 번호로 듣기 소켓을 열고 listenfd를 얻음
    이 소켓은 클라이언트의 연결 요청을 기다림
    listenfd = 듣기 소켓 열기(argv[1])

    무한 루프를 돌면서 클라이언트의 연결을 계속 기다림
    while (1) 
        클라이언트 주소의 구조체의 크기를 설정
        clientlen


        accept 함수는 클라이언트의 연결 요청을 수락하고,
        서버와 클라이언트 간의 통신을 위한 새로운 소켓(연결 소켓)을 생성
        connfd가 이 연결 소켓의 파일 디스크립터입니다.
        connfd = accept(듣기 소켓, 클라이언트 주소, 클라이언트 주소길이)

        클라이언트의 주소 정보를 호스트 이름과 포트 번호로 변환
        getnameinfo(클라이언트 주소, 클라이언트 주소 길이, 클라이언트 호스트 이름, MAXLINE, 클라이언트 포트 번호, MAXLINE, 0)

        어떤 클라이언트와 연결되었는지 호스트 이름과 포트 번호를 출력
        printf(클라이언트 호스트 이름, 포트 번호)

        echo함수를 호출하여 클라이언트와 데이터 송수신 처리
        echo(이 소켓의 파일 디스크립터)

        echo 함수 처리가 끝나면(클라이언트가 연결을 끊으면) 연결 소켓을 닫음
        close()
    
    프로그램 종료

echo.c


echo - 클라이언트로부터 받은 데이터를 그대로 다시 클라이언트에게 보내주는 함수
connfd: 통신에 사용할 연결 소켓의 파일 디스크립터
 
 void echo(통신에 사용할 연결 소켓의 파일 디스크립터)
    읽은 바이트 수를 저장할 변수

    데이터를 읽고 저장할 버퍼

    Robust I/O 구조체

    rio 구조체를 초기화 하고 connfd와 연결
    rio_readinitb(Robust I/O구조체, 통신에 사용할 연결 소켓의 파일 디스크립터)

    클라이언트로부터 한 줄씩 데이터를 읽습니다.
    rio_readlineb는 한 줄을 읽어 buf에 저장하고, 읽은 바이트 수(개행 문자 포함)를 반환
    클라이언트가 연결을 닫으면 0을 반환하여 루프가 종료
    while(n = (rio_readlineb(Robust I/O 구조체, 버퍼, MAXLINE))이 0이 아니라면)
        서버가 몇 바이트를 받았는지 콘솔에 출력
        printf("~바이트 전송 받음" n)

        받은 데이터를 그대로 클라이언트에게 다시 보냄
        rio_written은 버퍼에 있는 n바이트의 데이터를 connfd 소켓을 통해 전송
        rio_written(connfd 소켓, 버퍼, n바이트)
    