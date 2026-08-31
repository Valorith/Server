-- Selectable task reward metadata.

-- Server-authored selectable task rewards. Existing task reward methods and
-- reward_id_list remain unchanged; tasks opt in with reward_method = 3.
CREATE TABLE IF NOT EXISTS `task_reward_sets` (
	`reward_set_id` INT(10) UNSIGNED NOT NULL,
	`task_id` INT(10) UNSIGNED NOT NULL,
	`title` VARCHAR(255) NOT NULL DEFAULT '',
	`enabled` TINYINT(1) UNSIGNED NOT NULL DEFAULT 1,
	PRIMARY KEY (`reward_set_id`),
	UNIQUE KEY `task_reward_sets_task` (`task_id`),
	KEY `task_reward_sets_enabled` (`enabled`, `task_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `task_reward_options` (
	`reward_set_id` INT(10) UNSIGNED NOT NULL,
	`option_id` INT(10) UNSIGNED NOT NULL,
	`sequence` INT(10) UNSIGNED NOT NULL DEFAULT 0,
	`label` VARCHAR(255) NOT NULL DEFAULT '',
	`common_to_all` TINYINT(1) UNSIGNED NOT NULL DEFAULT 0,
	`flags` TINYINT(3) UNSIGNED NOT NULL DEFAULT 0,
	`enabled` TINYINT(1) UNSIGNED NOT NULL DEFAULT 1,
	PRIMARY KEY (`reward_set_id`, `option_id`),
	KEY `task_reward_options_sequence`
		(`reward_set_id`, `sequence`, `option_id`),
	KEY `task_reward_options_enabled`
		(`reward_set_id`, `enabled`, `sequence`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `task_rewards` (
	`reward_id` BIGINT(20) UNSIGNED NOT NULL AUTO_INCREMENT,
	`task_id` INT(10) UNSIGNED NOT NULL,
	`sequence` INT(10) UNSIGNED NOT NULL DEFAULT 0,
	`reward_type` TINYINT(3) UNSIGNED NOT NULL,
	`reward_data_id` INT(10) UNSIGNED NOT NULL DEFAULT 0,
	`amount` BIGINT(20) UNSIGNED NOT NULL DEFAULT 1,
	`description` VARCHAR(255) NOT NULL DEFAULT '',
	`enabled` TINYINT(1) UNSIGNED NOT NULL DEFAULT 1,
	PRIMARY KEY (`reward_id`),
	UNIQUE KEY `task_rewards_task_sequence` (`task_id`, `sequence`),
	KEY `task_rewards_enabled` (`task_id`, `enabled`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `task_reward_option_entries` (
	`reward_set_id` INT(10) UNSIGNED NOT NULL,
	`option_id` INT(10) UNSIGNED NOT NULL,
	`reward_id` BIGINT(20) UNSIGNED NOT NULL,
	PRIMARY KEY (`reward_set_id`, `option_id`, `reward_id`),
	UNIQUE KEY `task_reward_option_entries_reward` (`reward_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- CREATE TABLE IF NOT EXISTS does not repair partially-created tables. Verify
-- the exact unique signatures used for ownership and idempotency, and add only
-- those that are absent. Duplicate data deliberately makes ALTER fail.
SET @reward_uq_sql = IF(
	EXISTS (
		SELECT 1 FROM information_schema.statistics
		WHERE table_schema = DATABASE() AND table_name = 'task_reward_sets'
		GROUP BY index_name
		HAVING MAX(non_unique) = 0 AND COUNT(*) = 1
			AND COUNT(column_name) = COUNT(*)
			AND SUM(CASE WHEN sub_part IS NULL THEN 0 ELSE 1 END) = 0
			AND GROUP_CONCAT(column_name ORDER BY seq_in_index SEPARATOR ',') =
				'reward_set_id'
	),
	'SELECT 1',
	'ALTER TABLE `task_reward_sets` ADD UNIQUE INDEX (`reward_set_id`)'
);
PREPARE reward_uq_stmt FROM @reward_uq_sql;
EXECUTE reward_uq_stmt;
DEALLOCATE PREPARE reward_uq_stmt;

SET @reward_uq_sql = IF(
	EXISTS (
		SELECT 1 FROM information_schema.statistics
		WHERE table_schema = DATABASE() AND table_name = 'task_reward_sets'
		GROUP BY index_name
		HAVING MAX(non_unique) = 0 AND COUNT(*) = 1
			AND COUNT(column_name) = COUNT(*)
			AND SUM(CASE WHEN sub_part IS NULL THEN 0 ELSE 1 END) = 0
			AND GROUP_CONCAT(column_name ORDER BY seq_in_index SEPARATOR ',') =
				'task_id'
	),
	'SELECT 1',
	'ALTER TABLE `task_reward_sets` ADD UNIQUE INDEX (`task_id`)'
);
PREPARE reward_uq_stmt FROM @reward_uq_sql;
EXECUTE reward_uq_stmt;
DEALLOCATE PREPARE reward_uq_stmt;

SET @reward_uq_sql = IF(
	EXISTS (
		SELECT 1 FROM information_schema.statistics
		WHERE table_schema = DATABASE() AND table_name = 'task_reward_options'
		GROUP BY index_name
		HAVING MAX(non_unique) = 0 AND COUNT(*) = 2
			AND COUNT(column_name) = COUNT(*)
			AND SUM(CASE WHEN sub_part IS NULL THEN 0 ELSE 1 END) = 0
			AND GROUP_CONCAT(column_name ORDER BY seq_in_index SEPARATOR ',') =
				'reward_set_id,option_id'
	),
	'SELECT 1',
	'ALTER TABLE `task_reward_options` ADD UNIQUE INDEX (`reward_set_id`, `option_id`)'
);
PREPARE reward_uq_stmt FROM @reward_uq_sql;
EXECUTE reward_uq_stmt;
DEALLOCATE PREPARE reward_uq_stmt;

SET @reward_uq_sql = IF(
	EXISTS (
		SELECT 1 FROM information_schema.statistics
		WHERE table_schema = DATABASE() AND table_name = 'task_rewards'
		GROUP BY index_name
		HAVING MAX(non_unique) = 0 AND COUNT(*) = 1
			AND COUNT(column_name) = COUNT(*)
			AND SUM(CASE WHEN sub_part IS NULL THEN 0 ELSE 1 END) = 0
			AND GROUP_CONCAT(column_name ORDER BY seq_in_index SEPARATOR ',') =
				'reward_id'
	),
	'SELECT 1',
	'ALTER TABLE `task_rewards` ADD UNIQUE INDEX (`reward_id`)'
);
PREPARE reward_uq_stmt FROM @reward_uq_sql;
EXECUTE reward_uq_stmt;
DEALLOCATE PREPARE reward_uq_stmt;

SET @reward_uq_sql = IF(
	EXISTS (
		SELECT 1 FROM information_schema.statistics
		WHERE table_schema = DATABASE() AND table_name = 'task_rewards'
		GROUP BY index_name
		HAVING MAX(non_unique) = 0 AND COUNT(*) = 2
			AND COUNT(column_name) = COUNT(*)
			AND SUM(CASE WHEN sub_part IS NULL THEN 0 ELSE 1 END) = 0
			AND GROUP_CONCAT(column_name ORDER BY seq_in_index SEPARATOR ',') =
				'task_id,sequence'
	),
	'SELECT 1',
	'ALTER TABLE `task_rewards` ADD UNIQUE INDEX (`task_id`, `sequence`)'
);
PREPARE reward_uq_stmt FROM @reward_uq_sql;
EXECUTE reward_uq_stmt;
DEALLOCATE PREPARE reward_uq_stmt;

SET @reward_uq_sql = IF(
	EXISTS (
		SELECT 1 FROM information_schema.statistics
		WHERE table_schema = DATABASE()
			AND table_name = 'task_reward_option_entries'
		GROUP BY index_name
		HAVING MAX(non_unique) = 0 AND COUNT(*) = 3
			AND COUNT(column_name) = COUNT(*)
			AND SUM(CASE WHEN sub_part IS NULL THEN 0 ELSE 1 END) = 0
			AND GROUP_CONCAT(column_name ORDER BY seq_in_index SEPARATOR ',') =
				'reward_set_id,option_id,reward_id'
	),
	'SELECT 1',
	'ALTER TABLE `task_reward_option_entries` ADD UNIQUE INDEX (`reward_set_id`, `option_id`, `reward_id`)'
);
PREPARE reward_uq_stmt FROM @reward_uq_sql;
EXECUTE reward_uq_stmt;
DEALLOCATE PREPARE reward_uq_stmt;

SET @reward_uq_sql = IF(
	EXISTS (
		SELECT 1 FROM information_schema.statistics
		WHERE table_schema = DATABASE()
			AND table_name = 'task_reward_option_entries'
		GROUP BY index_name
		HAVING MAX(non_unique) = 0 AND COUNT(*) = 1
			AND COUNT(column_name) = COUNT(*)
			AND SUM(CASE WHEN sub_part IS NULL THEN 0 ELSE 1 END) = 0
			AND GROUP_CONCAT(column_name ORDER BY seq_in_index SEPARATOR ',') =
				'reward_id'
	),
	'SELECT 1',
	'ALTER TABLE `task_reward_option_entries` ADD UNIQUE INDEX (`reward_id`)'
);
PREPARE reward_uq_stmt FROM @reward_uq_sql;
EXECUTE reward_uq_stmt;
DEALLOCATE PREPARE reward_uq_stmt;

-- Verify the columns used by the runtime.
SELECT `reward_set_id`, `task_id`, `title`, `enabled`
FROM `task_reward_sets` LIMIT 0;
SELECT `reward_set_id`, `option_id`, `sequence`, `label`, `common_to_all`,
	`flags`, `enabled`
FROM `task_reward_options` LIMIT 0;
SELECT `reward_id`, `task_id`, `sequence`, `reward_type`, `reward_data_id`,
	`amount`, `description`, `enabled`
FROM `task_rewards` LIMIT 0;
SELECT `reward_set_id`, `option_id`, `reward_id`
FROM `task_reward_option_entries` LIMIT 0;
