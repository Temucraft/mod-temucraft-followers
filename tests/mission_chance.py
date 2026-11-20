"""Python port of src/TemucraftMissionChance.h (the garrison style mission success chance).

Kept line for line with the C++ so the numbers can be checked without a server. If you change one, change both and
run tests/test_mission_chance.py.
"""
import math

M_BATTLE, M_PATROL, M_TREASURE, M_RARE, M_EXPLORATION, M_TRAINING, M_LOGISTICS, M_PROFESSION = range(8)

EFF_ALONE, EFF_ALWAYS, EFF_RACE_PAIR, EFF_LONG, EFF_SHORT, EFF_TIME, EFF_MENTOR = 1, 2, 5, 6, 7, 17, 18
EFF_GOLD, EFF_XP, EFF_ENVIRONMENT, EFF_SLAYER, EFF_MISSION_TYPE, EFF_PROFESSION, EFF_LUCKY = 100, 101, 102, 103, 104, 105, 106


def race_bit(race):
    return 1 << (race - 1) if race else 0


# id: (name, effect, param, amount2, amount3)
TRAITS = {
    1: ("Swift Feet", EFF_TIME, 0, 75, 75),
    2: ("Gold Nose", EFF_GOLD, 0, 50, 50),
    3: ("Keen Mind", EFF_XP, 0, 50, 50),
    4: ("Lucky", EFF_LUCKY, 0, 0, 0),
    5: ("Watchful", EFF_MISSION_TYPE, (1 << M_PATROL) | (1 << M_EXPLORATION), 50, 75),
    6: ("Lone Wolf", EFF_ALONE, 0, 80, 120),
    7: ("Mentor", EFF_MENTOR, 0, 0, 0),
    8: ("Long Haul", EFF_LONG, 8, 50, 75),
    9: ("Quick Strike", EFF_SHORT, 4, 50, 75),
    10: ("Veteran", EFF_ALWAYS, 0, 30, 45),
    11: ("Friend of Humans", EFF_RACE_PAIR, race_bit(1), 50, 75),
    12: ("Friend of Orcs", EFF_RACE_PAIR, race_bit(2), 50, 75),
    13: ("Friend of Dwarves", EFF_RACE_PAIR, race_bit(3), 50, 75),
    14: ("Friend of Elves", EFF_RACE_PAIR, race_bit(4) | race_bit(10), 50, 75),
    15: ("Friend of Gnomes", EFF_RACE_PAIR, race_bit(7), 50, 75),
    16: ("Friend of Tauren", EFF_RACE_PAIR, race_bit(6), 50, 75),
    17: ("Friend of Trolls", EFF_RACE_PAIR, race_bit(8), 50, 75),
    18: ("Friend of the Forsaken", EFF_RACE_PAIR, race_bit(5), 50, 75),
    19: ("Friend of Draenei", EFF_RACE_PAIR, race_bit(11), 50, 75),
}
_ENV = ["Woodland Guide", "Mountain Born", "Plains Runner", "Frost Hardened", "Sand Walker", "Tunnel Rat", "Bog Wader",
        "Jungle Stalker", "Street Wise"]
for i, n in enumerate(_ENV, 1):
    TRAITS[20 + i] = (n, EFF_ENVIRONMENT, i, 60, 90)
_SLAYER = ["Undead", "Beast", "Dragon", "Elemental", "Demon", "Giant", "Bandit", "Troll", "Vrykul", "Nerubian",
           "Construct", "Ogre", "Orc", "Harpy", "Void"]
for i, n in enumerate(_SLAYER, 1):
    TRAITS[30 + i] = (n + " Slayer", EFF_SLAYER, i, 60, 90)
_PROF = ["Alchemist", "Blacksmith", "Enchanter", "Engineer", "Herbalist", "Scribe", "Jeweler", "Leatherworker", "Miner",
         "Skinner", "Tailor", "Cook", "Angler", "Medic"]
for i, n in enumerate(_PROF, 1):
    TRAITS[50 + i] = (n, EFF_PROFESSION, i, 60, 90)


def ability_slots(quality):
    return 3 if quality >= 5 else 2 if quality == 4 else 1


def trait_slots(quality):
    return 3 if quality >= 4 else 2 if quality == 3 else 1


def calc_chance(a, b, bias):
    return a + (b - a) * bias if bias >= 0 else a * (1.0 + bias)


def bias_of(level, item_level, m):
    b = (level - m["level"]) / 3.0
    if m.get("item_level", 0) > 0:
        b += (item_level - m["item_level"]) / 15.0
    return max(-1.0, min(1.0, b))


