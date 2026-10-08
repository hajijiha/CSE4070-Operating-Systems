# CSE4070 · 운영체제

Pintos의 사용자 프로그램, 시스템 콜, 스레드 스케줄링, 가상 메모리를 구현한 운영체제 프로젝트입니다.

| 항목 | 내용 |
|---|---|
| 학교 | 서강대학교 |
| 학기 | 2025-2 |
| 과목코드 | CSE4070 |
| 개발 환경 | C / Pintos / Make |
| 공통 자료 | [강의계획서](docs/syllabus.pdf) · [Pintos 설치 안내](docs/pintos-setup.pdf) |

## 프로젝트

| 순서 | 프로젝트 | 구현 내용 | 과제 자료 |
|---|---|---|---|
| [prj1](prj1/README.md) | User Program I | 인자 전달과 사용자 메모리·시스템 콜 처리 | [과제 설명](prj1/docs/assignment.pdf) |
| [prj2](prj2/README.md) | User Program II | 파일 디스크립터와 파일 시스템 콜 | [과제 설명](prj2/docs/assignment.pdf) |
| [prj3](prj3/README.md) | Threads | Alarm Clock, 우선순위 스케줄링, MLFQS | [과제 설명](prj3/docs/assignment.pdf) |
| [prj4](prj4/README.md) | Virtual Memory | 보조 페이지 테이블, 프레임·스왑 관리 | [과제 설명](prj4/docs/assignment.pdf) |

## 저장소 구조

```text
CSE4070-Operating-Systems/
├── README.md
├── docs/syllabus.pdf
├── prj1/  # User Program I
├── prj2/  # User Program II
├── prj3/  # Threads
├── prj4/  # Virtual Memory
```

## 자료 출처

프로젝트에는 구현 소스와 수업 제공 스켈레톤·테스트 도구가 포함됩니다.
수업 제공 코드와 도구의 출처·라이선스는 각 원본 파일의 표기를 따릅니다.

[빌드 및 테스트](docs/verification.md)
