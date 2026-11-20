"""Checks of the mission success chance (src/TemucraftMissionChance.h) through its Python port (mission_chance.py).

Run:  python3 tests/test_mission_chance.py
If g++ is installed it also compiles the real C++ header in a temporary folder and checks that it gives the same
numbers as the port for every case.

Where the numbers come from
---------------------------
Wowhead's mission page for the garrison mission "Gronnlings Abound" (mission 2) gives: level 90, 1 follower,
base chance (basebonuschance) 65, one enemy with the threat Wild Aggression, amount 300. Our threat 9 (Berserk Fury)
is the same threat. With Wowhead's calculator rules (MissionCalc.js):
  total weight = 1 x 100 + 300 = 400, coeff = 100 / 400 = 0.25
  level 90 follower, bias 0:     follower 100 x 0.25 = 25, counter 300 x 0.25 = 75 -> points 100
                                 chance = 65 + 35 x 100 / 100 = 100
  same follower, no counter:     points 25 -> 65 + 35 x 0.25 = 73.75
  level 88 (bias (88-90)/3 = -2/3): follower 100 x 1/3 x 0.25 = 8.33, counter 300 x 1/3 x 0.25 = 25
                                 points 33.33 -> 65 + 35 x 0.3333 = 76.67
  level 89 (bias -1/3):          follower 66.67 x 0.25 = 16.67, counter 200 x 0.25 = 50 -> 66.67 -> 88.33
  level 87 (bias -1):            nothing -> 65
  level 91 (bias +1/3):          follower (100 + 50/3) x 0.25 = 29.17, counter 75 -> 104.17 -> 65 + 35 x 1.0417 = 101.46
                                 (101% shown, 1.46% bonus chance)
  level 93 or more (bias +1):    follower 150 x 0.25 = 37.5, counter 75 -> 112.5 -> 104.375
The other cases below are worked out by hand the same way (see each comment).
"""
import os
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mission_chance as mc  # noqa: E402

GRONN = dict(level=90, base=65, slots=1, threats=[9])


def U(i, level, ilvl=0, race=0, abilities=(), traits=()):
    return dict(id=i, level=level, item_level=ilvl, race=race, abilities=list(abilities), traits=list(traits))


