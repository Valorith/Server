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
	'DO 0',
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
	'DO 0',
	'ALTER TABLE `character_task_reward_selections` ADD COLUMN `reward_snapshot` MEDIUMTEXT NOT NULL AFTER `reward_set_id`'
);
PREPARE reward_schema_stmt FROM @reward_schema_sql;
EXECUTE reward_schema_stmt;
DEALLOCATE PREPARE reward_schema_stmt;

-- Link development-era selections that still correspond to an active task to
-- the same durable occurrence used by runtime recovery. Rows whose task was
-- already removed retain a collision-safe synthetic source identity.
INSERT INTO `character_task_reward_instances` (
	`character_id`, `task_id`, `accepted_time`
)
SELECT DISTINCT
	selections.`character_id`,
	selections.`task_id`,
	selections.`accepted_time`
FROM `character_task_reward_selections` AS selections
INNER JOIN `character_tasks` AS tasks
	ON tasks.`charid` = selections.`character_id`
	AND tasks.`taskid` = selections.`task_id`
	AND tasks.`acceptedtime` = selections.`accepted_time`
LEFT JOIN `character_task_reward_instances` AS instances
	ON instances.`character_id` = selections.`character_id`
	AND instances.`task_id` = selections.`task_id`
WHERE instances.`occurrence_id` IS NULL
	AND (
		selections.`source_instance_id` = 0 OR
		selections.`source_instance_id` =
			9223372036854775808 + selections.`pending_reward_id`
	);

UPDATE `character_task_reward_selections` AS selections
INNER JOIN `character_tasks` AS tasks
	ON tasks.`charid` = selections.`character_id`
	AND tasks.`taskid` = selections.`task_id`
	AND tasks.`acceptedtime` = selections.`accepted_time`
INNER JOIN `character_task_reward_instances` AS instances
	ON instances.`character_id` = selections.`character_id`
	AND instances.`task_id` = selections.`task_id`
LEFT JOIN `character_task_reward_selections` AS linked
	ON linked.`character_id` = selections.`character_id`
	AND linked.`source_instance_id` = instances.`occurrence_id`
	AND linked.`pending_reward_id` <> selections.`pending_reward_id`
LEFT JOIN `character_task_reward_selections` AS newer
	ON newer.`character_id` = selections.`character_id`
	AND newer.`task_id` = selections.`task_id`
	AND newer.`accepted_time` = selections.`accepted_time`
	AND newer.`pending_reward_id` > selections.`pending_reward_id`
	AND (
		newer.`source_instance_id` = 0 OR
		newer.`source_instance_id` =
			9223372036854775808 + newer.`pending_reward_id`
	)
SET selections.`source_instance_id` = instances.`occurrence_id`
WHERE linked.`pending_reward_id` IS NULL
	AND newer.`pending_reward_id` IS NULL
	AND (
		selections.`source_instance_id` = 0 OR
		selections.`source_instance_id` =
			9223372036854775808 + selections.`pending_reward_id`
	);

-- A previously affected development database can contain both the migrated
-- row and a newer runtime-created row for the same completion. Keep the
-- linked row claimable and quarantine superseded rows from automatic restore.
UPDATE `character_task_reward_selections` AS selections
INNER JOIN `character_tasks` AS tasks
	ON tasks.`charid` = selections.`character_id`
	AND tasks.`taskid` = selections.`task_id`
	AND tasks.`acceptedtime` = selections.`accepted_time`
INNER JOIN `character_task_reward_instances` AS instances
	ON instances.`character_id` = selections.`character_id`
	AND instances.`task_id` = selections.`task_id`
INNER JOIN `character_task_reward_selections` AS linked
	ON linked.`character_id` = selections.`character_id`
	AND linked.`source_instance_id` = instances.`occurrence_id`
	AND linked.`pending_reward_id` <> selections.`pending_reward_id`
SET selections.`status` = 3,
	selections.`last_error` = 'superseded duplicate task reward'
WHERE selections.`source_instance_id` = 0 OR
	selections.`source_instance_id` =
		9223372036854775808 + selections.`pending_reward_id`;

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
	'DO 0',
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
	'DO 0',
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
	'DO 0',
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
	'DO 0',
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
	'DO 0',
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
	'DO 0',
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
	'DO 0',
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
	'DO 0',
	CONCAT(
		'ALTER TABLE `character_task_reward_selections` DROP INDEX `',
		REPLACE(@reward_old_source_index, '`', '``'),
		'`'
	)
);
PREPARE reward_schema_stmt FROM @reward_schema_sql;
EXECUTE reward_schema_stmt;
DEALLOCATE PREPARE reward_schema_stmt;

-- Verify the columns used by the runtime without returning result sets from
-- the migration executor.
DO EXISTS (
	SELECT `occurrence_id`, `character_id`, `task_id`, `accepted_time`
	FROM `character_task_reward_instances` WHERE 0
);
DO EXISTS (
	SELECT `pending_reward_id`, `character_id`, `task_id`, `accepted_time`,
		`source_instance_id`, `reward_set_id`, `reward_snapshot`,
		`selected_option_id`, `status`, `attempt_count`, `claimed_at`,
		`last_attempt_at`, `last_error`
	FROM `character_task_reward_selections` WHERE 0
);
DO EXISTS (
	SELECT `character_id`, `pending_reward_id`, `reward_id`, `status`,
		`attempt_count`, `granted_at`, `last_attempt_at`, `last_error`
	FROM `character_task_rewards` WHERE 0
);
