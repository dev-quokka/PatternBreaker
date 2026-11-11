DELIMITER $$

CREATE PROCEDURE `patternbreaker`.`save_raid_clear` (
    IN p_run_id CHAR(32) CHARACTER SET ascii COLLATE ascii_bin,
    IN p_user_id BIGINT UNSIGNED,
    IN p_elapsed_ms INT UNSIGNED,
    IN p_cleared_at_epoch BIGINT
)
SQL SECURITY INVOKER
MODIFIES SQL DATA
BEGIN
    DECLARE v_clear_record_id BIGINT UNSIGNED DEFAULT NULL;
    DECLARE v_cleared_at DATETIME;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        RESIGNAL;
    END;

    SET v_cleared_at = TIMESTAMPADD(SECOND, p_cleared_at_epoch, '1970-01-01 00:00:00');
    START TRANSACTION;
    BEGIN
        -- Another attempt may already have inserted this run. Validate it below.
        DECLARE v_duplicate BOOLEAN DEFAULT FALSE;
        DECLARE CONTINUE HANDLER FOR 1062 SET v_duplicate = TRUE;

        INSERT INTO `patternbreaker`.`raid_clear_records` (run_id, user_id, elapsed_ms, cleared_at)
        VALUES (p_run_id, p_user_id, p_elapsed_ms, v_cleared_at);

        IF v_duplicate THEN
            SELECT id INTO v_clear_record_id
            FROM `patternbreaker`.`raid_clear_records`
            WHERE run_id = p_run_id AND user_id = p_user_id
              AND elapsed_ms = p_elapsed_ms AND cleared_at = v_cleared_at
            FOR SHARE;
            IF v_clear_record_id IS NULL THEN
                SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Raid clear record mismatch';
            END IF;
        ELSE
            SET v_clear_record_id = LAST_INSERT_ID();
        END IF;
    END;

    INSERT INTO `patternbreaker`.`raid_best_records` (user_id, elapsed_ms, cleared_at, clear_record_id)
    VALUES (p_user_id, p_elapsed_ms, v_cleared_at, v_clear_record_id) AS candidate
    ON DUPLICATE KEY UPDATE
        -- Keep elapsed_ms last so both IF expressions compare against the previous time.
        clear_record_id = IF(candidate.elapsed_ms < raid_best_records.elapsed_ms,
                             candidate.clear_record_id, raid_best_records.clear_record_id),
        cleared_at = IF(candidate.elapsed_ms < raid_best_records.elapsed_ms,
                        candidate.cleared_at, raid_best_records.cleared_at),
        elapsed_ms = LEAST(candidate.elapsed_ms, raid_best_records.elapsed_ms);

    COMMIT;
END$$

DELIMITER ;