CASES = [
    # (name, mission, followers, expected chance, expected shown)
    ("gronnlings: level 90 with counter", GRONN, [U(1, 90, abilities=[9])], 100.0, 100),
    ("gronnlings: level 90 no counter", GRONN, [U(1, 90)], 73.75, 73),
    ("gronnlings: level 88 with counter", GRONN, [U(1, 88, abilities=[9])], 65 + 35 * (1 / 3), 76),
    ("gronnlings: level 89 with counter", GRONN, [U(1, 89, abilities=[9])], 65 + 35 * (2 / 3), 88),
    ("gronnlings: level 87 with counter", GRONN, [U(1, 87, abilities=[9])], 65.0, 65),
    ("gronnlings: level 91 with counter", GRONN, [U(1, 91, abilities=[9])], 65 + 35 * (29.1666666667 + 75) / 100, 101),
    ("gronnlings: level 93 with counter", GRONN, [U(1, 93, abilities=[9])], 104.375, 104),
    ("gronnlings: level 99 no counter (bias capped at +1)", GRONN, [U(1, 99)], 65 + 35 * 0.375, 78),
    ("no followers shows the base chance", GRONN, [], 65.0, 65),

    # item level: mission 100 / 645, follower 100 / 630 -> bias (630-645)/15 = -1 -> nothing; 660 -> +1
    ("item level below: bias -1", dict(level=100, item_level=645, base=40, slots=1, threats=[5]),
     [U(1, 100, 630, abilities=[5])], 40.0, 40),
    ("item level above: bias +1", dict(level=100, item_level=645, base=40, slots=1, threats=[5]),
     [U(1, 100, 660, abilities=[5])], 40 + 60 * (150 * 0.25 + 75) / 100, 107),

    # 3 slots, threats 5,5,9 (300 each): weight 300 + 900 = 1200, coeff 1/12. Three level 10 followers on a level 10
    # mission: 3 x 100/12 = 25. Abilities 5 and 5 remove both Crushing Blows (600/12 = 50), nobody has 9.
    # points 75, base 20 -> 20 + 80 x 0.75 = 80
    ("three followers, two of three threats", dict(level=10, base=20, slots=3, threats=[5, 5, 9]),
     [U(1, 10, abilities=[5]), U(2, 10, abilities=[5]), U(3, 10, abilities=[3])], 80.0, 80),
    # only two of three slots filled: 2 x 100/12 + 600/12 = 66.67 -> 20 + 80 x 0.6667 = 73.33
    ("empty slot counts against you", dict(level=10, base=20, slots=3, threats=[5, 5, 9]),
     [U(1, 10, abilities=[5]), U(2, 10, abilities=[5])], 20 + 80 * (200 / 12 + 50) / 100, 73),
    # a third Crushing Blow counter has nothing left to remove (two threats of that kind, two hits)
    ("a kind of threat is countered at most as many times as it appears", dict(level=10, base=20, slots=3, threats=[5, 5, 9]),
     [U(1, 10, abilities=[5]), U(2, 10, abilities=[5]), U(3, 10, abilities=[5])], 80.0, 80),
    # weights: threat 5 weighs 600 -> total 100 + 600 = 700; counter removes 600 -> (100 + 600)/700 = 100 points
    ("custom threat weight", dict(level=5, base=30, slots=1, threats=[5], weights=[600]),
     [U(1, 5, abilities=[5])], 100.0, 100),
    ("custom threat weight, no counter", dict(level=5, base=30, slots=1, threats=[5], weights=[600]),
     [U(1, 5)], 30 + 70 * (100 / 7) / 100, 40),

    # Mentor (trait 7): levels 7 and 10 on a level 10 mission, 2 slots, no threats: weight 200.
    # without mentor: (66.67 + 100)/2 = 83.33 points; with mentor everyone is level 10: 100 points
    ("without mentor", dict(level=10, base=50, slots=2), [U(1, 9), U(2, 10)], 50 + 50 * (100 * (2 / 3) + 100) / 200, 91),
    ("with mentor", dict(level=10, base=50, slots=2), [U(1, 9), U(2, 10, traits=[7])], 100.0, 100),

    # Lone Wolf (trait 6, 80 / 120): alone on a 1 slot mission with one threat (weight 400): 100/4 + 80/4 = 45
    ("lone wolf alone", dict(level=10, base=40, slots=1, threats=[1]), [U(1, 10, traits=[6])], 40 + 60 * 0.45, 67),
    # not alone: no bonus. 2 slots, weight 200 + 300 = 500: 2 x 100/5 = 40
    ("lone wolf with company", dict(level=10, base=40, slots=2, threats=[1]),
     [U(1, 10, traits=[6]), U(2, 10)], 40 + 60 * 0.40, 64),

    # environment trait (Woodland Guide 21, 60 / 90) in a forest; doubled on an exploration mission
    # weight 400: 25 + 15 = 40 points; exploration 25 + 30 = 55
    ("environment trait", dict(level=10, base=50, slots=1, threats=[2], environment=1),
     [U(1, 10, traits=[21])], 50 + 50 * 0.40, 70),
    ("environment trait on exploration", dict(level=10, base=50, slots=1, threats=[2], environment=1, type=mc.M_EXPLORATION),
     [U(1, 10, traits=[21])], 50 + 50 * 0.55, 77),
    ("environment trait in the wrong place", dict(level=10, base=50, slots=1, threats=[2], environment=2),
     [U(1, 10, traits=[21])], 50 + 50 * 0.25, 62),
    # slayer (Undead Slayer 31) at bias +1 gives amount3 90: level 13 on level 10 -> 150/4 + 90/4 = 60
    ("slayer trait at bias +1", dict(level=10, base=50, slots=1, threats=[2], enemy_race=1),
     [U(1, 13, traits=[31])], 50 + 50 * 0.60, 80),
    # race pairing: Friend of Dwarves (13) with a dwarf (race 3); 2 slots, weight 500: 40 + 50/5 = 50
    ("race pairing", dict(level=10, base=40, slots=2, threats=[1]),
     [U(1, 10, traits=[13]), U(2, 10, race=3)], 40 + 60 * 0.50, 70),
    ("race pairing does not count itself", dict(level=10, base=40, slots=2, threats=[1]),
     [U(1, 10, race=3, traits=[13]), U(2, 10, race=1)], 40 + 60 * 0.40, 64),
    # long and short missions (8 h and 4 h): weight 400 -> 25 + 12.5
    ("long haul on an 8 hour mission", dict(level=10, base=50, slots=1, threats=[2], duration=8 * 3600),
     [U(1, 10, traits=[8])], 50 + 50 * 0.375, 68),
    ("long haul on a 2 hour mission", dict(level=10, base=50, slots=1, threats=[2], duration=2 * 3600),
     [U(1, 10, traits=[8])], 50 + 50 * 0.25, 62),
    ("quick strike on a 2 hour mission", dict(level=10, base=50, slots=1, threats=[2], duration=2 * 3600),
     [U(1, 10, traits=[9])], 50 + 50 * 0.375, 68),
    # Watchful (5) on a patrol; profession trait (Herbalist 55) on a mission that needs it
    ("watchful on patrol", dict(level=10, base=50, slots=1, threats=[2], type=mc.M_PATROL),
     [U(1, 10, traits=[5])], 50 + 50 * 0.375, 68),
    ("profession trait on its mission", dict(level=10, base=50, slots=1, threats=[2], type=mc.M_PROFESSION, required_trait=55),
     [U(1, 10, traits=[55])], 50 + 50 * 0.40, 70),
    # Lucky (4) is a flat +5 at the end
    ("lucky", dict(level=10, base=50, slots=1, threats=[2]), [U(1, 10, traits=[4])], 50 + 50 * 0.25 + 5, 67),
    # cap at 200: base 100 + lots of points
    ("base 100 plus two Lucky goes over 100 (bonus chance 10)", dict(level=1, base=100, slots=1, max_chance=200), [U(1, 50, traits=[4, 4])], 110.0, 110),
    ("capped at 120", dict(level=1, base=90, slots=1, max_chance=120), [U(1, 50, traits=[10, 6])], 120.0, 120),
]


