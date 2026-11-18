/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

/*
 * mod-temucraft-companions: one of your mission-table followers (mod-temucraft-mission-table) walks with you and
 * fights in its role: a tank taunts what attacks you or your group, a healer heals whoever is hurt most, a damage
 * dealer attacks your target. A follower can only take the roles in its template's `roles` bitmask (1 tank,
 * 2 healer, 4 damage); it starts in the first one it has.
 *
 * Where it may come is the follower's activity mask: 2 dungeons, 8 raids, 32 the open world (world quests); 4 expeditions
 * and 16 follower dungeons are for the expedition and follower-dungeon features. Never in battlegrounds or arenas, and
 * never while the follower is away on a mission.
 *
 * Companions have their own level (companion_* columns of mod_temucraft_follower), earned from kills while they are out; each level makes
 * them a little stronger. Their strength also follows your level and the follower's quality and item level.
 *
 * Follower dungeons: in a 5-player dungeon, .companion party fills the free places of your group with followers that
 * have the follower-dungeon bit (16): a tank if nobody in the group tanks, a healer if nobody heals, damage for the
 * rest. They stay for that dungeon only (leaving it, logging out or .companion party dismiss sends them back).
 *
 * Commands: .companion list | summon <follower #> | dismiss | role <tank|healer|damage> | party [dismiss]
 * Text comes from module_string (module "mod-temucraft-companions"), with English built in as a fallback.
 */
