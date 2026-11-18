-- mod-temucraft-mission-table: owned followers and each character's missions
CREATE TABLE IF NOT EXISTS `mod_temucraft_follower` (
  `owner` INT UNSIGNED NOT NULL COMMENT 'character guid',
  `id` INT UNSIGNED NOT NULL COMMENT 'follower number on this character',
  `template_id` INT UNSIGNED NOT NULL,
  `level` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `xp` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_level` INT UNSIGNED NOT NULL DEFAULT 100,
  `quality` TINYINT UNSIGNED NOT NULL DEFAULT 2,
  `activity_mask` INT UNSIGNED NOT NULL DEFAULT 1 COMMENT '1 missions, 2 dungeons, 4 expeditions, 8 raids, 16 follower dungeons, 32 world quests',
  `state` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 idle, 1 on a mission, 2 out as a companion',
  `companion_level` INT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'mod-temucraft-companions',
  `companion_xp` INT UNSIGNED NOT NULL DEFAULT 0,
  `companion_role` TINYINT UNSIGNED NOT NULL DEFAULT 255 COMMENT '0 tank, 1 healer, 2 damage, 255 = first role it has',
  `companion_active` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 = comes back with you at login',
  PRIMARY KEY (`owner`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `mod_temucraft_mission` (
  `owner` INT UNSIGNED NOT NULL,
  `slot` INT UNSIGNED NOT NULL,
  `mission_id` INT UNSIGNED NOT NULL,
  `offered_at` INT UNSIGNED NOT NULL DEFAULT 0,
  `start_time` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = offered, not started',
  `end_time` INT UNSIGNED NOT NULL DEFAULT 0,
  `chance` INT UNSIGNED NOT NULL DEFAULT 0,
  `followers` VARCHAR(64) NOT NULL DEFAULT '',
  PRIMARY KEY (`owner`, `slot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Orders from the website's Companion page (Temucraft AoWoW): start a mission or collect finished ones. The server
-- carries them out right away for online characters, otherwise at their next login, and fills done_at / result.
CREATE TABLE IF NOT EXISTS `mod_temucraft_web_order` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `owner` INT UNSIGNED NOT NULL COMMENT 'character guid',
  `action` VARCHAR(16) NOT NULL COMMENT 'start, claim',
  `slot` INT UNSIGNED NOT NULL DEFAULT 0,
  `followers` VARCHAR(64) NOT NULL DEFAULT '',
  `created_at` INT UNSIGNED NOT NULL DEFAULT 0,
  `done_at` INT UNSIGNED NOT NULL DEFAULT 0,
  `result` VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`),
  KEY `pending` (`done_at`, `owner`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
