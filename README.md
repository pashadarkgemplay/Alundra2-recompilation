# Alundra 2 PC Recompilation

[English](#english) | [Русский](#russian)

---

## English

### Alundra 2 for PC (PlayStation US Version)
Native Windows recompilation. The game code is converted directly into PC machine code, so it runs without an emulator, stays light and fast, and supports enhancements the original console could not handle.

Requires your own copy of the game. This repository contains **no game assets**: the executable builds on your PC from your disc image.

### Features
* Widescreen support from 4:3 up to 32:9.
* Smooth motion up to 240 FPS (interpolated from internal 3D data without speeding up the game).
* Enhanced visuals: 2x to 4x geometry detail, clean scaling without edge blur, and texture filtering.
* Optional HD textures and HD interface (two switches in the launcher, off by default; the packs in `hd/` are encrypted and hold no readable game pictures).
* Fun mod: play as Alexia, Baron Diaz or Mephisto instead of Flint (Mods tab).
* Automatic keyboard and gamepad detection.
* Bilingual launcher (English and Russian) with setting explanations.
* Automatic updates and standard saves in a single memory card file (`saves/memcard1.mcr`).

### Installation
1. Download the repository as a ZIP (click **Code**, then **Download ZIP**).
2. Extract it to a folder with at least 3 GB of free space.
3. Run `start.bat` and wait a moment. The launcher will generate `A2Launcher.exe` and open it (run this executable for future launches).
4. Click **Build Game**, select your `.bin` image (US version, SLUS-010.17), and wait 5 to 7 minutes. This step is only done once.
5. Set your preferences and click **Play**.

Requirements: Windows 10 or 11 (64-bit) and an internet connection on first launch. The launcher automatically downloads all missing components (WebView2, Python, compiler, SDL2) and shows progress in the window.

### Controls
| Key | Action |
| :--- | :--- |
| Arrows / WASD | Movement |
| K / Space | Jump / Confirm |
| J | Attack |
| L | Talk / Pick up |
| I | Items / Magic |
| Q / E | Camera rotation |
| Tab | Item Menu |
| Shift | Run |
| Enter / Backspace | Start / Select |
| F11 | Fullscreen |
| F10 | Exit |

*Gamepads work automatically out of the box.*

### Current State
The game is fully playable from start to finish, with zero crashes.

If something does go wrong, the launcher shows the error details and lets you copy them. Post them in Discord along with your save file and a short description of what happened. Fixes roll out via small updates that only recompile the changed files.

### Updates
The launcher checks for updates on launch (can be disabled in the "System" tab). When an update is found, click "Update" to download the patch and recompile only the modified files. This usually takes under a minute and does not require the disc image. Your saves, game assets, and settings remain untouched.

### Troubleshooting
Crash logs are saved to `Desktop/alundra2logs` only when an error happens; please attach this log when reporting bugs. The build process output is shown directly in the launcher and saved to `build.log`.

### Tools Used
* IDA Pro
* Notepad :)
* Python
* DuckStation
* PsyQ SDK
* Claude Sonnet 5.5
* Claude Fable 5.1

> Unofficial fan project. Not affiliated with the original developers or publishers of Alundra 2.

---

## Russian

### Alundra 2 для ПК (PlayStation US)
Нативная рекомпиляция под Windows. Код оригинальной игры переводится прямо в машинный код вашего компьютера, поэтому игра работает без эмулятора, идет легко и быстро, а также поддерживает улучшения, недоступные на оригинальной консоли.

Вам понадобится собственная копия игры. В репозитории **нет игровых файлов**: бинарник собирается на вашем компьютере из вашего образа диска.

### Что сделано
* Поддержка широких экранов от 4:3 до 32:9.
* Плавная картинка до 240 кадров в секунду (кадры строятся на основе 3D данных игры, скорость геймплея остается нормальной).
* Улучшенная графика: детализация 3D увеличена в 2-4 раза, четкое масштабирование без мыла и фильтрация текстур.
* Необязательные HD-текстуры и HD-интерфейс (две галочки в лаунчере, по умолчанию выключены; паки в `hd/` зашифрованы и не содержат читаемых картинок игры).
* Весёлый мод: играть за Алексию, барона Диаса или Мефисто вместо Флинта (вкладка «Моды»).
* Автоматическая поддержка клавиатуры и геймпада.
* Лаунчер на русском и английском с подсказками к каждому пункту.
* Автоматические обновления и сохранения в стандартном файле карты памяти `saves/memcard1.mcr`.

### Установка
1. Скачайте репозиторий в архиве (кнопка **Code**, затем **Download ZIP**).
2. Распакуйте архив в папку, где свободно хотя бы 3 ГБ.
3. Запустите `start.bat` и подождите. Рядом появится файл `A2Launcher.exe`, и лаунчер откроется сам (в дальнейшем запускайте именно этот exe).
4. Нажмите **«Собрать игру»**, выберите ваш файл `.bin` (версия США, SLUS-010.17) и подождите 5-7 минут. Это нужно сделать только один раз.
5. Настройте графику под себя и нажмите **«Играть»**.

Требования: Windows 10 или 11 (64-бит) и интернет при первом запуске. Все недостающие компоненты (WebView2, Python, компилятор, SDL2) лаунчер загрузит сам и покажет прогресс в окне.

### Управление
| Клавиша | Действие |
| :--- | :--- |
| Стрелки / WASD | Движение |
| K / Пробел | Прыжок / Подтверждение |
| J | Атака |
| L | Диалог / Взять предмет |
| I | Предметы / Магия |
| Q / E | Вращение камеры |
| Tab | Меню инвентаря |
| Shift | Бег |
| Enter / Backspace | Start / Select |
| F11 | Полный экран |
| F10 | Выход |

*Геймпад подхватывается автоматически.*

### Текущее состояние
Игра полностью проходима от начала до конца, вылетов нет (0 вылетов).

Если что-то всё же пойдёт не так, лаунчер покажет детали ошибки и позволит их скопировать. Отправьте их в Discord вместе с файлом сохранения и коротко опишите, что произошло. Такие правки выходят быстрыми патчами, при этом у вас пересобирается только обновленная часть кода.

### Обновления
Лаунчер проверяет наличие патчей при запуске (функцию можно отключить во вкладке «Система»). При наличии обновления нажмите «Обновить»: загрузятся новые файлы и пересоберется только измененная часть, обычно это занимает меньше минуты и не требует заново выбирать образ диска. Ваши сохранения, настройки и распакованные ресурсы игры не сбрасываются.

### Если возникли проблемы
Логи вылетов пишутся в папку `Рабочий стол/alundra2logs` только при возникновении критической ошибки. Прикладывайте этот лог при сообщении о баге. Ход компиляции отображается в блоке «Вывод» и дублируется в файл `build.log`.

### Инструменты, которые использовались для работы
* IDA Pro
* Блокнот :)
* Python
* DuckStation
* PsyQ SDK
* Claude Sonnet 5.5
* Claude Fable 5.1

> Неофициальный фанатский проект. Не связан с авторами или издателями Alundra 2.