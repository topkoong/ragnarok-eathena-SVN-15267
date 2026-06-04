from pathlib import Path
import random
import re

# ============================================================
# eAthena SVN 15266/15267 - Mob-safe Fake Player Generator V4
# ------------------------------------------------------------
# IMPORTANT:
# - This generator does NOT use player JobIDs directly as shop/script NPC sprites.
# - It creates custom mob IDs and maps them to player job sprites through db/mob_avail.txt.
# - It avoids baby/small classes.
# - It forces Head_Top and Head_Middle to non-zero values for player-looking mobs.
# - It uses mounted job sprites for Knight/Crusader/Lord Knight/Paladin.
# - It uses cart options for Merchant-family fake vendors.
#
# Files generated:
#   db/fake_player_mob_db2_append.txt
#   db/fake_player_mob_avail_append.txt
#   npc/custom/fake_activity/fake_player_mob_spawns.txt
#   npc/custom/fake_activity/fake_leveling_mob_scenes.txt
#   npc/custom/fake_activity/fake_hidden_chatroom_overlays.txt
#   npc/custom/fake_activity/fake_activity_mob_safe_conf.txt
#   npc/custom/fake_activity/test_mob_fake_player.txt
#
# Reality check:
#   True native player vending boxes and true player chat rooms require a real BL_PC
#   / map_session_data object in memory. mob_avail.txt can make mobs LOOK like
#   real classes with hair/equipment/cart/peco, but it cannot create genuine player
#   vending or genuine player-owned chat rooms by itself.
# ============================================================

RANDOM_SEED = 15267
random.seed(RANDOM_SEED)

OUT_DIR = Path("npc/custom/fake_activity")
DB2_APPEND = Path("db/fake_player_mob_db2_append.txt")
AVAIL_APPEND = Path("db/fake_player_mob_avail_append.txt")
SPAWNS_FILE = OUT_DIR / "fake_player_mob_spawns.txt"
LEVELING_FILE = OUT_DIR / "fake_leveling_mob_scenes.txt"
CHATROOM_OVERLAY_FILE = OUT_DIR / "fake_hidden_chatroom_overlays.txt"
CONF_FILE = OUT_DIR / "fake_activity_mob_safe_conf.txt"
TEST_FILE = OUT_DIR / "test_mob_fake_player.txt"
README_FILE = OUT_DIR / "README_FAKE_ACTIVITY_V4.txt"

# Tuning
MAIN_CITY_FAKE_PLAYERS = 95
EPISODE_CITY_FAKE_PLAYERS = 45
VENDING_MERCHANTS_PER_MAIN_CITY = 30
VENDING_MERCHANTS_PER_EPISODE_CITY = 12
LEVELING_PLAYERS_PER_MAP = 30
CHAT_ROOMS_PER_MAIN_CITY = 12
CHAT_ROOMS_PER_EPISODE_CITY = 5

# We learned 3001-3999 is reserved for player clones in this eAthena build.
# Use 2000-2999 only, and dynamically skip IDs already used in mob_db/mob_db2.
PREFERRED_MOB_ID_MIN = 2000
PREFERRED_MOB_ID_MAX = 2999

# eAthena status.h options in this build:
# OPTION_CART1=8, CART2=128, CART3=256, CART4=512, CART5=1024
# OPTION_FALCON=16, OPTION_RIDING=32
OPTION_NOTHING = 0
OPTION_CARTS = [8, 128, 256, 512, 1024]
OPTION_FALCON = 16
OPTION_RIDING = 32

# Headgear VIEW IDs. These are intentionally small/common-style view IDs.
# mob_avail says weapon/shield use item IDs; headgear fields follow view IDs in examples.
HEAD_TOP_POOL = [1, 2, 4, 7, 10, 14, 16, 18, 24, 28, 36, 50, 67, 80, 102]
HEAD_MID_POOL = [1, 2, 3, 5, 8, 12, 15, 18, 24, 32, 57, 90, 184]
HEAD_BOTTOM_POOL = [0, 0, 0, 1, 5, 12, 54]

