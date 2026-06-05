// Copyright (c) eAthena Dev Team - licensed under GNU GPL.
// Fake Player subsystem for eAthena SVN 15267. See fakeplayer.h for design.

#include "../common/cbasetypes.h"
#include "../common/malloc.h"
#include "../common/nullpo.h"
#include "../common/random.h"
#include "../common/showmsg.h"
#include "../common/socket.h"
#include "../common/strlib.h"
#include "../common/timer.h"

#include "battle.h"
#include "chat.h"
#include "clif.h"
#include "itemdb.h"
#include "map.h"
#include "pc.h"
#include "skill.h"
#include "status.h"
#include "unit.h"
#include "vending.h"
#include "fakeplayer.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Registry. Fake players are tracked here so the driver timer can move/fight
// them and so they can be torn down cleanly on shutdown / by script command.
// ---------------------------------------------------------------------------
#define FP_MAX 20000         // hard cap; supports ~800/city across many cities
#define FP_TICK 1000         // driver scan interval (ms); each fake acts on its
                             // own schedule, the scan just checks who is "due"
#define FP_WANDER_RANGE 7    // cells a wanderer may step from its anchor
#define FP_FIGHT_RANGE 9     // search radius for a fight target

// Per-fake action cadence (ms). Each fake gets a randomised gap in this window
// between actions, so the crowd never walks/sits/emotes all on the same beat.
#define FP_ACT_MIN  4000
#define FP_ACT_MAX 14000
#define FP_SPREAD  12000     // initial random delay so spawns desync on startup

// A registry entry. We keep the behaviour flag, the home/anchor cell (so
// wanderers drift around a fixed spot instead of crossing the whole map) and a
// per-fake "next action" timestamp used to stagger movement/idle/talk.
struct fp_node {
	struct map_session_data* sd;
	int flag;
	short ax, ay;            // anchor cell, so wanderers stay near home
	unsigned int next_tick;  // gettick() value before which this fake stays idle
};

static struct fp_node fp_list[FP_MAX];
static int fp_count = 0;
static int fp_timer_id = INVALID_TIMER;

// A small spread of player classes (no baby/wedding/mounted-only sprites here;
// mounted look is applied via OPTION_RIDING on the base 2nd-job below).
static const int FP_CLASSES[] = {
	JOB_SWORDMAN, JOB_MAGE, JOB_ARCHER, JOB_ACOLYTE, JOB_MERCHANT, JOB_THIEF,
	JOB_KNIGHT, JOB_PRIEST, JOB_WIZARD, JOB_BLACKSMITH, JOB_HUNTER, JOB_ASSASSIN,
	JOB_CRUSADER, JOB_MONK, JOB_SAGE, JOB_ROGUE, JOB_ALCHEMIST,
	JOB_LORD_KNIGHT, JOB_HIGH_PRIEST, JOB_HIGH_WIZARD, JOB_WHITESMITH,
	JOB_SNIPER, JOB_ASSASSIN_CROSS, JOB_PALADIN, JOB_CHAMPION, JOB_PROFESSOR,
	JOB_STALKER, JOB_CREATOR,
};
#define FP_NCLASS ((int)(sizeof(FP_CLASSES)/sizeof(FP_CLASSES[0])))

// Only the merchant line may legally run a vending shop in RO.
static const int FP_VEND_CLASSES[] = {
	JOB_MERCHANT, JOB_BLACKSMITH, JOB_ALCHEMIST, JOB_WHITESMITH, JOB_CREATOR,
};
#define FP_NVENDCLASS ((int)(sizeof(FP_VEND_CLASSES)/sizeof(FP_VEND_CLASSES[0])))

static const char* FP_NAMES[] = {
	"Aria","Kael","Mira","Doran","Lune","Vex","Pippa","Garrin","Sable","Toki",
	"Nyx","Rook","Elda","Brann","Yuki","Cato","Wren","Hale","Sora","Mox",
};
#define FP_NNAME ((int)(sizeof(FP_NAMES)/sizeof(FP_NAMES[0])))

