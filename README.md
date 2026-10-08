# CSE4070 · 운영체제

Pintos 커널을 단계별로 확장한 운영체제 프로젝트 모음입니다. 아래 번호는 이 수업의 실제 제출 단계에 맞췄습니다.

| 항목 | 내용 |
|---|---|
| 학교 | 서강대학교 |
| 학기 | 2025-2 |
| 과목코드 | CSE4070 |
| 개발 환경 | C / Pintos / Make |
| 공통 자료 | [강의계획서](docs/syllabus.pdf) |

## 프로젝트

| 순서 | 프로젝트 | 구현 내용 |
|---|---|---|
| [prj1](prj1/README.md) | User Program I | 인자 전달과 사용자 메모리·시스템 콜 처리 |
| [prj2](prj2/README.md) | User Program II | 파일 디스크립터와 파일 시스템 콜 |
| [prj3](prj3/README.md) | Threads | Alarm Clock, 우선순위 스케줄링, MLFQS |
| [prj4](prj4/README.md) | Virtual Memory | 보조 페이지 테이블, 프레임·스왑 관리 |

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

프로젝트별 README에서 구현 파일, 실행 명령, 관련 문서와 검증 범위를 확인할 수 있습니다.
학기와 과목코드는 해당 학기의 강의계획서를 기준으로 기록했습니다.

## 자료 출처

제출본과 수업 제공 스켈레톤·테스트 도구를 함께 정리했습니다. 제공 코드와 팀 코드의
저작권 표시를 유지하고, 직접 구현한 부분은 프로젝트별 README에 구분했습니다.

[정리 시 검증 기록](docs/verification.md)