# Item IDs for weapon/shield fields in mob_avail.
WEAPONS = {
    "sword": [1101, 1108, 1116, 1122, 1142, 1162],
    "spear": [1401, 1407, 1413, 1451],
    "dagger": [1201, 1219, 1222, 1231, 1251, 1254],
    "axe": [1301, 1351, 1361],
    "mace": [1501, 1516, 1522],
    "rod": [1601, 1604, 1610],
    "bow": [1701, 1710, 1720, 1723],
    "instrument": [1901, 1905],
    "whip": [1950, 1954],
    "book": [1550, 1560],
    "katar": [1251, 1254, 1261],
    "none": [0],
}
SHIELDS = [0, 2101, 2105, 2113]

MAIN_CITY_ZONES = {
    "prontera": [(118, 150, 210, 220), (130, 115, 180, 150), (180, 150, 220, 205)],
    "morocc": [(125, 70, 190, 130), (120, 130, 185, 165)],
    "payon": [(135, 85, 190, 145), (150, 145, 190, 180)],
    "geffen": [(90, 45, 155, 100), (85, 100, 145, 135)],
    "izlude": [(95, 95, 160, 150), (85, 120, 135, 165)],
    "alberta": [(20, 215, 85, 250), (65, 45, 120, 90)],
    "aldebaran": [(115, 95, 175, 145), (130, 145, 175, 175)],
    "yuno": [(135, 160, 200, 215), (145, 125, 190, 165)],
    "comodo": [(160, 125, 230, 170), (175, 170, 235, 210)],
}

EPISODE_CITY_ZONES = {
    "amatsu": [(175, 70, 225, 110)],
    "louyang": [(185, 85, 235, 125)],
    "ayothaya": [(175, 145, 230, 190)],
    "lighthalzen": [(135, 80, 185, 125)],
    "rachel": [(110, 125, 165, 165)],
    "veins": [(185, 110, 235, 150)],
    "moscovia": [(195, 170, 240, 210)],
    "umbala": [(75, 140, 120, 180)],
    "niflheim": [(180, 155, 225, 195)],
}

LEVELING_MAPS = {
    "pay_dun00": [(70, 100, 190, 190)],
    "prt_fild08": [(80, 80, 270, 270)],
    "gef_fild10": [(80, 80, 260, 260)],
    "moc_fild17": [(80, 80, 250, 250)],
    "gl_chyard": [(80, 80, 250, 250)],
    "orcsdun01": [(35, 35, 190, 190)],
    "ice_dun01": [(60, 40, 230, 180)],
    "thor_v01": [(50, 50, 220, 230)],
    "lhz_dun01": [(50, 50, 250, 250)],
    "abbey01": [(50, 50, 250, 250)],
}