// Realistic vending-board titles. These travel in the REAL player vend board
// packet (clif_showvendingboard via vending_openvending), so nearby clients see
// a normal blue vend sign - never a monster name. Mix of S>/B>/WTS/WTB styles.
static const char* FP_VEND_TITLES[] = {
	"WTS Ori & Elu cheap","S> White Potion x99","Ygg Berry / Seed stock",
	"S> OBB / OPB / OCA","Old Card Album here","S> MVP Cards (PM me)",
	"Bloody Branch in stock","S> Slotted gear +refine","Refine mats - fair price",
	"WTS rare cards","Buff pots & supplies","S> Treasure Box","B> Cards & gears",
	"WTB Elunium any amt","S> leftover loot","Pricecheck inside ^^",
};
#define FP_NVEND ((int)(sizeof(FP_VEND_TITLES)/sizeof(FP_VEND_TITLES[0])))

// Overhead chat-room titles (real player-owned chat room, chat_createpcchat).
static const char* FP_CHAT_TITLES[] = {
	"LFP party - any job","Need HP & Bragi","ET 1/12 LFM","MVP hunt later?",
	"WoE guild recruiting","Leveling party here","Newbie friendly party",
	"Looking for tank","Card trade - PM","Selling/buying chat","AFK back soon",
	"Quest party LFM",
};
#define FP_NCHAT ((int)(sizeof(FP_CHAT_TITLES)/sizeof(FP_CHAT_TITLES[0])))

// Short lines a fake player may "say" (overhead chat bubble, clif_message).
// Kept generic so they fit any map/situation and look like idle player banter.
static const char* FP_CHAT_LINES[] = {
	"anyone selling oridecon?","lf party pls","where to level here?",
	"brb afk 5 min","nice drop!","gg","ty for the buff","wts cards pm me",
	"this map is crowded lol","need a priest T_T","wts +7 gear","any mvp up?",
	"how much for elu?","party? party?","almost leveled up","watch my back pls",
	"lol","omw","wait for me","got the loot",
};
#define FP_NCHATLINE ((int)(sizeof(FP_CHAT_LINES)/sizeof(FP_CHAT_LINES[0])))

// Safe emotion ids (see db/const.txt e_* / packet 0xc0). Idle, friendly set.
static const int FP_EMOTES[] = {
	1,  // !
	2,  // ?
	3,  // <music note>
	5,  // heart
	6,  // <sweat>
	7,  // ...
	9,  // money
	16, // ok / thumbs
	17, // <sleep>
	18, // <think>
	20, // :)
	21, // :(
	23, // :D
	29, // kekeke
};
#define FP_NEMOTE ((int)(sizeof(FP_EMOTES)/sizeof(FP_EMOTES[0])))

// Vend catalog: useful/rare items with sane base zeny prices. The price is
// randomised slightly per listing in fakeplayer_openvend.
struct fp_venditem { int nameid; int price; };
static const struct fp_venditem FP_VEND_CATALOG[] = {
	{   985,    30000 }, // Elunium
	{   984,    22000 }, // Oridecon
	{   607,     4500 }, // Yggdrasil Berry
	{   608,     2200 }, // Yggdrasil Seed
	{   504,     1100 }, // White Potion
	{   505,     2800 }, // Blue Potion
	{   603,    55000 }, // Old Blue Box
	{   617,    80000 }, // Old Purple Box
	{   616,   220000 }, // Old Card Album
	{   604,     3500 }, // Dead Branch
	{ 12103,   950000 }, // Bloody Branch
	{  7444,   260000 }, // Treasure Box
	{  4019,    12000 }, // Hornet Card
	{  4035,   450000 }, // Hydra Card
	{  4054,   380000 }, // Angeling Card
	{  4121,  1500000 }, // Phreeoni Card
};
#define FP_NCATALOG ((int)(sizeof(FP_VEND_CATALOG)/sizeof(FP_VEND_CATALOG[0])))

// Cosmetic equipment pools. Only the item ids matter here; the on-body "look"
// (view id) is resolved from the live item_db at spawn, so any id missing from
// this server's item_db is simply skipped instead of drawing a broken sprite.
static const int FP_HEAD_TOP[] = {
	2220,2226,2228,2230,2232,2234,2236,2246,2256,2258,
	5002,5009,5012,5026,5027,5029,5030,5031,5032,5036,
};
#define FP_NHEADTOP ((int)(sizeof(FP_HEAD_TOP)/sizeof(FP_HEAD_TOP[0])))

