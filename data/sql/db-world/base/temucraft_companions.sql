-- mod-temucraft-companions: the default companion bodies (used when a follower has no creature_entry of its own)
-- and the module's texts. Bodies: tank looks like a Stormwind guard, damage like an Orgrimmar grunt.
DELETE FROM `creature_template` WHERE `entry` IN (608810, 608811, 608812);
INSERT INTO `creature_template` (`entry`, `name`, `subname`, `IconName`, `gossip_menu_id`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`,
  `speed_walk`, `speed_run`, `rank`, `unit_class`, `unit_flags`, `type`, `type_flags`, `AIName`, `MovementType`, `HealthModifier`, `RegenHealth`,
  `flags_extra`, `ScriptName`) VALUES
(608810, 'Companion', 'Tank', '', 0, 80, 80, 2, 35, 0, 1, 1.14286, 0, 1, 0, 7, 0, '', 0, 1, 1, 0, 'npc_temucraft_companion'),
(608811, 'Companion', 'Healer', '', 0, 80, 80, 2, 35, 0, 1, 1.14286, 0, 2, 0, 7, 0, '', 0, 1, 1, 0, 'npc_temucraft_companion'),
(608812, 'Companion', 'Damage', '', 0, 80, 80, 2, 35, 0, 1, 1.14286, 0, 1, 0, 7, 0, '', 0, 1, 1, 0, 'npc_temucraft_companion');
DELETE FROM `creature_template_model` WHERE `CreatureID` IN (608810, 608811, 608812);
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
SELECT 608810, 0, `CreatureDisplayID`, 1, 1, 0 FROM `creature_template_model` WHERE `CreatureID` = 68 AND `Idx` = 0;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
SELECT 608811, 0, `CreatureDisplayID`, 1, 1, 0 FROM `creature_template_model` WHERE `CreatureID` = 28693 AND `Idx` = 0;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
SELECT 608812, 0, `CreatureDisplayID`, 1, 1, 0 FROM `creature_template_model` WHERE `CreatureID` = 3296 AND `Idx` = 0;

DELETE FROM `module_string` WHERE `module` = 'mod-temucraft-companions';
INSERT INTO `module_string` (`module`, `id`, `string`) VALUES
('mod-temucraft-companions', 1, 'Companions are turned off.'),
('mod-temucraft-companions', 2, 'You have no followers yet - visit a mission table.'),
('mod-temucraft-companions', 3, '#{} {} ({}), companion level {}{}'),
('mod-temucraft-companions', 4, 'You have no follower with that number (see .companion list).'),
('mod-temucraft-companions', 5, 'That follower is away on a mission.'),
('mod-temucraft-companions', 6, 'That follower can''t come with you here.'),
('mod-temucraft-companions', 7, '{} joins you (companion level {}).'),
('mod-temucraft-companions', 8, 'Your companion heads back to camp.'),
('mod-temucraft-companions', 9, 'Your companion is now a {}.'),
('mod-temucraft-companions', 10, '{} reached companion level {}.'),
('mod-temucraft-companions', 11, 'No companion is with you.'),
('mod-temucraft-companions', 12, 'Roles: tank, healer, damage.'),
('mod-temucraft-companions', 13, 'Your companion waits outside: it can''t come here.'),
('mod-temucraft-companions', 14, 'This follower can''t be a {}; it can be: {}.'),
('mod-temucraft-companions', 15, 'Follower dungeons are turned off.'),
('mod-temucraft-companions', 16, 'Call your follower party inside a 5-player dungeon.'),
('mod-temucraft-companions', 17, 'Your group is already full.'),
('mod-temucraft-companions', 18, 'No idle follower can come to follower dungeons (activity bit 16).'),
('mod-temucraft-companions', 19, 'Your follower party joins you: {}.'),
('mod-temucraft-companions', 20, 'Your follower party heads back to camp.');
-- Translations go in `module_string_locale` (`module`, `id`, `locale`, `string`).
