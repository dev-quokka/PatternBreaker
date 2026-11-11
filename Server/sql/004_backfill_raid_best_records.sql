INSERT INTO `patternbreaker`.`raid_best_records` (`user_id`, `elapsed_ms`, `cleared_at`, `clear_record_id`)
SELECT candidate.user_id, candidate.elapsed_ms, candidate.cleared_at, candidate.id
FROM (
    SELECT user_id, elapsed_ms, cleared_at, id,
           ROW_NUMBER() OVER (PARTITION BY user_id ORDER BY elapsed_ms, cleared_at, id) AS record_number
    FROM `patternbreaker`.`raid_clear_records`
) AS candidate
WHERE candidate.record_number = 1
ON DUPLICATE KEY UPDATE
    clear_record_id = IF(
        (candidate.elapsed_ms, candidate.cleared_at, candidate.id) <
        (raid_best_records.elapsed_ms, raid_best_records.cleared_at, raid_best_records.clear_record_id),
        candidate.id, raid_best_records.clear_record_id),
    cleared_at = IF(
        (candidate.elapsed_ms, candidate.cleared_at) < (raid_best_records.elapsed_ms, raid_best_records.cleared_at),
        candidate.cleared_at, raid_best_records.cleared_at),
    elapsed_ms = LEAST(candidate.elapsed_ms, raid_best_records.elapsed_ms);
