# 로컬 랭킹 시연 데이터

로컬 MySQL `127.0.0.1:3306`의 `patternbreaker` 개발 DB 전용입니다. 실행하기 전에 로컬 Drogon 서버를 종료해 백업과 변경 사이에 레이드 기록이 추가되지 않도록 합니다. Workbench에서는 같은 연결 탭에서 스크립트를 실행하고 결과를 확인한 뒤 `COMMIT;` 또는 `ROLLBACK;`을 명시적으로 실행합니다. 각 스크립트가 새 트랜잭션을 시작하므로 다음 스크립트로 넘어가기 전에 반드시 현재 트랜잭션을 종료합니다. SQL 오류가 나면 먼저 `ROLLBACK;`을 실행합니다.

## 백업과 읽기 전용 확인

PowerShell에서 로컬 백업 권한이 있는 계정을 지정합니다. `--password`는 값을 명령줄에 남기지 않고 입력 프롬프트를 띄웁니다. 백업에는 사용자 비밀번호 해시가 포함되므로 Git 작업 폴더 밖에 보관합니다.

```powershell
$backup = Join-Path $env:LOCALAPPDATA ('patternbreaker-before-demo-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.sql')
mysqldump --no-defaults --protocol=TCP --host=127.0.0.1 --port=3306 --user=root --password --single-transaction --no-tablespaces --set-gtid-purged=OFF --routines --databases patternbreaker --result-file="$backup"
if ($LASTEXITCODE -ne 0 -or (Get-Item -LiteralPath $backup).Length -eq 0) { throw 'Backup failed' }
```

먼저 `remove_testuser_demo_clear.sql`의 두 `SELECT`만 실행해 `testuser`의 `clear_id`, `run_id`, UTC 시각과 최고 기록의 참조를 확인합니다. `elapsed_ms=1`인 행이 여러 개라면 합성 기록임을 확인한 행마다 별도로 진행합니다. 결과만으로 어떤 플레이가 합성 기록인지 구분하기 어렵다면 해당 행을 보존합니다.

## 시연 기록 적용

1. 프로젝트 루트에서 아래 PowerShell을 실행합니다. 서버에 포함된 Argon2id 라이브러리로 시연 전용 해시를 만들며, 원본 암호는 32바이트 난수로 생성한 뒤 폐기합니다. 25개 시연 계정은 목록 표시용입니다. 실제 플레이는 기존 `testuser`로 진행합니다.

   ```powershell
   @'
   import ctypes as c
   import secrets
   from pathlib import Path

   dll = Path('Server/out/openssl-release/Release/argon2.dll').resolve(strict=True)
   hash_password = c.CDLL(str(dll)).argon2id_hash_encoded
   hash_password.argtypes = [c.c_uint32, c.c_uint32, c.c_uint32,
                             c.c_char_p, c.c_size_t, c.c_char_p, c.c_size_t,
                             c.c_size_t, c.c_char_p, c.c_size_t]
   hash_password.restype = c.c_int
   password, salt = secrets.token_bytes(32), secrets.token_bytes(16)
   encoded = c.create_string_buffer(255)
   if hash_password(3, 65536, 1, password, len(password), salt, len(salt), 32, encoded, len(encoded)):
       raise SystemExit('Argon2id failed')
   print("SET @demo_password_hash = '" + encoded.value.decode('ascii') + "';")
   '@ | python -
   ```

2. 출력된 `SET`을 Workbench에서 실행한 뒤 같은 연결에서 `seed_demo_ranking.sql`을 실행합니다. 신규 데이터의 예상 결과는 `seed_allowed=1`, `demo_clear_count=25`, `min_ms=8000`, `max_ms=32000`입니다. 같은 이름의 기존 계정이나 같은 `run_id`가 합성 기록과 정확히 일치하지 않으면 전체 시드를 건너뜁니다. 확인 후 커밋합니다.
3. 앞서 확인한 1ms 테스트 기록을 정리하려면 같은 연결에서 `SET @approved_clear_id = <clear_id>, @approved_run_id = '<run_id>';`를 실제 확인값으로 실행하고 `remove_testuser_demo_clear.sql`을 실행합니다. `removed_clears=1`이면 해당 한 건만 삭제 대상입니다. 삭제하는 행이 현재 최고 기록이면 다음 기록으로 교체하고, 남은 기록이 없으면 최고 기록 행만 제거합니다. 계정과 다른 이력은 유지합니다.
4. 서버를 다시 시작하고 `/ranking/top`과 로그인한 `testuser`의 `/ranking/me`를 확인합니다. 실제 기록이 이미 있으면 합성 기록과 함께 정렬되므로 전체 목록에는 25명보다 많이 표시될 수 있습니다.

## 삭제와 복구

- **커밋 전**에는 `ROLLBACK;`으로 되돌립니다.
- **시연 데이터 제거**는 `remove_demo_ranking.sql`을 실행해 결과를 확인하고 커밋합니다. 지정 이름·`run_id`·시간·UTC 시각이 모두 일치하는 합성 기록만 삭제합니다. 추가 레이드 이력이 있는 시연 계정은 보존하고 필요 시 최고 기록을 다시 연결합니다.
- **시연 데이터 재생성**은 `seed_demo_ranking.sql`을 다시 실행합니다. 삭제한 시연 계정 이름이 기존 이력과 함께 보존된 경우에는 이름 충돌로 건너뜁니다.
- **삭제한 `testuser` 기록 복구**는 백업을 별도의 로컬 MySQL 인스턴스에 먼저 복원하고, 승인했던 `clear_id`/`run_id`의 행만 추출합니다. 원본 DB에서는 해당 `testuser` ID와 `run_id` 미사용 여부를 확인한 뒤 기존 `save_raid_clear(run_id, user_id, elapsed_ms, cleared_at_epoch)`로 한 건을 재등록합니다. `cleared_at_epoch`는 백업의 UTC 시각을 `TIMESTAMPDIFF(SECOND, '1970-01-01 00:00:00', cleared_at)`으로 변환합니다. 이 방식은 새 숫자 `id`를 발급하므로 완전히 같은 `id`나 동률 순서까지 복원이 필요하면 검토한 원본 행과 최고 기록 참조를 트랜잭션으로 복구해야 합니다. 백업을 현재 개발 DB에 덮어쓰면 백업 이후의 다른 변경까지 사라질 수 있습니다.