# No baby/small classes. Mounted classes use the mounted sprite IDs.
# Scholar is the common name for Professor in many servers; eAthena constant is Job_Professor 4017.
CLASS_PROFILES = [
    # First jobs / early town variety
    {"code": "SWD", "name": "Swordman", "job": 1, "tier": "1st", "weapon": "sword", "level": (18, 55), "weight": 2},
    {"code": "MAGE", "name": "Mage", "job": 2, "tier": "1st", "weapon": "rod", "level": (18, 55), "weight": 2},
    {"code": "ARC", "name": "Archer", "job": 3, "tier": "1st", "weapon": "bow", "level": (18, 55), "weight": 2},
    {"code": "ACO", "name": "Acolyte", "job": 4, "tier": "1st", "weapon": "mace", "level": (18, 55), "weight": 2},
    {"code": "MER", "name": "Merchant", "job": 5, "tier": "1st", "weapon": "axe", "level": (20, 70), "weight": 8, "merchant": True},
    {"code": "THF", "name": "Thief", "job": 6, "tier": "1st", "weapon": "dagger", "level": (18, 55), "weight": 2},

    # Mounted 2nd jobs
    {"code": "KNT", "name": "Knight", "job": 13, "tier": "2nd", "weapon": "spear", "level": (55, 99), "weight": 6, "mounted": True},
    {"code": "CRU", "name": "Crusader", "job": 21, "tier": "2nd", "weapon": "spear", "level": (55, 99), "weight": 6, "mounted": True},

    # 2nd jobs
    {"code": "PRI", "name": "Priest", "job": 8, "tier": "2nd", "weapon": "mace", "level": (60, 99), "weight": 5},
    {"code": "WIZ", "name": "Wizard", "job": 9, "tier": "2nd", "weapon": "rod", "level": (60, 99), "weight": 4},
    {"code": "BS", "name": "Blacksmith", "job": 10, "tier": "2nd", "weapon": "axe", "level": (55, 99), "weight": 8, "merchant": True},
    {"code": "HNT", "name": "Hunter", "job": 11, "tier": "2nd", "weapon": "bow", "level": (55, 99), "weight": 4, "falcon": True},
    {"code": "ASN", "name": "Assassin", "job": 12, "tier": "2nd", "weapon": "katar", "level": (55, 99), "weight": 4},
    {"code": "MONK", "name": "Monk", "job": 15, "tier": "2nd", "weapon": "mace", "level": (55, 99), "weight": 3},
    {"code": "SAGE", "name": "Sage", "job": 16, "tier": "2nd", "weapon": "book", "level": (60, 99), "weight": 5},
    {"code": "ROG", "name": "Rogue", "job": 17, "tier": "2nd", "weapon": "dagger", "level": (55, 99), "weight": 3},
    {"code": "ALC", "name": "Alchemist", "job": 18, "tier": "2nd", "weapon": "axe", "level": (55, 99), "weight": 8, "merchant": True},
    {"code": "BRD", "name": "Bard", "job": 19, "tier": "2nd", "weapon": "instrument", "level": (55, 99), "weight": 8, "male_only": True},
    {"code": "DNC", "name": "Dancer", "job": 20, "tier": "2nd", "weapon": "whip", "level": (55, 99), "weight": 8, "female_only": True},

    # High/trans classes, required by user
    {"code": "LK", "name": "Lord Knight", "job": 4014, "tier": "trans", "weapon": "spear", "level": (75, 99), "weight": 12, "mounted": True},
    {"code": "HP", "name": "High Priest", "job": 4009, "tier": "trans", "weapon": "mace", "level": (75, 99), "weight": 12},
    {"code": "HW", "name": "High Wizard", "job": 4010, "tier": "trans", "weapon": "rod", "level": (75, 99), "weight": 10},
    {"code": "WS", "name": "Whitesmith", "job": 4011, "tier": "trans", "weapon": "axe", "level": (75, 99), "weight": 12, "merchant": True},
    {"code": "SNP", "name": "Sniper", "job": 4012, "tier": "trans", "weapon": "bow", "level": (75, 99), "weight": 6, "falcon": True},
    {"code": "AX", "name": "Assassin Cross", "job": 4013, "tier": "trans", "weapon": "katar", "level": (75, 99), "weight": 14},
    {"code": "PLD", "name": "Paladin", "job": 4022, "tier": "trans", "weapon": "spear", "level": (75, 99), "weight": 12, "mounted": True},
    {"code": "CHMP", "name": "Champion", "job": 4016, "tier": "trans", "weapon": "mace", "level": (75, 99), "weight": 10},
    {"code": "SCH", "name": "Scholar", "job": 4017, "tier": "trans", "weapon": "book", "level": (75, 99), "weight": 10},
    {"code": "STK", "name": "Stalker", "job": 4018, "tier": "trans", "weapon": "dagger", "level": (75, 99), "weight": 5},
    {"code": "CRE", "name": "Creator", "job": 4019, "tier": "trans", "weapon": "axe", "level": (75, 99), "weight": 12, "merchant": True},
    {"code": "CLW", "name": "Clown", "job": 4020, "tier": "trans", "weapon": "instrument", "level": (75, 99), "weight": 10, "male_only": True},
    {"code": "GYP", "name": "Gypsy", "job": 4021, "tier": "trans", "weapon": "whip", "level": (75, 99), "weight": 10, "female_only": True},
]

