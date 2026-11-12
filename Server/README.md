# PatternBreaker HTTP 서버

Windows에서 독립 실행되는 C++17 Drogon 1.9.13 서버입니다. `127.0.0.1:18080`에서 HTTP 요청을 받으며, MySQL 연결에는 OpenSSL 기반 MariaDB Connector/C를 사용합니다.

## 정식 Release 빌드

Visual Studio 2022의 **C++를 사용한 데스크톱 개발** 워크로드, CMake 3.21 이상, vcpkg가 필요합니다. vcpkg가 없다면 [Microsoft의 vcpkg 설치 안내](https://learn.microsoft.com/vcpkg/get_started/overview)에 따라 설치하세요.

프로젝트 루트에서 아래 PowerShell 명령을 실행하세요. 정식 빌드 디렉터리는 `Server/out/openssl-release`이며, vcpkg 의존성은 그 아래 `vcpkg_installed`에 설치됩니다. `C:\vcpkg`의 도구 체인이 매니페스트의 `drogon[mysql]`과 `libmariadb[openssl]`을 사용합니다.

```powershell
cmake -S Server -B Server/out/openssl-release -G 'Visual Studio 17 2022' -A x64 `
  '-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake' `
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build Server/out/openssl-release --config Release
```

실행 파일은 `Server/out/openssl-release/Release/PatternBreakerServer.exe`입니다. 빌드 시 `libmariadb.dll`, OpenSSL DLL, MySQL 8 기본 인증 플러그인 `caching_sha2_password.dll`을 실행 파일과 함께 배치합니다.

## MySQL 환경 변수

MySQL을 실행하고 `patternbreaker_app` 계정으로 `127.0.0.1:3306`의 `patternbreaker` 데이터베이스에 접속할 수 있는지 확인하세요. 서버를 실행할 PowerShell에서 아래 변수를 설정합니다. 비밀번호는 입력 프롬프트에 입력하며, 변수는 현재 PowerShell과 여기서 시작한 프로세스에 적용됩니다.

```powershell
$env:PB_DB_HOST = '127.0.0.1'
$env:PB_DB_PORT = '3306'
$env:PB_DB_NAME = 'patternbreaker'
$env:PB_DB_USER = 'patternbreaker_app'
$pbDatabasePassword = Read-Host 'MySQL password' -AsSecureString
$env:PB_DB_PASSWORD = [System.Net.NetworkCredential]::new('', $pbDatabasePassword).Password
Remove-Variable pbDatabasePassword
```

환경 변수를 설정한 같은 PowerShell에서 실행 파일을 시작하세요. `MARIADB_PLUGIN_DIR`은 이 빌드의 인증 플러그인 폴더를 가리킵니다. 설정을 바꾼 뒤에는 서버를 다시 시작합니다. 다른 빌드가 `18080` 포트를 사용 중이면 먼저 그 서버를 종료하세요.

```powershell
$pbServerRelease = (Resolve-Path '.\Server\out\openssl-release\Release').Path
$env:MARIADB_PLUGIN_DIR = $pbServerRelease
& (Join-Path $pbServerRelease 'PatternBreakerServer.exe')
```

### Visual Studio 2022에서 실행 및 디버깅

위에서 생성한 솔루션을 환경 변수가 설정된 PowerShell에서 Visual Studio로 여세요. Community 이외의 에디션이라면 `devenv.exe` 경로를 해당 설치 위치로 바꾸세요.

```powershell
$pbServerBuild = (Resolve-Path '.\Server\out\openssl-release').Path
$env:MARIADB_PLUGIN_DIR = Join-Path $pbServerBuild 'Release'
$pbServerSolution = Join-Path $pbServerBuild 'PatternBreakerServer.sln'
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.exe' $pbServerSolution
```

솔루션 구성을 **Release / x64**로 선택하고 `PatternBreakerServer`를 시작 프로젝트로 지정한 뒤 `Ctrl+F5`로 실행하세요. 중단점 디버깅이 필요하면 위 PowerShell에서 `MARIADB_PLUGIN_DIR`의 `Release`를 `RelWithDebInfo`로 바꿔 새 Visual Studio 인스턴스를 시작하고, **RelWithDebInfo / x64**에서 `F5`를 누르세요. 이 구성은 디버그 심볼을 생성합니다.

이미 실행 중인 Visual Studio는 새 환경 변수를 상속하지 않으므로, 위 PowerShell에서 새 인스턴스를 시작하세요. 비밀번호는 PowerShell 프롬프트에 입력하고, Visual Studio 설정 파일이나 저장소의 파일에 기록하지 마세요.

## 응답 확인

서버가 실행 중인 상태에서 다른 PowerShell 창을 열어 실행하세요.

```powershell
curl.exe -i http://127.0.0.1:18080/health
curl.exe -i http://127.0.0.1:18080/health/db
```

| 요청 | 상태 | 본문 |
| --- | --- | --- |
| `GET /health` | `200 OK` | `{"status":"ok"}` |
| `GET /health/db` · `SELECT 1` 성공 | `200 OK` | `{"database":"ok"}` |
| `GET /health/db` · 설정 누락, 접속 불가 또는 쿼리 실패 | `503 Service Unavailable` | `{"database":"unavailable"}` |

DB 클라이언트는 시작 시 연결을 비동기로 수립하므로, 시작 직후 `/health/db`가 503이면 잠시 뒤 다시 요청하세요. 서버는 실행한 창에서 `Ctrl+C`로 종료합니다. 테스트가 끝나면 `Remove-Item Env:PB_DB_PASSWORD`로 현재 PowerShell의 비밀번호 변수를 지울 수 있습니다.

## 회원가입 API

MySQL Workbench에서 [테이블 생성 SQL](sql/001_create_users.sql)을 검토한 뒤 `patternbreaker` 데이터베이스에서 직접 실행하세요. `PB_DB_USER` 계정에 `patternbreaker.users`에 대한 `INSERT` 권한이 있어야 합니다. `/health/db`로 접속을 확인한 뒤, 사용하지 않은 계정명으로 아래 요청을 보내세요. 예시 비밀번호는 테스트용입니다.

```powershell
'{"username":"testuser","password":"examplepassword"}' |
  curl.exe -i -H 'Content-Type: application/json' --data-binary '@-' http://127.0.0.1:18080/auth/register
```

계정명은 영문 대소문자, 숫자, 밑줄로 이루어진 3~32자입니다. 중복 판정은 대소문자를 구분하지 않습니다. 비밀번호는 ASCII 제어 문자를 제외한 12~128 UTF-8 바이트이며, JSON 본문은 최대 4096바이트입니다.

| 상태 | JSON 본문 | 조건 |
| --- | --- | --- |
| `201 Created` | `{"status":"registered"}` | 계정 저장 완료 |
| `400 Bad Request` | `{"status":"invalid_request"}` | JSON 또는 입력 규칙 오류 |
| `409 Conflict` | `{"status":"username_taken"}` | 계정명 중복 |
| `503 Service Unavailable` | `{"status":"database_unavailable"}` | DB 연결 또는 저장 실패 |
| `503 Service Unavailable` | `{"status":"registration_busy"}` 또는 `{"status":"registration_unavailable"}` | 해싱 대기열 포화 또는 해싱 실패 |

같은 요청을 다시 보내면 `409`가 반환됩니다. 저장되는 값은 요청마다 생성한 솔트를 포함하는 Argon2id 인코딩 해시입니다.

## 로그인과 JWT 인증

서버를 시작할 PowerShell에서 아래 명령으로 암호학적으로 안전한 32바이트 난수를 생성해 `PB_JWT_SECRET`에 64자리 16진수로 설정하세요. `PB_DB_*`를 설정한 같은 PowerShell에서 서버나 Visual Studio를 시작합니다. 키는 현재 프로세스와 자식 프로세스에만 전달됩니다.

```powershell
$pbJwtBytes = New-Object byte[] 32
$pbJwtRandom = [System.Security.Cryptography.RandomNumberGenerator]::Create()
try {
    $pbJwtRandom.GetBytes($pbJwtBytes)
    $env:PB_JWT_SECRET = -join ($pbJwtBytes | ForEach-Object { $_.ToString('x2') })
} finally {
    $pbJwtRandom.Dispose()
    [Array]::Clear($pbJwtBytes, 0, $pbJwtBytes.Length)
    Remove-Variable pbJwtBytes, pbJwtRandom
}
```

키를 명령 출력이나 파일에 남기지 마세요. 새 키로 서버를 다시 시작하면 이전 키로 발급한 토큰은 거부됩니다. 키가 누락됐거나 형식이 잘못되면 로그인과 `/auth/me`는 `503`을 반환합니다. 서버는 `127.0.0.1`에서만 요청을 받습니다.

`PB_DB_USER`는 기존 `users` 테이블을 조회할 `SELECT` 권한이 필요합니다. 서버 실행 후 등록된 계정으로 아래 요청을 보내세요. 비밀번호는 숨김 프롬프트에 입력하고, 로그인 응답은 변수에 받아 토큰을 화면에 출력하지 않습니다.

```powershell
$pbUser = Read-Host 'Username'
$pbPassword = Read-Host 'Account password' -AsSecureString
try {
    $pbLogin = Invoke-RestMethod -Method Post -Uri 'http://127.0.0.1:18080/auth/login' `
      -ContentType 'application/json' -Body (@{
        username = $pbUser
        password = [System.Net.NetworkCredential]::new('', $pbPassword).Password
      } | ConvertTo-Json -Compress) -ErrorAction Stop
    $pbLogin | Select-Object user_id, username, token_type, expires_in
    Invoke-RestMethod -Uri 'http://127.0.0.1:18080/auth/me' `
      -Headers @{ Authorization = "Bearer $($pbLogin.access_token)" }
} finally {
    Remove-Variable pbPassword, pbLogin -ErrorAction SilentlyContinue
}
```

로그인 성공 시 HS256 토큰이 발급됩니다. `sub`에는 사용자 ID, `iss`에는 `patternbreaker-server`, `aud`에는 `patternbreaker-api`가 들어갑니다. `iat`부터 `exp`까지의 유효기간은 3600초입니다. `/auth/me`는 토큰을 검증한 뒤 DB에서 사용자 이름을 읽습니다.

잘못된 비밀번호와 존재하지 않는 계정은 모두 `401`과 `{"status":"invalid_credentials"}`를 반환합니다. 존재하지 않는 계정에도 Argon2id 검증을 수행해 응답 시간 차이를 줄입니다. 잘못된 JSON은 `400`, DB 장애는 `503`입니다. `/auth/me`에 서명이 바뀐 토큰이나 만료된 토큰을 보내면 `401`을 반환합니다.

로그인 요청 제한은 서버 프로세스 전체에서 최근 60초에 10회입니다. 유효한 형식으로 인증 설정과 DB 확인을 통과한 시도부터 집계하며, 초과 시 `429`를 반환합니다. 루프백의 모든 사용자와 계정이 한도를 공유하고, 서버를 재시작하면 집계가 초기화됩니다. 여러 서버 프로세스 간에는 한도를 공유하지 않습니다. Argon2id 작업 큐는 회원가입과 로그인이 공유하며, 대기열이 가득 차면 `503`을 반환합니다.

## 레이드 세션과 클리어 기록

`raid_clear_records`는 성공한 클리어의 전체 이력을 보관합니다. `raid_best_records`에는 랭킹에 쓰는 사용자별 최고 기록만 저장하므로 조회할 때마다 전체 이력을 집계할 필요가 없습니다. MySQL 설치 순서와 계정 권한은 아래 랭킹 설정 절을 참고하세요.

`POST /raid/start`는 Bearer JWT의 `sub`를 사용자 ID로 사용합니다. OpenSSL 난수 16바이트로 만든 32자리 `run_id`와 `steady_clock` 시작 시각을 `RaidSession`에 등록하고, `201`과 `run_id`, `time_limit_seconds: 120`을 반환합니다. 시작 요청에 JSON 본문은 필요 없습니다.

`POST /raid/finish`는 같은 JWT 사용자와 `run_id`의 소유권을 확인합니다. `result`는 `clear` 또는 `dead`를 받습니다. `dead`는 세션을 종료하고 `200`을 반환합니다. 최초 `clear`는 120초 미만일 때만 수락하며, 서버의 `steady_clock` 차이를 밀리초로 변환하고 클리어 시각을 UTC로 확정합니다. 응답의 `elapsed_ms`는 MySQL에 저장이 확인된 뒤 전달됩니다. 요청에 `user_id`, 시간, 점수가 포함돼도 기록에는 사용하지 않습니다.

세션과 사용자별 인덱스를 하나의 뮤텍스로 보호합니다. 상태는 `Active`, `Saving`, `AwaitingRetry`입니다. `Saving`으로 먼저 전환한 뒤 잠금을 풀고 파라미터를 바인딩해 `CALL save_raid_clear(?, ?, ?, ?)`를 비동기로 실행합니다. 프로시저 내부의 InnoDB 트랜잭션이 이력과 최고 기록을 함께 커밋합니다. 런타임 SQL 오류가 발생하면 롤백합니다. 이 구조는 DB 연결이 끊겨 Drogon이 다시 연결하더라도 이력과 최고 기록의 쓰기가 서로 다른 연결에서 실행되는 상황을 피합니다.

`UNIQUE (run_id)`가 중복 이력을 차단합니다. 서버가 DB 커밋을 확인하지 못한 경우 같은 `run_id`로 재시도하면 프로시저가 기존 행의 사용자, 시간과 UTC 시각을 대조합니다. 서버가 커밋을 확인했고 클라이언트가 응답을 받지 못한 경우에는 같은 사용자의 `clear` 재시도에 원래 `elapsed_ms`와 `200`을 반환합니다. 이 경우 DB 저장은 한 번만 실행됩니다.

저장 실패나 타임아웃 시 `503`에 `retryable`이 포함됩니다. `true`면 최초 클리어 요청 후 10분 안에 같은 `run_id`와 `result: clear`로 다시 요청하세요. 최초 경과 시간과 UTC 시각을 유지합니다. DB 콜백이 도착하지 않아도 시도별 최대 8초 후 `Saving`에서 복구합니다. 응답 타이머, 요청 시 만료 검사와 30초 주기 정리를 사용하며, 늦은 콜백은 시도 번호가 다른 저장 상태를 변경할 수 없습니다. 성공 응답은 기한 내 커밋 확인을 받은 경우에만 반환합니다.

진행·저장·재시도를 합쳐 최대 4,096개를 유지합니다. 종료 확인과 응답 재전송에 필요한 데이터는 최대 4,096건을 최대 10분간 보관합니다. 서버 재시작 시 메모리의 세션과 종료 확인 데이터는 사라지며, MySQL에 커밋된 행은 유지됩니다.

| HTTP | `status` | 조건 |
| --- | --- | --- |
| `201` | `started` | 레이드 시작 |
| `200` | `clear` / `dead` | 클리어 기록 저장 확인 / 사망 종료 |
| `400` | `invalid_request` | 종료 JSON 또는 값 형식 오류 |
| `401` | `invalid_token` | JWT 누락, 검증 실패, 만료 |
| `404` | `raid_not_found` | 알 수 없거나 다른 사용자의 `run_id` |
| `409` | `raid_already_active` / `raid_saving` / `raid_finished` / `raid_expired` | 세션 충돌, 중복 요청 또는 만료 |
| `503` | `database_unavailable` / `auth_unavailable` / `raid_unavailable` | 저장 실패, JWT 설정 오류 또는 세션 한도·난수 생성 실패 |

### PowerShell에서 확인

기존 `PB_DB_*`와 `PB_JWT_SECRET`으로 서버를 실행하고 `/health/db`의 `200`을 확인하세요. 아래 명령은 실제 클리어 기록을 한 건 생성합니다. 사망 경로는 새 레이드에서 `result = 'dead'`로 시험할 수 있습니다.

```powershell
$pbUser = Read-Host 'Username'
$pbPassword = Read-Host 'Account password' -AsSecureString
try {
    $pbLogin = Invoke-RestMethod -Method Post -Uri 'http://127.0.0.1:18080/auth/login' `
      -ContentType 'application/json' -Body (@{
        username = $pbUser
        password = [System.Net.NetworkCredential]::new('', $pbPassword).Password
      } | ConvertTo-Json -Compress) -ErrorAction Stop
    $pbHeaders = @{ Authorization = "Bearer $($pbLogin.access_token)" }
    $pbRaid = Invoke-RestMethod -Method Post -Uri 'http://127.0.0.1:18080/raid/start' -Headers $pbHeaders
    $pbFinishBody = @{ run_id = $pbRaid.run_id; result = 'clear' } | ConvertTo-Json -Compress
    Invoke-RestMethod -Method Post -Uri 'http://127.0.0.1:18080/raid/finish' `
      -Headers $pbHeaders -ContentType 'application/json' -Body $pbFinishBody
} finally {
    Remove-Variable pbPassword, pbLogin, pbHeaders -ErrorAction SilentlyContinue
}
```

Workbench에서 아래 조회로 저장된 기록을 확인하세요.

```sql
SELECT run_id, user_id, elapsed_ms, cleared_at
FROM patternbreaker.raid_clear_records
ORDER BY id DESC
LIMIT 10;
```

서버는 소유권과 경과 시간을 검증하지만, Unreal 클라이언트가 실제로 보스를 처치했는지는 확인하지 못합니다. 인증된 사용자가 일찍 `clear`를 보내면 짧은 시간이 기록될 수 있으므로, 이 구조만으로 부정행위를 막을 수 없습니다.

## 레이드 랭킹

### Workbench 설정 순서

서버를 중지한 상태에서 Workbench로 MySQL 8.0에 접속한 뒤 아래 파일을 순서대로 실행하세요. 기존 설치에는 `001`과 `002`가 적용되어 있어야 합니다.

1. [003_create_raid_best_records.sql](sql/003_create_raid_best_records.sql)로 최고 기록 테이블을 생성합니다. `user_id` 기본 키가 사용자당 한 행을 보장합니다. `elapsed_ms`, UTC `cleared_at`, 이력을 가리키는 `clear_record_id`를 보관하며, `(elapsed_ms, cleared_at, clear_record_id)` 인덱스를 랭킹 정렬에 사용합니다.
2. [004_backfill_raid_best_records.sql](sql/004_backfill_raid_best_records.sql)로 기존 이력을 초기 반영합니다. 사용자마다 시간, UTC 달성 시각, 이력 ID 오름차순으로 첫 행을 선택합니다. 같은 이력으로 다시 실행해도 중복 행이 생기지 않습니다.
3. [005_create_save_raid_clear.sql](sql/005_create_save_raid_clear.sql) 전체를 한 번 실행해 프로시저를 등록합니다. 이후 `PB_DB_NAME=patternbreaker`로 서버를 시작합니다. `PB_DB_USER`에는 기존 `users` 조회 권한, `raid_clear_records`의 `INSERT`·`SELECT`, `raid_best_records`의 `INSERT`·`UPDATE`·`SELECT`, `patternbreaker.save_raid_clear`의 `EXECUTE` 권한이 필요합니다.

새 클리어의 시간은 기존 최고 기록보다 빠를 때만 반영합니다. 첫 클리어는 최고 기록을 생성하고, 동일하거나 느린 시간은 이력에만 남습니다. 예를 들어 같은 사용자가 80초, 75초, 90초, 70초로 클리어하면 이력은 네 건이고 최고 기록은 70초입니다.

### 랭킹 API 확인

`GET /ranking/top`은 인증 없이 조회할 수 있습니다. 최고 기록을 시간, UTC 달성 시각, 이력 ID 오름차순으로 최대 100명까지 읽고 `users`를 조인합니다. 기록이 없으면 `{"rankings":[]}`를 반환합니다.

```powershell
$pbBase = 'http://127.0.0.1:18080'
Invoke-RestMethod "$pbBase/ranking/top"
```

`GET /ranking/me`는 JWT의 `sub`에 해당하는 사용자를 조회합니다. 기록이 없는 사용자는 `best_record: null`을 받습니다. 위 레이드 테스트의 `try` 블록에서 로그인 직후 또는 종료 요청 뒤에 개인 조회를 추가하면 토큰을 출력하지 않고 확인할 수 있습니다.

```powershell
Invoke-RestMethod 'http://127.0.0.1:18080/ranking/me' -Headers $pbHeaders
```

```json
{"rankings":[{"rank":1,"user_id":1,"username":"testuser","elapsed_ms":45000,"cleared_at":"2026-11-12T10:00:00Z"}]}
```

```json
{"user_id":1,"username":"testuser","best_record":{"elapsed_ms":45000,"cleared_at":"2026-11-12T10:00:00Z"}}
```

`401`은 `{"status":"invalid_token"}`을 반환합니다. DB 접속·쿼리 실패는 `503`과 `{"status":"database_unavailable"}`을 반환하며, `/ranking/me`의 JWT 설정 오류는 `503`과 `{"status":"auth_unavailable"}`을 반환합니다.

Workbench에서 최고 기록과 원본 클리어 이력을 함께 확인할 수 있습니다.

```sql
SELECT u.username, b.elapsed_ms, b.cleared_at, b.clear_record_id, h.run_id
FROM patternbreaker.raid_best_records AS b
JOIN patternbreaker.users AS u ON u.id = b.user_id
JOIN patternbreaker.raid_clear_records AS h ON h.id = b.clear_record_id
ORDER BY b.elapsed_ms, b.cleared_at, b.clear_record_id
LIMIT 100;
```