static const int FP_HEAD_MID[] = { 2202,2203,2205,2243,5006 };
#define FP_NHEADMID ((int)(sizeof(FP_HEAD_MID)/sizeof(FP_HEAD_MID[0])))

static const int FP_HEAD_LOW[] = { 5004,2266,5594,5462,5463 };
#define FP_NHEADLOW ((int)(sizeof(FP_HEAD_LOW)/sizeof(FP_HEAD_LOW[0])))

static const int FP_SHIELDS[] = { 2105,2106,2107,2108,2110 };
#define FP_NSHIELD ((int)(sizeof(FP_SHIELDS)/sizeof(FP_SHIELDS[0])))

static int fp_is_vend_class(int class_)
{
	int i;
	for (i = 0; i < FP_NVENDCLASS; i++)
		if (FP_VEND_CLASSES[i] == class_) return 1;
	return 0;
}

// Resolve an item's on-body sprite "look" (item_db View column). 0 if absent.
static int fp_item_look(int nameid)
{
	struct item_data* id = itemdb_exists(nameid);
	return id ? id->look : 0;
}

static int fp_rand_in(const int* arr, int n)
{
	return arr[rnd() % n];
}

// Pick a sensible weapon nameid for a class (used directly as the weapon view).
static int fp_pick_weapon(int class_)
{
	switch (class_) {
	case JOB_MAGE: case JOB_WIZARD: case JOB_HIGH_WIZARD:
	case JOB_SAGE: case JOB_PROFESSOR: {
		static const int w[] = {1601,1602,1604}; return fp_rand_in(w,3); }
	case JOB_ARCHER: case JOB_HUNTER: case JOB_SNIPER: {
		static const int w[] = {1701,1702};      return fp_rand_in(w,2); }
	case JOB_ACOLYTE: case JOB_PRIEST: case JOB_HIGH_PRIEST:
	case JOB_MONK: case JOB_CHAMPION: {
		static const int w[] = {1504,1505};      return fp_rand_in(w,2); }
	case JOB_THIEF: case JOB_ROGUE: case JOB_STALKER: {
		static const int w[] = {1201,1216,1219}; return fp_rand_in(w,3); }
	case JOB_ASSASSIN: case JOB_ASSASSIN_CROSS: {
		static const int w[] = {1250,1255};      return fp_rand_in(w,2); }
	case JOB_MERCHANT: case JOB_BLACKSMITH: case JOB_WHITESMITH:
	case JOB_ALCHEMIST: case JOB_CREATOR: {
		static const int w[] = {1301,1302,1504}; return fp_rand_in(w,3); }
	case JOB_KNIGHT: case JOB_LORD_KNIGHT: case JOB_CRUSADER:
	case JOB_PALADIN: {
		static const int w[] = {1101,1107,1119,1404,1410}; return fp_rand_in(w,5); }
	default: { // Swordman & anything else: basic swords
		static const int w[] = {1101,1107,1119}; return fp_rand_in(w,3); }
	}
}

static int fp_class_can_shield(int class_)
{
	switch (class_) {
	case JOB_SWORDMAN: case JOB_KNIGHT: case JOB_LORD_KNIGHT:
	case JOB_CRUSADER: case JOB_PALADIN: case JOB_MERCHANT:
	case JOB_BLACKSMITH: case JOB_WHITESMITH: case JOB_ACOLYTE:
	case JOB_PRIEST: case JOB_HIGH_PRIEST:
		return 1;
	default:
		return 0;
	}
}

// Dress a fake player in believable cosmetic gear (weapon, shield, headgears).
static void fp_apply_look(struct map_session_data* sd, int class_)
{
	int look;

	// Weapon: the spawn packet carries the weapon nameid directly as its view.
	sd->vd.weapon = (unsigned short)fp_pick_weapon(class_);

	// Shield only for classes that realistically carry one (~45% of them).
	if (fp_class_can_shield(class_) && (rnd() % 100) < 45)
		sd->vd.shield = (unsigned short)fp_rand_in(FP_SHIELDS, FP_NSHIELD);
	else
		sd->vd.shield = 0;

	// Top headgear: almost everyone wears one.
	if ((rnd() % 100) < 85) {
		look = fp_item_look(fp_rand_in(FP_HEAD_TOP, FP_NHEADTOP));
		sd->status.head_top = (short)look;
		sd->vd.head_top = (unsigned short)look;
	}
	// Middle headgear (glasses etc): ~40%.
	if ((rnd() % 100) < 40) {
		look = fp_item_look(fp_rand_in(FP_HEAD_MID, FP_NHEADMID));
		sd->status.head_mid = (short)look;
		sd->vd.head_mid = (unsigned short)look;
	}
	// Lower headgear: ~30%.
	if ((rnd() % 100) < 30) {
		look = fp_item_look(fp_rand_in(FP_HEAD_LOW, FP_NHEADLOW));
		sd->status.head_bottom = (short)look;
		sd->vd.head_bottom = (unsigned short)look;
	}
}

