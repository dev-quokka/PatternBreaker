-- Local development only. Back up first. Inspect results before explicitly committing.
USE `patternbreaker`;

DROP TEMPORARY TABLE IF EXISTS pb_demo_cleanup;
CREATE TEMPORARY TABLE pb_demo_cleanup (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    clear_id BIGINT UNSIGNED NOT NULL,
    keep_account BOOLEAN NOT NULL
) ENGINE=InnoDB;

START TRANSACTION;

INSERT INTO pb_demo_cleanup (user_id, clear_id, keep_account)
WITH RECURSIVE slots AS (SELECT 1 AS n UNION ALL SELECT n + 1 FROM slots WHERE n < 25)
SELECT u.id, r.id, EXISTS (SELECT 1 FROM raid_clear_records AS other WHERE other.user_id = u.id AND other.id <> r.id)
FROM slots AS s
JOIN users AS u ON BINARY u.username = BINARY CONCAT('test_player_', LPAD(s.n, 2, '0'))
JOIN raid_clear_records AS r ON r.user_id = u.id
    AND r.run_id = CONCAT('de6ddea08bb940198b461e33f04000', LPAD(LOWER(HEX(s.n)), 2, '0'))
    AND r.elapsed_ms = 7000 + 1000 * s.n AND r.cleared_at = '2025-01-01 00:00:00'
WHERE NOT EXISTS (SELECT 1 FROM raid_best_records AS b WHERE b.clear_record_id = r.id AND b.user_id <> u.id);

-- Keep accounts with additional history. If their seed is still best, use their next real clear.
UPDATE raid_best_records AS b JOIN pb_demo_cleanup AS d ON d.user_id = b.user_id AND d.clear_id = b.clear_record_id
JOIN raid_clear_records AS r ON r.id = (
    SELECT other.id FROM raid_clear_records AS other
    WHERE other.user_id = d.user_id AND other.id <> d.clear_id
    ORDER BY other.elapsed_ms, other.cleared_at, other.id LIMIT 1
)
SET b.clear_record_id = r.id, b.elapsed_ms = r.elapsed_ms, b.cleared_at = r.cleared_at
WHERE d.keep_account;

DELETE b FROM raid_best_records AS b JOIN pb_demo_cleanup AS d ON d.user_id = b.user_id AND d.clear_id = b.clear_record_id;
DELETE r FROM raid_clear_records AS r JOIN pb_demo_cleanup AS d ON d.clear_id = r.id AND d.user_id = r.user_id;
DELETE u FROM users AS u JOIN pb_demo_cleanup AS d ON d.user_id = u.id
WHERE NOT d.keep_account AND NOT EXISTS (SELECT 1 FROM raid_clear_records AS r WHERE r.user_id = u.id)
    AND NOT EXISTS (SELECT 1 FROM raid_best_records AS b WHERE b.user_id = u.id);

SELECT ROW_COUNT() AS removed_demo_accounts;
SELECT COUNT(*) AS matched_demo_clears, COALESCE(SUM(keep_account), 0) AS accounts_kept_with_other_history
FROM pb_demo_cleanup;
-- COMMIT;
