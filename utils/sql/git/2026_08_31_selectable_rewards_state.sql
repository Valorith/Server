-- Selectable task reward delivery state.

-- A task acceptance reserves a server-owned occurrence token without changing
-- character_tasks.accepted_time. REPLACE intentionally allocates a new token
-- when the same character accepts the same task again.
CREATE TABLE IF NOT EXISTS `character_task_reward_instances` (
	`occurrence_id` BIGINT(20) UNSIGNED NOT NULL AUTO_INCREMENT,
	`character_id` INT(10) UNSIGNED NOT NULL,
	`task_id` INT(10) UNSIGNED NOT NULL,
	`accepted_time` INT(10) UNSIGNED NOT NULL,
	PRIMARY KEY (`occurrence_id`),
	UNIQUE KEY `character_task_reward_instances_source`
		(`character_id`, `task_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- A pending row is created before a completed task is removed from
-- character_tasks. source_instance_id identifies the source task occurrence;
-- accepted_time is retained as diagnostic metadata only.
-- status: 0=claim pending/in progress, 1=fully granted,
-- 2=explicit/retryable delivery failure, 3=ambiguous delivery.
CREATE TABLE IF NOT EXISTS `character_task_reward_selections` (
	`pending_reward_id` INT(10) UNSIGNED NOT NULL AUTO_INCREMENT,
	`character_id` INT(10) UNSIGNED NOT NULL,
	`task_id` INT(10) UNSIGNED NOT NULL,
	`accepted_time` INT(10) UNSIGNED NOT NULL,
	`source_instance_id` BIGINT(20) UNSIGNED NOT NULL,
	`reward_set_id` INT(10) UNSIGNED NOT NULL,
	`reward_snapshot` MEDIUMTEXT NOT NULL,
	`selected_option_id` INT(10) UNSIGNED NOT NULL DEFAULT 0,
	`status` TINYINT(3) UNSIGNED NOT NULL DEFAULT 0,
	`attempt_count` INT(10) UNSIGNED NOT NULL DEFAULT 0,
	`claimed_at` INT(10) UNSIGNED NOT NULL DEFAULT 0,
	`last_attempt_at` INT(10) UNSIGNED NOT NULL DEFAULT 0,
	`last_error` VARCHAR(255) NOT NULL DEFAULT '',
	PRIMARY KEY (`pending_reward_id`),
	UNIQUE KEY `character_task_reward_selections_source`
		(`character_id`, `source_instance_id`),
	KEY `character_task_reward_selections_status`
		(`status`, `character_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Per-entry delivery is the idempotency boundary for claim retries.
CREATE TABLE IF NOT EXISTS `character_task_rewards` (
	`character_id` INT(10) UNSIGNED NOT NULL,
	`pending_reward_id` INT(10) UNSIGNED NOT NULL,
	`reward_id` BIGINT(20) UNSIGNED NOT NULL,
	`status` TINYINT(3) UNSIGNED NOT NULL DEFAULT 0,
	`attempt_count` INT(10) UNSIGNED NOT NULL DEFAULT 0,
	`granted_at` INT(10) UNSIGNED NOT NULL DEFAULT 0,
	`last_attempt_at` INT(10) UNSIGNED NOT NULL DEFAULT 0,
	`last_error` VARCHAR(255) NOT NULL DEFAULT '',
	PRIMARY KEY (`pending_reward_id`, `reward_id`),
	KEY `character_task_rewards_character` (`character_id`, `pending_reward_id`),
	KEY `character_task_rewards_status` (`status`, `character_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Normalize existing task reward rows to the occurrence-token schema.
SET @reward_schema_sql = IF(
	EXISTS (
		SELECT 1 FROM information_schema.columns
		WHERE table_schema = DATABASE()
			AND table_name = 'character_task_reward_selections'
			AND column_name = 'source_instance_id'
	),
	'SELECT 1',
	'ALTER TABLE `character_task_reward_selections` ADD COLUMN `source_instance_id` BIGINT(20) UNSIGNED NOT NULL DEFAULT 0 AFTER `accepted_time`'
);
PREPARE reward_schema_stmt FROM @reward_schema_sql;
EXECUTE reward_schema_stmt;
DEALLOCATE PREPARE reward_schema_stmt;

SET @reward_schema_sql = IF(
	EXISTS (
		SELECT 1 FROM information_schema.columns
		WHERE table_schema = DATABASE()
			AND table_name = 'character_task_reward_selections'
			AND column_name = 'reward_snapshot'
	),
	'SELECT 1',
	'ALTER TABLE `character_task_reward_selections` ADD COLUMN `reward_snapshot` MEDIUMTEXT NOT NULL AFTER `reward_set_id`'
);
PREPARE reward_schema_stmt FROM @reward_schema_sql;
EXECUTE reward_schema_stmt;
DEALLOCATE PREPARE reward_schema_stmt;

UPDATE `character_task_reward_selections`
SET `source_instance_id` = 9223372036854775808 + `pending_reward_id`
WHERE `source_instance_id` = 0;

-- Ensure task reward idempotency constraints are present.
SET @reward_uq_sql = IF(
	EXISTS (
		SELECT 1 FROM information_schema.statistics
		WHERE table_schema = DATABASE()
			AND table_name = 'character_task_reward_instances'
		GROUP BY index_name
		HAVING MAX(non_unique) = 0 AND COUNT(*) = 1
			AND COUNT(column_name) = COUNT(*)
			AND SUM(CASE WHEN sub_part IS NULL THEN 0 ELSE 1 END) = 0
			AND GROUP_CONCAT(column_name ORDER BY seq_in_index SEPARATOR ',') =
				'occurrence_id'
	),
	'SELECT 1',
	'ALTER TABLE `character_task_reward_instances` ADD UNIQUE INDEX (`occurrence_id`)'
);
PREPARE reward_uq_stmt FROM @reward_uq_sql;
EXECUTE reward_uq_stmt;
DEALLOCATE PREPARE reward_uq_stmt;

SET @reward_schema_sql = IF(
	EXISTS (
		SELECT 1 FROM information_schema.columns
		WHERE table_schema = DATABASE()
			AND table_name = 'character_task_reward_instances'
			AND column_name = 'occurrence_id'
			AND LOWER(extra) LIKE '%auto_increment%'
	),
	'SELECT 1',
	'ALTER TABLE `character_task_reward_instances` MODIFY COLUMN `occurrence_id` BIGINT(20) UNSIGNED NOT NULL AUTO_INCREMENT'
);
PREPARE reward_schema_stmt FROM @reward_schema_sql;
EXECUTE reward_schema_stmt;
DEALLOCATE PREPARE reward_schema_stmt;

SET @reward_uq_sql = IF(
	EXISTS (
		SELECT 1 FROM information_schema.statistics
		WHERE table_schema = DATABASE()
			AND table_name = 'character_task_reward_instances'
		GROUP BY index_name
		HAVING MAX(non_unique) = 0 AND COUNT(*) = 2
			AND COUNT(column_name) = COUNT(*)
			AND SUM(CASE WHEN sub_part IS NULL THEN 0 ELSE 1 END) = 0
			AND GROUP_CONCAT(column_name ORDER BY seq_in_index SEPARATOR ',') =
				'character_id,task_id'
	),
	'SELECT 1',
	'ALTER TABLE `character_task_reward_instances` ADD UNIQUE INDEX (`character_id`, `task_id`)'
);
PREPARE reward_uq_stmt FROM @reward_uq_sql;
EXECUTE reward_uq_stmt;
DEALLOCATE PREPARE reward_uq_stmt;

SET @reward_uq_sql = IF(
	EXISTS (
		SELECT 1 FROM information_schema.statistics
		WHERE table_schema = DATABASE()
			AND table_name = 'character_task_reward_selections'
		GROUP BY index_name
		HAVING MAX(non_unique) = 0 AND COUNT(*) = 1
			AND COUNT(column_name) = COUNT(*)
			AND SUM(CASE WHEN sub_part IS NULL THEN 0 ELSE 1 END) = 0
			AND GROUP_CONCAT(column_name ORDER BY seq_in_index SEPARATOR ',') =
				'pending_reward_id'
	),
	'SELECT 1',
	'ALTER TABLE `character_task_reward_selections` ADD UNIQUE INDEX (`pending_reward_id`)'
);
PREPARE reward_uq_stmt FROM @reward_uq_sql;
EXECUTE reward_uq_stmt;
DEALLOCATE PREPARE reward_uq_stmt;

SET @reward_schema_sql = IF(
	EXISTS (
		SELECT 1 FROM information_schema.columns
		WHERE table_schema = DATABASE()
			AND table_name = 'character_task_reward_selections'
			AND column_name = 'pending_reward_id'
			AND LOWER(extra) LIKE '%auto_increment%'
	),
	'SELECT 1',
	'ALTER TABLE `character_task_reward_selections` MODIFY COLUMN `pending_reward_id` INT(10) UNSIGNED NOT NULL AUTO_INCREMENT'
);
PREPARE reward_schema_stmt FROM @reward_schema_sql;
EXECUTE reward_schema_stmt;
DEALLOCATE PREPARE reward_schema_stmt;

SET @reward_uq_sql = IF(
	EXISTS (
		SELECT 1 FROM information_schema.statistics
		WHERE table_schema = DATABASE()
			AND table_name = 'character_task_reward_selections'
		GROUP BY index_name
		HAVING MAX(non_unique) = 0 AND COUNT(*) = 2
			AND COUNT(column_name) = COUNT(*)
			AND SUM(CASE WHEN sub_part IS NULL THEN 0 ELSE 1 END) = 0
			AND GROUP_CONCAT(column_name ORDER BY seq_in_index SEPARATOR ',') =
				'character_id,source_instance_id'
	),
	'SELECT 1',
	'ALTER TABLE `character_task_reward_selections` ADD UNIQUE INDEX (`character_id`, `source_instance_id`)'
);
PREPARE reward_uq_stmt FROM @reward_uq_sql;
EXECUTE reward_uq_stmt;
DEALLOCATE PREPARE reward_uq_stmt;

SET @reward_uq_sql = IF(
	EXISTS (
		SELECT 1 FROM information_schema.statistics
		WHERE table_schema = DATABASE()
			AND table_name = 'character_task_rewards'
		GROUP BY index_name
		HAVING MAX(non_unique) = 0 AND COUNT(*) = 2
			AND COUNT(column_name) = COUNT(*)
			AND SUM(CASE WHEN sub_part IS NULL THEN 0 ELSE 1 END) = 0
			AND GROUP_CONCAT(column_name ORDER BY seq_in_index SEPARATOR ',') =
				'pending_reward_id,reward_id'
	),
	'SELECT 1',
	'ALTER TABLE `character_task_rewards` ADD UNIQUE INDEX (`pending_reward_id`, `reward_id`)'
);
PREPARE reward_uq_stmt FROM @reward_uq_sql;
EXECUTE reward_uq_stmt;
DEALLOCATE PREPARE reward_uq_stmt;

SET @reward_old_source_index = (
	SELECT index_name
	FROM (
		SELECT index_name, MAX(non_unique) AS is_non_unique,
			COUNT(*) AS index_columns,
			COUNT(column_name) AS named_columns,
			SUM(CASE WHEN sub_part IS NULL THEN 0 ELSE 1 END) AS prefix_columns,
			GROUP_CONCAT(column_name ORDER BY seq_in_index SEPARATOR ',')
				AS column_signature
		FROM information_schema.statistics
		WHERE table_schema = DATABASE()
			AND table_name = 'character_task_reward_selections'
		GROUP BY index_name
	) AS existing_indexes
	WHERE is_non_unique = 0
		AND index_columns = 3
		AND named_columns = index_columns
		AND prefix_columns = 0
		AND column_signature = 'character_id,task_id,accepted_time'
		AND index_name <> 'PRIMARY'
	LIMIT 1
);
SET @reward_schema_sql = IF(
	@reward_old_source_index IS NULL,
	'SELECT 1',
	CONCAT(
		'ALTER TABLE `character_task_reward_selections` DROP INDEX `',
		REPLACE(@reward_old_source_index, '`', '``'),
		'`'
	)
);
PREPARE reward_schema_stmt FROM @reward_schema_sql;
EXECUTE reward_schema_stmt;
DEALLOCATE PREPARE reward_schema_stmt;

-- Verify the columns used by the runtime.
SELECT `occurrence_id`, `character_id`, `task_id`, `accepted_time`
FROM `character_task_reward_instances` LIMIT 0;
SELECT `pending_reward_id`, `character_id`, `task_id`, `accepted_time`,
	`source_instance_id`, `reward_set_id`, `reward_snapshot`,
	`selected_option_id`, `status`,
	`attempt_count`, `claimed_at`, `last_attempt_at`, `last_error`
FROM `character_task_reward_selections` LIMIT 0;
SELECT `character_id`, `pending_reward_id`, `reward_id`, `status`,
	`attempt_count`, `granted_at`, `last_attempt_at`, `last_error`
FROM `character_task_rewards` LIMIT 0;