VENDOR_TITLES = [
    "S>Cheap Pots", "S>Ori/Elu", "S>MVP Loot", "S>Cards", "S>Slotted", "S>Gemstones",
    "S>Weapon", "S>Armor", "S>Upgrade", "S>Ranked Pots", "S>OC/DC", "S>Supplies",
]

CHAT_ROOM_TITLES = [
    "LFP Thor", "Need HP", "Bio3 Party", "ET 1/12", "MVP party", "Need Bragi", "WoE Guild", "GH party",
    "LF Tank", "Need HW", "ABBEY party", "Ice Dun LFP", "Guild Recruit", "PVP anyone?",
]

CITY_TALKS = [
    "Buff pls", "LFP", "Need party", "Warp pls", "B> loot", "S> stuff", "ET party?", "MVP later?",
    "Need HP", "Bio party?", "Thor?", "brb", "z z z", "OC/DC here", "Guild?",
]
LEVEL_TALKS = [
    "Need heal!", "Mob here", "Pulling", "Good exp", "Almost level", "SP pls", "Attack!", "Rest SP",
    "Bragi up", "Tank here", "Safe wall", "Lex pls", "EDP ready", "Cart boost",
]


def read_text_safe(path: Path) -> str:
    try:
        return path.read_text(errors="ignore")
    except FileNotFoundError:
        return ""


def existing_mob_ids() -> set:
    used = set()
    for p in [Path("db/mob_db.txt"), Path("db/mob_db2.txt")]:
        text = read_text_safe(p)
        for line in text.splitlines():
            line = line.strip()
            if not line or line.startswith("//"):
                continue
            m = re.match(r"^(\d+),", line)
            if m:
                used.add(int(m.group(1)))
    return used


def pick_free_ids(count: int) -> list:
    used = existing_mob_ids()
    ids = []
    for mob_id in range(PREFERRED_MOB_ID_MIN, PREFERRED_MOB_ID_MAX + 1):
        if mob_id in used:
            continue
        # avoid 3001-3999 entirely by range choice, and avoid 3000 too
        ids.append(mob_id)
        if len(ids) >= count:
            return ids
    raise RuntimeError("Not enough free mob IDs in 2000-2999. Clean old fake blocks or expand source safely.")


def weighted_choice(profiles):
    return random.choices(profiles, weights=[p.get("weight", 1) for p in profiles], k=1)[0]


def choose_level(profile):
    lo, hi = profile["level"]
    # Some level 99, many lower-level. Trans classes skew high.
    if random.random() < (0.35 if profile["tier"] == "trans" else 0.18):
        return 99
    return random.randint(lo, hi)


def choose_sex(profile):
    if profile.get("male_only"):
        return 1
    if profile.get("female_only"):
        return 0
    return random.randint(0, 1)  # eAthena examples use 1 male, 0 female


def choose_option(profile):
    option = OPTION_NOTHING
    if profile.get("mounted"):
        option |= OPTION_RIDING
    if profile.get("falcon"):
        option |= OPTION_FALCON
    if profile.get("merchant"):
        option |= random.choice(OPTION_CARTS)
    return option


