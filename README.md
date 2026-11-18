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
  level, item level, duration, follower slots, cost and enemy threats. Pick followers, see the success chance change,
  then start it.
- **Success chance**: base 40%, plus or minus for follower level and item level against the mission's, plus up to
  60% for the share of threats your followers counter, plus Lucky followers. Up to 200%: everything above 100% is the
  chance of the bonus reward.
- Missions keep running while you're offline. **Mission report** collects the results: gold, items (into the bags,
  or by mail when full), follower experience (a quarter on failure), level-ups (+5 item levels each).
- Mission types: battle, patrol (double follower experience), treasure (double gold), rare.
- Threats: Hazardous Ground, Lethal Lackeys, Area Assault, Hexes, Crushing Blow, Hordes, Arcane Barrage,
  Race Against Time, Berserk Fury. Traits: Swift Feet (-25% duration), Gold Nose (+50% gold), Keen Mind
  (+50% experience), Lucky (+5% chance), Watchful.

### Followers

`mod_temucraft_follower_template` (world database) defines them: name, the roles it can fill (`roles` bitmask:
1 tank, 2 healer, 4 damage — 2 = only a healer, 3 = healer or tank, 7 = any), quality,
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

The module comes with 12 followers (3 starting ones, 9 to hand out) and 22 missions from level 1 to 20 that reward
gold, Dream Shards, Abyss Crystals, Frozen Orbs, Runed Orbs and Emblems. Change or add rows freely and use
`.missions reload`.

### The window (client addon)

`addon/TemucraftMissions` is a 3.3.5 addon. Copy that folder into the client's `Interface\AddOns\`. It has four tabs:

- **Missions**: the offered missions; click one to see its threats, cost and duration, then **Send** followers (only
  idle ones that can do missions are listed). The success chance comes from the server as you pick. **Start mission**.
- **In Progress**: running missions with a live countdown; **Claim rewards** when they're done.
- **Followers**: roster with roles, level and experience, item level, state, counters and traits.
- **Companion** (only shown when mod-temucraft-companions is installed): summon a follower, dismiss it, change its role.

Open it with `/missions` (or `/tmt`), `/companions` for the companion tab, or "Open the mission table window" in the
table's gossip (shown once the addon has said hello). Looking works anywhere; starting and claiming need a mission
table within `Addon.Range` yards unless `Temucraft.MissionTable.Addon.Anywhere = 1`.

It talks to the server with addon whispers to yourself (prefix `TMT`; the companions module uses `TCP`), which the
server swallows, so nothing appears in chat. `AddonChannel = 1` must stay on in `worldserver.conf` (the default).
Messages are `~`-separated, see the comment above `SendList` in the source.

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

1. Put this folder in `modules/`, copy `conf/mod-temucraft-mission-table.conf.dist` to your config folder as
   `mod-temucraft-mission-table.conf`, re-run CMake and build.
2. The SQL in `data/sql/` is applied by the worldserver on its next start.

### What it changes

| Database | Table | What |
|---|---|---|
| world | `mod_temucraft_follower_template`, `mod_temucraft_mission_template` | new: followers and missions |
| world | `creature_template`, `creature_template_model`, `npc_text` | the mission table (608804) |
| world | `module_string` | the module's texts |
| characters | `mod_temucraft_follower`, `mod_temucraft_mission`, `mod_temucraft_web_order` | new: each character's followers and missions (the follower row also holds the `companion_*` columns used by mod-temucraft-companions) |

### Texts and translations

The player-facing texts are in the `module_string` table under the module name `mod-temucraft-mission-table`.
Translations go in `module_string_locale` with the same module and id plus the locale. If a row is missing the
English text in the code is used.

### Works with other modules

- mod-temucraft-companions summons these followers to fight beside you where their activity mask allows. The two
  share `mod_temucraft_follower.state` (0 in the roster, 1 on a mission, 2 out as a companion), so a follower can't be
  in both places; the table re-reads it every time it is used.

Part of the Temucraft modules for AzerothCore 3.3.5a.

## Companions

One of your mission-table followers walks with you and fights in its role. Needs mod-temucraft-mission-table (it
reads that module's followers).

### Roles

- **Tank** — attacks your target and taunts whatever is hitting you or your group.
- **Healer** — keeps back and heals whoever is hurt most (Renew, Flash Heal when someone is low).
- **Damage** — attacks your target.

Each follower can only take the roles in its `roles` bitmask (set in mod-temucraft-mission-table's follower
template: 1 tank, 2 healer, 4 damage — e.g. 2 = only a healer, 3 = healer or tank, 7 = any). It starts in the first
one it has; `.companion role tank|healer|damage` switches between the ones it can do (and is remembered).

### Where it may come

The follower's **activity mask** (set per follower in mod-temucraft-mission-table, changeable with `.follower mask`):
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

1. Install mod-temucraft-mission-table.
2. Put this folder in `modules/`, copy `conf/mod-temucraft-companions.conf.dist` to your config folder as
   `mod-temucraft-companions.conf`, re-run CMake and build.
3. The SQL in `data/sql/` is applied by the worldserver on its next start.

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

The TemucraftMissions client addon from mod-temucraft-mission-table (`addon/`) has a Companion tab: summon, dismiss
and change role with buttons. `/companions` opens it there. It uses addon whispers with the prefix `TCP`.

### Texts and translations

The player-facing texts are in the `module_string` table under the module name `mod-temucraft-companions`.
Translations go in `module_string_locale` with the same module and id plus the locale. If a row is missing the
English text in the code is used.

### Follower state (shared with the mission table)

`mod_temucraft_follower.state` in the characters database says where each follower is, and both modules read it
before using a follower and change it only from the value they expect:

| state | meaning |
|---|---|
| (no row) | the character doesn't have that follower |
| 0 | in the roster: free for a mission or to come along |
| 1 | on a mission (set by the mission table, back to 0 when the result is collected) |
| 2 | out in the world as a companion (set here, back to 0 on dismiss, logout and server start) |

So a follower that is with you can't be sent on a mission, and one on a mission can't be summoned.

Part of the Temucraft modules for AzerothCore 3.3.5a.
