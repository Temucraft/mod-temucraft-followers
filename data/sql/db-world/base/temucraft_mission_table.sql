-- mod-temucraft-mission-table: the mission table, follower and mission templates, and the module's texts
CREATE TABLE IF NOT EXISTS `mod_temucraft_follower_template` (
  `id` INT UNSIGNED NOT NULL,
  `creature_entry` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'creature used when the follower is out as a companion',
  `name` VARCHAR(100) NOT NULL,
  `roles` TINYINT UNSIGNED NOT NULL DEFAULT 4 COMMENT 'roles it can fill, bitmask: 1 tank, 2 healer, 4 damage (e.g. 5 = tank or damage, 7 = any)',
  `quality` TINYINT UNSIGNED NOT NULL DEFAULT 2 COMMENT '2 uncommon, 3 rare, 4 epic',
  `abilities` VARCHAR(64) NOT NULL DEFAULT '' COMMENT 'threats it counters: 1 Hazardous Ground, 2 Lethal Lackeys, 3 Area Assault, 4 Hexes, 5 Crushing Blow, 6 Hordes, 7 Arcane Barrage, 8 Race Against Time, 9 Berserk Fury',
  `traits` VARCHAR(64) NOT NULL DEFAULT '' COMMENT '1 Swift Feet, 2 Gold Nose, 3 Keen Mind, 4 Lucky, 5 Watchful',
  `activity_mask` INT UNSIGNED NOT NULL DEFAULT 1 COMMENT '1 missions, 2 dungeons, 4 expeditions, 8 raids, 16 follower dungeons, 32 world quests',
  `source` TINYINT UNSIGNED NOT NULL DEFAULT 4 COMMENT '0 everyone at the start, 1 quest, 2 reputation, 3 item, 4 only by GM',
  `source_value` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'quest id, faction id or item entry',
  `source_value2` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'reputation rank (4 friendly, 5 honored, 6 revered, 7 exalted)',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `mod_temucraft_mission_template` (
  `id` INT UNSIGNED NOT NULL,
  `name` VARCHAR(100) NOT NULL,
  `type` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 battle, 1 patrol (double follower xp), 2 treasure (double gold), 3 rare',
  `level` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `item_level` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'follower item level it expects (0 = none)',
  `duration` INT UNSIGNED NOT NULL DEFAULT 1800 COMMENT 'seconds',
  `slots` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'followers needed, 1-3',
  `threats` VARCHAR(64) NOT NULL DEFAULT '',
  `cost` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'copper to start it',
  `xp` INT UNSIGNED NOT NULL DEFAULT 100 COMMENT 'follower experience on success (a quarter on failure)',
  `gold` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'copper on success',
  `reward_item` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `bonus_item` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'rolled with the chance above 100%',
  `bonus_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `weight` INT UNSIGNED NOT NULL DEFAULT 100 COMMENT 'how often it is offered (0 = never)',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Starting set: three followers everyone gets, nine more to hand out by quest / reputation / item (source 4 = GM only
-- until you set a source). Rewards use stock items: Dream Shard, Abyss Crystal, Frozen Orb, Runed Orb, Emblems.
DELETE FROM `mod_temucraft_follower_template` WHERE `id` BETWEEN 1 AND 12;
INSERT INTO `mod_temucraft_follower_template` (`id`, `creature_entry`, `name`, `roles`, `quality`, `abilities`, `traits`, `activity_mask`, `source`, `source_value`, `source_value2`) VALUES
(1, 0, 'Sergeant Kara Thornwood', 5, 2, '5,9', '', 63, 0, 0, 0),
(2, 0, 'Brother Aldwin', 2, 2, '3,4', '3', 63, 0, 0, 0),
(3, 0, 'Tarin Swiftarrow', 4, 2, '2,6', '1', 63, 0, 0, 0),
(4, 0, 'Captain Rokhan Ironhide', 1, 3, '5,1', '5', 63, 4, 0, 0),
(5, 0, 'Sister Lirael Dawnmend', 6, 3, '3,7', '4', 63, 4, 0, 0),
(6, 0, 'Vex the Shadow', 4, 3, '2,8', '2', 63, 4, 0, 0),
(7, 0, 'Archmage Celindra', 4, 3, '6,7', '3', 63, 4, 0, 0),
(8, 0, 'Grimbold Stonefist', 5, 3, '5,9', '1', 63, 4, 0, 0),
(9, 0, 'Elder Moonwhisper', 6, 4, '3,4,1', '4', 63, 4, 0, 0),
(10, 0, 'Korga Bloodtusk', 4, 4, '9,6,2', '2', 63, 4, 0, 0),
(11, 0, 'Lady Selene Ashwood', 4, 4, '7,8,4', '1', 63, 4, 0, 0),
(12, 0, 'Thane Brokk Anvilborn', 1, 4, '5,1,9', '5', 63, 4, 0, 0);

DELETE FROM `mod_temucraft_mission_template` WHERE `id` BETWEEN 1 AND 22;
INSERT INTO `mod_temucraft_mission_template` (`id`, `name`, `type`, `level`, `item_level`, `duration`, `slots`, `threats`, `cost`, `xp`, `gold`, `reward_item`, `reward_count`, `bonus_item`, `bonus_count`, `weight`) VALUES
(1, 'Wolves at the Gate', 0, 1, 0, 1800, 1, '9', 0, 200, 50000, 0, 0, 0, 0, 100),
(2, 'Scout the Ridge', 1, 1, 0, 1800, 1, '1', 0, 300, 0, 0, 0, 0, 0, 80),
(3, 'Lost Caravan', 2, 1, 0, 3600, 1, '2', 0, 200, 150000, 0, 0, 0, 0, 60),
(4, 'Bandit Camp', 0, 2, 0, 3600, 2, '2,6', 0, 300, 80000, 34052, 1, 34052, 2, 100),
(5, 'The Haunted Mill', 0, 3, 0, 3600, 2, '4,7', 10000, 350, 90000, 34052, 1, 34057, 1, 100),
(6, 'Ogre Mound', 0, 4, 0, 5400, 2, '5,9', 10000, 400, 100000, 34052, 2, 34057, 1, 90),
(7, 'Border Patrol', 1, 4, 0, 5400, 2, '1,8', 0, 700, 0, 0, 0, 0, 0, 70),
(8, 'Pirate Hoard', 2, 5, 0, 7200, 2, '2,6', 20000, 400, 400000, 0, 0, 43102, 1, 50),
(9, 'Cult of the Damned', 0, 6, 0, 7200, 2, '4,3', 20000, 500, 120000, 34057, 1, 43102, 1, 90),
(10, 'Frozen Depths', 0, 7, 0, 10800, 3, '1,5,7', 30000, 600, 150000, 43102, 1, 43102, 2, 80),
(11, 'Night Watch', 1, 7, 0, 7200, 2, '2,8', 0, 1000, 0, 0, 0, 0, 0, 60),
(12, 'Titan Vault', 2, 8, 0, 14400, 3, '7,8,1', 40000, 600, 800000, 45087, 1, 45087, 2, 40),
(13, 'Siege Engines', 0, 9, 0, 10800, 3, '5,6,9', 40000, 700, 200000, 47241, 2, 47241, 4, 80),
(14, 'Scourge Outpost', 0, 10, 0, 14400, 3, '3,4,6', 50000, 800, 220000, 47241, 3, 49426, 1, 80),
(15, 'Dragon Roost', 0, 11, 150, 14400, 3, '7,5,1', 60000, 900, 250000, 49426, 1, 49426, 2, 70),
(16, 'Ancient Barrow', 0, 12, 160, 18000, 3, '4,7,8', 60000, 1000, 280000, 49426, 2, 45087, 2, 70),
(17, 'Shadow Court', 0, 13, 170, 18000, 3, '2,3,7', 70000, 1100, 300000, 49426, 2, 43102, 4, 60),
(18, 'Rumours of Gold', 2, 14, 170, 21600, 2, '2,9', 50000, 900, 1500000, 0, 0, 45087, 2, 30),
(19, 'Elemental Rift', 0, 15, 180, 21600, 3, '1,7,6', 80000, 1200, 320000, 49426, 3, 49426, 5, 60),
(20, 'The Long Patrol', 1, 16, 180, 28800, 3, '1,8,9', 0, 2500, 0, 0, 0, 0, 0, 40),
(21, 'Fortress of Bone', 0, 18, 190, 28800, 3, '5,3,4', 100000, 1500, 400000, 49426, 4, 49426, 6, 50),
(22, 'Rare: The Lichs Phylactery', 3, 20, 200, 43200, 3, '7,4,8', 150000, 3000, 600000, 49426, 6, 49426, 10, 10);

DELETE FROM `creature_template` WHERE `entry` = 608804;
INSERT INTO `creature_template` (`entry`, `name`, `subname`, `IconName`, `gossip_menu_id`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`,
  `speed_walk`, `speed_run`, `rank`, `unit_class`, `unit_flags`, `type`, `type_flags`, `AIName`, `MovementType`, `HealthModifier`, `RegenHealth`,
  `flags_extra`, `ScriptName`) VALUES
(608804, 'Commander Halvar Greystone', 'Mission Table', '', 0, 80, 80, 2, 35, 1, 1, 1.14286, 0, 1, 768, 7, 0, '', 0, 1, 1, 2, 'npc_temucraft_mission_table');
DELETE FROM `creature_template_model` WHERE `CreatureID` = 608804;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
SELECT 608804, 0, `CreatureDisplayID`, 1, 1, 0 FROM `creature_template_model` WHERE `CreatureID` = 28693 AND `Idx` = 0;

DELETE FROM `npc_text` WHERE `ID` = 608804;
INSERT INTO `npc_text` (`ID`, `text0_0`, `text0_1`, `Probability0`) VALUES
(608804, 'Maps, reports and a hundred small wars that need fighting. Pick a task and the right people for it; I will make sure they come back - most of the time.', '', 1);

DELETE FROM `module_string` WHERE `module` = 'mod-temucraft-mission-table';
INSERT INTO `module_string` (`module`, `id`, `string`) VALUES
('mod-temucraft-mission-table', 1, 'Mission report ({} ready)'),
('mod-temucraft-mission-table', 2, 'Available missions ({})'),
('mod-temucraft-mission-table', 3, 'Missions in progress ({})'),
('mod-temucraft-mission-table', 4, 'Followers ({})'),
('mod-temucraft-mission-table', 5, 'Back'),
('mod-temucraft-mission-table', 6, 'The mission table is closed.'),
('mod-temucraft-mission-table', 7, 'You have no followers yet.'),
('mod-temucraft-mission-table', 8, '{}{} - level {}, {} follower(s), {}'),
('mod-temucraft-mission-table', 9, '{} - {}% - {}'),
('mod-temucraft-mission-table', 10, '{} ({}) - level {}, item level {}{} | counters: {} | {}'),
('mod-temucraft-mission-table', 11, '{} - level {}{}, {}, cost {} gold'),
('mod-temucraft-mission-table', 12, '  Threat: {}'),
('mod-temucraft-mission-table', 13, 'Success chance: {}%  ({}/{} followers, {} of {} threats countered)'),
('mod-temucraft-mission-table', 14, 'Send {}'),
('mod-temucraft-mission-table', 15, 'Start the mission'),
('mod-temucraft-mission-table', 16, 'Choose again'),
('mod-temucraft-mission-table', 17, 'Choose enough followers first.'),
('mod-temucraft-mission-table', 18, 'This mission costs {} gold.'),
('mod-temucraft-mission-table', 19, '|cff66ccffMission started:|r {} ({}% chance), back in {}.'),
('mod-temucraft-mission-table', 21, '|cff66ccffMission complete:|r {} succeeded.'),
('mod-temucraft-mission-table', 22, '|cff66ccffMission complete:|r {} failed.'),
('mod-temucraft-mission-table', 23, '  Bonus reward: {} x{}!'),
('mod-temucraft-mission-table', 24, '  {} reached level {}.'),
('mod-temucraft-mission-table', 25, '|cff66ccffMission table:|r {} joined you as a follower.'),
('mod-temucraft-mission-table', 26, '|cff66ccffMission table:|r {} mission(s) finished while you were away.'),
('mod-temucraft-mission-table', 27, 'No mission is finished yet.'),
('mod-temucraft-mission-table', 28, 'No missions right now. Check back later.'),
('mod-temucraft-mission-table', 29, 'No missions in progress.'),
('mod-temucraft-mission-table', 31, '  Reward: {} gold.'),
('mod-temucraft-mission-table', 32, '  Reward: {} x{}.'),
('mod-temucraft-mission-table', 33, '  Followers gained {} experience.'),
('mod-temucraft-mission-table', 34, 'Mission rewards'),
('mod-temucraft-mission-table', 35, 'Your followers brought this back from their mission.'),
('mod-temucraft-mission-table', 36, '  Sent: {}'),
('mod-temucraft-mission-table', 37, 'One of those followers is busy (on a mission or out as a companion).'),
('mod-temucraft-mission-table', 38, 'Open the mission table window'),
('mod-temucraft-mission-table', 39, 'You need to be at a mission table for that.');
-- Translations go in `module_string_locale` (`module`, `id`, `locale`, `string`).