def choose_appearance(profile):
    sex = choose_sex(profile)
    hair = random.randint(1, 20)
    hair_color = random.randint(0, 8)
    weapon = random.choice(WEAPONS.get(profile.get("weapon", "none"), [0]))
    shield = random.choice(SHIELDS) if profile.get("weapon") in ["sword", "spear", "mace", "rod", "book"] else 0
    head_top = random.choice(HEAD_TOP_POOL)
    head_mid = random.choice(HEAD_MID_POOL)
    head_bottom = random.choice(HEAD_BOTTOM_POOL)
    option = choose_option(profile)
    dye = random.randint(0, 3)
    return sex, hair, hair_color, weapon, shield, head_top, head_mid, head_bottom, option, dye


def make_mob_db_row(mob_id, profile, level):
    # Passive/non-hostile, high HP so it does not die easily if accidentally hit.
    # Mode 0x81 is normal passive-ish visual movement suitable for test; keep EXP/drop zero.
    sprite_name = f"FAKE_{profile['code']}_{mob_id}"
    display_name = profile["name"]
    hp = 500000 if level >= 90 else 200000
    return (
        f"{mob_id},{sprite_name},{display_name},{display_name},{level},{hp},0,0,0,"
        "1,1,1,1,1,1,1,1,1,1,1,10,12,0,7,20,0x81,180,1000,500,500,"
        "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0"
    )


def rand_pos(zones, used, map_name):
    for _ in range(1000):
        x1, y1, x2, y2 = random.choice(zones)
        x = random.randint(x1, x2)
        y = random.randint(y1, y2)
        key = (map_name, x, y)
        if key not in used:
            used.add(key)
            return x, y
    x1, y1, x2, y2 = random.choice(zones)
    return random.randint(x1, x2), random.randint(y1, y2)


def npc_name(title, prefix, idx):
    clean = re.sub(r"[^A-Za-z0-9_>/-]", "", title)[:12] or prefix
    return f"{clean}#{prefix}{idx:04d}"


