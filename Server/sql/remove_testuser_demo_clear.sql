-- Local development only. First run the SELECT queries and identify the exact synthetic run.
-- Set @approved_clear_id and @approved_run_id manually in this connection after review.
-- This script leaves the transaction open for inspection. COMMIT or ROLLBACK explicitly.
USE `patternbreaker`;

SELECT u.id AS user_id, r.id AS clear_id, r.run_id, r.elapsed_ms, r.cleared_at,
       b.user_id AS referencing_user_id, b.clear_record_id AS best_source_id
FROM users AS u JOIN raid_clear_records AS r ON r.user_id = u.id
LEFT JOIN raid_best_records AS b ON b.clear_record_id = r.id
WHERE BINARY u.username = BINARY 'testuser' AND r.elapsed_ms = 1
ORDER BY r.cleared_at, r.id;

SELECT b.user_id, b.elapsed_ms, b.cleared_at, b.clear_record_id
FROM users AS u JOIN raid_best_records AS b ON b.user_id = u.id
WHERE BINARY u.username = BINARY 'testuser';

START TRANSACTION;

SET @testuser_id = NULL, @remove_clear_id = NULL, @replacement_id = NULL, @best_source = NULL;
SELECT id INTO @testuser_id FROM users WHERE username = 'testuser' AND BINARY username = BINARY 'testuser' FOR UPDATE;
SELECT id INTO @remove_clear_id FROM raid_clear_records
WHERE id = @approved_clear_id AND run_id = @approved_run_id AND user_id = @testuser_id AND elapsed_ms = 1
FOR UPDATE;
SELECT clear_record_id INTO @best_source FROM raid_best_records WHERE user_id = @testuser_id FOR UPDATE;

SET @remove_allowed = @remove_clear_id IS NOT NULL AND NOT EXISTS (
    SELECT 1 FROM raid_best_records WHERE clear_record_id = @remove_clear_id AND user_id <> @testuser_id
);
SELECT id INTO @replacement_id FROM raid_clear_records
WHERE @remove_allowed AND @best_source = @remove_clear_id AND user_id = @testuser_id AND id <> @remove_clear_id
ORDER BY elapsed_ms, cleared_at, id LIMIT 1 FOR UPDATE;

UPDATE raid_best_records AS b JOIN raid_clear_records AS r ON r.id = @replacement_id
SET b.clear_record_id = r.id, b.elapsed_ms = r.elapsed_ms, b.cleared_at = r.cleared_at
WHERE @remove_allowed AND b.user_id = @testuser_id AND b.clear_record_id = @remove_clear_id;

DELETE FROM raid_best_records
WHERE @remove_allowed AND user_id = @testuser_id AND clear_record_id = @remove_clear_id AND @replacement_id IS NULL;

DELETE FROM raid_clear_records WHERE @remove_allowed AND id = @remove_clear_id AND user_id = @testuser_id;
SELECT ROW_COUNT() AS removed_clears, @testuser_id AS kept_user_id, @replacement_id AS replacement_clear_id;

SET @approved_clear_id = NULL, @approved_run_id = NULL;
-- COMMIT;
