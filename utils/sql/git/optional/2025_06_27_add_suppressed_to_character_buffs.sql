-- Add suppressed column to character_buffs to persist buff suppression state across zones
ALTER TABLE `character_buffs` ADD COLUMN `suppressed` tinyint(1) unsigned NOT NULL DEFAULT 0 AFTER `instrument_mod`;
