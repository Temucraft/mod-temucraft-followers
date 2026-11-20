/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

/*
 * Mission success chance for mod-temucraft-mission-table, worked like the Warlords garrison mission table.
 * Plain C++ with no server headers, so the same code can be checked outside the server (see tests/).
 *
 *  1. Bias per follower: (follower level - mission level) / 3, plus (follower item level - mission item level) / 15
 *     when the mission has an item level. Clamped to -1 .. +1.
 *  2. CalcChance(a, b, bias): bias >= 0 gives a + (b - a) * bias, below 0 gives a * (1 + bias).
 *  3. Total weight = slots * 100 + the weight of every threat (300 unless the mission says otherwise).
 *     coeff = 100 / total weight.
 *  4. Each follower adds CalcChance(100, 150, bias) * coeff points.
 *  5. Threats of one kind are summed. Each matching ability removes CalcChance(w, w, bias) from that sum (w = the
 *     average weight of one threat of that kind); what it removes, times coeff, is added. A follower below the
 *     mission level counters less.
 *  6. Traits add CalcChance(amount2, amount3, bias) * coeff when they apply (environment, enemy race, alone, paired
 *     race, long or short mission, mission type, profession, always). Mentor raises everyone to the best level and
 *     item level in the group. Lucky adds a flat amount at the end.
 *  7. chance = base + (100 - base) * points / 100 (+ Lucky). It may go over 100: the part above 100 is the chance of
 *     the bonus reward. Capped at maxChance (200).
 */
