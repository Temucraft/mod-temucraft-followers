-- mod-temucraft-mission-table, part 2: the garrison success formula, environments, enemy races, cooldowns,
-- locations, profession missions and the new traits. Safe to run again: columns are only added when missing, and the
-- sample rows are updated by id.

-- ---------------------------------------------------------------------------------------------- new columns
SET @s = (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `mod_temucraft_mission_template` ADD COLUMN `base_chance` TINYINT UNSIGNED NOT NULL DEFAULT 50 COMMENT ''success chance with no points (0-100); points fill the rest up to 100 and beyond''',
  'DO 0') FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_temucraft_mission_template' AND COLUMN_NAME = 'base_chance');
PREPARE st FROM @s; EXECUTE st; DEALLOCATE PREPARE st;

SET @s = (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `mod_temucraft_mission_template` ADD COLUMN `threat_weights` VARCHAR(64) NOT NULL DEFAULT '''' COMMENT ''weight of each threat, same order as threats (empty or 0 = Chance.ThreatWeight, 300)''',
  'DO 0') FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_temucraft_mission_template' AND COLUMN_NAME = 'threat_weights');
PREPARE st FROM @s; EXECUTE st; DEALLOCATE PREPARE st;

SET @s = (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `mod_temucraft_mission_template` ADD COLUMN `environment` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''0 none, 1 Forest, 2 Mountains, 3 Plains, 4 Snow, 5 Desert, 6 Underground, 7 Swamp, 8 Jungle, 9 Town''',
  'DO 0') FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_temucraft_mission_template' AND COLUMN_NAME = 'environment');
PREPARE st FROM @s; EXECUTE st; DEALLOCATE PREPARE st;

SET @s = (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `mod_temucraft_mission_template` ADD COLUMN `enemy_race` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''0 none, 1 Undead, 2 Beast, 3 Dragonkin, 4 Elemental, 5 Demon, 6 Giant, 7 Humanoid, 8 Troll, 9 Vrykul, 10 Nerubian, 11 Construct, 12 Ogre, 13 Orc, 14 Harpy, 15 Aberration''',
  'DO 0') FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_temucraft_mission_template' AND COLUMN_NAME = 'enemy_race');
PREPARE st FROM @s; EXECUTE st; DEALLOCATE PREPARE st;

SET @s = (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `mod_temucraft_mission_template` ADD COLUMN `cooldown` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''seconds before it is offered again after a success (0 = any time, 2147483647 or more = never: one-time story mission)''',
  'DO 0') FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_temucraft_mission_template' AND COLUMN_NAME = 'cooldown');
PREPARE st FROM @s; EXECUTE st; DEALLOCATE PREPARE st;

SET @s = (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `mod_temucraft_mission_template` ADD COLUMN `location` VARCHAR(64) NOT NULL DEFAULT '''' COMMENT ''zone shown with the mission''',
  'DO 0') FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_temucraft_mission_template' AND COLUMN_NAME = 'location');
PREPARE st FROM @s; EXECUTE st; DEALLOCATE PREPARE st;

SET @s = (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `mod_temucraft_mission_template` ADD COLUMN `required_trait` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''a sent follower must have this trait (profession missions); only offered when the roster has it''',
  'DO 0') FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_temucraft_mission_template' AND COLUMN_NAME = 'required_trait');
PREPARE st FROM @s; EXECUTE st; DEALLOCATE PREPARE st;

SET @s = (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `mod_temucraft_follower_template` ADD COLUMN `race` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''player race id (1 Human, 2 Orc, 3 Dwarf, 4 Night Elf, 5 Undead, 6 Tauren, 7 Gnome, 8 Troll, 10 Blood Elf, 11 Draenei) for race pairing traits''',
  'DO 0') FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_temucraft_follower_template' AND COLUMN_NAME = 'race');
PREPARE st FROM @s; EXECUTE st; DEALLOCATE PREPARE st;