static struct map_session_data* fp_find(int gid)
{
	int i;
	for (i = 0; i < fp_count; i++)
		if (fp_list[i].sd && fp_list[i].sd->bl.id == gid)
			return fp_list[i].sd;
	return NULL;
}

// ---------------------------------------------------------------------------
// Creation. Mirrors the order pc_authok() brings a real player onto the map:
//   set bl.id/type -> fill status -> unit_dataset -> state.active=1 ->
//   status_set_viewdata -> status_calc_pc -> map_addiddb -> map_addblock ->
//   clif_spawn.  The one deliberate difference is sd->fd = 0 (no socket).
// ---------------------------------------------------------------------------
int fakeplayer_create(const char* name, int class_, int m, int x, int y, int flag)
{
	struct map_session_data* sd;

	if (fp_count >= FP_MAX) {
		ShowError("fakeplayer_create: registry full (%d).\n", FP_MAX);
		return 0;
	}
	if (m < 0 || m >= map_num) {
		ShowError("fakeplayer_create: bad map index %d.\n", m);
		return 0;
	}
	if (!pcdb_checkid(class_)) {  // reject non-player sprite ids up front
		ShowError("fakeplayer_create: '%d' is not a player class id.\n", class_);
		return 0;
	}

	CREATE(sd, struct map_session_data, 1); // calloc => fully zeroed

	sd->fd = 0;        // vacuum session: clif sends to us are dropped
	sd->state.fakeplayer = 1; // never persisted to char-server (see chrif_save)
	sd->bl.id = map_get_new_object_id();
	sd->bl.type = BL_PC;
	sd->bl.m = m;
	sd->bl.x = sd->ud.to_x = x;
	sd->bl.y = sd->ud.to_y = y;

	// ---- character status / appearance -----------------------------------
	sd->status.account_id = sd->bl.id;   // synthetic; never written to char-server
	sd->status.char_id = sd->bl.id;
	sd->status.class_ = (short)class_;
	sd->class_ = (unsigned short)class_;
	safestrncpy(sd->status.name, name, NAME_LENGTH);

	// Give the synthetic char a valid location index. Even though we never
	// save it, this keeps any incidental code path from asking the char-server
	// to resolve map index 0 ("mapindex_id2name: ... non-existant map [0]").
	sd->mapindex = map[m].index;
	sd->status.last_point.map = map[m].index;
	sd->status.last_point.x = (short)x;
	sd->status.last_point.y = (short)y;
	sd->status.save_point = sd->status.last_point;

	sd->status.base_level = (unsigned int)(40 + rnd() % 60); // 40..99
	sd->status.job_level = (unsigned int)(20 + rnd() % 50);
	sd->status.sex = (unsigned char)(rnd() % 2);

	sd->status.hair = (short)(1 + rnd() % 20);
	sd->status.hair_color = (short)(rnd() % 9);
	sd->status.clothes_color = (short)(rnd() % 4);
	sd->status.weapon = 0; // real cosmetic weapon/shield/headgear set below
	sd->status.shield = 0;
	sd->status.head_top = 0;
	sd->status.head_mid = 0;
	sd->status.head_bottom = 0;

	// Mounted look for the knight/paladin family without using the *2 sprite ids.
	if (class_ == JOB_KNIGHT || class_ == JOB_CRUSADER ||
		class_ == JOB_LORD_KNIGHT || class_ == JOB_PALADIN)
		sd->status.option |= OPTION_RIDING;

	// Minimal stat block so status_calc_pc has sane inputs.
	sd->status.str = sd->status.agi = sd->status.vit = 30;
	sd->status.int_ = sd->status.dex = sd->status.luk = 30;

	// ---- bring onto map ----------------------------------------------------
	unit_dataset(&sd->bl);            // zeroes ud, sets ud timers to INVALID_TIMER

									  // CRITICAL: pc-level timer ids must start at INVALID_TIMER (-1), not 0.
									  // CREATE() (calloc) left them 0, which is a *valid* timer slot; code such
									  // as status_calc_pc -> pc_delautobonus would then delete_timer(0,...) and
									  // flood "function mismatch" errors. Mirror pc_authok()'s initialisation.
	{
		int i;
		sd->invincible_timer = INVALID_TIMER;
		sd->npc_timer_id = INVALID_TIMER;
		sd->pvp_timer = INVALID_TIMER;
		sd->rental_timer = INVALID_TIMER;
		for (i = 0; i < MAX_SKILL_LEVEL; i++)             sd->spirit_timer[i] = INVALID_TIMER;
		for (i = 0; i < ARRAYLENGTH(sd->autobonus); i++) sd->autobonus[i].active = INVALID_TIMER;
		for (i = 0; i < ARRAYLENGTH(sd->autobonus2); i++) sd->autobonus2[i].active = INVALID_TIMER;
		for (i = 0; i < ARRAYLENGTH(sd->autobonus3); i++) sd->autobonus3[i].active = INVALID_TIMER;
	}

	sd->state.active = 1;

	status_set_viewdata(&sd->bl, class_);
	status_calc_pc(sd, 1);            // VERIFY: harmless on an empty inventory/skilltree
	sd->sc.option = sd->status.option;

	// Dress it up so nearby clients see a believable, fully-equipped player.
	// Done after status_set_viewdata/status_calc_pc so nothing overwrites vd.
	fp_apply_look(sd, class_);

	// Never die / never look injured.
	sd->battle_status.hp = sd->battle_status.max_hp;
	sd->battle_status.sp = sd->battle_status.max_sp;

	map_addiddb(&sd->bl);
	if (map_addblock(&sd->bl)) {     // non-zero == failure
		aFree(sd);
		return 0;
	}
	clif_spawn(&sd->bl);
	if (sd->status.option)
		clif_changeoption(&sd->bl);   // make riding/cart visible to viewers

	fp_list[fp_count].sd = sd;
	fp_list[fp_count].flag = flag;
	fp_list[fp_count].ax = (short)x;
	fp_list[fp_count].ay = (short)y;
	// Stagger the first action so a freshly populated map doesn't move in unison.
	fp_list[fp_count].next_tick = gettick() + (unsigned int)(rnd() % FP_SPREAD);
	fp_count++;

	return sd->bl.id;
}

