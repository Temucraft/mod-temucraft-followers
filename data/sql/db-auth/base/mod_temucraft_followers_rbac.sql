-- mod-temucraft-followers: its game master commands as RBAC permissions (100601-100604). Each is linked to the
-- stock security role of its old level (194 moderator, 193 game master, 192 administrator) and to the matching
-- Temucraft GM tier (1011 Triager, 1012 Entertainer, 1014 Administrator). Grant or deny them per account with
-- .rbac account grant / deny, or link them to other roles.
DELETE FROM `rbac_linked_permissions` WHERE `linkedId` BETWEEN 100601 AND 100699;
DELETE FROM `rbac_permissions` WHERE `id` BETWEEN 100601 AND 100699;
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(100601, 'Command: follower add'),
(100602, 'Command: follower mask'),
(100603, 'Command: missions reload'),
(100604, 'Command: missions finish');
INSERT INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(193, 100601),
(1012, 100601),
(193, 100602),
(1012, 100602),
(192, 100603),
(1014, 100603),
(193, 100604),
(1012, 100604);