#include "Chat.h"
#include "ChatCommand.h"
#include "CommandScript.h"
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "TemucraftAddonMessage.h"
#include "TemucraftModuleText.h"
#include "WorldScript.h"
#include <algorithm>
#include <unordered_map>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    constexpr char const* MODULE = "mod-temucraft-companions";
    constexpr uint32 NPC_BODY_TANK = 608810, NPC_BODY_HEALER = 608811, NPC_BODY_DAMAGE = 608812;
    constexpr uint32 SPELL_TAUNT = 355, SPELL_FLASH_HEAL = 48071, SPELL_RENEW = 48068;
    enum Role : uint8 { ROLE_TANK = 0, ROLE_HEALER = 1, ROLE_DAMAGE = 2 };
    enum Mask : uint32 { ACT_DUNGEONS = 2, ACT_EXPEDITIONS = 4, ACT_RAIDS = 8, ACT_FOLLOWER_DUNGEONS = 16, ACT_WORLD = 32 };

    enum Str : uint32
    {
        S_OFF = 1, S_NONE, S_LIST_LINE, S_NOT_FOUND, S_ON_MISSION, S_NOT_HERE, S_SUMMONED, S_DISMISSED, S_ROLE,
        S_LEVEL_UP, S_NOT_OUT, S_BAD_ROLE, S_LEFT_BEHIND, S_CANT_ROLE,
        S_PARTY_OFF, S_PARTY_NOT_HERE, S_PARTY_FULL, S_PARTY_NONE, S_PARTY_JOINED, S_PARTY_GONE
    };

    struct Settings
    {
        bool enable = true;
        bool followerDungeons = true;
        uint32 maxLevel = 30, xpPerLevel = 500, expeditionPhase = 2, partySize = 5;
        float healthTank = 3.0f, healthOther = 1.5f, damageTank = 0.8f, damageHealer = 0.5f, damageDamage = 1.4f, perLevel = 0.03f;
    } cfg;

    struct Template { uint32 creatureEntry = 0; std::string name; uint8 roles = 4; };
    bool CanBe(uint8 roles, uint8 role) { return roles & (1 << role); }
    uint8 FirstRole(uint8 roles) { for (uint8 r = 0; r < 3; ++r) if (CanBe(roles, r)) return r; return ROLE_DAMAGE; }
    char const* const ROLE_WORDS[] = { "tank", "healer", "damage" };
    std::string RoleList(uint8 roles)
    {
        std::string out;
        for (uint8 r = 0; r < 3; ++r) if (CanBe(roles, r)) { if (!out.empty()) out += "/"; out += ROLE_WORDS[r]; }
        return out;
    }
    std::unordered_map<uint32, Template> templates;
    struct Out { ObjectGuid creature; uint32 follower = 0, tmpl = 0, mask = 0, companionLevel = 1, xp = 0; uint8 role = ROLE_DAMAGE, quality = 2; uint32 itemLevel = 100; };
    std::unordered_map<uint32, Out> active;   // by player guid counter
    std::unordered_map<uint32, std::vector<Out>> party;   // follower-dungeon followers, by player guid counter
    std::unordered_map<uint32, uint32> partyMap;          // the dungeon map they were called in

    template<typename T>
    char const* Text(T who, uint32 id, char const* fallback) { return TemucraftModuleText(MODULE, who, id, fallback); }

    void LoadSettings()
    {
        cfg.enable = sConfigMgr->GetOption<bool>("Temucraft.Companions.Enable", true);
        cfg.maxLevel = sConfigMgr->GetOption<uint32>("Temucraft.Companions.MaxLevel", 30);
        cfg.xpPerLevel = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("Temucraft.Companions.XpPerLevel", 500));
        cfg.healthTank = sConfigMgr->GetOption<float>("Temucraft.Companions.Health.Tank", 3.0f);
        cfg.healthOther = sConfigMgr->GetOption<float>("Temucraft.Companions.Health.Other", 1.5f);
        cfg.damageTank = sConfigMgr->GetOption<float>("Temucraft.Companions.Damage.Tank", 0.8f);
        cfg.damageHealer = sConfigMgr->GetOption<float>("Temucraft.Companions.Damage.Healer", 0.5f);
        cfg.damageDamage = sConfigMgr->GetOption<float>("Temucraft.Companions.Damage.Damage", 1.4f);
        cfg.perLevel = sConfigMgr->GetOption<float>("Temucraft.Companions.PerCompanionLevel", 0.03f);
        cfg.expeditionPhase = sConfigMgr->GetOption<uint32>("Temucraft.Companions.ExpeditionPhase", 2);
        cfg.followerDungeons = sConfigMgr->GetOption<bool>("Temucraft.Companions.FollowerDungeons", true);
        cfg.partySize = std::clamp<uint32>(sConfigMgr->GetOption<uint32>("Temucraft.Companions.FollowerDungeons.PartySize", 5), 2, 5);
    }
    void LoadTemplates()
    {
        templates.clear();
        if (QueryResult r = WorldDatabase.Query("SELECT id, creature_entry, name, roles FROM mod_temucraft_follower_template"))
            do
            {
                Field* f = r->Fetch();
                uint8 roles = f[3].Get<uint8>() & 7;
                templates[f[0].Get<uint32>()] = { f[1].Get<uint32>(), f[2].Get<std::string>(), uint8(roles ? roles : 4) };
            } while (r->NextRow());
    }

    // which activity bit the player's whereabouts need; 0 = never allowed. A dungeon seen from the expedition phase
    // (mod-temucraft-expeditions) is an expedition.
    uint32 RequiredBit(Player* player)
    {
        Map* map = player ? player->GetMap() : nullptr;
        if (!map || map->IsBattlegroundOrArena()) return 0;
        if (map->IsDungeon() && cfg.expeditionPhase > 1 && player->GetPhaseMask() == cfg.expeditionPhase) return ACT_EXPEDITIONS;
        if (map->IsRaid()) return ACT_RAIDS;
        if (map->IsDungeon()) return ACT_DUNGEONS;
        return ACT_WORLD;
    }

    Creature* Find(Player* player)
    {
        auto it = active.find(player->GetGUID().GetCounter());
        if (it == active.end()) return nullptr;
        return ObjectAccessor::GetCreature(*player, it->second.creature);
    }

    void ApplyStats(Creature* c, Player* owner, Out const& o)
    {
        c->SetLevel(owner->GetLevel());
        CreatureBaseStats const* stats = sObjectMgr->GetCreatureBaseStats(owner->GetLevel(), c->GetCreatureTemplate()->unit_class);
        float growth = (1.0f + cfg.perLevel * (o.companionLevel - 1)) * (0.8f + 0.1f * o.quality) * (1.0f + (float(o.itemLevel) - 100.0f) / 400.0f);
        uint32 health = uint32(stats->BaseHealth[EXPANSION_WRATH_OF_THE_LICH_KING] * (o.role == ROLE_TANK ? cfg.healthTank : cfg.healthOther) * growth);
        c->SetCreateHealth(health); c->SetMaxHealth(health); c->SetFullHealth();
        float dmg = stats->BaseDamage[EXPANSION_WRATH_OF_THE_LICH_KING] * (o.role == ROLE_TANK ? cfg.damageTank : o.role == ROLE_HEALER ? cfg.damageHealer : cfg.damageDamage) * growth;
        c->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, dmg * 0.9f);
        c->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, dmg * 1.1f);
        c->UpdateAttackPowerAndDamage();
        c->SetFaction(owner->GetFaction());
    }

    struct npc_temucraft_companionAI : public ScriptedAI
    {
        explicit npc_temucraft_companionAI(Creature* creature) : ScriptedAI(creature) { }

        ObjectGuid owner;
        uint8 role = ROLE_DAMAGE;
        int32 healTimer = 1500, tauntTimer = 1000, checkTimer = 1000;

        Player* Owner() { return ObjectAccessor::GetPlayer(*me, owner); }

        void Follow(Player* p)
        {
            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
                me->GetMotionMaster()->MoveFollow(p, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE + float(M_PI) / 2);
        }

        Unit* MostHurt(Player* p)
        {
            Unit* best = nullptr; float pct = 85.0f;
            auto consider = [&](Unit* u) { if (u && u->IsAlive() && me->IsWithinDistInMap(u, 40.0f) && u->GetHealthPct() < pct) { pct = u->GetHealthPct(); best = u; } };
            consider(me); consider(p);
            if (Group* g = p->GetGroup())
                for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
                    consider(ref->GetSource());
            return best;
        }

        void UpdateAI(uint32 diff) override
        {
            Player* p = Owner();
            if (!p || !p->IsInWorld() || p->GetMap() != me->GetMap()) { me->DespawnOrUnsummon(); return; }
            // follow the owner into other phases (expeditions), and leave where it may not be
            if (me->GetPhaseMask() != p->GetPhaseMask())
            {
                me->SetPhaseMask(p->GetPhaseMask(), true);
                auto it = active.find(p->GetGUID().GetCounter());
                if (it != active.end() && !(it->second.mask & RequiredBit(p)))
                {
                    ChatHandler(p->GetSession()).SendSysMessage(Text(p, S_LEFT_BEHIND, "Your companion waits outside: it can't come here."));
                    it->second.creature.Clear();
                    me->DespawnOrUnsummon();
                    return;
                }
            }
            if (!p->IsAlive()) { if (me->IsInCombat()) EnterEvadeMode(); Follow(p); return; }
            if (me->GetDistance(p) > 70.0f) { me->NearTeleportTo(p->GetPositionX(), p->GetPositionY(), p->GetPositionZ(), p->GetOrientation()); return; }

            if (role == ROLE_HEALER && (healTimer -= diff) <= 0)
            {
                healTimer = 2500;
                if (Unit* hurt = MostHurt(p))
                {
                    me->CastSpell(hurt, hurt->GetHealthPct() < 50.0f ? SPELL_FLASH_HEAL : SPELL_RENEW, true);
                    return;
                }
            }

            Unit* victim = me->GetVictim();
            if (!victim || !victim->IsAlive())
            {
                Unit* target = p->GetVictim();
                if (!target && p->IsInCombat()) target = p->getAttackerForHelper();
                if (target && target->IsAlive() && me->IsValidAttackTarget(target) && role != ROLE_HEALER)
                    AttackStart(target);
                else if (!me->IsInCombat())
                    Follow(p);
                return;
            }

            if (role == ROLE_TANK && (tauntTimer -= diff) <= 0)
            {
                tauntTimer = 2000;
                // pull whatever is hitting the owner or a group member
                Unit* loose = p->getAttackerForHelper();
                if (Group* g = p->GetGroup())
                    for (GroupReference* ref = g->GetFirstMember(); ref && !loose; ref = ref->next())
                        if (Player* m = ref->GetSource()) loose = m->getAttackerForHelper();
                if (loose && loose->GetVictim() != me && me->IsValidAttackTarget(loose) && me->IsWithinDistInMap(loose, 30.0f))
                    me->CastSpell(loose, SPELL_TAUNT, true);
            }
            DoMeleeAttackIfReady();
        }

        void EnterEvadeMode(EvadeReason /*why*/ = EVADE_REASON_OTHER) override
        {
            me->CombatStop(true);
            me->GetThreatMgr().ClearAllThreat();
            if (Player* p = Owner()) me->GetMotionMaster()->MoveFollow(p, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE + float(M_PI) / 2);
        }
    };

    class npc_temucraft_companion : public CreatureScript
    {
    public:
        npc_temucraft_companion() : CreatureScript("npc_temucraft_companion") { }
        CreatureAI* GetAI(Creature* creature) const override { return new npc_temucraft_companionAI(creature); }
    };

    // follower state in mod_temucraft_follower, shared with the mission table: 0 in the roster, 1 on a mission,
    // 2 out in the world as a companion
    constexpr uint8 STATE_IDLE = 0, STATE_ON_MISSION = 1, STATE_COMPANION = 2;
    void SetState(uint32 owner, uint32 followerId, uint8 from, uint8 to)
    {
        CharacterDatabase.DirectExecute("UPDATE mod_temucraft_follower SET state = {} WHERE owner = {} AND id = {} AND state = {}", to, owner, followerId, from);
    }

    void Dismiss(Player* player, bool tell, bool keepActive = false)
    {
        if (Creature* c = Find(player)) c->DespawnOrUnsummon();
        auto it = active.find(player->GetGUID().GetCounter());
        if (it != active.end()) SetState(player->GetGUID().GetCounter(), it->second.follower, STATE_COMPANION, STATE_IDLE);
        if (keepActive) { active.erase(player->GetGUID().GetCounter()); return; }
        if (active.erase(player->GetGUID().GetCounter()) && tell)
            ChatHandler(player->GetSession()).SendSysMessage(Text(player, S_DISMISSED, "Your companion heads back to camp."));
        CharacterDatabase.Execute("UPDATE mod_temucraft_follower SET companion_active = 0 WHERE owner = {}", player->GetGUID().GetCounter());
    }

    // spawn the active companion next to the player (after summon, login or a map change)
    bool Spawn(Player* player, Out& o, bool tell)
    {
        uint32 bit = RequiredBit(player);
        if (!bit || !(o.mask & bit))
        {
            if (tell) ChatHandler(player->GetSession()).SendSysMessage(Text(player, S_NOT_HERE, "That follower can't come with you here."));
            return false;
        }
        Template const& t = templates[o.tmpl];
        uint32 entry = t.creatureEntry && sObjectMgr->GetCreatureTemplate(t.creatureEntry) ? t.creatureEntry
            : o.role == ROLE_TANK ? NPC_BODY_TANK : o.role == ROLE_HEALER ? NPC_BODY_HEALER : NPC_BODY_DAMAGE;
        Position pos = player->GetNearPosition(2.0f, float(M_PI) / 2);
        TempSummon* c = player->SummonCreature(entry, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), player->GetOrientation(), TEMPSUMMON_MANUAL_DESPAWN);
        if (!c) return false;
        if (t.creatureEntry && entry == t.creatureEntry)
            c->SetAI(new npc_temucraft_companionAI(c));
        c->SetCreatorGUID(player->GetGUID());
        c->SetName(t.name);
        if (auto* ai = dynamic_cast<npc_temucraft_companionAI*>(c->AI())) { ai->owner = player->GetGUID(); ai->role = o.role; }
        ApplyStats(c, player, o);
        c->GetMotionMaster()->MoveFollow(player, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE + float(M_PI) / 2);
        o.creature = c->GetGUID();
        if (tell)
            ChatHandler(player->GetSession()).PSendSysMessage(Text(player, S_SUMMONED, "{} joins you (companion level {})."), t.name, o.companionLevel);
        return true;
    }

    // read the follower from the mission table's tables
    bool Summon(Player* player, uint32 followerId, bool tell)
    {
        uint32 owner = player->GetGUID().GetCounter();
        QueryResult r = CharacterDatabase.Query("SELECT template_id, activity_mask, state, item_level, quality, companion_level, companion_xp, companion_role FROM mod_temucraft_follower WHERE owner = {} AND id = {}", owner, followerId);
        if (!r) { if (tell) ChatHandler(player->GetSession()).SendSysMessage(Text(player, S_NOT_FOUND, "You have no follower with that number (see .companion list).")); return false; }
        Field* f = r->Fetch();
        if (f[2].Get<uint8>() == STATE_ON_MISSION) { if (tell) ChatHandler(player->GetSession()).SendSysMessage(Text(player, S_ON_MISSION, "That follower is away on a mission.")); return false; }
        Dismiss(player, false);
        Out o; o.follower = followerId; o.tmpl = f[0].Get<uint32>(); o.mask = f[1].Get<uint32>(); o.itemLevel = f[3].Get<uint32>(); o.quality = f[4].Get<uint8>();
        uint8 roles = templates.count(o.tmpl) ? templates[o.tmpl].roles : 4;
        o.role = FirstRole(roles);
        o.companionLevel = std::max<uint32>(1, f[5].Get<uint32>()); o.xp = f[6].Get<uint32>();
        if (uint8 role = f[7].Get<uint8>(); role <= 2 && CanBe(roles, role)) o.role = role;
        if (!Spawn(player, o, tell)) return false;
        active[owner] = o;
        SetState(owner, followerId, STATE_IDLE, STATE_COMPANION);
        CharacterDatabase.Execute("UPDATE mod_temucraft_follower SET companion_active = (id = {}) WHERE owner = {}", followerId, owner);
        return true;
    }

    // --------------------------------------------------------------------------------- follower dungeons
    bool FollowerDungeonHere(Player* p)
    {
        Map* map = p->GetMap();
        return map && map->IsDungeon() && !map->IsRaid() && !(cfg.expeditionPhase > 1 && p->GetPhaseMask() == cfg.expeditionPhase);
    }

    // the role a player's spec plays (talent tree with the most points); 2 = damage
    uint8 PlayerRole(Player* m)
    {
        uint8 points[3] = { 0, 0, 0 };
        m->GetTalentTreePoints(points);
        if (!points[0] && !points[1] && !points[2])
            return ROLE_DAMAGE;
        uint32 const spec = m->getClass() * 10 + m->GetMostPointsTalentTree();
        switch (spec)
        {
            case CLASS_WARRIOR * 10 + 2: case CLASS_PALADIN * 10 + 1: case CLASS_DRUID * 10 + 1:
            case CLASS_DEATH_KNIGHT * 10 + 0: case CLASS_DEATH_KNIGHT * 10 + 1: case CLASS_DEATH_KNIGHT * 10 + 2:
                return ROLE_TANK;
            case CLASS_PRIEST * 10 + 0: case CLASS_PRIEST * 10 + 1: case CLASS_PALADIN * 10 + 0:
            case CLASS_SHAMAN * 10 + 2: case CLASS_DRUID * 10 + 2:
                return ROLE_HEALER;
            default:
                return ROLE_DAMAGE;
        }
    }

    void PartyDismiss(Player* player, bool tell)
    {
        uint32 const owner = player->GetGUID().GetCounter();
        auto it = party.find(owner);
        if (it == party.end())
            return;
        for (Out& o : it->second)
        {
            if (Creature* c = ObjectAccessor::GetCreature(*player, o.creature))
                c->DespawnOrUnsummon();
            SetState(owner, o.follower, STATE_COMPANION, STATE_IDLE);
        }
        party.erase(it);
        partyMap.erase(owner);
        if (tell)
            ChatHandler(player->GetSession()).SendSysMessage(Text(player, S_PARTY_GONE, "Your follower party heads back to camp."));
    }

    // fill the group's free places with followers (follower-dungeon bit), tank and healer first if the group lacks them
    void PartyFill(Player* player)
    {
        ChatHandler h(player->GetSession());
        if (!cfg.followerDungeons)
        {
            h.SendSysMessage(Text(player, S_PARTY_OFF, "Follower dungeons are turned off."));
            return;
        }
        if (!FollowerDungeonHere(player))
        {
            h.SendSysMessage(Text(player, S_PARTY_NOT_HERE, "Call your follower party inside a 5-player dungeon."));
            return;
        }
        uint32 const owner = player->GetGUID().GetCounter();
        PartyDismiss(player, false);

        // who is there already: the group's players, and your own companion
        uint32 members = 1;
        bool hasTank = false, hasHealer = false;
        auto count = [&](uint8 role) { hasTank |= role == ROLE_TANK; hasHealer |= role == ROLE_HEALER; };
        if (Group* g = player->GetGroup())
        {
            members = 0;
            for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
                if (Player* m = ref->GetSource())
                {
                    ++members;
                    count(PlayerRole(m));
                }
        }
        else
            count(PlayerRole(player));
        auto mine = active.find(owner);
        if (mine != active.end() && Find(player))
        {
            ++members;
            count(mine->second.role);
        }
        if (members >= cfg.partySize)
        {
            h.SendSysMessage(Text(player, S_PARTY_FULL, "Your group is already full."));
            return;
        }

        // idle followers that may come to follower dungeons, best first
        struct Pick { Out o; uint8 roles; };
        std::vector<Pick> pool;
        if (QueryResult r = CharacterDatabase.Query("SELECT id, template_id, activity_mask, item_level, quality, companion_level, companion_xp FROM mod_temucraft_follower "
                "WHERE owner = {} AND state = 0 AND (activity_mask & {}) ORDER BY quality DESC, item_level DESC, companion_level DESC, id", owner, uint32(ACT_FOLLOWER_DUNGEONS)))
            do
            {
                Field* f = r->Fetch();
                Out o; o.follower = f[0].Get<uint32>(); o.tmpl = f[1].Get<uint32>(); o.mask = f[2].Get<uint32>(); o.itemLevel = f[3].Get<uint32>();
                o.quality = f[4].Get<uint8>(); o.companionLevel = std::max<uint32>(1, f[5].Get<uint32>()); o.xp = f[6].Get<uint32>();
                pool.push_back({ o, uint8(templates.count(o.tmpl) ? templates[o.tmpl].roles : 4) });
            } while (r->NextRow());

        std::vector<Out>& out = party[owner];
        auto take = [&](uint8 role) -> bool
        {
            for (auto it = pool.begin(); it != pool.end(); ++it)
                if (CanBe(it->roles, role))
                {
                    Out o = it->o;
                    o.role = role;
                    o.mask |= ACT_DUNGEONS;                       // Spawn's check: it may be in this dungeon
                    pool.erase(it);
                    if (!Spawn(player, o, false))
                        return false;
                    SetState(owner, o.follower, STATE_IDLE, STATE_COMPANION);
                    out.push_back(o);
                    return true;
                }
            return false;
        };
        uint32 places = cfg.partySize - members;
        if (!hasTank && places && take(ROLE_TANK)) --places;
        if (!hasHealer && places && take(ROLE_HEALER)) --places;
        while (places && take(ROLE_DAMAGE)) --places;
        if (out.empty())
        {
            party.erase(owner);
            h.SendSysMessage(Text(player, S_PARTY_NONE, "No idle follower can come to follower dungeons (activity bit 16)."));
            return;
        }
        partyMap[owner] = player->GetMapId();
        std::string names;
        for (Out const& o : out)
            names += (names.empty() ? "" : ", ") + (templates.count(o.tmpl) ? templates[o.tmpl].name : std::string("?")) + " (" + ROLE_WORDS[o.role] + ")";
        h.PSendSysMessage(Text(player, S_PARTY_JOINED, "Your follower party joins you: {}."), names);
    }

    class temucraft_companions_player : public PlayerScript
    {
    public:
        temucraft_companions_player() : PlayerScript("temucraft_companions_player",
            { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_MAP_CHANGED, PLAYERHOOK_ON_CREATURE_KILL }) { }

        void OnPlayerLogin(Player* player) override
        {
            if (!cfg.enable) return;
            if (QueryResult r = CharacterDatabase.Query("SELECT id FROM mod_temucraft_follower WHERE owner = {} AND companion_active = 1 LIMIT 1", player->GetGUID().GetCounter()))
                Summon(player, r->Fetch()[0].Get<uint32>(), false);
        }
        void OnPlayerLogout(Player* player) override
        {
            // back in the roster while you're offline (it stays 'active', so it comes back at login)
            Dismiss(player, false, true);
            PartyDismiss(player, false);
        }
        void OnPlayerMapChanged(Player* player) override
        {
            // the follower party belongs to its dungeon
            if (auto pm = partyMap.find(player->GetGUID().GetCounter()); pm != partyMap.end() && pm->second != player->GetMapId())
                PartyDismiss(player, true);
            auto it = active.find(player->GetGUID().GetCounter());
            if (it == active.end()) return;
            if (!Spawn(player, it->second, false))
            {
                ChatHandler(player->GetSession()).SendSysMessage(Text(player, S_LEFT_BEHIND, "Your companion waits outside: it can't come here."));
                // stays 'active', so it comes back on the next map that allows it
            }
        }
        void OnPlayerCreatureKill(Player* player, Creature* killed) override
        {
            auto it = active.find(player->GetGUID().GetCounter());
            if (it == active.end() || !killed || killed->GetLevel() + 10 < player->GetLevel()) return;
            Out& o = it->second;
            if (o.companionLevel >= cfg.maxLevel) return;
            o.xp += killed->isWorldBoss() || killed->IsDungeonBoss() ? 100 : killed->isElite() ? 20 : 5;
            bool up = false;
            while (o.companionLevel < cfg.maxLevel && o.xp >= cfg.xpPerLevel * o.companionLevel) { o.xp -= cfg.xpPerLevel * o.companionLevel; ++o.companionLevel; up = true; }
            CharacterDatabase.Execute("UPDATE mod_temucraft_follower SET companion_level = {}, companion_xp = {} WHERE owner = {} AND id = {}", o.companionLevel, o.xp, player->GetGUID().GetCounter(), o.follower);
            if (up)
            {
                ChatHandler(player->GetSession()).PSendSysMessage(Text(player, S_LEVEL_UP, "{} reached companion level {}."), templates[o.tmpl].name, o.companionLevel);
                if (Creature* c = Find(player)) ApplyStats(c, player, o);
            }
        }
    };

    class temucraft_companions_commands : public CommandScript
    {
    public:
        temucraft_companions_commands() : CommandScript("temucraft_companions_commands") { }

        ChatCommandTable GetCommands() const override
        {
            static ChatCommandTable sub =
            {
                { "list",    HandleList,    SEC_PLAYER, Console::No },
                { "summon",  HandleSummon,  SEC_PLAYER, Console::No },
                { "dismiss", HandleDismiss, SEC_PLAYER, Console::No },
                { "role",    HandleRole,    SEC_PLAYER, Console::No },
                { "party",   HandleParty,   SEC_PLAYER, Console::No },
            };
            static ChatCommandTable root = { { "companion", sub } };
            return root;
        }

        static bool Enabled(ChatHandler* h)
        {
            if (!cfg.enable) h->SendSysMessage(Text(h, S_OFF, "Companions are turned off."));
            return cfg.enable;
        }
        static bool HandleList(ChatHandler* handler)
        {
            Player* p = handler->GetPlayer();
            if (!p || !Enabled(handler)) return true;
            QueryResult r = CharacterDatabase.Query("SELECT id, template_id, state, activity_mask, companion_level FROM mod_temucraft_follower WHERE owner = {} ORDER BY id", p->GetGUID().GetCounter());
            if (!r) { handler->SendSysMessage(Text(handler, S_NONE, "You have no followers yet - visit a mission table.")); return true; }
            do
            {
                Field* f = r->Fetch(); uint32 t = f[1].Get<uint32>();
                static char const* const states[] = { "", " - on a mission", " - with you" };
                handler->PSendSysMessage(Text(handler, S_LIST_LINE, "#{} {} ({}), companion level {}{}"), f[0].Get<uint32>(), templates.count(t) ? templates[t].name : "?",
                    RoleList(templates.count(t) ? templates[t].roles : 4), f[4].Get<uint32>(), states[std::min<uint8>(f[2].Get<uint8>(), 2)]);
            } while (r->NextRow());
            return true;
        }
        static bool HandleSummon(ChatHandler* handler, uint32 followerId)
        {
            Player* p = handler->GetPlayer();
            if (p && Enabled(handler)) Summon(p, followerId, true);
            return true;
        }
        static bool HandleDismiss(ChatHandler* handler)
        {
            Player* p = handler->GetPlayer();
            if (!p) return true;
            if (!active.count(p->GetGUID().GetCounter())) { handler->SendSysMessage(Text(handler, S_NOT_OUT, "No companion is with you.")); return true; }
            Dismiss(p, true);
            return true;
        }
        static bool HandleParty(ChatHandler* handler, Optional<std::string> what)
        {
            Player* p = handler->GetPlayer();
            if (!p || !Enabled(handler)) return true;
            if (what && *what == "dismiss") PartyDismiss(p, true);
            else PartyFill(p);
            return true;
        }
        static bool HandleRole(ChatHandler* handler, std::string role)
        {
            Player* p = handler->GetPlayer();
            if (!p) return true;
            auto it = active.find(p->GetGUID().GetCounter());
            if (it == active.end()) { handler->SendSysMessage(Text(handler, S_NOT_OUT, "No companion is with you.")); return true; }
            uint8 r = role == "tank" ? ROLE_TANK : role == "healer" || role == "heal" ? ROLE_HEALER : role == "damage" || role == "dps" ? ROLE_DAMAGE : 255;
            if (r == 255) { handler->SendSysMessage(Text(handler, S_BAD_ROLE, "Roles: tank, healer, damage.")); return true; }
            uint8 roles = templates.count(it->second.tmpl) ? templates[it->second.tmpl].roles : 4;
            if (!CanBe(roles, r))
            {
                handler->PSendSysMessage(Text(handler, S_CANT_ROLE, "This follower can't be a {}; it can be: {}."), ROLE_WORDS[r], RoleList(roles));
                return true;
            }
            it->second.role = r;
            CharacterDatabase.Execute("UPDATE mod_temucraft_follower SET companion_role = {} WHERE owner = {} AND id = {}", r, p->GetGUID().GetCounter(), it->second.follower);
            if (Creature* c = Find(p))
            {
                if (auto* ai = dynamic_cast<npc_temucraft_companionAI*>(c->AI())) ai->role = r;
                ApplyStats(c, p, it->second);
            }
            handler->PSendSysMessage(Text(handler, S_ROLE, "Your companion is now a {}."), ROLE_WORDS[r]);
            return true;
        }
    };

    // ------------------------------------------------------------------------------------------- addon window
    // TemucraftMissions (client addon) <-> server, prefix TCP, fields '~':
    //   client: HELLO | LIST | SUMMON~follower | DISMISS | ROLE~0|1|2 | PARTY | PARTYDISMISS
    //   server: HI | BEGIN~activeFollower~activeRole~allowedHereMask | F~id~name~roles~level~xp~xpNext~state~mask~role | END
    constexpr char const* ADDON_PREFIX = "TCP";
    void SendCompanionList(Player* p)
    {
        uint32 owner = p->GetGUID().GetCounter();
        auto it = active.find(owner);
        TemucraftAddonSend(p, ADDON_PREFIX, Acore::StringFormat("BEGIN~{}~{}~{}", it != active.end() ? it->second.follower : 0,
            it != active.end() ? it->second.role : 255, RequiredBit(p)));
        if (QueryResult r = CharacterDatabase.Query("SELECT id, template_id, state, activity_mask, companion_level, companion_xp, companion_role FROM mod_temucraft_follower WHERE owner = {} ORDER BY id", owner))
            do
            {
                Field* f = r->Fetch(); uint32 t = f[1].Get<uint32>(); uint32 level = std::max<uint32>(1, f[4].Get<uint32>());
                TemucraftAddonSend(p, ADDON_PREFIX, Acore::StringFormat("F~{}~{}~{}~{}~{}~{}~{}~{}~{}", f[0].Get<uint32>(),
                    TemucraftAddonClean(templates.count(t) ? templates[t].name : "?"), templates.count(t) ? templates[t].roles : 4, level, f[5].Get<uint32>(),
                    level >= cfg.maxLevel ? 0 : cfg.xpPerLevel * level, f[2].Get<uint8>(), f[3].Get<uint32>(), f[6].Get<uint8>()));
            } while (r->NextRow());
        TemucraftAddonSend(p, ADDON_PREFIX, "END");
    }

    class temucraft_companions_addon : public PlayerScript
    {
    public:
        temucraft_companions_addon() : PlayerScript("temucraft_companions_addon", { PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT }) { }

        using PlayerScript::OnPlayerCanUseChat;
        bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg, Player* receiver) override
        {
            std::string body;
            if (!TemucraftAddonParse(player, type, language, msg, receiver, ADDON_PREFIX, body)) return true;
            if (!cfg.enable) return false;
            std::vector<std::string> a = TemucraftAddonFields(body);
            if (a.empty()) return false;
            ChatHandler h(player->GetSession());
            if (a[0] == "HELLO") TemucraftAddonSend(player, ADDON_PREFIX, "HI");
            else if (a[0] == "SUMMON" && a.size() >= 2)
            {
                if (Optional<uint32> id = Acore::StringTo<uint32>(a[1])) temucraft_companions_commands::HandleSummon(&h, *id);
                SendCompanionList(player);
            }
            else if (a[0] == "DISMISS") { temucraft_companions_commands::HandleDismiss(&h); SendCompanionList(player); }
            else if (a[0] == "ROLE" && a.size() >= 2)
            {
                static char const* const words[] = { "tank", "healer", "damage" };
                if (Optional<uint32> r = Acore::StringTo<uint32>(a[1]); r && *r <= 2) temucraft_companions_commands::HandleRole(&h, words[*r]);
                SendCompanionList(player);
            }
            else if (a[0] == "PARTY") { PartyFill(player); SendCompanionList(player); }
            else if (a[0] == "PARTYDISMISS") { PartyDismiss(player, true); SendCompanionList(player); }
            else if (a[0] == "LIST") SendCompanionList(player);
            return false;
        }
    };

    class temucraft_companions_world : public WorldScript
    {
    public:
        temucraft_companions_world() : WorldScript("temucraft_companions_world", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE }) { }
        // a companion that had to wait outside comes back as soon as the player is somewhere it may be
        void OnUpdate(uint32 diff) override
        {
            if (!cfg.enable || (timer += diff) < 2000) return;
            timer = 0;
            for (auto& [owner, o] : active)
            {
                Player* p = ObjectAccessor::FindPlayerByLowGUID(owner);
                if (!p || !p->IsInWorld() || p->IsBeingTeleported() || !p->IsAlive() || Find(p)) continue;
                uint32 bit = RequiredBit(p);
                if (bit && (o.mask & bit)) Spawn(p, o, false);
            }
        }
        uint32 timer = 0;
        void OnAfterConfigLoad(bool /*reload*/) override { LoadSettings(); }
        void OnStartup() override
        {
            LoadTemplates();
            // nobody is online yet: a companion left 'out' by a crash goes back to the roster
            CharacterDatabase.DirectExecute("UPDATE mod_temucraft_follower SET state = 0 WHERE state = 2");
        }
    };
}

void AddTemucraftCompanionsScripts()
{
    new temucraft_companions_world();
    new npc_temucraft_companion();
    new temucraft_companions_player();
    new temucraft_companions_commands();
    new temucraft_companions_addon();
}
