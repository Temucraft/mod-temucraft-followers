# mod-temucraft-followers

Followers for Temucraft (AzerothCore, WotLK 3.3.5a), in one module: the **mission table** (followers you collect and
send on missions), **companions** (one follower walks and fights beside you) and **follower dungeons** (followers
fill the free places of your group in a 5-player dungeon). They share one follower list
(`mod_temucraft_follower`), so they live together. All C++.

Before: two modules, mod-temucraft-mission-table and mod-temucraft-companions. Their tables, SQL file names, texts
(`module_string` rows of both names), settings (`Temucraft.MissionTable.*`, `Temucraft.Companions.*`), addon prefixes
(TMT, TCP) and the TemucraftMissions window are unchanged, so nothing on an existing server needs migrating: take the
two old folders out of `modules/` and put this one in.

## Follower dungeons

In a 5-player dungeon (not an expedition), `.companion party` or **Fill party** on the window's Companion tab fills
the free places of your group up to `Temucraft.Companions.FollowerDungeons.PartySize` (5):

- a **tank** if nobody in the group plays a tank spec (and your own companion isn't tanking),
- a **healer** if nobody heals,
- **damage** for the rest.

Only idle followers with the follower-dungeon bit (16 in their activity mask) come, best first (quality, item level,
companion level). They fight with the companion AI in their role, stay for that dungeon only and go back to the roster
when you leave it, log out, or use `.companion party dismiss` / **Send party back**.

## Mission table

Warlords of Draenor / Legion style missions on AzerothCore 3.3.5a: send followers on timed missions for gold, items
and follower experience. It works from the mission table's gossip menu alone; the **TemucraftMissions** client addon
(in `addon/`) adds a proper window.

### How it plays

- Talk to the **mission table** (creature 608804, Commander Halvar Greystone; place it with `.npc add 608804`).
  On the first visit you get the starting followers.
- **Available missions**: each character has its own list (default 6), refreshed every 24 hours. A mission shows its
  level, item level, duration, follower slots, cost, where it is (zone and environment), the enemy race and the enemy
  threats with their weights. Pick followers, see the success chance change, then start it. A mission with a
  `cooldown` is not offered again for that long after a success; a one-time story mission never comes back.
- **Success chance** works like the Warlords garrison mission table (details below). Up to 200%: everything above 100%
  is the chance of the bonus reward.
- Missions keep running while you're offline. **Mission report** collects the results: gold, items (into the bags,
  or by mail when full), follower experience (a quarter on failure), level-ups (+5 item levels each).
- Mission types (`type`):

  | type | name | what is different |
  |---|---|---|
  | 0 | battle | nothing |
  | 1 | patrol | double follower experience |
  | 2 | treasure | double gold |
  | 3 | rare | shown in purple, usually rarer and bigger |
  | 4 | exploration | environment traits count double |
  | 5 | training | double follower experience, no gold or items |
  | 6 | logistics | twice the reward and bonus items (goods, materials) |
  | 7 | profession | needs a follower with the trait in `required_trait`; only offered when your roster has one |

- Threats (ids 1-9): Hazardous Ground, Lethal Lackeys, Area Assault, Hexes, Crushing Blow, Hordes, Arcane Barrage,
  Race Against Time, Berserk Fury. Each has a weight (300 unless `threat_weights` says otherwise).
- Environments (`environment`): 1 Forest, 2 Mountains, 3 Plains, 4 Snow, 5 Desert, 6 Underground, 7 Swamp, 8 Jungle,
  9 Town. Enemy races (`enemy_race`): 1 Undead, 2 Beast, 3 Dragonkin, 4 Elemental, 5 Demon, 6 Giant, 7 Humanoid,
  8 Troll, 9 Vrykul, 10 Nerubian, 11 Construct, 12 Ogre, 13 Orc, 14 Harpy, 15 Aberration.

### Success chance

The same steps as the garrison mission table (the code is in `src/TemucraftMissionChance.h`):

1. Each follower gets a **bias** from -1 to +1: (follower level - mission level) / 3, plus (follower item level -
   mission item level) / 15 when the mission has an item level.
