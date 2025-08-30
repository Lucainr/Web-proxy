- Makefile 설정 후 `make all`로 compile

- echo server 실행

```bash
./echoserver 1234
```

- `1234`는 port번호
- 연결 성공

```bash
Connect to (localhost, ~~~)
```

Terminal 하나를 더 열음


- 서버 실행에 성공했으니 서버에 연결
- `./echoclient [localhost](http://localhost) 1234` 내 IP주소(localhost) + 내가 설정했던 port 번호 입력
- 입력후 문자열 아무거나 입력
- 그럼 server를 등록했던 Terminal에
server received 4bytes 출럭이 나옴
    
    ```
    abc -> 3bytes 지만 \n 다음 행 까지 포함하여 4bytes를 받았다고 출력됨
    ```