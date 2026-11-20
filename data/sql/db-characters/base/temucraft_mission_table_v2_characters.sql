-- mod-temucraft-mission-table, part 2: when each character may be offered a mission again (mission template
-- `cooldown`, set after a success). 4294967295 = never again (one-time story missions).
CREATE TABLE IF NOT EXISTS `mod_temucraft_mission_cooldown` (
  `owner` INT UNSIGNED NOT NULL COMMENT 'character guid',
  `mission_id` INT UNSIGNED NOT NULL COMMENT 'mod_temucraft_mission_template.id',
  `ready_at` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'unix time it may be offered again',
  PRIMARY KEY (`owner`, `mission_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