-- comments that describe the new meanings (same types and defaults as before)
ALTER TABLE `mod_temucraft_mission_template` MODIFY COLUMN `type` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 battle, 1 patrol (double follower xp), 2 treasure (double gold), 3 rare, 4 exploration (environment traits count double), 5 training (double xp, no gold or items), 6 logistics (double item counts), 7 profession (needs required_trait)';
ALTER TABLE `mod_temucraft_follower_template` MODIFY COLUMN `quality` TINYINT UNSIGNED NOT NULL DEFAULT 2 COMMENT '2 uncommon (1 ability, 1 trait), 3 rare (1, 2), 4 epic (2, 3), 5 legendary (3, 3)';
ALTER TABLE `mod_temucraft_follower_template` MODIFY COLUMN `traits` VARCHAR(64) NOT NULL DEFAULT '' COMMENT 'trait ids, see the module README (1 Swift Feet, 2 Gold Nose, 3 Keen Mind, 4 Lucky, 5 Watchful, 6 Lone Wolf, 7 Mentor, 8 Long Haul, 9 Quick Strike, 10 Veteran, 11-19 race friends, 21-29 environments, 31-45 slayers, 51-64 professions)';

-- ---------------------------------------------------------------------------------------------- sample followers
-- Abilities and traits now fit the quality: Uncommon 1 + 1, Rare 1 + 2, Epic 2 + 3, Legendary 3 + 3.
UPDATE `mod_temucraft_follower_template` SET `race` = 1,  `abilities` = '9',     `traits` = '23'          WHERE `id` = 1;  -- Sergeant Kara Thornwood: Berserk Fury; Plains Runner
UPDATE `mod_temucraft_follower_template` SET `race` = 3,  `abilities` = '3',     `traits` = '3'           WHERE `id` = 2;  -- Brother Aldwin: Area Assault; Keen Mind
UPDATE `mod_temucraft_follower_template` SET `race` = 4,  `abilities` = '2',     `traits` = '1'           WHERE `id` = 3;  -- Tarin Swiftarrow: Lethal Lackeys; Swift Feet
UPDATE `mod_temucraft_follower_template` SET `race` = 2,  `abilities` = '5',     `traits` = '5,43'        WHERE `id` = 4;  -- Captain Rokhan Ironhide: Watchful, Orc Slayer
UPDATE `mod_temucraft_follower_template` SET `race` = 11, `abilities` = '3',     `traits` = '4,55'        WHERE `id` = 5;  -- Sister Lirael Dawnmend: Lucky, Herbalist
UPDATE `mod_temucraft_follower_template` SET `race` = 10, `abilities` = '2',     `traits` = '2,6'         WHERE `id` = 6;  -- Vex the Shadow: Gold Nose, Lone Wolf
UPDATE `mod_temucraft_follower_template` SET `race` = 7,  `abilities` = '7',     `traits` = '3,61'        WHERE `id` = 7;  -- Archmage Celindra: Keen Mind, Tailor
UPDATE `mod_temucraft_follower_template` SET `race` = 3,  `abilities` = '9',     `traits` = '26,59'       WHERE `id` = 8;  -- Grimbold Stonefist: Tunnel Rat, Miner
UPDATE `mod_temucraft_follower_template` SET `race` = 4,  `abilities` = '3,4',   `traits` = '4,21,51'     WHERE `id` = 9;  -- Elder Moonwhisper: Lucky, Woodland Guide, Alchemist
UPDATE `mod_temucraft_follower_template` SET `race` = 8,  `abilities` = '9,6',   `traits` = '2,28,13'     WHERE `id` = 10; -- Korga Bloodtusk: Gold Nose, Jungle Stalker, Friend of Dwarves
UPDATE `mod_temucraft_follower_template` SET `race` = 5,  `abilities` = '7,8',   `traits` = '1,31,9'      WHERE `id` = 11; -- Lady Selene Ashwood: Swift Feet, Undead Slayer, Quick Strike
UPDATE `mod_temucraft_follower_template` SET `race` = 3,  `abilities` = '5,1',   `traits` = '7,24,8'      WHERE `id` = 12; -- Thane Brokk Anvilborn: Mentor, Frost Hardened, Long Haul

