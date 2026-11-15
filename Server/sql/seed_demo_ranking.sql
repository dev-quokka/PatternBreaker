-- Local development only. Back up first and follow demo_ranking.md.
-- Supply @demo_password_hash as an Argon2id PHC string generated from a discarded random password.
-- Inspect the final result, then explicitly COMMIT or ROLLBACK in this same session.
USE `patternbreaker`;

DROP TEMPORARY TABLE IF EXISTS pb_demo_manifest;
CREATE TEMPORARY TABLE pb_demo_manifest (
    slot INT NOT NULL PRIMARY KEY,
    username VARCHAR(32) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
    run_id CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    elapsed_ms INT UNSIGNED NOT NULL,
    existing_user_id BIGINT UNSIGNED NULL,
    existing_clear_id BIGINT UNSIGNED NULL
) ENGINE=InnoDB;

INSERT INTO pb_demo_manifest (slot, username, run_id, elapsed_ms)
WITH RECURSIVE slots AS (SELECT 1 AS n UNION ALL SELECT n + 1 FROM slots WHERE n < 25)
SELECT n, CONCAT('test_player_', LPAD(n, 2, '0')),
       CONCAT('de6ddea08bb940198b461e33f04000', LPAD(LOWER(HEX(n)), 2, '0')),
       7000 + 1000 * n
FROM slots;

START TRANSACTION;

UPDATE pb_demo_manifest AS m
LEFT JOIN users AS u ON u.username = m.username
LEFT JOIN raid_clear_records AS r ON r.run_id = m.run_id
SET m.existing_user_id = u.id, m.existing_clear_id = r.id;

-- A matching username alone never identifies an account as belonging to this seed.
SELECT m.username, m.run_id, m.existing_user_id, m.existing_clear_id
FROM pb_demo_manifest AS m
LEFT JOIN users AS u ON u.id = m.existing_user_id
LEFT JOIN raid_clear_records AS r ON r.id = m.existing_clear_id
WHERE (m.existing_user_id IS NOT NULL OR m.existing_clear_id IS NOT NULL)
  AND (r.id IS NULL OR u.id IS NULL OR BINARY u.username <> BINARY m.username
       OR r.user_id <> u.id OR r.elapsed_ms <> m.elapsed_ms OR r.cleared_at <> '2025-01-01 00:00:00');

SET @demo_manifest_count = (SELECT COUNT(*) FROM pb_demo_manifest);
SET @demo_new_count = (SELECT COUNT(*) FROM pb_demo_manifest WHERE existing_user_id IS NULL);
SET @demo_no_conflicts = (SELECT COUNT(*) = 0
         FROM pb_demo_manifest AS m
         LEFT JOIN users AS u ON u.id = m.existing_user_id
         LEFT JOIN raid_clear_records AS r ON r.id = m.existing_clear_id
         WHERE (m.existing_user_id IS NOT NULL OR m.existing_clear_id IS NOT NULL)
           AND (r.id IS NULL OR u.id IS NULL OR BINARY u.username <> BINARY m.username
                OR r.user_id <> u.id OR r.elapsed_ms <> m.elapsed_ms OR r.cleared_at <> '2025-01-01 00:00:00'));
SET @demo_seed_safe = @demo_manifest_count = 25 AND @demo_no_conflicts
    AND (@demo_new_count = 0 OR COALESCE(REGEXP_LIKE(CONVERT(@demo_password_hash USING ascii),
           '^[$]argon2id[$]v=19[$]m=[0-9]+,t=[0-9]+,p=[0-9]+[$][A-Za-z0-9+/]+[$][A-Za-z0-9+/]+$', 'c'), 0));

INSERT INTO users (username, password_hash)
SELECT username, @demo_password_hash FROM pb_demo_manifest
WHERE @demo_seed_safe = 1 AND existing_user_id IS NULL;

INSERT INTO raid_clear_records (run_id, user_id, elapsed_ms, cleared_at)
SELECT m.run_id, u.id, m.elapsed_ms, '2025-01-01 00:00:00'
FROM pb_demo_manifest AS m JOIN users AS u ON BINARY u.username = BINARY m.username
WHERE @demo_seed_safe = 1 AND m.existing_clear_id IS NULL;

-- For a fresh account the seed is its only clear. Reruns preserve any existing best row.
INSERT INTO raid_best_records (user_id, elapsed_ms, cleared_at, clear_record_id)
SELECT r.user_id, r.elapsed_ms, r.cleared_at, r.id
FROM pb_demo_manifest AS m JOIN raid_clear_records AS r ON r.run_id = m.run_id
WHERE @demo_seed_safe = 1 AND m.existing_user_id IS NULL;

SELECT @demo_seed_safe AS seed_allowed, COUNT(r.id) AS demo_clear_count,
       MIN(r.elapsed_ms) AS min_ms, MAX(r.elapsed_ms) AS max_ms
FROM pb_demo_manifest AS m JOIN raid_clear_records AS r ON r.run_id = m.run_id;

-- Expected on a fresh seed: 1 / 25 / 8000 / 32000. On any error, ROLLBACK.
-- COMMIT;