def generate():
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    # One custom mob per profile plus extra merchant/vendor variants.
    base_profiles = []
    for profile in CLASS_PROFILES:
        # Create 3 variants for popular requested classes, 2 for others.
        variants = 3 if profile.get("weight", 1) >= 8 else 2
        for _ in range(variants):
            base_profiles.append(profile)

    # Extra vendor variants so merchant-family shops look common and diverse.
    merchant_profiles = [p for p in CLASS_PROFILES if p.get("merchant")]
    for _ in range(35):
        base_profiles.append(random.choice(merchant_profiles))

    mob_ids = pick_free_ids(len(base_profiles))

    mob_db_rows = [
        "//===== Fake Player mob_db2 Append V4 ========================",
        "//= Append this block to db/mob_db2.txt ONCE only.",
        "//= IDs are dynamically selected from free range 2000-2999.",
        "// START FAKE PLAYER MOB DB2 V4",
    ]
    avail_rows = [
        "//===== Fake Player mob_avail Append V4 =====================",
        "//= Append this block to db/mob_avail.txt ONCE only.",
        "//= Format: MobID,SpriteID,Sex,Hair,Hair_Color,Weapon,Shield,Head_Top,Head_Middle,Head_Bottom,Option,Dye_Color",
        "// START FAKE PLAYER MOB AVAIL V4",
    ]

    generated = []
    for mob_id, profile in zip(mob_ids, base_profiles):
        level = choose_level(profile)
        sex, hair, hair_color, weapon, shield, head_top, head_mid, head_bottom, option, dye = choose_appearance(profile)
        mob_db_rows.append(make_mob_db_row(mob_id, profile, level))
        avail_rows.append(f"{mob_id},{profile['job']},{sex},{hair},{hair_color},{weapon},{shield},{head_top},{head_mid},{head_bottom},{option},{dye}")
        generated.append({"id": mob_id, "profile": profile, "level": level})

    mob_db_rows.append("// END FAKE PLAYER MOB DB2 V4")
    avail_rows.append("// END FAKE PLAYER MOB AVAIL V4")

    DB2_APPEND.write_text("\n".join(mob_db_rows) + "\n")
    AVAIL_APPEND.write_text("\n".join(avail_rows) + "\n")

    all_mobs = generated
    merchant_mobs = [m for m in all_mobs if m["profile"].get("merchant")]

    used_cells = set()
    spawn_lines = [
        "//===== eAthena Script =======================================",
        "//= Fake Player Mobs V4 - player-looking mobs via mob_avail.txt",
        "//= No baby/small classes. Mounted jobs use Peco sprites/options.",
        "//============================================================",
        "",
    ]

    # City population
    count = 0
    for map_name, zones in MAIN_CITY_ZONES.items():
        spawn_lines.append(f"// Main city fake players - {map_name}")
        for _ in range(MAIN_CITY_FAKE_PLAYERS):
            mob = random.choice(all_mobs)
            prof = mob["profile"]
            x, y = rand_pos(zones, used_cells, map_name)
            name = random.choice(CITY_TALKS) if random.random() < 0.30 else f"{prof['name']} Lv{mob['level']}"
            spawn_lines.append(f"{map_name},{x},{y},0,0\tmonster\t{name}\t{mob['id']},1,0,0,0")
            count += 1
        # Merchant-like vending-looking mobs. They are mobs named like shops and carry carts.
        for _ in range(VENDING_MERCHANTS_PER_MAIN_CITY):
            mob = random.choice(merchant_mobs)
            x, y = rand_pos(zones, used_cells, map_name)
            name = random.choice(VENDOR_TITLES)
            spawn_lines.append(f"{map_name},{x},{y},0,0\tmonster\t{name}\t{mob['id']},1,0,0,0")
            count += 1
        spawn_lines.append("")

    for map_name, zones in EPISODE_CITY_ZONES.items():
        spawn_lines.append(f"// Episode city fake players - {map_name}")
        for _ in range(EPISODE_CITY_FAKE_PLAYERS):
            mob = random.choice(all_mobs)
            prof = mob["profile"]
            x, y = rand_pos(zones, used_cells, map_name)
            name = random.choice(CITY_TALKS) if random.random() < 0.25 else f"{prof['name']} Lv{mob['level']}"
            spawn_lines.append(f"{map_name},{x},{y},0,0\tmonster\t{name}\t{mob['id']},1,0,0,0")
            count += 1
        for _ in range(VENDING_MERCHANTS_PER_EPISODE_CITY):
            mob = random.choice(merchant_mobs)
            x, y = rand_pos(zones, used_cells, map_name)
            name = random.choice(VENDOR_TITLES)
            spawn_lines.append(f"{map_name},{x},{y},0,0\tmonster\t{name}\t{mob['id']},1,0,0,0")
            count += 1
        spawn_lines.append("")

    SPAWNS_FILE.write_text("\n".join(spawn_lines) + "\n")

    # Leveling scenes
    level_lines = [
        "//===== eAthena Script =======================================",
        "//= Fake Leveling Player Mobs V4",
        "//============================================================",
        "",
    ]
    leveling_pool = [m for m in all_mobs if not m["profile"].get("merchant")]
    for map_name, zones in LEVELING_MAPS.items():
        level_lines.append(f"// Fake leveling mobs - {map_name}")
        for _ in range(LEVELING_PLAYERS_PER_MAP):
            mob = random.choice(leveling_pool)
            x, y = rand_pos(zones, used_cells, map_name)
            name = random.choice(LEVEL_TALKS) if random.random() < 0.35 else f"{mob['profile']['name']} Lv{mob['level']}"
            level_lines.append(f"{map_name},{x},{y},0,0\tmonster\t{name}\t{mob['id']},1,0,0,0")
        level_lines.append("")
    LEVELING_FILE.write_text("\n".join(level_lines) + "\n")

    # Hidden chatroom overlay: closest script-only way to show native chat-room boxes above same cells.
    # This is not a true player-owned chat room, but it avoids visible safe NPC sprite overlays.
    chat_lines = [
        "//===== eAthena Script =======================================",
        "//= OPTIONAL hidden waitingroom overlays V4",
        "//= These are invisible NPC controllers that create native overhead chat-room boxes.",
        "//= This is NOT a real player-owned chat room. True player chat requires BL_PC sessions.",
        "//============================================================",
        "",
    ]
    idx = 0
    for map_name, zones in MAIN_CITY_ZONES.items():
        for _ in range(CHAT_ROOMS_PER_MAIN_CITY):
            idx += 1
            x, y = rand_pos(zones, used_cells, map_name)
            title = random.choice(CHAT_ROOM_TITLES)
            n = npc_name(title, "CR", idx)
            chat_lines.append(f"{map_name},{x},{y},4\tscript\t{n}\t-1,{{")
            chat_lines.append("\tend;")
            chat_lines.append("OnInit:")
            chat_lines.append(f"\twaitingroom \"{title}\",20;")
            chat_lines.append("\tend;")
            chat_lines.append("}")
            chat_lines.append("")
    for map_name, zones in EPISODE_CITY_ZONES.items():
        for _ in range(CHAT_ROOMS_PER_EPISODE_CITY):
            idx += 1
            x, y = rand_pos(zones, used_cells, map_name)
            title = random.choice(CHAT_ROOM_TITLES)
            n = npc_name(title, "CR", idx)
            chat_lines.append(f"{map_name},{x},{y},4\tscript\t{n}\t-1,{{")
            chat_lines.append("\tend;")
            chat_lines.append("OnInit:")
            chat_lines.append(f"\twaitingroom \"{title}\",20;")
            chat_lines.append("\tend;")
            chat_lines.append("}")
            chat_lines.append("")
    CHATROOM_OVERLAY_FILE.write_text("\n".join(chat_lines) + "\n")

    # Test file uses the first generated mob, which should be a non-baby visible class.
    test_mob = generated[0]
    TEST_FILE.write_text(
        "// Test one fake player-looking mob first.\n"
        f"prontera,150,150,0,0\tmonster\t{test_mob['profile']['name']} Lv{test_mob['level']}\t{test_mob['id']},1,0,0,0\n"
    )

    CONF_FILE.write_text(
        "// Enable gradually. Restart map-server after DB append.\n"
        "npc: npc/custom/fake_activity/test_mob_fake_player.txt\n"
        "// After the test works, comment the test line and enable these:\n"
        "// npc: npc/custom/fake_activity/fake_player_mob_spawns.txt\n"
        "// npc: npc/custom/fake_activity/fake_leveling_mob_scenes.txt\n"
        "// Optional invisible chat-room boxes. Not true player chat rooms:\n"
        "// npc: npc/custom/fake_activity/fake_hidden_chatroom_overlays.txt\n"
    )

    README_FILE.write_text(
        "Fake Activity V4 README\n"
        "=======================\n\n"
        "1) Run: python tools/generate_market_vendors.py\n"
        "2) Append generated DB files ONCE only:\n"
        "   type db\\fake_player_mob_db2_append.txt >> db\\mob_db2.txt\n"
        "   type db\\fake_player_mob_avail_append.txt >> db\\mob_avail.txt\n"
        "3) Add only test_mob_fake_player.txt first to npc/scripts_main.conf.\n"
        "4) Fully restart map-server. Do not use @reloadscript for DB changes.\n\n"
        "Important: mob_avail gives visual player sprites, hair, gear, cart, and peco.\n"
        "It does not create real player vending or real player-owned chat rooms.\n"
    )

    print("Generated Fake Activity V4")
    print(f"Custom mob IDs: {generated[0]['id']} - {generated[-1]['id']} ({len(generated)} total)")
    print(f"City spawn count: {count}")
    print("Append DB files once, then restart map-server.")


if __name__ == "__main__":
    generate()