def compute(m, units):
    """m: dict(level, item_level, base, slots, duration, type, environment, enemy_race, required_trait, threats,
    weights, default_weight, lucky, max_chance). units: list of dict(id, level, item_level, race, abilities, traits).
    Returns dict like the C++ Explain."""
    m = dict(dict(level=1, item_level=0, base=50, slots=1, duration=1800, type=M_BATTLE, environment=0, enemy_race=0,
                  required_trait=0, threats=[], weights=[], default_weight=300, lucky=5.0, max_chance=200.0), **m)
    ex = dict(followers=[], threats=[], traits=[], total_weight=0.0, points=0.0, lucky=0.0, chance=0.0, countered=0,
              mentor=False)
    if not units:
        ex["chance"] = float(max(0, min(100, m["base"])))
        return finish(ex)

    best_level = max(u["level"] for u in units)
    best_ilvl = max(u.get("item_level", 0) for u in units)
    ex["mentor"] = any(TRAITS.get(t, (None, None))[1] == EFF_MENTOR for u in units for t in u.get("traits", []))

    order, kinds = [], {}
    total = max(m["slots"], 1) * 100.0
    for i, t in enumerate(m["threats"]):
        w = m["weights"][i] if i < len(m["weights"]) and m["weights"][i] else m["default_weight"]
        if t not in kinds:
            order.append(t)
            kinds[t] = [0, 0]
        kinds[t][0] += 1
        kinds[t][1] += w
        total += w
    ex["total_weight"] = total
    coeff = 100.0 / total

    bias = []
    for u in units:
        level = best_level if ex["mentor"] else u["level"]
        ilvl = best_ilvl if ex["mentor"] else u.get("item_level", 0)
        b = bias_of(level, ilvl, m)
        bias.append(b)
        pts = calc_chance(100.0, 150.0, b) * coeff
        ex["followers"].append(dict(id=u["id"], level=level, item_level=ilvl, bias=b, points=pts))
        ex["points"] += pts

    by_bias = sorted(range(len(units)), key=lambda i: -bias[i])   # stable, like std::stable_sort
    remaining = {t: float(kinds[t][1]) for t in order}
    hits = {t: 0 for t in order}
    parts = {t: dict(threat=t, count=kinds[t][0], weight=kinds[t][1], removed=0.0, points=0.0, by=[]) for t in order}
    for i in by_bias:
        for a in units[i].get("abilities", []):
            if a not in remaining or remaining[a] <= 0.0 or hits[a] >= kinds[a][0]:
                continue
            unit = kinds[a][1] / kinds[a][0]
            removed = min(remaining[a], calc_chance(unit, unit, bias[i]))
            remaining[a] -= removed
            hits[a] += 1
            parts[a]["removed"] += removed
            parts[a]["points"] += removed * coeff
            parts[a]["by"].append(units[i]["id"])
            ex["points"] += removed * coeff
    for t in order:
        ex["countered"] += hits[t]
        ex["threats"].append(parts[t])

    for i, u in enumerate(units):
        for tid in u.get("traits", []):
            if tid not in TRAITS:
                continue
            name, eff, param, a2, a3 = TRAITS[tid]
            applies, scale = False, 1.0
            if eff == EFF_ALONE:
                applies = len(units) == 1
            elif eff == EFF_ALWAYS:
                applies = True
            elif eff == EFF_RACE_PAIR:
                applies = any(j != i and (race_bit(v.get("race", 0)) & param) for j, v in enumerate(units))
            elif eff == EFF_LONG:
                applies = m["duration"] >= param * 3600
            elif eff == EFF_SHORT:
                applies = m["duration"] < param * 3600
            elif eff == EFF_ENVIRONMENT:
                applies = bool(m["environment"]) and m["environment"] == param
                if m["type"] == M_EXPLORATION:
                    scale = 2.0
            elif eff == EFF_SLAYER:
                applies = bool(m["enemy_race"]) and m["enemy_race"] == param
            elif eff == EFF_MISSION_TYPE:
                applies = m["type"] <= 31 and bool((1 << m["type"]) & param)
            elif eff == EFF_PROFESSION:
                applies = m["required_trait"] == tid
            elif eff == EFF_LUCKY:
                ex["lucky"] += m["lucky"]
            if not applies:
                continue
            pts = calc_chance(a2, a3, bias[i]) * coeff * scale
            ex["traits"].append(dict(follower=u["id"], trait=tid, points=pts))
            ex["points"] += pts

    base = float(max(0, min(100, m["base"])))
    ex["chance"] = base + (100.0 - base) * ex["points"] / 100.0 + ex["lucky"]
    ex["chance"] = max(0.0, min(m["max_chance"], ex["chance"]))
    return finish(ex)


def finish(ex):
    ex["shown"] = int(math.floor(ex["chance"] + 1e-6))   # what the server stores and shows
    return ex
