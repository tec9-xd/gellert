# build / inject

chmod +x build.sh inject.sh
./build.sh
# start cs2, then:
sudo ./inject.sh
# INSERT or F11 opens the menu. Ctrl+C unloads.

# add a hack

1. create src/features/myhack.cpp
2. copy the AimbotFeature skeleton
3. set tab() to "Aimbot" / "ESP" / "Movement" / "Visuals" / "Sounds"
4. implement on_menu / on_draw / on_create_move
5. REGISTER_FEATURE(MyHackFeature);
6. rebuild + reinject

ESP is the same loop as before (controllers 1..64, 72u head, team/hp filters).
Bunnyhop holds space; SDL eats SPACE while airborne so the game sees a fresh jump on landing.
# gellert
