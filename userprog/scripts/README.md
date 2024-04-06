## Running test.sh

1. `/userprog` 폴더로 이동
2. 다음 명령어 실행
    ```bash
    bash scripts/test.sh -t <test_name> -c '<test_command>'
    ```
    - test_name: test 디렉토리에 있는 테스트 파일 이름, e.g. `args-single`
    - test_command: 테스트 실행 명령어, e.g. `args-single onearg`

    실행하게 되면 명령어는 다음과 같은 형태이다.
    ```bash
    bash scripts/test.sh -t args-single -c 'args-single onearg'
    ```

## Running debug.sh

1. `/userprog` 폴더로 이동
2. 다음 명령어 실행
    ```bash
    bash scripts/debug.sh -t <test_name> -c '<test_command>'
    ```
    test_name, test_command는 위와 동일하다.
3. vscode 디버거로 프로세스에 attach, 실행 이후는 동일