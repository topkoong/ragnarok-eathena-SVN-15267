// Copyright (c) eAthena Dev Team - licensed under GNU GPL.
// Fake Player subsystem for eAthena SVN 15267.
//
// Creates synthetic BL_PC units (real map_session_data objects) that are NOT
// backed by a network connection. Because they are genuine player block-lists,
// nearby real clients render them as players: no HP bar, not mob-targetable,
// real player name, and they can own a real vend shop / real chat room and use
// the normal movement & attack code paths.
//
// Safety model: every fake player uses sd->fd = 0 (eAthena's "vacuum" session,
// see socket.c socket_init). session_isValid/isActive reject fd 0, so any
// clif_* aimed AT the fake player is silently dropped, while area packets
// (spawn/move/vend board/chat box/damage) still reach real viewers on THEIR fd.

#ifndef _FAKEPLAYER_H_
#define _FAKEPLAYER_H_

struct map_session_data;

// Behaviour flags (bitmask) passed to fakeplayer_create / script command.
enum fakeplayer_flag {
	FP_STILL   = 0x0, // stand in place
	FP_WANDER  = 0x1, // periodically walk to a nearby random cell
	FP_FIGHT   = 0x2, // seek and attack the nearest monster in range
};

// Create one fake player. Returns the new GID (bl.id) or 0 on failure.
int  fakeplayer_create(const char* name, int class_, int m, int x, int y, int flag);

// Give a fake player a cart + Vending skill, fill its cart, and open a REAL vend
// shop with the given title. items[] = pairs of {nameid, price}. Returns 1 on ok.
int  fakeplayer_openvend(int gid, const char* title, const int* items, const int* prices, int count);

// Open a REAL player-owned overhead chat room on a fake player. Returns 1 on ok.
int  fakeplayer_openchat(int gid, const char* title, int limit);

// Bulk-populate a CITY map with `count` fake players: mixed classes/looks,
// a fraction as vendors and chat-room owners, the rest wandering. No fighters.
// Returns number created.
int  fakeplayer_populate_city(int m, int count);

// Bulk-populate a FIELD/DUNGEON map with `count` fake players: mostly geared
// wander+fight grinders, a few roamers and a couple of party-chat hosts. No
// vendors. Returns number created.
int  fakeplayer_populate_field(int m, int count);

// Number of live fake players (used to keep them out of autosave math).
int  fakeplayer_count(void);

// Remove a single fake player, or all fake players (optionally on one map).
int  fakeplayer_remove(int gid);
int  fakeplayer_remove_all(int m); // m < 0 == every map

void do_init_fakeplayer(void);
void do_final_fakeplayer(void);

#endif /* _FAKEPLAYER_H_ */
