## Running run.sh

1. `/userprog` 폴더로 이동
2. 다음 명령어 실행
    ```bash
    bash scripts/run.sh -m <test_mode> -t <test_name> -c '<test_command>' -p <file_to_put>
    ```
    - test_mode: `test` 또는 `debug`, debug 모드는 vscode 디버깅을 위한 모드
    - test_name: test 디렉토리에 있는 테스트 파일 이름
        - e.g. `args-single`
    - test_command: 테스트 실행 명령어
        - e.g. `args-single onearg`
    - file_to_put: 테스트를 위해 디스크에 추가로 저장할 파일 이름, 여러 파일을 추가하려면 `-p` 옵션을 여러 번 사용하면 됨 (생략 가능)
        - e.g. `sample.txt` 

    실행하게 되면 명령어는 다음과 같은 형태이다.
    ```bash
    bash scripts/test.sh -m test -t open-normal -c 'open-normal' -p sample.txt
    ```

    위 명령어를 실행하게 되면 실제로는 bash script를 통해 다음과 같은 명령어가 실행된다.
    ```bash
    pintos --fs-disk filesys.dsk -p tests/userprog/open-normal:open-normal -p ../../tests/userprog/sample.txt:sample.txt -- -q -f run "open-normal"
    ```

## Running make check
1. `/userprog` 폴더로 이동
2. 다음 명령어 실행
    ```bash
    bash scripts/check.sh
    ```

    실행하게 되면 알아서 make clean, make, cd build, make check를 실행한다.