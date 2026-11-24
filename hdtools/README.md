# HD textures / HD-текстуры

## English

The packs live in `hd/`: `ui.a2hd` (menus, HUD, icons, portraits, fonts) and `world.a2hd` (scenery, characters, effects),
each with data parts `*.a2hd.000`, `.001`, ... The launcher has two switches for them in the Graphics tab, both off by default.
In the game: F9 turns HD on and off, Shift+F9 highlights what is replaced (green) and what is original (red).

How it works: every picture of the game has a name (`hdwork/catalog.txt` lists them after a build, e.g. `grp021_s0_0` =
GRP.GP group 21, slot 0, stream 0; `pack0_0302_0` = PACK0 record 302; `font537_033` = glyph 33 of the dialogue font).
When the game sends a picture to video memory, its rows are recognised and the HD version is drawn in its place, even when
the game sends one picture in several pieces. HD pictures keep the game's palette indices, so palette effects still work.
Apart from the small font glyphs, the packs hold no original game pictures: they only make sense together with your own copy of the game.

Building the packs (needs the built game, Python with numpy and Pillow, and realesrgan-ncnn-vulkan):

    python hdtools/hdbuild.py catalog
    python hdtools/hdbuild.py prep
    python hdtools/hdbuild.py ai        (set A2ESRGAN to the path of realesrgan-ncnn-vulkan.exe)
    python hdtools/hdcheck.py           (removes broken network outputs; run ai again after it)
    python hdtools/hdbuild.py pack

Your own picture for a texture: save it as `hdtools/override/<name>.png` (4x the original size, alpha = transparency),
then run `python hdtools/hdbuild.py pack`.

## Русский

Паки лежат в `hd/`: `ui.a2hd` (меню, HUD, иконки, портреты, шрифты) и `world.a2hd` (локации, персонажи, эффекты),
у каждого есть части данных `*.a2hd.000`, `.001`, ... В лаунчере на вкладке «Графика» две галочки, по умолчанию выключены.
В игре: F9 включает и выключает HD, Shift+F9 подсвечивает заменённое (зелёным) и оригинальное (красным).

Как это работает: у каждой картинки игры есть имя (после сборки они перечислены в `hdwork/catalog.txt`, например
`grp021_s0_0` = группа 21 GRP.GP, слот 0, поток 0; `pack0_0302_0` = запись 302 PACK0; `font537_033` = глиф 33 диалогового
шрифта). Когда игра отправляет картинку в видеопамять, её строки опознаются и на её место рисуется HD-версия, даже если игра
шлёт одну картинку несколькими кусками. HD-картинки хранят индексы палитры игры, поэтому палитровые эффекты работают.
Кроме маленьких глифов шрифта, оригинальных картинок игры в паках нет: они имеют смысл только вместе с вашей копией игры.

Своя картинка для текстуры: сохраните её как `hdtools/override/<имя>.png` (в 4 раза больше оригинала, альфа = прозрачность)
и запустите `python hdtools/hdbuild.py pack`.

## Hero mod / Мод героя

`hd/hero_alexia.bin`, `hd/hero_diaz.bin` and `hd/hero_mephisto.bin` are replacement hero models made by `hdtools/hero/build2.py`: every part of the character's own model is put whole on a bone of Flint's skeleton (nothing is cut or recoloured), and its texture page and palettes are written into free video memory when the hero is loaded (the game's own tables are pointed at them). Flint's sword and shield stay. The files are encrypted like the packs. Run `python hdtools/hero/build2.py alexia diaz mephisto` (needs numpy) to rebuild them from your own game files.
Запуск: `python a2pc.py play --hero alexia|diaz|mephisto`. Файлы `hero_*.bin` — настоящие модели и текстуры персонажей на скелете Флинта; пересобрать из своих файлов игры: `python hdtools/hero/build2.py alexia diaz mephisto` (нужен numpy).
