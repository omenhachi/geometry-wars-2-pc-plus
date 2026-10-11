Geometry Wars: Retro Evolved 2 - PC port
Version @VERSION@

Geometry Wars: Retro Evolved 2 for Xbox Live Arcade, statically recompiled to
run natively on Windows with the ReXGlue SDK.

Source code and issue tracker:
https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled


NO GAME FILES ARE INCLUDED
--------------------------
You need your own copy of the game: the Geometry Wars: Retro Evolved 2 Xbox
Live Arcade package. This port only runs this exact release:

  Title ID:      584108FF
  Game version:  0.0.1.2 (Xbox Live Arcade)

Any other version is refused with "Different version of Geometry Wars: Retro
Evolved 2".


REQUIREMENTS
------------
- Windows 10 or 11, 64-bit
- A DirectX 12 capable graphics card
- The latest Microsoft Visual C++ Redistributable 2015-2022 (x64). Most PCs
  have it. If Windows reports a missing VCRUNTIME140.dll or MSVCP140.dll,
  install it from https://aka.ms/vs/17/release/vc_redist.x64.exe
- Your own Geometry Wars: Retro Evolved 2 Xbox Live Arcade package, plus about
  50 MB of free disk space next to the program for the game files


INSTALL
-------
1. Extract the ZIP anywhere you can write to (not "Program Files").
2. Run geometry_wars_retro_evolved_2.exe. The launcher opens.
3. On the Play page, click "Install from package...".
4. Select your Geometry Wars: Retro Evolved 2 Xbox Live Arcade package. It is
   one file with a long hexadecimal name and no extension. On an Xbox 360 hard
   drive it is in Content\0000000000000000\584108FF\000D0000\
5. Wait while the launcher checks the package and copies the game files into
   the "game" folder next to the program.
6. When the Play page says "Ready to play", press PLAY.

To repair the game files, use "Reinstall from package..." on the Play page.
Your settings and saves are kept.


WINDOWS SMARTSCREEN
-------------------
The first time you run geometry_wars_retro_evolved_2.exe, Windows SmartScreen
may show a warning, because the program is not digitally signed. If you
downloaded the release from the project's GitHub releases page, choose "More
info", then "Run anyway".


PLAYING
-------
- F11 switches between Windowed and Borderless Fullscreen.
- Xbox controllers work without setup.
- The keyboard works too; the Controls page lists the keys.
- Frame rate: the Gameplay page offers 30, 60, 90, 120, 144, 165, 240 and
  Unlimited. 60 is the original game. Game speed stays the same at every rate.
- F2 shows or hides a frame counter in the corner of the window.
- F7 opens the achievements list.
- Scores, leaderboards and achievements are kept on this PC only. There is no
  Xbox Live connection and no online play.
- To open the launcher again after turning it off, hold Shift while starting.


WHERE THINGS ARE
----------------
- Game files:     the "game" folder next to geometry_wars_retro_evolved_2.exe
- Logs:           the "logs" folder next to geometry_wars_retro_evolved_2.exe
- Settings, profile, scores and achievements:
                  Documents\geometry_wars_retro_evolved_2

If the game does not start or crashes, please attach the newest file from the
"logs" folder to a report at
https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues


LICENSE
-------
The original project code of this port is licensed under the GNU General
Public License v2.0 only (GPL-2.0-only). The full text is in LICENSE.

Third-party components keep their own licenses. Their license files and
notices are in the "licenses" folder: ReXGlue-LICENSE.txt and
THIRD-PARTY-LICENSES.txt.

Game files are not included and are not covered by this project's license.
This port is not affiliated with or endorsed by the makers or publishers of
Geometry Wars: Retro Evolved 2.