def run_python():
    bad = 0
    for name, m, units, want, shown in CASES:
        got = mc.compute(m, units)
        ok = abs(got["chance"] - want) < 1e-6 and got["shown"] == shown
        print(("ok   " if ok else "FAIL ") + f"{name}: {got['chance']:.4f} (shown {got['shown']}), expected {want:.4f} ({shown})")
        bad += not ok
    return bad


def serialize(m, units):
    full = dict(dict(level=1, item_level=0, base=50, slots=1, duration=1800, type=0, environment=0, enemy_race=0,
                     required_trait=0, threats=[], weights=[], default_weight=300, lucky=5.0, max_chance=200.0), **m)
    head = " ".join(str(full[k]) for k in ("level", "item_level", "base", "slots", "duration", "type", "environment",
                                           "enemy_race", "required_trait", "default_weight", "lucky", "max_chance"))
    us = ";".join(f"{u['id']},{u['level']},{u['item_level']},{u['race']},{':'.join(map(str, u['abilities']))},"
                  f"{':'.join(map(str, u['traits']))}" for u in units)
    return f"{head}|{','.join(map(str, full['threats']))}|{','.join(map(str, full['weights']))}|{us}"


DRIVER = r'''
#include "TemucraftMissionChance.h"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
using namespace TemucraftMission;
static std::vector<std::string> split(std::string const& s, char c)
{
    std::vector<std::string> out; std::string cur; std::istringstream in(s);
    while (std::getline(in, cur, c)) out.push_back(cur);
    if (!s.empty() && s.back() == c) out.push_back("");
    return out;
}
int main()
{
    std::string line;
    while (std::getline(std::cin, line))
    {
        auto parts = split(line, '|');
        std::istringstream h(parts[0]);
        Setup m; int type, env, enemy, req;
        h >> m.level >> m.itemLevel >> m.base >> m.slots >> m.duration >> type >> env >> enemy >> req >> m.defaultWeight >> m.lucky >> m.maxChance;
        m.type = type; m.environment = env; m.enemyRace = enemy; m.requiredTrait = req;
        for (auto& t : split(parts[1], ',')) if (!t.empty()) m.threats.push_back(uint8_t(std::stoi(t)));
        for (auto& w : split(parts[2], ',')) if (!w.empty()) m.weights.push_back(uint32_t(std::stoul(w)));
        std::vector<Member> units;
        if (parts.size() > 3)
            for (auto& us : split(parts[3], ';'))
            {
                if (us.empty()) continue;
                auto f = split(us, ',');
                Member u; u.id = std::stoul(f[0]); u.level = std::stoi(f[1]); u.itemLevel = std::stoi(f[2]); u.race = uint8_t(std::stoi(f[3]));
                for (auto& a : split(f[4], ':')) if (!a.empty()) u.abilities.push_back(uint8_t(std::stoi(a)));
                for (auto& t : split(f[5], ':')) if (!t.empty()) u.traits.push_back(uint8_t(std::stoi(t)));
                units.push_back(u);
            }
        Explain ex = Compute(m, units);
        std::printf("%.9f %d %u\n", ex.chance, ex.Chance(), ex.countered);
    }
}
'''


def run_cpp():
    gxx = shutil.which("g++")
    if not gxx:
        print("g++ not found: C++ cross-check skipped")
        return 0
    src_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src")
    with tempfile.TemporaryDirectory() as tmp:
        drv, exe = os.path.join(tmp, "driver.cpp"), os.path.join(tmp, "driver")
        open(drv, "w").write(DRIVER)
        subprocess.run([gxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", src_dir, drv, "-o", exe], check=True)
        lines = "\n".join(serialize(m, u) for _, m, u, _, _ in CASES) + "\n"
        out = subprocess.run([exe], input=lines, capture_output=True, text=True, check=True).stdout.split("\n")
    bad = 0
    for (name, m, units, want, shown), row in zip(CASES, out):
        chance, cshown, countered = row.split()
        py = mc.compute(m, units)
        ok = abs(float(chance) - py["chance"]) < 1e-6 and int(cshown) == py["shown"] and int(countered) == py["countered"]
        if not ok:
            print(f"FAIL C++ {name}: C++ {chance} ({cshown}), Python {py['chance']:.9f} ({py['shown']})")
        bad += not ok
    print(f"C++ cross-check: {len(CASES) - bad} of {len(CASES)} cases match the Python port")
    return bad


if __name__ == "__main__":
    failures = run_python() + run_cpp()
    print("all good" if not failures else f"{failures} failure(s)")
    sys.exit(1 if failures else 0)
