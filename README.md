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
The game boots, "New Game" works, and the intro cutscene plus early areas up to the Ox Tank are playable. This is an early build, so the game cannot be finished yet.

If you reach an uncompiled section, the launcher will display the crash address and let you copy the details. Post this info in Discord along with your save file and a short description of what caused the crash (e.g. after landing a hit). Fixes roll out via small updates that only recompile the changed files.

### Roadmap
* Performance improvements: reduce CPU overhead and offload more rendering tasks to the GPU.
* Playability: trace and fix remaining code so the full game can be completed.
* Polishing: expanded settings, refined gamepad input, better visuals, and mod support.

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

> Unofficial fan project. Not affiliated with the original developers or publishers of Alundra 2. A reverse engineering breakdown will be published later.

---

## Russian

### Alundra 2 для ПК (PlayStation US)
Нативная рекомпиляция под Windows. Код оригинальной игры переводится прямо в машинный код вашего компьютера, поэтому игра работает без эмулятора, идет легко и быстро, а также поддерживает улучшения, недоступные на оригинальной консоли.

Вам понадобится собственная копия игры. В репозитории **нет игровых файлов**: бинарник собирается на вашем компьютере из вашего образа диска.

### Что сделано
* Поддержка широких экранов от 4:3 до 32:9.
* Плавная картинка до 240 кадров в секунду (кадры строятся на основе 3D данных игры, скорость геймплея остается нормальной).
* Улучшенная графика: детализация 3D увеличена в 2-4 раза, четкое масштабирование без мыла и фильтрация текстур.
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
Игра стартует, работает «Новая игра», катсцены и первые локации вплоть до Ox Tank проходятся стабильно. Это ранняя версия, пройти игру до финальных титров пока нельзя.

Если вы дойдете до участка, код которого еще не обработан, лаунчер выдаст окно с адресом и деталями сбоя. Скопируйте эти данные и отправьте в Discord вместе с файлом сохранения, а также опишите, что именно произошло в момент вылета (например, после удара по врагу). Такие правки выходят быстрыми патчами, при этом у вас пересобирается только обновленная часть кода.

### Планы на разработку
* Оптимизация: перенос части расчетов на видеокарту и снижение нагрузки на процессор.
* Проходимость: исправление оставшихся функций для полного прохождения от начала до конца.
* Доработки: расширенные настройки, улучшенный отклик геймпада, поддержка модов.

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

> Неофициальный фанатский проект. Не связан с авторами или издателями Alundra 2. Подробный разбор реверс-инжиниринга игры будет опубликован позже.