-- one legendary follower (GM only until you give it a source)
DELETE FROM `mod_temucraft_follower_template` WHERE `id` = 13;
INSERT INTO `mod_temucraft_follower_template` (`id`, `creature_entry`, `name`, `roles`, `quality`, `abilities`, `traits`, `activity_mask`, `source`, `source_value`, `source_value2`, `race`) VALUES
(13, 0, 'Highlord Aeric Vael', 5, 5, '5,9,1', '7,10,33', 63, 4, 0, 0, 1);   -- Mentor, Veteran, Dragon Slayer

-- ---------------------------------------------------------------------------------------------- sample missions
-- base_chance, threat weights (300 unless a threat is tougher), environment, enemy race, location, cooldown
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 60, `threat_weights` = '',        `environment` = 1, `enemy_race` = 2,  `location` = 'Elwynn Forest',         `cooldown` = 0      WHERE `id` = 1;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 60, `threat_weights` = '',        `environment` = 2, `enemy_race` = 7,  `location` = 'Redridge Mountains',    `cooldown` = 0      WHERE `id` = 2;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 55, `threat_weights` = '',        `environment` = 3, `enemy_race` = 7,  `location` = 'The Barrens',           `cooldown` = 0      WHERE `id` = 3;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 50, `threat_weights` = '',        `environment` = 3, `enemy_race` = 7,  `location` = 'Westfall',              `cooldown` = 0      WHERE `id` = 4;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 50, `threat_weights` = '',        `environment` = 1, `enemy_race` = 1,  `location` = 'Duskwood',              `cooldown` = 0      WHERE `id` = 5;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 45, `threat_weights` = '400,300', `environment` = 7, `enemy_race` = 12, `location` = 'Dustwallow Marsh',      `cooldown` = 0      WHERE `id` = 6;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 55, `threat_weights` = '',        `environment` = 3, `enemy_race` = 7,  `location` = 'Hillsbrad Foothills',   `cooldown` = 0      WHERE `id` = 7;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 45, `threat_weights` = '',        `environment` = 8, `enemy_race` = 7,  `location` = 'Stranglethorn Vale',    `cooldown` = 0      WHERE `id` = 8;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 45, `threat_weights` = '',        `environment` = 3, `enemy_race` = 1,  `location` = 'Western Plaguelands',   `cooldown` = 0      WHERE `id` = 9;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 40, `threat_weights` = '',        `environment` = 6, `enemy_race` = 10, `location` = 'Dragonblight',          `cooldown` = 0      WHERE `id` = 10;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 55, `threat_weights` = '',        `environment` = 1, `enemy_race` = 2,  `location` = 'Duskwood',              `cooldown` = 0      WHERE `id` = 11;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 40, `threat_weights` = '',        `environment` = 6, `enemy_race` = 11, `location` = 'Badlands',              `cooldown` = 86400  WHERE `id` = 12;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 40, `threat_weights` = '',        `environment` = 3, `enemy_race` = 13, `location` = 'Arathi Highlands',      `cooldown` = 0      WHERE `id` = 13;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 40, `threat_weights` = '',        `environment` = 3, `enemy_race` = 1,  `location` = 'Eastern Plaguelands',   `cooldown` = 0      WHERE `id` = 14;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 35, `threat_weights` = '300,400,300', `environment` = 2, `enemy_race` = 3, `location` = 'Burning Steppes',   `cooldown` = 0      WHERE `id` = 15;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 35, `threat_weights` = '',        `environment` = 6, `enemy_race` = 9,  `location` = 'Howling Fjord',         `cooldown` = 0      WHERE `id` = 16;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 35, `threat_weights` = '',        `environment` = 9, `enemy_race` = 5,  `location` = 'Stormwind City',        `cooldown` = 0      WHERE `id` = 17;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 45, `threat_weights` = '',        `environment` = 5, `enemy_race` = 7,  `location` = 'Tanaris',               `cooldown` = 86400  WHERE `id` = 18;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 35, `threat_weights` = '',        `environment` = 5, `enemy_race` = 4,  `location` = 'Silithus',              `cooldown` = 0      WHERE `id` = 19;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 40, `threat_weights` = '',        `environment` = 4, `enemy_race` = 6,  `location` = 'The Storm Peaks',       `cooldown` = 0      WHERE `id` = 20;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 30, `threat_weights` = '400,300,300', `environment` = 4, `enemy_race` = 1, `location` = 'Icecrown',         `cooldown` = 0      WHERE `id` = 21;
UPDATE `mod_temucraft_mission_template` SET `base_chance` = 25, `threat_weights` = '450,300,300', `environment` = 6, `enemy_race` = 1, `location` = 'Western Plaguelands', `cooldown` = 604800 WHERE `id` = 22;

