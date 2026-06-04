Fake Activity V4 README
=======================

1) Run: python tools/generate_market_vendors.py
2) Append generated DB files ONCE only:
   type db\fake_player_mob_db2_append.txt >> db\mob_db2.txt
   type db\fake_player_mob_avail_append.txt >> db\mob_avail.txt
3) Add only test_mob_fake_player.txt first to npc/scripts_main.conf.
4) Fully restart map-server. Do not use @reloadscript for DB changes.

Important: mob_avail gives visual player sprites, hair, gear, cart, and peco.
It does not create real player vending or real player-owned chat rooms.