2. `CalcChance(a, b, bias)` = `a + (b - a) x bias` when the bias is 0 or more, `a x (1 + bias)` below 0.
3. **Total weight** = follower slots x 100 + the weight of every threat. Points are counted out of this total.
4. Each follower adds `CalcChance(100, 150, bias)`. An empty slot adds nothing.
5. Each ability that matches a threat removes `CalcChance(weight, weight, bias)` of it and adds that much. A follower
   below the mission level counters only part of a threat (3 levels below: nothing).
6. Traits that apply add `CalcChance(amount, amount at +1, bias)` (see the trait table).
7. `chance = base_chance + (100 - base_chance) x points / total weight`, plus 5 for each Lucky follower.

Example (the first garrison mission on Wowhead): level 90, 1 slot, base 65, one threat of weight 300. A level 90
follower that counters it: (100 + 300) / 400 = all points, so 100%. Without the counter: 65 + 35 x 100/400 = 73%.
Three levels above the mission: 104% (4% bonus chance).

`tests/test_mission_chance.py` checks these numbers (and 30 more cases) with a Python copy of the formula, and
compiles the real C++ header to compare when g++ is there: `python3 tests/test_mission_chance.py`.

### Traits

Ability and trait slots by quality: Uncommon 1 ability + 1 trait, Rare 1 + 2, Epic 2 + 3, Legendary 3 + 3.
Extra ones in a template are ignored with a warning (`Temucraft.MissionTable.EnforceSlots`). Amounts are points at
bias 0 / bias +1 (out of the total weight).

| id | trait | effect |
|---|---|---|
| 1 | Swift Feet | missions take 25% less time |
| 2 | Gold Nose | +50% gold |
| 3 | Keen Mind | +50% follower experience |
| 4 | Lucky | +5% chance (`Chance.Lucky`) |
| 5 | Watchful | 50 / 75 on patrol and exploration missions |
| 6 | Lone Wolf | 80 / 120 when it is the only follower |
| 7 | Mentor | everyone on the mission counts at the best level and item level of the group |
| 8 | Long Haul | 50 / 75 on missions of 8 hours or more |
| 9 | Quick Strike | 50 / 75 on missions shorter than 4 hours |
| 10 | Veteran | 30 / 45 always |
| 11-19 | Friend of Humans, Orcs, Dwarves, Elves (Night Elf or Blood Elf), Gnomes, Tauren, Trolls, the Forsaken, Draenei | 50 / 75 with another follower of that race (`race` on the follower template) |
| 21-29 | Woodland Guide, Mountain Born, Plains Runner, Frost Hardened, Sand Walker, Tunnel Rat, Bog Wader, Jungle Stalker, Street Wise | 60 / 90 when the environment matches (20 + environment); double on exploration |
| 31-45 | Undead, Beast, Dragon, Elemental, Demon, Giant, Bandit (humanoid), Troll, Vrykul, Nerubian, Construct, Ogre, Orc, Harpy, Void (aberration) Slayer | 60 / 90 against that enemy race (30 + enemy race) |
| 51-64 | Alchemist, Blacksmith, Enchanter, Engineer, Herbalist, Scribe, Jeweler, Leatherworker, Miner, Skinner, Tailor, Cook, Angler, Medic | can take that profession's missions (`required_trait`), 60 / 90 on them |

### Followers

`mod_temucraft_follower_template` (world database) defines them: name, the roles it can fill (`roles` bitmask:
1 tank, 2 healer, 4 damage; 2 = only a healer, 3 = healer or tank, 7 = any), quality,
abilities (threats countered), traits, the creature used when it is out as a companion, an **activity mask** and how
it is earned:

| source | earned by | source_value | source_value2 |
|---|---|---|---|
| 0 | everyone, at the first visit | | |
| 1 | completing a quest | quest id | |
| 2 | reaching a reputation rank | faction id | rank (4 friendly … 7 exalted) |
| 3 | using an item (e.g. a shop contract; give the item the script `item_temucraft_follower_contract`) | item entry | |
| 4 | only from a GM | | |

Activity mask bits: 1 missions, 2 dungeons, 4 expeditions, 8 raids, 16 follower dungeons, 32 world quests. Each owned
follower keeps its own copy, which a GM can change (`.follower mask`).

