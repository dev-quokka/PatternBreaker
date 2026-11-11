CREATE TABLE IF NOT EXISTS `patternbreaker`.`raid_clear_records` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `run_id` CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    `user_id` BIGINT UNSIGNED NOT NULL,
    `elapsed_ms` INT UNSIGNED NOT NULL,
    `cleared_at` DATETIME NOT NULL COMMENT 'UTC',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uq_raid_clear_run_id` (`run_id`),
    KEY `ix_raid_clear_user_best` (`user_id`, `elapsed_ms`, `cleared_at`, `id`),
    KEY `ix_raid_clear_global` (`elapsed_ms`, `cleared_at`, `id`),
    CONSTRAINT `fk_raid_clear_user` FOREIGN KEY (`user_id`) REFERENCES `patternbreaker`.`users` (`id`),
    CONSTRAINT `chk_raid_clear_time_limit` CHECK (`elapsed_ms` < 120000)
) ENGINE=InnoDB;
