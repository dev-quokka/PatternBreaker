CREATE TABLE IF NOT EXISTS `patternbreaker`.`raid_best_records` (
    `user_id` BIGINT UNSIGNED NOT NULL,
    `elapsed_ms` INT UNSIGNED NOT NULL,
    `cleared_at` DATETIME NOT NULL COMMENT 'UTC',
    `clear_record_id` BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (`user_id`),
    KEY `ix_raid_best_rank` (`elapsed_ms`, `cleared_at`, `clear_record_id`),
    KEY `ix_raid_best_source` (`clear_record_id`),
    CONSTRAINT `fk_raid_best_user` FOREIGN KEY (`user_id`) REFERENCES `patternbreaker`.`users` (`id`),
    CONSTRAINT `fk_raid_best_source` FOREIGN KEY (`clear_record_id`) REFERENCES `patternbreaker`.`raid_clear_records` (`id`),
    CONSTRAINT `chk_raid_best_time_limit` CHECK (`elapsed_ms` < 120000)
) ENGINE=InnoDB;