The module comes with 13 followers (3 starting ones, 10 to hand out, one of them Legendary) and 30 missions from
level 1 to 20: the 22 first ones, a one-time story mission (23), and one each of exploration, training and
logistics plus four profession missions (24-30). They reward gold, Dream Shards, Abyss Crystals, Frozen Orbs, Runed
Orbs, Emblems and trade goods. Change or add rows freely and use `.missions reload`.

Mission template columns added in part 2 (`temucraft_mission_table_v2_garrison.sql`, all with defaults so old rows
keep working): `base_chance` (50), `threat_weights` (''), `environment` (0), `enemy_race` (0), `cooldown` (0, in
seconds; 2147483647 or more = one-time), `location` ('', the zone shown), `required_trait` (0). Follower template:
`race` (0).

### The window (client addon)

`addon/TemucraftMissions` is a 3.3.5 addon. Copy that folder into the client's `Interface\AddOns\`. It has four tabs:

- **Missions**: the offered missions; click one to see where it is (zone, environment), the enemy race, its threats
  with their weights, cost and duration, then **Send** followers (only idle ones that can do missions are listed;
  abilities that counter one of the mission's threats are green). The success chance comes from the server as you
  pick: hover it (**[?]**) for the breakdown, which shows what each follower adds, which threats are countered and by
  whom, and which traits (environment, slayer, race friends...) helped. **Start mission**.
- **In Progress**: running missions with a live countdown; **Claim rewards** when they're done.
- **Followers**: roster with roles, race, level and experience, item level, state, counters and traits (hover a
  follower to read what its traits do).
- **Companion** (only shown when companions are on, `Temucraft.Companions.Enable`): summon a follower, dismiss it, change its role.

Open it with `/missions` (or `/tmt`), `/companions` for the companion tab, or "Open the mission table window" in the
table's gossip (shown once the addon has said hello). Looking works anywhere; starting and claiming need a mission
table within `Addon.Range` yards unless `Temucraft.MissionTable.Addon.Anywhere = 1`.

It talks to the server with addon whispers to yourself (prefix `TMT`; the companions module uses `TCP`), which the
server swallows, so nothing appears in chat. `AddonChannel = 1` must stay on in `worldserver.conf` (the default).
Messages are `~`-separated, see the comment above `SendList` in the source. New fields are only added at the end of
a message, so an older window still works with a newer server (it just doesn't show the new parts). The same window
is built into the client by mod-temucraft-client-version (FrameXML); keep both copies the same.

The gossip menu shows the same breakdown under the success chance (points per follower, countered threats, traits).

### On the website

The Temucraft database site (AoWoW fork) has a **Companion** page (Tools → Companion): with the game account
linked, it shows each character's followers, offered and running missions, expeditions and disguises, and can
**start a mission** or **collect finished ones**. Those become orders in `mod_temucraft_web_order` (characters
database); the server checks them like the in-game window does and carries them out right away for online
characters, otherwise at their next login (`Temucraft.MissionTable.WebOrders`).

### Commands

| Command | Who | What |
|---|---|---|
| `.follower list` | everyone | your followers |
| `.follower add <template>` | GM | give the selected player a follower |
| `.follower mask <follower> <mask>` | GM | change an owned follower's activity mask |
| `.missions finish` | GM | finish the selected player's running missions now |
| `.missions reload` | admin | reload the follower and mission templates |

### Install

1. Put this folder in `modules/`, copy `conf/mod-temucraft-followers.conf.dist` to your config folder as
   `mod-temucraft-followers.conf`, re-run CMake and build.
2. The SQL in `data/sql/` is applied by the worldserver on its next start. Part 2 of the mission table
   (`temucraft_mission_table_v2_garrison.sql`, `temucraft_mission_table_v2_characters.sql`) only adds columns and a
   table when they are missing and updates the sample rows by id, so it is safe on a server that already has the
   module. Settings changed in part 2: `Chance.Base`, `Chance.PerLevelAbove`, `Chance.PerLevelBelow`,
   `Chance.PerItemLevel` and `Chance.Counters` are gone (each mission has its own `base_chance` now);
   `Chance.ThreatWeight`, `Chance.Max` and `EnforceSlots` are new; `Chance.Lucky` stays.

### What it changes

| Database | Table | What |
|---|---|---|
| world | `mod_temucraft_follower_template`, `mod_temucraft_mission_template` | new: followers and missions (part 2 adds columns, see above) |
| world | `creature_template`, `creature_template_model`, `npc_text` | the mission table (608804) |
| world | `module_string` | the module's texts |
| characters | `mod_temucraft_follower`, `mod_temucraft_mission`, `mod_temucraft_web_order` | new: each character's followers and missions (the follower row also holds the `companion_*` columns used by the companions) |
| characters | `mod_temucraft_mission_cooldown` | new (part 2): when a mission with a cooldown may be offered again to a character |

### Texts and translations

The player-facing texts are in the `module_string` table under the module name `mod-temucraft-mission-table`.
Translations go in `module_string_locale` with the same module and id plus the locale. If a row is missing the
English text in the code is used.

### Works with other modules

- The companions (below) summon these followers to fight beside you where their activity mask allows. Both
  share `mod_temucraft_follower.state` (0 in the roster, 1 on a mission, 2 out as a companion), so a follower can't be
  in both places; the table re-reads it every time it is used.

## Companions

One of your mission table followers walks with you and fights in its role.

### Roles

- **Tank**: attacks your target and taunts whatever is hitting you or your group.
- **Healer**: keeps back and heals whoever is hurt most (Renew, Flash Heal when someone is low).
- **Damage**: attacks your target.

Each follower can only take the roles in its `roles` bitmask (set in the follower
template: 1 tank, 2 healer, 4 damage; e.g. 2 = only a healer, 3 = healer or tank, 7 = any). It starts in the first
one it has; `.companion role tank|healer|damage` switches between the ones it can do (and is remembered).

### Where it may come

The follower's **activity mask** (set per follower in the follower template, changeable with `.follower mask`):
2 dungeons, 8 raids, 32 the open world (world quests). 4 expeditions and 16 follower dungeons are for those features.
Never in battlegrounds or arenas, and never while the follower is away on a mission. If you go somewhere it may not
come, it waits and comes back on the next map that allows it.

### Strength and levels

Health and damage follow your level (as a normal creature of your level times the multipliers in the config), the
follower's quality and item level, and the companion's own level. Companions level up from kills while they are out
(up to level 30 by default); each level adds 3%.

### Commands

| Command | What |
|---|---|
| `.companion list` | your followers, the roles each can take, and their companion level |
| `.companion summon <#>` | bring follower # with you |
| `.companion dismiss` | send it back |
| `.companion role <tank\|healer\|damage>` | change its role |

A companion that was out when you logged off comes back when you log in.

### Install

Part of this module: nothing to install separately (see Install above). Turn it off with
`Temucraft.Companions.Enable = 0`.

### What it changes

| Database | Table | What |
|---|---|---|
| world | `creature_template`, `creature_template_model` | default companion bodies 608810-608812 (used when a follower has no `creature_entry`) |
| world | `module_string` | the module's texts |
| characters | `mod_temucraft_follower` | `companion_level`, `companion_xp`, `companion_role`, `companion_active` on the mission table's follower row (added here if missing; an old `mod_temucraft_companion` table is copied over and dropped) |

### Expeditions

Inside an expedition ([mod-temucraft-expeditions](https://github.com/Temucraft/mod-temucraft-expeditions)) the companion needs the expedition bit (4) instead of the dungeon bit and moves into
the expedition phase with you. A companion that had to wait outside comes back by itself within a couple of seconds once
you are somewhere it may be.

### Window

The TemucraftMissions client addon (`addon/`) has a Companion tab: summon, dismiss
and change role with buttons. `/companions` opens it there. It uses addon whispers with the prefix `TCP`.

### Texts and translations

The player-facing texts are in the `module_string` table under the module name `mod-temucraft-companions`.
Translations go in `module_string_locale` with the same module and id plus the locale. If a row is missing the
English text in the code is used.

### Follower state (shared with the mission table)

`mod_temucraft_follower.state` in the characters database says where each follower is, and both parts read it
before using a follower and change it only from the value they expect:

| state | meaning |
|---|---|
| (no row) | the character doesn't have that follower |
| 0 | in the roster: free for a mission or to come along |
| 1 | on a mission (set by the mission table, back to 0 when the result is collected) |
| 2 | out in the world as a companion (set here, back to 0 on dismiss, logout and server start) |

So a follower that is with you can't be sent on a mission, and one on a mission can't be summoned.

Part of the Temucraft modules for AzerothCore 3.3.5a.