#ifndef TEMUCRAFT_MISSION_CHANCE_H
#define TEMUCRAFT_MISSION_CHANCE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace TemucraftMission
{
    enum MissionType : uint8_t
    {
        M_BATTLE = 0, M_PATROL = 1, M_TREASURE = 2, M_RARE = 3, M_EXPLORATION = 4, M_TRAINING = 5, M_LOGISTICS = 6,
        M_PROFESSION = 7
    };
    constexpr uint8_t MAX_MISSION_TYPE = 7;
    inline char const* const TYPE_NAMES[] = { "battle", "patrol", "treasure", "rare", "exploration", "training", "logistics", "profession" };

    inline char const* const THREAT_NAMES[] = { "", "Hazardous Ground", "Lethal Lackeys", "Area Assault", "Hexes", "Crushing Blow",
                                                "Hordes", "Arcane Barrage", "Race Against Time", "Berserk Fury" };
    constexpr uint8_t MAX_THREAT = 9;

    // where the mission takes place (mission template `environment`)
    inline char const* const ENV_NAMES[] = { "", "Forest", "Mountains", "Plains", "Snow", "Desert", "Underground", "Swamp", "Jungle", "Town" };
    constexpr uint8_t MAX_ENV = 9;

    // who the followers fight (mission template `enemy_race`)
    inline char const* const ENEMY_NAMES[] = { "", "Undead", "Beast", "Dragonkin", "Elemental", "Demon", "Giant", "Humanoid", "Troll",
                                               "Vrykul", "Nerubian", "Construct", "Ogre", "Orc", "Harpy", "Aberration" };
    constexpr uint8_t MAX_ENEMY = 15;

    // the follower's own race (follower template `race`, the player race ids)
    inline char const* const RACE_NAMES[] = { "", "Human", "Orc", "Dwarf", "Night Elf", "Undead", "Tauren", "Gnome", "Troll", "", "Blood Elf", "Draenei" };
    constexpr uint8_t MAX_RACE = 11;
    constexpr uint32_t RaceBit(uint8_t race) { return race ? 1u << (race - 1) : 0u; }

    enum TraitEffect : uint8_t
    {
        EFF_NONE = 0,
        EFF_ALONE = 1,         // only when it is the only follower on the mission
        EFF_ALWAYS = 2,        // always
        EFF_RACE_PAIR = 5,     // another follower of a race in `param` (race bits) is on the mission
        EFF_LONG = 6,          // mission lasts `param` hours or more
        EFF_SHORT = 7,         // mission lasts less than `param` hours
        EFF_TIME = 17,         // mission time x amount2 / 100
        EFF_MENTOR = 18,       // the others count at the best level and item level of the group
        EFF_GOLD = 100,        // gold reward + amount2 %
        EFF_XP = 101,          // follower experience + amount2 %
        EFF_ENVIRONMENT = 102, // mission environment is `param`
        EFF_SLAYER = 103,      // mission enemy race is `param`
        EFF_MISSION_TYPE = 104,// mission type bit (1 << type) in `param`
        EFF_PROFESSION = 105,  // profession trait: missions that need it (`required_trait`) get the bonus
        EFF_LUCKY = 106        // flat bonus to the final chance (Temucraft.MissionTable.Chance.Lucky)
    };

    struct TraitInfo
    {
        uint8_t id;
        char const* name;
        uint8_t effect;
        uint32_t param;
        double amount2, amount3;   // CalcChance amounts (weight points at bias 0 and at bias +1); percent for TIME/GOLD/XP
        char const* text;
    };

    // Trait ids are stored in mod_temucraft_follower_template.traits; 1-5 are the original ones.
    inline std::vector<TraitInfo> const& Traits()
    {
        static std::vector<TraitInfo> const list =
        {
            { 1, "Swift Feet", EFF_TIME, 0, 75, 75, "Missions take 25% less time." },
            { 2, "Gold Nose", EFF_GOLD, 0, 50, 50, "+50% gold from missions." },
            { 3, "Keen Mind", EFF_XP, 0, 50, 50, "+50% follower experience." },
            { 4, "Lucky", EFF_LUCKY, 0, 0, 0, "A flat bonus to the success chance." },
            { 5, "Watchful", EFF_MISSION_TYPE, (1u << M_PATROL) | (1u << M_EXPLORATION), 50, 75, "Better on patrol and exploration missions." },
            { 6, "Lone Wolf", EFF_ALONE, 0, 80, 120, "Better when sent alone." },
            { 7, "Mentor", EFF_MENTOR, 0, 0, 0, "The others on the mission count at the best level and item level of the group." },
            { 8, "Long Haul", EFF_LONG, 8, 50, 75, "Better on missions of 8 hours or more." },
            { 9, "Quick Strike", EFF_SHORT, 4, 50, 75, "Better on missions shorter than 4 hours." },
            { 10, "Veteran", EFF_ALWAYS, 0, 30, 45, "A little better on every mission." },
            // race pairing
            { 11, "Friend of Humans", EFF_RACE_PAIR, RaceBit(1), 50, 75, "Better with a Human follower." },
            { 12, "Friend of Orcs", EFF_RACE_PAIR, RaceBit(2), 50, 75, "Better with an Orc follower." },
            { 13, "Friend of Dwarves", EFF_RACE_PAIR, RaceBit(3), 50, 75, "Better with a Dwarf follower." },
            { 14, "Friend of Elves", EFF_RACE_PAIR, RaceBit(4) | RaceBit(10), 50, 75, "Better with a Night Elf or Blood Elf follower." },
            { 15, "Friend of Gnomes", EFF_RACE_PAIR, RaceBit(7), 50, 75, "Better with a Gnome follower." },
            { 16, "Friend of Tauren", EFF_RACE_PAIR, RaceBit(6), 50, 75, "Better with a Tauren follower." },
            { 17, "Friend of Trolls", EFF_RACE_PAIR, RaceBit(8), 50, 75, "Better with a Troll follower." },
            { 18, "Friend of the Forsaken", EFF_RACE_PAIR, RaceBit(5), 50, 75, "Better with an Undead follower." },
            { 19, "Friend of Draenei", EFF_RACE_PAIR, RaceBit(11), 50, 75, "Better with a Draenei follower." },
            // environment (20 + environment)
            { 21, "Woodland Guide", EFF_ENVIRONMENT, 1, 60, 90, "Better in forests." },
            { 22, "Mountain Born", EFF_ENVIRONMENT, 2, 60, 90, "Better in the mountains." },
            { 23, "Plains Runner", EFF_ENVIRONMENT, 3, 60, 90, "Better on open plains." },
            { 24, "Frost Hardened", EFF_ENVIRONMENT, 4, 60, 90, "Better in snow." },
            { 25, "Sand Walker", EFF_ENVIRONMENT, 5, 60, 90, "Better in the desert." },
            { 26, "Tunnel Rat", EFF_ENVIRONMENT, 6, 60, 90, "Better underground." },
            { 27, "Bog Wader", EFF_ENVIRONMENT, 7, 60, 90, "Better in swamps." },
            { 28, "Jungle Stalker", EFF_ENVIRONMENT, 8, 60, 90, "Better in the jungle." },
            { 29, "Street Wise", EFF_ENVIRONMENT, 9, 60, 90, "Better in towns and cities." },
            // slayer (30 + enemy race)
            { 31, "Undead Slayer", EFF_SLAYER, 1, 60, 90, "Better against the undead." },
            { 32, "Beast Slayer", EFF_SLAYER, 2, 60, 90, "Better against beasts." },
            { 33, "Dragon Slayer", EFF_SLAYER, 3, 60, 90, "Better against dragonkin." },
            { 34, "Elemental Slayer", EFF_SLAYER, 4, 60, 90, "Better against elementals." },
            { 35, "Demon Slayer", EFF_SLAYER, 5, 60, 90, "Better against demons." },
            { 36, "Giant Slayer", EFF_SLAYER, 6, 60, 90, "Better against giants." },
            { 37, "Bandit Slayer", EFF_SLAYER, 7, 60, 90, "Better against humanoids." },
            { 38, "Troll Slayer", EFF_SLAYER, 8, 60, 90, "Better against trolls." },
            { 39, "Vrykul Slayer", EFF_SLAYER, 9, 60, 90, "Better against vrykul." },
            { 40, "Nerubian Slayer", EFF_SLAYER, 10, 60, 90, "Better against nerubians." },
            { 41, "Construct Slayer", EFF_SLAYER, 11, 60, 90, "Better against constructs." },
            { 42, "Ogre Slayer", EFF_SLAYER, 12, 60, 90, "Better against ogres." },
            { 43, "Orc Slayer", EFF_SLAYER, 13, 60, 90, "Better against orcs." },
            { 44, "Harpy Slayer", EFF_SLAYER, 14, 60, 90, "Better against harpies." },
            { 45, "Void Slayer", EFF_SLAYER, 15, 60, 90, "Better against aberrations." },
            // professions (50 + profession); profession missions need one of these
            { 51, "Alchemist", EFF_PROFESSION, 1, 60, 90, "Alchemy: can take alchemy missions." },
            { 52, "Blacksmith", EFF_PROFESSION, 2, 60, 90, "Blacksmithing: can take blacksmithing missions." },
            { 53, "Enchanter", EFF_PROFESSION, 3, 60, 90, "Enchanting: can take enchanting missions." },
            { 54, "Engineer", EFF_PROFESSION, 4, 60, 90, "Engineering: can take engineering missions." },
            { 55, "Herbalist", EFF_PROFESSION, 5, 60, 90, "Herbalism: can take herbalism missions." },
            { 56, "Scribe", EFF_PROFESSION, 6, 60, 90, "Inscription: can take inscription missions." },
            { 57, "Jeweler", EFF_PROFESSION, 7, 60, 90, "Jewelcrafting: can take jewelcrafting missions." },
            { 58, "Leatherworker", EFF_PROFESSION, 8, 60, 90, "Leatherworking: can take leatherworking missions." },
            { 59, "Miner", EFF_PROFESSION, 9, 60, 90, "Mining: can take mining missions." },
            { 60, "Skinner", EFF_PROFESSION, 10, 60, 90, "Skinning: can take skinning missions." },
            { 61, "Tailor", EFF_PROFESSION, 11, 60, 90, "Tailoring: can take tailoring missions." },
            { 62, "Cook", EFF_PROFESSION, 12, 60, 90, "Cooking: can take cooking missions." },
            { 63, "Angler", EFF_PROFESSION, 13, 60, 90, "Fishing: can take fishing missions." },
            { 64, "Medic", EFF_PROFESSION, 14, 60, 90, "First Aid: can take first aid missions." },
        };
        return list;
    }

    inline TraitInfo const* GetTrait(uint8_t id)
    {
        for (TraitInfo const& t : Traits())
            if (t.id == id)
                return &t;
        return nullptr;
    }

    // ability and trait slots by quality: Uncommon 1 + 1, Rare 1 + 2, Epic 2 + 3, Legendary 3 + 3
    inline uint8_t AbilitySlots(uint8_t quality) { return quality >= 5 ? 3 : quality == 4 ? 2 : 1; }
    inline uint8_t TraitSlots(uint8_t quality) { return quality >= 4 ? 3 : quality == 3 ? 2 : 1; }

    struct Member
    {
        uint32_t id = 0;
        int32_t level = 1, itemLevel = 0;
        uint8_t race = 0;
        std::vector<uint8_t> abilities, traits;
    };

    struct Setup
    {
        int32_t level = 1, itemLevel = 0, base = 50;
        uint32_t slots = 1, duration = 1800;   // duration in seconds, after scaling and time traits
        uint8_t type = M_BATTLE, environment = 0, enemyRace = 0, requiredTrait = 0;
        std::vector<uint8_t> threats;
        std::vector<uint32_t> weights;         // per threat, same order; missing or 0 = defaultWeight
        uint32_t defaultWeight = 300;
        double lucky = 5.0, maxChance = 200.0;
    };

    struct Explain
    {
        struct FollowerPart { uint32_t id; int32_t level, itemLevel; double bias, points; };
        struct ThreatPart { uint8_t threat; uint32_t count, weight; double removed, points; std::vector<uint32_t> by; };
        struct TraitPart { uint32_t follower; uint8_t trait; double points; };
        std::vector<FollowerPart> followers;
        std::vector<ThreatPart> threats;
        std::vector<TraitPart> traits;
        double totalWeight = 0, points = 0, lucky = 0, chance = 0;
        uint32_t countered = 0;   // threats met by an ability
        bool mentor = false;
        int32_t Chance() const { return int32_t(std::floor(chance + 1e-6)); }   // what is stored and shown (rounded down)
    };

    inline double CalcChance(double a, double b, double bias)
    {
        return bias >= 0 ? a + (b - a) * bias : a * (1.0 + bias);
    }

    inline double Bias(int32_t level, int32_t itemLevel, Setup const& m)
    {
        double b = double(level - m.level) / 3.0;
        if (m.itemLevel > 0)
            b += double(itemLevel - m.itemLevel) / 15.0;
        return std::clamp(b, -1.0, 1.0);
    }

    inline bool HasTraitEffect(Member const& u, uint8_t effect)
    {
        for (uint8_t t : u.traits)
            if (TraitInfo const* ti = GetTrait(t); ti && ti->effect == effect)
                return true;
        return false;
    }

    inline Explain Compute(Setup const& m, std::vector<Member> const& units)
    {
        Explain ex;
        if (units.empty())
        {
            ex.chance = double(std::clamp(m.base, 0, 100));
            return ex;
        }

        // Mentor: everybody at the best level and item level of the group
        int32_t bestLevel = 0, bestItemLevel = 0;
        for (Member const& u : units)
        {
            bestLevel = std::max(bestLevel, u.level);
            bestItemLevel = std::max(bestItemLevel, u.itemLevel);
            if (HasTraitEffect(u, EFF_MENTOR))
                ex.mentor = true;
        }

        // threats summed by kind, in the order they first appear
        std::vector<uint8_t> order;
        std::map<uint8_t, std::pair<uint32_t, uint32_t>> kinds;   // threat -> (count, summed weight)
        double total = double(std::max<uint32_t>(m.slots, 1)) * 100.0;
        for (size_t i = 0; i < m.threats.size(); ++i)
        {
            uint8_t t = m.threats[i];
            uint32_t w = i < m.weights.size() && m.weights[i] ? m.weights[i] : m.defaultWeight;
            if (!kinds.count(t))
                order.push_back(t);
            kinds[t].first += 1;
            kinds[t].second += w;
            total += double(w);
        }
        ex.totalWeight = total;
        double const coeff = 100.0 / total;

        std::vector<double> bias(units.size());
        for (size_t i = 0; i < units.size(); ++i)
        {
            Member const& u = units[i];
            int32_t level = ex.mentor ? bestLevel : u.level;
            int32_t itemLevel = ex.mentor ? bestItemLevel : u.itemLevel;
            bias[i] = Bias(level, itemLevel, m);
            double pts = CalcChance(100.0, 150.0, bias[i]) * coeff;
            ex.followers.push_back({ u.id, level, itemLevel, bias[i], pts });
            ex.points += pts;
        }

        // counters: the best placed followers first, each ability once
        std::vector<size_t> byBias(units.size());
        for (size_t i = 0; i < units.size(); ++i) byBias[i] = i;
        std::stable_sort(byBias.begin(), byBias.end(), [&](size_t a, size_t b) { return bias[a] > bias[b]; });
        std::map<uint8_t, double> remaining;
        std::map<uint8_t, uint32_t> hits;
        for (uint8_t t : order) remaining[t] = double(kinds[t].second);
        std::map<uint8_t, Explain::ThreatPart> parts;
        for (uint8_t t : order) parts[t] = { t, kinds[t].first, kinds[t].second, 0.0, 0.0, {} };
        for (size_t i : byBias)
            for (uint8_t a : units[i].abilities)
            {
                auto r = remaining.find(a);
                if (r == remaining.end() || r->second <= 0.0 || hits[a] >= kinds[a].first)
                    continue;
                double unit = double(kinds[a].second) / double(kinds[a].first);
                double removed = std::min(r->second, CalcChance(unit, unit, bias[i]));
                r->second -= removed;
                ++hits[a];
                parts[a].removed += removed;
                parts[a].points += removed * coeff;
                parts[a].by.push_back(units[i].id);
                ex.points += removed * coeff;
            }
        for (uint8_t t : order)
        {
            ex.countered += hits[t];
            ex.threats.push_back(parts[t]);
        }

        // traits
        for (size_t i = 0; i < units.size(); ++i)
            for (uint8_t tid : units[i].traits)
            {
                TraitInfo const* t = GetTrait(tid);
                if (!t) continue;
                bool applies = false;
                double scale = 1.0;
                switch (t->effect)
                {
                    case EFF_ALONE: applies = units.size() == 1; break;
                    case EFF_ALWAYS: applies = true; break;
                    case EFF_RACE_PAIR:
                        for (size_t j = 0; j < units.size(); ++j)
                            if (j != i && (RaceBit(units[j].race) & t->param))
                                applies = true;
                        break;
                    case EFF_LONG: applies = m.duration >= t->param * 3600; break;
                    case EFF_SHORT: applies = m.duration < t->param * 3600; break;
                    case EFF_ENVIRONMENT:
                        applies = m.environment && m.environment == t->param;
                        if (m.type == M_EXPLORATION) scale = 2.0;   // exploration missions care most about the ground
                        break;
                    case EFF_SLAYER: applies = m.enemyRace && m.enemyRace == t->param; break;
                    case EFF_MISSION_TYPE: applies = m.type <= 31 && ((1u << m.type) & t->param); break;
                    case EFF_PROFESSION: applies = m.requiredTrait == t->id; break;
                    case EFF_LUCKY: ex.lucky += m.lucky; break;
                    default: break;
                }
                if (!applies) continue;
                double pts = CalcChance(t->amount2, t->amount3, bias[i]) * coeff * scale;
                ex.traits.push_back({ units[i].id, t->id, pts });
                ex.points += pts;
            }

        double base = double(std::clamp(m.base, 0, 100));
        ex.chance = base + (100.0 - base) * ex.points / 100.0 + ex.lucky;
        ex.chance = std::clamp(ex.chance, 0.0, m.maxChance);
        return ex;
    }
}

#endif