-- new samples: a one-time story mission and the new mission types (ids 23-30)
DELETE FROM `mod_temucraft_mission_template` WHERE `id` BETWEEN 23 AND 30;
INSERT INTO `mod_temucraft_mission_template` (`id`, `name`, `type`, `level`, `item_level`, `duration`, `slots`, `threats`, `cost`, `xp`, `gold`,
  `reward_item`, `reward_count`, `bonus_item`, `bonus_count`, `weight`, `base_chance`, `threat_weights`, `environment`, `enemy_race`, `cooldown`, `location`, `required_trait`) VALUES
(23, 'Story: The First Report',     0, 1,  0, 1800,  1, '9',     0,     300,  100000, 0,     0,  34052, 1, 200, 70, '',    1, 2,  4294967295, 'Elwynn Forest',     0),
(24, 'Map the Wetlands',            4, 3,  0, 5400,  2, '1,6',   10000, 400,  60000,  34052, 1,  34057, 1, 70,  50, '',    7, 2,  0,          'Wetlands',          0),
(25, 'Sparring in the Yard',        5, 2,  0, 3600,  2, '8',     0,     600,  0,      0,     0,  0,     0, 60,  70, '',    9, 7,  0,          'Stormwind City',    0),
(26, 'Supply Run',                  6, 5,  0, 7200,  2, '2,1',   20000, 400,  50000,  36913, 5,  36913, 5, 60,  55, '',    3, 7,  0,          'Arathi Highlands',  0),
(27, 'Herbs of the Basin',          7, 4,  0, 5400,  1, '1',     0,     350,  0,      36901, 10, 36901, 10, 50, 60, '',    8, 2,  0,          'Sholazar Basin',    55),
(28, 'Deep Veins',                  7, 6,  0, 7200,  1, '6',     0,     400,  0,      36912, 10, 37701, 5, 50,  55, '',    6, 4,  0,          'The Storm Peaks',   59),
(29, 'A Fine Cloth Order',          7, 8,  0, 10800, 1, '2',     0,     500,  0,      33470, 20, 33470, 20, 50, 55, '',    9, 7,  0,          'Dalaran',           61),
(30, 'The Alchemist''s Errand',     7, 10, 0, 14400, 2, '4,8',   20000, 700,  0,      35625, 2,  36860, 2, 50,  50, '',    9, 8,  0,          'Zul''Drak',         51);

-- ---------------------------------------------------------------------------------------------- texts
DELETE FROM `module_string` WHERE `module` = 'mod-temucraft-mission-table' AND `id` BETWEEN 40 AND 46;
INSERT INTO `module_string` (`module`, `id`, `string`) VALUES
('mod-temucraft-mission-table', 40, '  {} - {}, enemies: {}, base chance {}%'),
('mod-temucraft-mission-table', 41, '  Threat: {} (weight {})'),
('mod-temucraft-mission-table', 42, '  Needs a follower with: {}'),
('mod-temucraft-mission-table', 43, '  {} (level {}, item level {}): +{:.1f} points'),
('mod-temucraft-mission-table', 44, '  {} countered by {}: +{:.1f} points'),
('mod-temucraft-mission-table', 45, '  {} ({}): +{:.1f} points'),
('mod-temucraft-mission-table', 46, 'You need a follower with {} for this mission.');
