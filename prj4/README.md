# Project 4 · Virtual Memory

보조 페이지 테이블, 지연 로딩, 프레임 선택·퇴출과 스왑 입출력을 구현합니다.

## 구조

주요 구현 파일은 `src/vm/page.*`, `src/vm/frame.*`, `src/vm/swap.*`, `src/userprog/exception.c`입니다.
`src/lib/`, `src/devices/`, `src/tests/`, Makefile과 실행 도구는 Pintos의 제공 코드와 테스트를 포함합니다.
각 프로젝트의 `src/`는 해당 단계의 독립적인 커널 소스입니다.

## 빌드와 실행

Linux x86 환경에 GCC, Binutils, Make, Perl과 Pintos용 에뮬레이터가 필요합니다.
프로젝트 폴더에서 실행합니다.

```bash
export PATH="$PWD/src/utils:$PATH"
cd src/vm
make
cd build
make check
```

`make check`에는 수업에서 구성한 Bochs 또는 호환되는 QEMU 환경이 필요합니다.
`src/utils/pintos`의 기본 시뮬레이터 설정과 설치한 환경이 일치하는지 확인합니다.
QEMU를 사용하는 개별 실행은 `pintos --qemu ...` 형태로 옵션을 지정할 수 있습니다.
사용자 프로그램 테스트에는 파일 시스템 이미지와 실행 파일 준비도 필요하므로
제공 테스트 Makefile의 절차를 사용합니다.

## 자료와 검증

- [제출 보고서](docs/report.docx)
- [Pintos 저작권 및 라이선스](LICENSE), [원 저자](AUTHORS)
- 2026-10-08: Ubuntu에서 `src/vm`의 커널 빌드 통과.
- 에뮬레이터 기반 `make check`와 부팅 테스트는 미실행입니다.
  컴파일 경고는 기존 코드에서 발생했습니다.
