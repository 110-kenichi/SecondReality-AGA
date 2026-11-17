Second Reality - unofficial Amiga AGA port
==========================================

Future Crew's PC demo "Second Reality" (Assembly 1993), ported to the
Amiga 1200 / AGA from the original source code released in 2013.
This is an unofficial fan port; see LICENSE.txt.

Original demo by Future Crew (code: Psi, Trug, Wildfire; graphics:
Marvel, Pixel, Skaven; music: Purple Motion, Skaven). The port is
released under the Unlicense, like the original source.
Source: https://github.com/110-kenichi/SecondReality-AGA

Requirements
------------
- Amiga with AGA chipset (A1200 / A4000), Kickstart 3.1
- 2 MB Chip RAM and Fast RAM (tested with 8 MB)
- CPU 68030 or better recommended (68020 works but heavy parts are slow;
  68060 supported - the build avoids instructions the 060 traps)
- PAL display (50 Hz). NTSC is not supported: music and timing would be
  wrong.

Files
-----
  m2.exe          the demo
  m2.exe.info     Workbench icon
  data/           graphics, music and other data (must stay next to m2.exe)
  LICENSE.txt     licence and credits
  readme.txt      this file

Installing / running
--------------------
Copy m2.exe, m2.exe.info and the data folder into any folder on your
hard disk and start m2.exe from the Shell or by double-clicking the
icon. Data is loaded from PROGDIR:data/.

Controls
--------
  Left mouse button   quit
  Right mouse button  skip to the next part

Known limitations
-----------------
- Speed depends on the CPU. Parts may run with fewer frames per second on
  slower machines; music stays in time.
- Screen is 320x256 PAL; most parts are shown at the original 320x200 in
  the middle of the screen.
- Profiling: create data/prof.cfg (any content) to write per-part timings
  to data/prof.txt when the demo exits.
- Triple buffering: create data/triple.cfg (any content). Parts that take
  a little more than one frame to draw run faster, but the frame interval
  is less even. Double buffering is the default.
- With triple buffering, data/predict.cfg (any content) times each frame
  by the frame it is expected to appear on. Off by default.

------------------------------------------------------------------------
日本語
------------------------------------------------------------------------
Future Crew の PC デモ「Second Reality」(Assembly 1993) を、2013 年に
公開された原作のソースコードから Amiga 1200 / AGA へ移植した非公式版です。

必要な環境: AGA (A1200/A4000)、Kickstart 3.1、Chip RAM 2MB + Fast RAM
(8MB で確認)、68030 以上を推奨 (68060 対応)、PAL 50Hz (NTSC 非対応)。

起動: m2.exe と m2.exe.info と data フォルダを好きなフォルダに置き、
Shell またはアイコンから起動。
左クリックで終了、右クリックで次のパートへ。

トリプルバッファ: data/triple.cfg (中身は何でも可) を置くと有効になります。
描画が 1 フレームを少し超えるパートの fps が上がりますが、表示の間隔は
不揃いになります。既定はダブルバッファです。
トリプルバッファのときに data/predict.cfg も置くと、各フレームが表示される
タイミングを予測して動きを合わせます (既定は OFF)。
