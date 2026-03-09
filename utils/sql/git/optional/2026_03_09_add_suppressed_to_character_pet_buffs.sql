-- Add suppressed column to character_pet_buffs to persist pet buff suppression state across zones
ALTER TABLE `character_pet_buffs`
	ADD COLUMN `suppressed` tinyint(1) unsigned NOT NULL DEFAULT 0 AFTER `instrument_mod`;