// ---------------------------------------------------------------------------
// Real vend shop. Requires Vending skill + active cart + valid cart items,
// then drives the exact same vending_openvending() path a real client uses.
// ---------------------------------------------------------------------------
int fakeplayer_openvend(int gid, const char* title, const int* items, const int* prices, int count)
{
	struct map_session_data* sd = fp_find(gid);
	uint8 buf[8 * MAX_VENDING];
	int i, n = 0;

	if (sd == NULL) return 0;
	if (count < 1) return 0;
	if (count > MAX_VENDING) count = MAX_VENDING;

	// Only the merchant line may run a vend shop (Merchant/Blacksmith/
	// Alchemist/Whitesmith/Creator). Reject anything else so we never show an
	// impossible vendor.
	if (!fp_is_vend_class(sd->class_)) {
		ShowWarning("fakeplayer_openvend: class %d cannot vend; skipping gid %d.\n", sd->class_, gid);
		return 0;
	}

	// Vending preconditions (see vending_openvending): skill + cart.
	sd->status.skill[MC_VENDING].id = MC_VENDING;
	sd->status.skill[MC_VENDING].lv = 10;
	sd->sc.option |= OPTION_CART1;        // pc_iscarton() reads sc.option
	sd->status.option |= OPTION_CART1;
	clif_changeoption(&sd->bl);

	// Fill the cart and build the client-style request payload.
	for (i = 0; i < count; i++) {
		int nameid = items[i];
		int price = prices[i] > 0 ? prices[i] : 100;
		if (!itemdb_exists(nameid)) continue;

		memset(&sd->status.cart[n], 0, sizeof(sd->status.cart[n]));
		sd->status.cart[n].nameid = nameid;
		sd->status.cart[n].amount = 1 + rnd() % 50;
		sd->status.cart[n].identify = 1;
		sd->status.cart[n].attribute = 0;

		// entry = index(2) + amount(2) + value(4); client cart index is pos+2.
		*(uint16*)(buf + 8 * n + 0) = (uint16)(n + 2);
		*(uint16*)(buf + 8 * n + 2) = (uint16)sd->status.cart[n].amount;
		*(uint32*)(buf + 8 * n + 4) = (uint32)price;
		n++;
	}
	if (n == 0) return 0;
	sd->cart_num = n;

	vending_openvending(sd, title, true, buf, n);
	return sd->state.vending ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Real player-owned overhead chat room.
// ---------------------------------------------------------------------------
int fakeplayer_openchat(int gid, const char* title, int limit)
{
	struct map_session_data* sd = fp_find(gid);
	if (sd == NULL) return 0;
	if (limit < 1) limit = 20;
	if (sd->chatID) return 0; // already owns one
	chat_createpcchat(sd, title, "", limit, true);
	return sd->chatID ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Behaviour driver.
//
// A single repeating timer (FP_TICK) scans the registry, but each fake only
// acts when its own randomised "next_tick" is reached. That desynchronisation
// is what makes the crowd look alive: walkers, sitters, emoters and talkers all
// fire on different beats instead of the whole map stepping in unison.
//
// Roles:
//   - vendors      : stand perfectly still (shop must not drift), rare emote.
//   - chat hosts   : stand in their room, talk / emote ("talkers").
//   - fighters     : seek the nearest monster in FP_FIGHT_RANGE and attack;
//                    when nothing is near they fall back to wandering.
//   - wanderers    : stroll near home, sit to rest, emote, or chat.
// ---------------------------------------------------------------------------

// map_foreachinrange callback: remember the first live monster found.
static int fp_pick_target(struct block_list* bl, va_list ap)
{
	int* out = va_arg(ap, int*);
	if (bl->type == BL_MOB && !status_isdead(bl) && *out == 0)
		*out = bl->id;
	return 0;
}

// Make a fake "say" a random idle line as a real overhead chat bubble. The
// "Name : text" format matches normal player public chat (clif_message sends
// it to nearby clients only; the fake itself has no socket).
static void fp_say(struct map_session_data* sd)
{
	char buf[128];
	safesnprintf(buf, sizeof(buf), "%s : %s",
		sd->status.name, FP_CHAT_LINES[rnd() % FP_NCHATLINE]);
	clif_message(&sd->bl, buf);
}

static void fp_emote(struct map_session_data* sd)
{
	clif_emotion(&sd->bl, FP_EMOTES[rnd() % FP_NEMOTE]);
}

// Run one behaviour step for a single fake; returns the delay (ms) until it
// should act again.
static int fp_act(struct fp_node* node)
{
	struct map_session_data* sd = node->sd;
	int flag = node->flag;
	int r;

	// Vendors keep their shop anchored; just look alive once in a while.
	if (sd->state.vending) {
		if (rnd() % 100 < 15) fp_emote(sd);
		return FP_ACT_MIN + (int)(rnd() % (FP_ACT_MAX - FP_ACT_MIN));
	}

	// Chat-room hosts: the "talkers". Stay in the room, banter and emote.
	if (sd->chatID) {
		r = rnd() % 100;
		if (r < 55)      fp_say(sd);
		else if (r < 80) fp_emote(sd);
		return FP_ACT_MIN + (int)(rnd() % (FP_ACT_MAX - FP_ACT_MIN));
	}

	// Still mid-walk or mid-swing: let the action finish, re-check shortly.
	if (sd->ud.walktimer != INVALID_TIMER || sd->ud.attacktimer != INVALID_TIMER)
		return 1500 + (int)(rnd() % 1500);

	// Fighters: engage the nearest monster if one is in range.
	if (flag & FP_FIGHT) {
		int target = 0;
		map_foreachinrange(fp_pick_target, &sd->bl, FP_FIGHT_RANGE, BL_MOB, &target);
		if (target) {
			if (sd->vd.dead_sit) { pc_setstand(sd); clif_standing(&sd->bl); }
			unit_attack(&sd->bl, target, 1);   // 1 == continuous melee
			return FP_ACT_MIN / 2 + (int)(rnd() % FP_ACT_MIN);
		}
		// nothing to hit: drift around like a player looking for mobs
	}

	// Stand up if we had been resting.
	if (sd->vd.dead_sit) {
		pc_setstand(sd);
		clif_standing(&sd->bl);
		return 800 + (int)(rnd() % 1500);
	}

	if (flag & FP_WANDER) {
		r = rnd() % 100;
		if (r < 60) {                          // short stroll near the anchor
			short nx = (short)(node->ax - FP_WANDER_RANGE + (rnd() % (FP_WANDER_RANGE * 2 + 1)));
			short ny = (short)(node->ay - FP_WANDER_RANGE + (rnd() % (FP_WANDER_RANGE * 2 + 1)));
			unit_walktoxy(&sd->bl, nx, ny, 0);
		} else if (r < 78) {                   // sit down for a breather
			pc_setsit(sd);
			clif_sitting(&sd->bl);
		} else if (r < 92) {                   // emote
			fp_emote(sd);
		} else {                               // chat
			fp_say(sd);
		}
	} else {
		// "Still" fakes with no wander flag: occasional emote / chat only.
		if (rnd() % 100 < 50) fp_emote(sd);
		else                  fp_say(sd);
	}

	return FP_ACT_MIN + (int)(rnd() % (FP_ACT_MAX - FP_ACT_MIN));
}

static int fakeplayer_driver(int tid, unsigned int tick, int id, intptr_t data)
{
	int i;
	for (i = 0; i < fp_count; i++) {
		struct fp_node* node = &fp_list[i];
		if (node->sd == NULL) continue;
		if (DIFF_TICK(tick, node->next_tick) < 0) continue; // not its turn yet
		node->next_tick = tick + (unsigned int)fp_act(node);
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Bulk city populate: ~60% plain (wandering) players, ~25% vendors, ~15% chat.
// ---------------------------------------------------------------------------
int fakeplayer_populate_city(int m, int count)
{
	int made = 0, i;
	if (m < 0 || m >= map_num) return 0;

	for (i = 0; i < count; i++) {
		char name[NAME_LENGTH];
		int class_ = FP_CLASSES[rnd() % FP_NCLASS];
		int roll = rnd() % 100;
		int gid, x = 0, y = 0, tries;

		// Find a walkable cell (bounded retries so a crowded/blocked map can't
		// spin forever).
		for (tries = 0; tries < 30; tries++) {
			x = 1 + rnd() % (map[m].xs - 2);
			y = 1 + rnd() % (map[m].ys - 2);
			if (!map_getcell(m, x, y, CELL_CHKNOPASS)) break;
		}
		if (tries == 30) continue; // give up on this one

		snprintf(name, NAME_LENGTH, "%s%d", FP_NAMES[rnd() % FP_NNAME], rnd() % 1000);

		if (roll < 25) {            // vendor: still + cart shop (merchant line only)
			int it[MAX_VENDING], pr[MAX_VENDING], k, num = 2 + rnd() % 5;
			class_ = FP_VEND_CLASSES[rnd() % FP_NVENDCLASS];
			gid = fakeplayer_create(name, class_, m, x, y, FP_STILL);
			if (!gid) continue;
			if (num > MAX_VENDING) num = MAX_VENDING;
			for (k = 0; k < num; k++) {
				const struct fp_venditem* v = &FP_VEND_CATALOG[rnd() % FP_NCATALOG];
				it[k] = v->nameid;
				// jitter the base price by +-25% to look like a live market
				pr[k] = v->price * (75 + (int)(rnd() % 51)) / 100;
				if (pr[k] < 1) pr[k] = 1;
			}
			fakeplayer_openvend(gid, FP_VEND_TITLES[rnd() % FP_NVEND], it, pr, num);
		}
		else if (roll < 40) {     // chat-room owner: still
			gid = fakeplayer_create(name, class_, m, x, y, FP_STILL);
			if (!gid) continue;
			fakeplayer_openchat(gid, FP_CHAT_TITLES[rnd() % FP_NCHAT], 12);
		}
		else {                     // ordinary wanderer
			gid = fakeplayer_create(name, class_, m, x, y, FP_WANDER);
			if (!gid) continue;
		}
		made++;
	}
	ShowInfo("fakeplayer: populated city '%s' with %d fake players.\n", map[m].name, made);
	return made;
}

// ---------------------------------------------------------------------------
// Combat-zone populate (fields & dungeons): the crowd you want to SEE grinding.
//   ~80% wander + fight, ~15% pure wander, ~5% party chat host.
// No vendors here (you don't shop in a dungeon). Every fake is auto-dressed
// with a class-appropriate weapon/shield/headgear by fakeplayer_create, so the
// ones swinging at monsters actually look geared for the fight.
//
// Any class can spawn here, so all jobs end up represented across the world.
// ---------------------------------------------------------------------------
int fakeplayer_populate_field(int m, int count)
{
	int made = 0, i;
	if (m < 0 || m >= map_num) return 0;

	for (i = 0; i < count; i++) {
		char name[NAME_LENGTH];
		int class_ = FP_CLASSES[rnd() % FP_NCLASS];
		int roll = rnd() % 100;
		int gid, x = 0, y = 0, tries;

		for (tries = 0; tries < 30; tries++) {
			x = 1 + rnd() % (map[m].xs - 2);
			y = 1 + rnd() % (map[m].ys - 2);
			if (!map_getcell(m, x, y, CELL_CHKNOPASS)) break;
		}
		if (tries == 30) continue;

		snprintf(name, NAME_LENGTH, "%s%d", FP_NAMES[rnd() % FP_NNAME], rnd() % 1000);

		if (roll < 5) {            // a party host shouting for members
			gid = fakeplayer_create(name, class_, m, x, y, FP_STILL);
			if (!gid) continue;
			fakeplayer_openchat(gid, FP_CHAT_TITLES[rnd() % FP_NCHAT], 12);
		}
		else if (roll < 20) {      // roamer (wander only)
			gid = fakeplayer_create(name, class_, m, x, y, FP_WANDER);
			if (!gid) continue;
		}
		else {                     // the grinders: wander + fight
			gid = fakeplayer_create(name, class_, m, x, y, FP_WANDER | FP_FIGHT);
			if (!gid) continue;
		}
		made++;
	}
	ShowInfo("fakeplayer: populated field/dungeon '%s' with %d fake players.\n", map[m].name, made);
	return made;
}

// ---------------------------------------------------------------------------
// Teardown. Manual (not unit_free_pc) so we never touch char-server / storage.
// ---------------------------------------------------------------------------
static void fp_destroy(struct map_session_data* sd)
{
	if (sd == NULL) return;
	if (sd->state.vending) vending_closevending(sd);
	if (sd->chatID)        chat_leavechat(sd, 0); // owner leaving deletes the room
	unit_stop_walking(&sd->bl, 1);
	unit_stop_attack(&sd->bl);
	clif_clearunit_area(&sd->bl, CLR_OUTSIGHT);
	map_delblock(&sd->bl);
	map_deliddb(&sd->bl);
	aFree(sd);
}

int fakeplayer_count(void)
{
	return fp_count;
}

int fakeplayer_remove(int gid)
{
	int i;
	for (i = 0; i < fp_count; i++) {
		if (fp_list[i].sd && fp_list[i].sd->bl.id == gid) {
			fp_destroy(fp_list[i].sd);
			fp_list[i] = fp_list[--fp_count];
			return 1;
		}
	}
	return 0;
}

int fakeplayer_remove_all(int m)
{
	int i, removed = 0;
	for (i = 0; i < fp_count; ) {
		if (fp_list[i].sd && (m < 0 || fp_list[i].sd->bl.m == m)) {
			fp_destroy(fp_list[i].sd);
			fp_list[i] = fp_list[--fp_count];
			removed++;
		}
		else i++;
	}
	return removed;
}

void do_init_fakeplayer(void)
{
	memset(fp_list, 0, sizeof(fp_list));
	fp_count = 0;
	add_timer_func_list(fakeplayer_driver, "fakeplayer_driver");
	fp_timer_id = add_timer_interval(gettick() + FP_TICK, fakeplayer_driver, 0, 0, FP_TICK);
	ShowStatus("Fake player subsystem ready.\n");
}

void do_final_fakeplayer(void)
{
	fakeplayer_remove_all(-1);
}
