/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

/*
 * mod-temucraft-mission-table: Warlords of Draenor / Legion style missions on 3.3.5, through gossip.
 *
 * - Followers (world table mod_temucraft_follower_template) are earned from quests, reputation ranks, an item (shop
 *   contracts) or given to everyone at the start; each has the roles it can fill (tank / healer / damage bitmask), a quality, abilities that counter mission threats,
 *   traits, and an activity mask saying where it may be used (missions, dungeons, expeditions, raids, follower dungeons,
 *   world quests). Owned followers (characters table mod_temucraft_follower) have their own level, item level, mask.
 * - Missions (world table mod_temucraft_mission_template) have a level, item level, duration, 1-3 follower slots,
 *   enemy threats, a cost and rewards. Every character gets its own list of offered missions, refreshed on a timer.
 * - Success chance: base + level and item level difference + share of threats countered + traits, capped at 200%.
 *   Above 100% the bonus reward is rolled with the extra chance.
 * - Missions run while the player is offline; the results wait at the table (Mission report).
 *
 * Text comes from module_string (module "mod-temucraft-mission-table"), with English built in as a fallback.
 */
#include "Chat.h"
#include "ChatCommand.h"
#include "CommandScript.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureScript.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Item.h"
#include "ItemScript.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "QuestDef.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "StringFormat.h"
#include "TemucraftAddonMessage.h"
#include "TemucraftModuleText.h"
#include "Tokenize.h"
#include "WorldScript.h"
#include "WorldSession.h"
#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <unordered_map>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    constexpr char const* MODULE = "mod-temucraft-mission-table";
    constexpr uint32 NPC_TABLE = 608804;
    constexpr uint32 TEXT_TABLE = 608804;

    enum ActivityMask : uint32
    {
        ACT_MISSIONS = 1, ACT_DUNGEONS = 2, ACT_EXPEDITIONS = 4, ACT_RAIDS = 8, ACT_FOLLOWER_DUNGEONS = 16, ACT_WORLD_QUESTS = 32
    };
    enum Source : uint8 { SRC_STARTER = 0, SRC_QUEST = 1, SRC_REPUTATION = 2, SRC_ITEM = 3, SRC_NONE = 4 };
    enum FollowerState : uint8 { F_IDLE = 0, F_ON_MISSION = 1, F_COMPANION = 2 };
    enum Trait : uint8 { TRAIT_FAST = 1, TRAIT_TREASURE = 2, TRAIT_QUICK_STUDY = 3, TRAIT_LUCKY = 4, TRAIT_PATROLLER = 5 };
    enum MissionType : uint8 { M_BATTLE = 0, M_PATROL = 1, M_TREASURE = 2, M_RARE = 3 };

    enum Str : uint32
    {
        S_MAIN_REPORT = 1, S_MAIN_OFFERS, S_MAIN_RUNNING, S_MAIN_FOLLOWERS, S_BACK, S_OFF, S_NO_FOLLOWERS,
        S_OFFER_LINE, S_RUNNING_LINE, S_FOLLOWER_LINE, S_MISSION_HEAD, S_THREAT_LINE, S_CHANCE_LINE, S_PICK_LINE,
        S_START, S_CLEAR, S_NEED_FOLLOWERS, S_NO_MONEY, S_STARTED, S_CLAIM, S_SUCCESS, S_FAILED, S_BONUS,
        S_LEVEL_UP, S_NEW_FOLLOWER, S_READY_LOGIN, S_NOTHING_READY, S_NONE_OFFERED, S_NONE_RUNNING, S_COUNTERED,
        S_REWARD_GOLD, S_REWARD_ITEM, S_REWARD_XP, S_MAIL_SUBJECT, S_MAIL_BODY, S_PICKED_LINE, S_BUSY,
        S_WINDOW, S_NEED_TABLE
    };
    constexpr char const* ADDON_PREFIX = "TMT";

    char const* const THREAT_NAMES[] = { "", "Hazardous Ground", "Lethal Lackeys", "Area Assault", "Hexes", "Crushing Blow",
                                         "Hordes", "Arcane Barrage", "Race Against Time", "Berserk Fury" };
    constexpr uint8 MAX_THREAT = 9;
    char const* const TRAIT_NAMES[] = { "", "Swift Feet", "Gold Nose", "Keen Mind", "Lucky", "Watchful" };
    char const* const ROLE_NAMES[] = { "Tank", "Healer", "Damage" };
    std::string RoleNames(uint8 roles)
    {
        std::string out;
        for (uint8 i = 0; i < 3; ++i) if (roles & (1 << i)) { if (!out.empty()) out += "/"; out += ROLE_NAMES[i]; }
        return out.empty() ? std::string("-") : out;
    }

    struct FollowerTemplate
    {
        uint32 id = 0, creatureEntry = 0;
        std::string name;
        uint8 roles = 4, quality = 2;   // roles: 1 tank, 2 healer, 4 damage
        std::vector<uint8> abilities, traits;
        uint32 mask = ACT_MISSIONS;
        uint8 source = SRC_NONE;
        uint32 sourceValue = 0, sourceValue2 = 0;
    };
    struct MissionTemplate
    {
        uint32 id = 0;
        std::string name;
        uint8 type = M_BATTLE, level = 1, slots = 1;
        uint32 itemLevel = 0, duration = 1800, cost = 0, xp = 100, gold = 0, item = 0, itemCount = 0, bonusItem = 0, bonusCount = 0, weight = 100;
        std::vector<uint8> threats;
    };
    struct Follower
    {
        uint32 id = 0, tmpl = 0, xp = 0, itemLevel = 0, mask = 0;
        uint8 level = 1, quality = 2, state = F_IDLE;
    };
    struct Mission
    {
        uint32 slot = 0, mission = 0;
        uint32 offeredAt = 0, start = 0, end = 0, chance = 0;
        std::vector<uint32> followers;
        bool running() const { return start != 0; }
    };
    struct PlayerData
    {
        std::map<uint32, Follower> followers;
        std::map<uint32, Mission> missions;   // by slot
        uint32 nextFollower = 1, nextSlot = 1;
        uint32 viewing = 0;                   // offered mission being staffed
        std::vector<uint32> picked;
        bool addon = false;                   // the TemucraftMissions addon said hello this session
    };

    struct Settings
    {
        bool enable = true;
        uint32 offers = 6, offerHours = 24, maxFollowers = 20, maxLevel = 20, xpPerLevel = 400;
        float durationScale = 1.0f;
        int32 baseChance = 40, perLevel = 10, perLevelBelow = 15;
        float perItemLevel = 0.5f;
        int32 counterShare = 60, luckyBonus = 5;
        bool mailRewards = false;
        bool addonAnywhere = false, webOrders = true;
        float addonRange = 15.0f;
    } cfg;

    std::unordered_map<uint32, FollowerTemplate> followerTemplates;
    std::unordered_map<uint32, MissionTemplate> missionTemplates;
    std::unordered_map<uint32, PlayerData> players;   // by player guid counter
    std::mt19937 rng{ std::random_device{}() };

    template<typename T>
    char const* Text(T who, uint32 id, char const* fallback) { return TemucraftModuleText(MODULE, who, id, fallback); }

    std::vector<uint8> ParseList(std::string const& s)
    {
        std::vector<uint8> out;
        for (std::string_view part : Acore::Tokenize(s, ',', false))
            if (Optional<uint32> v = Acore::StringTo<uint32>(part))
                out.push_back(uint8(*v));
        return out;
    }
    std::vector<uint32> ParseIds(std::string const& s)
    {
        std::vector<uint32> out;
        for (std::string_view part : Acore::Tokenize(s, ',', false))
            if (Optional<uint32> v = Acore::StringTo<uint32>(part))
                out.push_back(*v);
        return out;
    }
    std::string JoinIds(std::vector<uint32> const& v)
    {
        std::string out;
        for (uint32 x : v) { if (!out.empty()) out += ','; out += std::to_string(x); }
        return out;
    }
    std::string Duration(uint32 seconds)
    {
        if (seconds >= 3600) return Acore::StringFormat("{}h {:02}m", seconds / 3600, (seconds % 3600) / 60);
        if (seconds >= 60) return Acore::StringFormat("{}m", seconds / 60);
        return Acore::StringFormat("{}s", seconds);
    }
    uint32 Now() { return uint32(GameTime::GetGameTime().count()); }

    void LoadSettings()
    {
        cfg.enable = sConfigMgr->GetOption<bool>("Temucraft.MissionTable.Enable", true);
        cfg.offers = sConfigMgr->GetOption<uint32>("Temucraft.MissionTable.Offers", 6);
        cfg.offerHours = sConfigMgr->GetOption<uint32>("Temucraft.MissionTable.OfferHours", 24);
        cfg.maxFollowers = sConfigMgr->GetOption<uint32>("Temucraft.MissionTable.MaxFollowers", 20);
        cfg.maxLevel = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("Temucraft.MissionTable.MaxFollowerLevel", 20));
        cfg.xpPerLevel = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("Temucraft.MissionTable.XpPerLevel", 400));
        cfg.durationScale = sConfigMgr->GetOption<float>("Temucraft.MissionTable.DurationScale", 1.0f);
        cfg.baseChance = sConfigMgr->GetOption<int32>("Temucraft.MissionTable.Chance.Base", 40);
        cfg.perLevel = sConfigMgr->GetOption<int32>("Temucraft.MissionTable.Chance.PerLevelAbove", 10);
        cfg.perLevelBelow = sConfigMgr->GetOption<int32>("Temucraft.MissionTable.Chance.PerLevelBelow", 15);
        cfg.perItemLevel = sConfigMgr->GetOption<float>("Temucraft.MissionTable.Chance.PerItemLevel", 0.5f);
        cfg.counterShare = sConfigMgr->GetOption<int32>("Temucraft.MissionTable.Chance.Counters", 60);
        cfg.luckyBonus = sConfigMgr->GetOption<int32>("Temucraft.MissionTable.Chance.Lucky", 5);
        cfg.mailRewards = sConfigMgr->GetOption<bool>("Temucraft.MissionTable.MailRewards", false);
        cfg.addonAnywhere = sConfigMgr->GetOption<bool>("Temucraft.MissionTable.Addon.Anywhere", false);
        cfg.webOrders = sConfigMgr->GetOption<bool>("Temucraft.MissionTable.WebOrders", true);
        cfg.addonRange = sConfigMgr->GetOption<float>("Temucraft.MissionTable.Addon.Range", 15.0f);
    }

    void LoadTemplates()
    {
        followerTemplates.clear(); missionTemplates.clear();
        if (QueryResult r = WorldDatabase.Query("SELECT id, creature_entry, name, roles, quality, abilities, traits, activity_mask, source, source_value, source_value2 FROM mod_temucraft_follower_template"))
            do
            {
                Field* f = r->Fetch(); FollowerTemplate t;
                t.id = f[0].Get<uint32>(); t.creatureEntry = f[1].Get<uint32>(); t.name = f[2].Get<std::string>();
                t.roles = f[3].Get<uint8>() & 7; if (!t.roles) t.roles = 4; t.quality = f[4].Get<uint8>();
                t.abilities = ParseList(f[5].Get<std::string>()); t.traits = ParseList(f[6].Get<std::string>());
                t.mask = f[7].Get<uint32>(); t.source = f[8].Get<uint8>(); t.sourceValue = f[9].Get<uint32>(); t.sourceValue2 = f[10].Get<uint32>();
                followerTemplates[t.id] = t;
            } while (r->NextRow());
        if (QueryResult r = WorldDatabase.Query("SELECT id, name, type, level, item_level, duration, slots, threats, cost, xp, gold, reward_item, reward_count, bonus_item, bonus_count, weight FROM mod_temucraft_mission_template"))
            do
            {
                Field* f = r->Fetch(); MissionTemplate m;
                m.id = f[0].Get<uint32>(); m.name = f[1].Get<std::string>(); m.type = f[2].Get<uint8>(); m.level = f[3].Get<uint8>();
                m.itemLevel = f[4].Get<uint32>(); m.duration = f[5].Get<uint32>(); m.slots = std::clamp<uint8>(f[6].Get<uint8>(), 1, 3);
                m.threats = ParseList(f[7].Get<std::string>()); m.cost = f[8].Get<uint32>(); m.xp = f[9].Get<uint32>(); m.gold = f[10].Get<uint32>();
                m.item = f[11].Get<uint32>(); m.itemCount = f[12].Get<uint32>(); m.bonusItem = f[13].Get<uint32>(); m.bonusCount = f[14].Get<uint32>();
                m.weight = f[15].Get<uint32>();
                missionTemplates[m.id] = m;
            } while (r->NextRow());
    }

    // ------------------------------------------------------------------------------------------- per player data
    // The follower's state (0 in the roster, 1 on a mission, 2 out in the world as a companion) is shared with
    // mod-temucraft-companions through the state column, so it is never written from memory here: only SetState
    // changes it, and only from the state it is expected to be in.
    void SaveFollower(Player* player, Follower const& f)
    {
        CharacterDatabase.Execute("INSERT INTO mod_temucraft_follower (owner, id, template_id, level, xp, item_level, quality, activity_mask, state) VALUES ({}, {}, {}, {}, {}, {}, {}, {}, 0) "
            "ON DUPLICATE KEY UPDATE level = VALUES(level), xp = VALUES(xp), item_level = VALUES(item_level), quality = VALUES(quality), activity_mask = VALUES(activity_mask)",
            player->GetGUID().GetCounter(), f.id, f.tmpl, f.level, f.xp, f.itemLevel, f.quality, f.mask);
    }
    void SetState(Player* player, uint32 followerId, uint8 from, uint8 to)
    {
        CharacterDatabase.DirectExecute("UPDATE mod_temucraft_follower SET state = {} WHERE owner = {} AND id = {} AND state = {}", to, player->GetGUID().GetCounter(), followerId, from);
    }
    void SaveMission(Player* player, Mission const& m)
    {
        CharacterDatabase.Execute("REPLACE INTO mod_temucraft_mission (owner, slot, mission_id, offered_at, start_time, end_time, chance, followers) VALUES ({}, {}, {}, {}, {}, {}, {}, '{}')",
            player->GetGUID().GetCounter(), m.slot, m.mission, m.offeredAt, m.start, m.end, m.chance, JoinIds(m.followers));
    }
    void DeleteMission(Player* player, uint32 slot)
    {
        CharacterDatabase.Execute("DELETE FROM mod_temucraft_mission WHERE owner = {} AND slot = {}", player->GetGUID().GetCounter(), slot);
    }

    PlayerData& Data(Player* player) { return players[player->GetGUID().GetCounter()]; }

    void LoadPlayer(Player* player)
    {
        PlayerData& d = players[player->GetGUID().GetCounter()];
        d = PlayerData();
        uint32 owner = player->GetGUID().GetCounter();
        if (QueryResult r = CharacterDatabase.Query("SELECT id, template_id, level, xp, item_level, quality, activity_mask, state FROM mod_temucraft_follower WHERE owner = {}", owner))
            do
            {
                Field* f = r->Fetch(); Follower fo;
                fo.id = f[0].Get<uint32>(); fo.tmpl = f[1].Get<uint32>(); fo.level = f[2].Get<uint8>(); fo.xp = f[3].Get<uint32>();
                fo.itemLevel = f[4].Get<uint32>(); fo.quality = f[5].Get<uint8>(); fo.mask = f[6].Get<uint32>(); fo.state = f[7].Get<uint8>();
                d.followers[fo.id] = fo; d.nextFollower = std::max(d.nextFollower, fo.id + 1);
            } while (r->NextRow());
        if (QueryResult r = CharacterDatabase.Query("SELECT slot, mission_id, offered_at, start_time, end_time, chance, followers FROM mod_temucraft_mission WHERE owner = {}", owner))
            do
            {
                Field* f = r->Fetch(); Mission m;
                m.slot = f[0].Get<uint32>(); m.mission = f[1].Get<uint32>(); m.offeredAt = f[2].Get<uint32>(); m.start = f[3].Get<uint32>();
                m.end = f[4].Get<uint32>(); m.chance = f[5].Get<uint32>();
                m.followers = ParseIds(f[6].Get<std::string>());
                if (missionTemplates.count(m.mission)) { d.missions[m.slot] = m; d.nextSlot = std::max(d.nextSlot, m.slot + 1); }
                else
                {
                    // its template was removed: free the followers and forget the mission
                    for (uint32 id : m.followers)
                        if (auto it = d.followers.find(id); it != d.followers.end() && it->second.state == F_ON_MISSION)
                        {
                            it->second.state = F_IDLE;
                            CharacterDatabase.Execute("UPDATE mod_temucraft_follower SET state = 0 WHERE owner = {} AND id = {} AND state = 1", owner, id);
                        }
                    CharacterDatabase.Execute("DELETE FROM mod_temucraft_mission WHERE owner = {} AND slot = {}", owner, m.slot);
                }
            } while (r->NextRow());
    }

    // states can change outside this module (companions), so they are read again whenever the table is used
    void RefreshStates(Player* player)
    {
        PlayerData& d = players[player->GetGUID().GetCounter()];
        if (QueryResult r = CharacterDatabase.Query("SELECT id, state FROM mod_temucraft_follower WHERE owner = {}", player->GetGUID().GetCounter()))
            do
            {
                auto it = d.followers.find(r->Fetch()[0].Get<uint32>());
                if (it != d.followers.end()) it->second.state = r->Fetch()[1].Get<uint8>();
            } while (r->NextRow());
    }

    bool HasTemplate(PlayerData const& d, uint32 tmpl)
    {
        for (auto const& [id, f] : d.followers) if (f.tmpl == tmpl) return true;
        return false;
    }

    bool GrantFollower(Player* player, uint32 tmplId, bool announce = true)
    {
        auto it = followerTemplates.find(tmplId);
        if (it == followerTemplates.end()) return false;
        PlayerData& d = Data(player);
        if (HasTemplate(d, tmplId) || d.followers.size() >= cfg.maxFollowers) return false;
        Follower f; f.id = d.nextFollower++; f.tmpl = tmplId; f.level = 1; f.quality = it->second.quality; f.mask = it->second.mask;
        f.itemLevel = 100 + 10 * f.quality;
        d.followers[f.id] = f; SaveFollower(player, f);
        if (announce)
            ChatHandler(player->GetSession()).PSendSysMessage(Text(player, S_NEW_FOLLOWER, "|cff66ccffMission table:|r {} joined you as a follower."), it->second.name);
        return true;
    }

    void GiveStarters(Player* player)
    {
        for (auto const& [id, t] : followerTemplates)
            if (t.source == SRC_STARTER) GrantFollower(player, id, true);
    }

    // ------------------------------------------------------------------------------------------- missions
    void RefreshOffers(Player* player)
    {
        PlayerData& d = Data(player);
        uint32 now = Now();
        std::set<uint32> have;
        for (auto it = d.missions.begin(); it != d.missions.end();)
        {
            Mission& m = it->second;
            if (!m.running() && now - m.offeredAt > cfg.offerHours * 3600) { DeleteMission(player, m.slot); it = d.missions.erase(it); continue; }
            have.insert(m.mission); ++it;
        }
        uint32 offered = 0;
        for (auto const& [s, m] : d.missions) if (!m.running()) ++offered;
        // top follower level decides which missions show up (a few levels above it)
        uint32 top = 1;
        for (auto const& [id, f] : d.followers) top = std::max<uint32>(top, f.level);
        while (offered < cfg.offers)
        {
            std::vector<MissionTemplate const*> pool; uint32 total = 0;
            for (auto const& [id, t] : missionTemplates)
                if (!have.count(id) && t.level <= top + 2 && t.weight) { pool.push_back(&t); total += t.weight; }
            if (pool.empty()) break;
            uint32 roll = std::uniform_int_distribution<uint32>(0, total - 1)(rng);
            MissionTemplate const* pick = pool.back();
            for (auto const* t : pool) { if (roll < t->weight) { pick = t; break; } roll -= t->weight; }
            Mission m; m.slot = d.nextSlot++; m.mission = pick->id; m.offeredAt = now;
            d.missions[m.slot] = m; SaveMission(player, m); have.insert(pick->id); ++offered;
        }
    }

    // abilities of the chosen followers countering the mission's threats (each ability once)
    uint32 Countered(MissionTemplate const& t, PlayerData const& d, std::vector<uint32> const& picked)
    {
        std::multiset<uint8> abilities;
        for (uint32 id : picked)
            if (auto it = d.followers.find(id); it != d.followers.end())
                if (auto tt = followerTemplates.find(it->second.tmpl); tt != followerTemplates.end())
                    abilities.insert(tt->second.abilities.begin(), tt->second.abilities.end());
        uint32 n = 0;
        for (uint8 threat : t.threats)
            if (auto a = abilities.find(threat); a != abilities.end()) { abilities.erase(a); ++n; }
        return n;
    }
    bool HasTrait(PlayerData const& d, std::vector<uint32> const& picked, uint8 trait)
    {
        for (uint32 id : picked)
            if (auto it = d.followers.find(id); it != d.followers.end())
                if (auto tt = followerTemplates.find(it->second.tmpl); tt != followerTemplates.end())
                    if (std::find(tt->second.traits.begin(), tt->second.traits.end(), trait) != tt->second.traits.end())
                        return true;
        return false;
    }
    int32 Chance(MissionTemplate const& t, PlayerData const& d, std::vector<uint32> const& picked)
    {
        if (picked.empty()) return 0;
        float level = 0, ilvl = 0;
        for (uint32 id : picked) { Follower const& f = d.followers.at(id); level += f.level; ilvl += f.itemLevel; }
        level /= picked.size(); ilvl /= picked.size();
        float chance = cfg.baseChance;
        float diff = level - t.level;
        chance += diff >= 0 ? diff * cfg.perLevel : diff * cfg.perLevelBelow;
        if (t.itemLevel) chance += (ilvl - t.itemLevel) * cfg.perItemLevel;
        chance += t.threats.empty() ? cfg.counterShare / 2.0f : cfg.counterShare * float(Countered(t, d, picked)) / t.threats.size();
        for (uint32 id : picked)
            if (auto tt = followerTemplates.find(d.followers.at(id).tmpl); tt != followerTemplates.end())
                for (uint8 tr : tt->second.traits)
                    if (tr == TRAIT_LUCKY) chance += cfg.luckyBonus;
        chance *= float(picked.size()) / t.slots;
        return std::clamp<int32>(int32(chance), 0, 200);
    }

    void GiveItem(Player* player, uint32 item, uint32 count)
    {
        if (!item || !count || !sObjectMgr->GetItemTemplate(item)) return;
        if (!cfg.mailRewards && player->AddItem(item, count))
            return;
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        MailDraft draft(Text(player, S_MAIL_SUBJECT, "Mission rewards"), Text(player, S_MAIL_BODY, "Your followers brought this back from their mission."));
        if (Item* it = Item::CreateItem(item, count, player))
        {
            it->SaveToDB(trans);
            draft.AddItem(it);
        }
        draft.SendMailTo(trans, MailReceiver(player), MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_DEFAULT));
        CharacterDatabase.CommitTransaction(trans);
    }

    void ClaimAll(Player* player)
    {
        PlayerData& d = Data(player);
        uint32 now = Now(), done = 0;
        ChatHandler h(player->GetSession());
        for (auto it = d.missions.begin(); it != d.missions.end();)
        {
            Mission m = it->second;
            if (!m.running() || m.end > now) { ++it; continue; }
            MissionTemplate const& t = missionTemplates.at(m.mission);
            uint32 roll = std::uniform_int_distribution<uint32>(1, 100)(rng);
            bool success = roll <= std::min<uint32>(m.chance, 100);
            bool bonus = success && m.chance > 100 && std::uniform_int_distribution<uint32>(1, 100)(rng) <= m.chance - 100;
            h.PSendSysMessage(Text(player, success ? S_SUCCESS : S_FAILED, success ? "|cff66ccffMission complete:|r {} succeeded." : "|cff66ccffMission complete:|r {} failed."), t.name);
            uint32 xp = success ? t.xp : t.xp / 4;
            if (HasTrait(d, m.followers, TRAIT_QUICK_STUDY)) xp = xp * 3 / 2;
            if (t.type == M_PATROL) xp *= 2;
            if (success)
            {
                uint32 gold = t.gold;
                if (t.type == M_TREASURE) gold *= 2;
                if (HasTrait(d, m.followers, TRAIT_TREASURE)) gold = gold * 3 / 2;
                if (gold) { player->ModifyMoney(gold); h.PSendSysMessage(Text(player, S_REWARD_GOLD, "  Reward: {} gold."), gold / 10000); }
                if (t.item) { GiveItem(player, t.item, t.itemCount); h.PSendSysMessage(Text(player, S_REWARD_ITEM, "  Reward: {} x{}."), sObjectMgr->GetItemTemplate(t.item) ? sObjectMgr->GetItemTemplate(t.item)->Name1 : "?", t.itemCount); }
                if (bonus && t.bonusItem)
                {
                    GiveItem(player, t.bonusItem, t.bonusCount);
                    h.PSendSysMessage(Text(player, S_BONUS, "  Bonus reward: {} x{}!"), sObjectMgr->GetItemTemplate(t.bonusItem) ? sObjectMgr->GetItemTemplate(t.bonusItem)->Name1 : "?", t.bonusCount);
                }
            }
            for (uint32 id : m.followers)
            {
                auto fit = d.followers.find(id);
                if (fit == d.followers.end()) continue;
                Follower& f = fit->second;
                f.state = F_IDLE;
                SetState(player, f.id, F_ON_MISSION, F_IDLE);
                if (f.level < cfg.maxLevel)
                {
                    f.xp += xp;
                    while (f.level < cfg.maxLevel && f.xp >= cfg.xpPerLevel * f.level)
                    {
                        f.xp -= cfg.xpPerLevel * f.level; ++f.level; f.itemLevel += 5;
                        h.PSendSysMessage(Text(player, S_LEVEL_UP, "  {} reached level {}."), followerTemplates.count(f.tmpl) ? followerTemplates[f.tmpl].name : "?", f.level);
                    }
                }
                SaveFollower(player, f);
            }
            h.PSendSysMessage(Text(player, S_REWARD_XP, "  Followers gained {} experience."), xp);
            DeleteMission(player, m.slot);
            it = d.missions.erase(it);
            ++done;
        }
        if (!done) h.SendSysMessage(Text(player, S_NOTHING_READY, "No mission is finished yet."));
    }

    // ------------------------------------------------------------------------------------------- the table
    enum Actions : uint32
    {
        A_MAIN = 1, A_CLAIM, A_OFFERS, A_RUNNING, A_FOLLOWERS, A_NOOP, A_START, A_CLEAR, A_WINDOW,
        A_OFFER_BASE = 1000, A_PICK_BASE = 1u << 30
    };

    std::string FollowerLine(Player* player, Follower const& f)
    {
        FollowerTemplate const* t = followerTemplates.count(f.tmpl) ? &followerTemplates[f.tmpl] : nullptr;
        std::string counters, traits;
        if (t)
        {
            for (uint8 a : t->abilities) if (a && a <= MAX_THREAT) { if (!counters.empty()) counters += ", "; counters += THREAT_NAMES[a]; }
            for (uint8 tr : t->traits) if (tr && tr <= 5) { if (!traits.empty()) traits += ", "; traits += TRAIT_NAMES[tr]; }
        }
        static char const* const state[] = { "", " [on a mission]", " [companion]" };
        return Acore::StringFormat(Text(player, S_FOLLOWER_LINE, "{} ({}) - level {}, item level {}{} | counters: {} | {}"),
            t ? t->name : "?", t ? RoleNames(t->roles) : std::string("?"), f.level, f.itemLevel, state[std::min<uint8>(f.state, 2)],
            counters.empty() ? "-" : counters, traits.empty() ? "-" : traits);
    }

    void ShowMain(Player* player, Creature* table)
    {
        PlayerData& d = Data(player);
        RefreshOffers(player);
        uint32 now = Now(), ready = 0, running = 0, offered = 0;
        for (auto const& [s, m] : d.missions)
        {
            if (!m.running()) ++offered;
            else if (m.end <= now) ++ready;
            else ++running;
        }
        ClearGossipMenuFor(player);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, Acore::StringFormat(Text(player, S_MAIN_REPORT, "Mission report ({} ready)"), ready), GOSSIP_SENDER_MAIN, A_CLAIM);
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, Acore::StringFormat(Text(player, S_MAIN_OFFERS, "Available missions ({})"), offered), GOSSIP_SENDER_MAIN, A_OFFERS);
        AddGossipItemFor(player, GOSSIP_ICON_TAXI, Acore::StringFormat(Text(player, S_MAIN_RUNNING, "Missions in progress ({})"), running), GOSSIP_SENDER_MAIN, A_RUNNING);
        AddGossipItemFor(player, GOSSIP_ICON_TRAINER, Acore::StringFormat(Text(player, S_MAIN_FOLLOWERS, "Followers ({})"), d.followers.size()), GOSSIP_SENDER_MAIN, A_FOLLOWERS);
        if (d.addon)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, Text(player, S_WINDOW, "Open the mission table window"), GOSSIP_SENDER_MAIN, A_WINDOW);
        SendGossipMenuFor(player, TEXT_TABLE, table->GetGUID());
    }

    void ShowOffers(Player* player, Creature* table)
    {
        PlayerData& d = Data(player);
        ClearGossipMenuFor(player);
        bool any = false;
        for (auto const& [slot, m] : d.missions)
        {
            if (m.running()) continue;
            MissionTemplate const& t = missionTemplates.at(m.mission);
            static char const* const types[] = { "", " (patrol)", " (treasure)", " (rare)" };
            AddGossipItemFor(player, t.type == M_RARE ? GOSSIP_ICON_INTERACT_1 : GOSSIP_ICON_BATTLE,
                Acore::StringFormat(Text(player, S_OFFER_LINE, "{}{} - level {}, {} follower(s), {}"), t.name, types[std::min<uint8>(t.type, 3)], t.level, t.slots,
                    Duration(uint32(t.duration * cfg.durationScale))), GOSSIP_SENDER_MAIN, A_OFFER_BASE + slot);
            any = true;
        }
        if (!any) AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_NONE_OFFERED, "No missions right now. Check back later."), GOSSIP_SENDER_MAIN, A_MAIN);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_BACK, "Back"), GOSSIP_SENDER_MAIN, A_MAIN);
        SendGossipMenuFor(player, TEXT_TABLE, table->GetGUID());
    }

    void ShowRunning(Player* player, Creature* table)
    {
        PlayerData& d = Data(player);
        uint32 now = Now();
        ClearGossipMenuFor(player);
        bool any = false;
        for (auto const& [slot, m] : d.missions)
        {
            if (!m.running()) continue;
            MissionTemplate const& t = missionTemplates.at(m.mission);
            AddGossipItemFor(player, GOSSIP_ICON_TAXI, Acore::StringFormat(Text(player, S_RUNNING_LINE, "{} - {}% - {}"), t.name, m.chance,
                m.end > now ? Duration(m.end - now) : std::string("done")), GOSSIP_SENDER_MAIN, m.end > now ? A_RUNNING : A_CLAIM);
            any = true;
        }
        if (!any) AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_NONE_RUNNING, "No missions in progress."), GOSSIP_SENDER_MAIN, A_MAIN);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_BACK, "Back"), GOSSIP_SENDER_MAIN, A_MAIN);
        SendGossipMenuFor(player, TEXT_TABLE, table->GetGUID());
    }

    void ShowFollowers(Player* player, Creature* table)
    {
        PlayerData& d = Data(player);
        ClearGossipMenuFor(player);
        if (d.followers.empty()) AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_NO_FOLLOWERS, "You have no followers yet."), GOSSIP_SENDER_MAIN, A_MAIN);
        for (auto const& [id, f] : d.followers)
            AddGossipItemFor(player, GOSSIP_ICON_TRAINER, FollowerLine(player, f), GOSSIP_SENDER_MAIN, A_FOLLOWERS);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_BACK, "Back"), GOSSIP_SENDER_MAIN, A_MAIN);
        SendGossipMenuFor(player, TEXT_TABLE, table->GetGUID());
    }

    void ShowMission(Player* player, Creature* table)
    {
        PlayerData& d = Data(player);
        auto mit = d.missions.find(d.viewing);
        if (mit == d.missions.end() || mit->second.running()) { ShowOffers(player, table); return; }
        MissionTemplate const& t = missionTemplates.at(mit->second.mission);
        ClearGossipMenuFor(player);
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, Acore::StringFormat(Text(player, S_MISSION_HEAD, "{} - level {}{}, {}, cost {} gold"), t.name, t.level,
            t.itemLevel ? Acore::StringFormat(" (item level {})", t.itemLevel) : std::string(), Duration(uint32(t.duration * cfg.durationScale)), t.cost / 10000),
            GOSSIP_SENDER_MAIN, A_NOOP);
        for (uint8 threat : t.threats)
            if (threat && threat <= MAX_THREAT)
                AddGossipItemFor(player, GOSSIP_ICON_INTERACT_2, Acore::StringFormat(Text(player, S_THREAT_LINE, "  Threat: {}"), THREAT_NAMES[threat]), GOSSIP_SENDER_MAIN, A_NOOP);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, Acore::StringFormat(Text(player, S_CHANCE_LINE, "Success chance: {}%  ({}/{} followers, {} of {} threats countered)"),
            Chance(t, d, d.picked), d.picked.size(), t.slots, Countered(t, d, d.picked), t.threats.size()), GOSSIP_SENDER_MAIN, A_NOOP);
        for (uint32 id : d.picked)
            AddGossipItemFor(player, GOSSIP_ICON_TRAINER, Acore::StringFormat(Text(player, S_PICKED_LINE, "  Sent: {}"), FollowerLine(player, d.followers.at(id))), GOSSIP_SENDER_MAIN, A_NOOP);
        if (d.picked.size() < t.slots)
            for (auto const& [id, f] : d.followers)
                if (f.state == F_IDLE && (f.mask & ACT_MISSIONS) && std::find(d.picked.begin(), d.picked.end(), id) == d.picked.end())
                    AddGossipItemFor(player, GOSSIP_ICON_TRAINER, Acore::StringFormat(Text(player, S_PICK_LINE, "Send {}"), FollowerLine(player, f)), GOSSIP_SENDER_MAIN, A_PICK_BASE + id);
        if (!d.picked.empty())
        {
            if (d.picked.size() == t.slots)
                AddGossipItemFor(player, GOSSIP_ICON_BATTLE, Text(player, S_START, "Start the mission"), GOSSIP_SENDER_MAIN, A_START, "", t.cost, false);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_CLEAR, "Choose again"), GOSSIP_SENDER_MAIN, A_CLEAR);
        }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_BACK, "Back"), GOSSIP_SENDER_MAIN, A_OFFERS);
        SendGossipMenuFor(player, TEXT_TABLE, table->GetGUID());
    }

    // start d.viewing with d.picked; false (and a message) if it can't
    bool StartCore(Player* player)
    {
        PlayerData& d = Data(player);
        auto mit = d.missions.find(d.viewing);
        if (mit == d.missions.end() || mit->second.running()) return false;
        MissionTemplate const& t = missionTemplates.at(mit->second.mission);
        ChatHandler h(player->GetSession());
        if (d.picked.size() != t.slots) { h.SendSysMessage(Text(player, S_NEED_FOLLOWERS, "Choose enough followers first.")); return false; }
        RefreshStates(player);
        for (uint32 id : d.picked)
            if (!d.followers.count(id) || d.followers[id].state != F_IDLE || !(d.followers[id].mask & ACT_MISSIONS))
            {
                h.SendSysMessage(Text(player, S_BUSY, "One of those followers is busy (on a mission or out as a companion)."));
                d.picked.clear(); return false;
            }
        if (!player->HasEnoughMoney(t.cost)) { h.PSendSysMessage(Text(player, S_NO_MONEY, "This mission costs {} gold."), t.cost / 10000); return false; }
        player->ModifyMoney(-int32(t.cost));
        Mission& m = mit->second;
        uint32 duration = uint32(t.duration * cfg.durationScale);
        if (HasTrait(d, d.picked, TRAIT_FAST)) duration = duration * 3 / 4;
        m.chance = Chance(t, d, d.picked); m.start = Now(); m.end = m.start + duration; m.followers = d.picked;
        for (uint32 id : d.picked) { d.followers[id].state = F_ON_MISSION; SetState(player, id, F_IDLE, F_ON_MISSION); }
        SaveMission(player, m);
        h.PSendSysMessage(Text(player, S_STARTED, "|cff66ccffMission started:|r {} ({}% chance), back in {}."), t.name, m.chance, Duration(duration));
        d.picked.clear(); d.viewing = 0;
        return true;
    }

    void StartMission(Player* player, Creature* table)
    {
        PlayerData& d = Data(player);
        auto mit = d.missions.find(d.viewing);
        if (mit == d.missions.end() || mit->second.running()) { ShowOffers(player, table); return; }
        if (StartCore(player)) ShowMain(player, table);
        else ShowMission(player, table);
    }

    // ------------------------------------------------------------------------------------------- addon window
    // TemucraftMissions (client addon) <-> server, prefix TMT, fields '~':
    //   client: HELLO | LIST | CHANCE~slot~ids | START~slot~ids | CLAIM
    //   server: OPEN | BEGIN~now~gold~near | F~id~name~roles~level~xp~xpNext~ilvl~quality~state~mask~abilities~traits
    //           | O~slot~name~type~level~ilvl~seconds~slots~cost~threats~expiresIn
    //           | R~slot~name~chance~secondsLeft~followers | C~slot~chance~countered~threats | END
    std::string CsvBytes(std::vector<uint8> const& v)
    {
        std::string out;
        for (uint8 x : v) { if (!out.empty()) out += ','; out += std::to_string(x); }
        return out;
    }

    bool NearTable(Player* player)
    {
        return cfg.addonAnywhere || player->FindNearestCreature(NPC_TABLE, cfg.addonRange);
    }

    void SendList(Player* player)
    {
        PlayerData& d = Data(player);
        if (d.followers.empty()) GiveStarters(player);
        RefreshStates(player);
        RefreshOffers(player);
        uint32 now = Now();
        TemucraftAddonSend(player, ADDON_PREFIX, Acore::StringFormat("BEGIN~{}~{}~{}", now, player->GetMoney(), NearTable(player) ? 1 : 0));
        for (auto const& [id, f] : d.followers)
        {
            FollowerTemplate const* t = followerTemplates.count(f.tmpl) ? &followerTemplates[f.tmpl] : nullptr;
            TemucraftAddonSend(player, ADDON_PREFIX, Acore::StringFormat("F~{}~{}~{}~{}~{}~{}~{}~{}~{}~{}~{}~{}", f.id,
                TemucraftAddonClean(t ? t->name : "?"), t ? t->roles : 4, f.level, f.xp, f.level >= cfg.maxLevel ? 0 : cfg.xpPerLevel * f.level,
                f.itemLevel, f.quality, f.state, f.mask, t ? CsvBytes(t->abilities) : "", t ? CsvBytes(t->traits) : ""));
        }
        for (auto const& [slot, m] : d.missions)
        {
            MissionTemplate const& t = missionTemplates.at(m.mission);
            if (!m.running())
            {
                uint32 expires = m.offeredAt + cfg.offerHours * 3600;
                TemucraftAddonSend(player, ADDON_PREFIX, Acore::StringFormat("O~{}~{}~{}~{}~{}~{}~{}~{}~{}~{}", slot, TemucraftAddonClean(t.name), t.type, t.level,
                    t.itemLevel, uint32(t.duration * cfg.durationScale), t.slots, t.cost, CsvBytes(t.threats), expires > now ? expires - now : 0));
            }
            else
                TemucraftAddonSend(player, ADDON_PREFIX, Acore::StringFormat("R~{}~{}~{}~{}~{}", slot, TemucraftAddonClean(t.name), m.chance,
                    m.end > now ? m.end - now : 0, JoinIds(m.followers)));
        }
        TemucraftAddonSend(player, ADDON_PREFIX, "END");
    }

    // ids from the addon: owned, idle, can do missions, no repeats, at most `slots`
    std::vector<uint32> PickedFromAddon(PlayerData& d, std::string const& csv, uint32 slots)
    {
        std::vector<uint32> out;
        for (uint32 id : ParseIds(csv))
        {
            auto it = d.followers.find(id);
            if (it == d.followers.end() || it->second.state != F_IDLE || !(it->second.mask & ACT_MISSIONS)) continue;
            if (std::find(out.begin(), out.end(), id) != out.end() || out.size() >= slots) continue;
            out.push_back(id);
        }
        return out;
    }

    void HandleAddon(Player* player, std::string const& body)
    {
        std::vector<std::string> a = TemucraftAddonFields(body);
        if (a.empty()) return;
        PlayerData& d = Data(player);
        std::string const& cmd = a[0];
        if (cmd == "HELLO") { d.addon = true; TemucraftAddonSend(player, ADDON_PREFIX, "HI"); return; }
        if (cmd == "LIST") { SendList(player); return; }
        if (cmd == "CLAIM" || cmd == "START")
            if (!NearTable(player))
            {
                ChatHandler(player->GetSession()).SendSysMessage(Text(player, S_NEED_TABLE, "You need to be at a mission table for that."));
                return;
            }
        if (cmd == "CLAIM") { ClaimAll(player); SendList(player); return; }
        if ((cmd == "CHANCE" || cmd == "START") && a.size() >= 3)
        {
            Optional<uint32> slot = Acore::StringTo<uint32>(a[1]);
            auto mit = slot ? d.missions.find(*slot) : d.missions.end();
            if (mit == d.missions.end() || mit->second.running()) { SendList(player); return; }
            MissionTemplate const& t = missionTemplates.at(mit->second.mission);
            RefreshStates(player);
            std::vector<uint32> picked = PickedFromAddon(d, a[2], t.slots);
            if (cmd == "CHANCE")
            {
                TemucraftAddonSend(player, ADDON_PREFIX, Acore::StringFormat("C~{}~{}~{}~{}", *slot, Chance(t, d, picked), Countered(t, d, picked), t.threats.size()));
                return;
            }
            d.viewing = *slot; d.picked = picked;
            StartCore(player);
            d.picked.clear(); d.viewing = 0;
            SendList(player);
        }
    }

    // ------------------------------------------------------------------------------------------- website orders
    // The website's Companion page queues "start" / "claim" in mod_temucraft_web_order; they are checked like the
    // window's requests (followers idle, enough of them, gold) and carried out for the character.
    void ProcessOrders(Player* player)
    {
        if (!cfg.enable || !cfg.webOrders) return;
        uint32 owner = player->GetGUID().GetCounter();
        QueryResult r = CharacterDatabase.Query("SELECT id, action, slot, followers FROM mod_temucraft_web_order WHERE owner = {} AND done_at = 0 ORDER BY id", owner);
        if (!r) return;
        PlayerData& d = Data(player);
        do
        {
            Field* f = r->Fetch();
            uint32 id = f[0].Get<uint32>();
            std::string action = f[1].Get<std::string>(), result;
            if (action == "claim") { ClaimAll(player); result = "claimed"; }
            else if (action == "start")
            {
                uint32 slot = f[2].Get<uint32>();
                auto mit = d.missions.find(slot);
                if (mit == d.missions.end() || mit->second.running()) result = "mission no longer offered";
                else
                {
                    RefreshStates(player);
                    d.viewing = slot;
                    d.picked = PickedFromAddon(d, f[3].Get<std::string>(), missionTemplates.at(mit->second.mission).slots);
                    result = StartCore(player) ? "started" : "could not start (followers busy, too few, or not enough gold)";
                    d.picked.clear(); d.viewing = 0;
                }
            }
            else result = "unknown order";
            std::string escaped = result;
            CharacterDatabase.EscapeString(escaped);
            CharacterDatabase.Execute("UPDATE mod_temucraft_web_order SET done_at = {}, result = '{}' WHERE id = {}", Now(), escaped, id);
        } while (r->NextRow());
        if (d.addon) SendList(player);
    }

    class npc_temucraft_mission_table : public CreatureScript
    {
    public:
        npc_temucraft_mission_table() : CreatureScript("npc_temucraft_mission_table") { }

        bool OnGossipHello(Player* player, Creature* creature) override
        {
            if (!cfg.enable) { ChatHandler(player->GetSession()).SendSysMessage(Text(player, S_OFF, "The mission table is closed.")); return true; }
            if (Data(player).followers.empty()) GiveStarters(player);
            RefreshStates(player);
            ShowMain(player, creature);
            return true;
        }

        bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
        {
            PlayerData& d = Data(player);
            switch (action)
            {
                case A_MAIN: ShowMain(player, creature); return true;
                case A_CLAIM: ClaimAll(player); ShowMain(player, creature); return true;
                case A_OFFERS: d.picked.clear(); RefreshStates(player); ShowOffers(player, creature); return true;
                case A_RUNNING: ShowRunning(player, creature); return true;
                case A_FOLLOWERS: ShowFollowers(player, creature); return true;
                case A_NOOP: ShowMission(player, creature); return true;
                case A_START: StartMission(player, creature); return true;
                case A_CLEAR: d.picked.clear(); ShowMission(player, creature); return true;
                case A_WINDOW:
                    CloseGossipMenuFor(player);
                    TemucraftAddonSend(player, ADDON_PREFIX, "OPEN");
                    SendList(player);
                    return true;
                default: break;
            }
            if (action >= A_PICK_BASE)
            {
                uint32 id = action - A_PICK_BASE;
                if (d.followers.count(id) && d.followers[id].state == F_IDLE && std::find(d.picked.begin(), d.picked.end(), id) == d.picked.end())
                    d.picked.push_back(id);
                ShowMission(player, creature);
            }
            else if (action >= A_OFFER_BASE)
            {
                d.viewing = action - A_OFFER_BASE; d.picked.clear(); RefreshStates(player);
                ShowMission(player, creature);
            }
            return true;
        }
    };

    // ------------------------------------------------------------------------------------------- earning followers
    class temucraft_mission_table_player : public PlayerScript
    {
    public:
        temucraft_mission_table_player() : PlayerScript("temucraft_mission_table_player",
            { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST, PLAYERHOOK_ON_REPUTATION_RANK_CHANGE, PLAYERHOOK_ON_DELETE,
              PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT }) { }

        using PlayerScript::OnPlayerCanUseChat;
        // the TemucraftMissions addon whispering itself; swallowed so nothing shows in chat
        bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg, Player* receiver) override
        {
            std::string body;
            if (!TemucraftAddonParse(player, type, language, msg, receiver, ADDON_PREFIX, body)) return true;
            if (cfg.enable) HandleAddon(player, body);
            return false;
        }

        void OnPlayerLogin(Player* player) override
        {
            LoadPlayer(player);
            ProcessOrders(player);
            if (!cfg.enable) return;
            uint32 now = Now(), ready = 0;
            for (auto const& [s, m] : Data(player).missions) if (m.running() && m.end <= now) ++ready;
            if (ready)
                ChatHandler(player->GetSession()).PSendSysMessage(Text(player, S_READY_LOGIN, "|cff66ccffMission table:|r {} mission(s) finished while you were away."), ready);
        }
        void OnPlayerLogout(Player* player) override { players.erase(player->GetGUID().GetCounter()); }
        void OnPlayerDelete(ObjectGuid guid, uint32 /*accountId*/) override
        {
            CharacterDatabase.Execute("DELETE FROM mod_temucraft_follower WHERE owner = {}", guid.GetCounter());
            CharacterDatabase.Execute("DELETE FROM mod_temucraft_mission WHERE owner = {}", guid.GetCounter());
        }
        void OnPlayerCompleteQuest(Player* player, Quest const* quest) override
        {
            if (!cfg.enable || !quest) return;
            for (auto const& [id, t] : followerTemplates)
                if (t.source == SRC_QUEST && t.sourceValue == quest->GetQuestId()) GrantFollower(player, id);
        }
        void OnPlayerReputationRankChange(Player* player, uint32 factionId, ReputationRank newRank, ReputationRank /*oldRank*/, bool increased) override
        {
            if (!cfg.enable || !increased) return;
            for (auto const& [id, t] : followerTemplates)
                if (t.source == SRC_REPUTATION && t.sourceValue == factionId && uint32(newRank) >= t.sourceValue2) GrantFollower(player, id);
        }
    };

    // follower contracts (shop or loot items): template.source = 3, source_value = item entry
    class item_temucraft_follower_contract : public ItemScript
    {
    public:
        item_temucraft_follower_contract() : ItemScript("item_temucraft_follower_contract") { }

        bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
        {
            if (!cfg.enable) return true;
            for (auto const& [id, t] : followerTemplates)
                if (t.source == SRC_ITEM && t.sourceValue == item->GetEntry() && GrantFollower(player, id))
                {
                    player->DestroyItemCount(item->GetEntry(), 1, true);
                    return true;
                }
            return true;
        }
    };

    // ------------------------------------------------------------------------------------------- GM commands
    class temucraft_mission_table_commands : public CommandScript
    {
    public:
        temucraft_mission_table_commands() : CommandScript("temucraft_mission_table_commands") { }

        ChatCommandTable GetCommands() const override
        {
            static ChatCommandTable follower =
            {
                { "add",  HandleFollowerAdd,  100601 /* SEC_GAMEMASTER -> RBAC */, Console::No },
                { "mask", HandleFollowerMask, 100602 /* SEC_GAMEMASTER -> RBAC */, Console::No },
                { "list", HandleFollowerList, SEC_PLAYER,     Console::No },
            };
            static ChatCommandTable missions =
            {
                { "reload", HandleReload, 100603 /* SEC_ADMINISTRATOR -> RBAC */, Console::Yes },
                { "finish", HandleFinish, 100604 /* SEC_GAMEMASTER -> RBAC */,    Console::No },
            };
            static ChatCommandTable root = { { "follower", follower }, { "missions", missions } };
            return root;
        }

        static Player* Target(ChatHandler* handler)
        {
            Player* p = handler->getSelectedPlayer();
            return p ? p : handler->GetPlayer();
        }
        static bool HandleFollowerAdd(ChatHandler* handler, uint32 templateId)
        {
            Player* p = Target(handler);
            if (!p || !GrantFollower(p, templateId)) { handler->SendSysMessage("No such follower template, already owned, or the roster is full."); return true; }
            return true;
        }
        static bool HandleFollowerMask(ChatHandler* handler, uint32 followerId, uint32 mask)
        {
            Player* p = Target(handler);
            if (!p) return false;
            PlayerData& d = Data(p);
            auto it = d.followers.find(followerId);
            if (it == d.followers.end()) { handler->SendSysMessage("No such follower."); return true; }
            it->second.mask = mask; SaveFollower(p, it->second);
            handler->PSendSysMessage("Follower {} activity mask is now {}.", followerId, mask);
            return true;
        }
        static bool HandleFollowerList(ChatHandler* handler)
        {
            Player* p = Target(handler);
            if (!p) return false;
            for (auto const& [id, f] : Data(p).followers)
                handler->PSendSysMessage("#{} {} (mask {})", id, FollowerLine(p, f), f.mask);
            return true;
        }
        static bool HandleReload(ChatHandler* handler)
        {
            LoadTemplates();
            // missions whose template is gone can't be shown or finished: drop them (the code looks templates up directly)
            for (auto& [owner, d] : players)
                for (auto it = d.missions.begin(); it != d.missions.end();)
                {
                    if (missionTemplates.count(it->second.mission)) { ++it; continue; }
                    CharacterDatabase.Execute("DELETE FROM mod_temucraft_mission WHERE owner = {} AND slot = {}", owner, it->first);
                    CharacterDatabase.Execute("UPDATE mod_temucraft_follower SET state = 0 WHERE owner = {} AND state = 1 AND FIND_IN_SET(id, '{}')", owner, JoinIds(it->second.followers));
                    for (uint32 id : it->second.followers) if (d.followers.count(id)) d.followers[id].state = F_IDLE;
                    it = d.missions.erase(it);
                }
            handler->PSendSysMessage("mod-temucraft-mission-table: {} follower and {} mission templates.", followerTemplates.size(), missionTemplates.size());
            return true;
        }
        static bool HandleFinish(ChatHandler* handler)
        {
            Player* p = Target(handler);
            if (!p) return false;
            uint32 now = Now();
            for (auto& [s, m] : Data(p).missions) if (m.running() && m.end > now) { m.end = now; SaveMission(p, m); }
            handler->SendSysMessage("All running missions are now finished.");
            return true;
        }
    };

    class temucraft_mission_table_world : public WorldScript
    {
    public:
        temucraft_mission_table_world() : WorldScript("temucraft_mission_table_world", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE }) { }
        void OnAfterConfigLoad(bool /*reload*/) override { LoadSettings(); }
        void OnStartup() override { LoadTemplates(); }

        // website orders for characters that are online
        void OnUpdate(uint32 diff) override
        {
            if (!cfg.enable || !cfg.webOrders || (timer += diff) < 15000) return;
            timer = 0;
            QueryResult r = CharacterDatabase.Query("SELECT DISTINCT owner FROM mod_temucraft_web_order WHERE done_at = 0");
            if (!r) return;
            do
                if (Player* p = ObjectAccessor::FindPlayerByLowGUID(r->Fetch()[0].Get<uint32>()); p && p->IsInWorld() && players.count(p->GetGUID().GetCounter()))
                    ProcessOrders(p);
            while (r->NextRow());
        }
    private:
        uint32 timer = 0;
    };
}

void AddTemucraftMissionTableScripts()
{
    new temucraft_mission_table_world();
    new npc_temucraft_mission_table();
    new temucraft_mission_table_player();
    new item_temucraft_follower_contract();
    new temucraft_mission_table_commands();
}
