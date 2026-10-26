(function () {
'use strict';

var host = window.chrome && window.chrome.webview;
function send(cmd, arg) { if (host) host.postMessage(arg === undefined ? cmd : cmd + '\t' + arg); }

var $ = function (s, r) { return (r || document).querySelector(s); };
var $$ = function (s, r) { return Array.prototype.slice.call((r || document).querySelectorAll(s)); };

// every text is [en, ru]
var S = {
  'brand.sub': ['recompiled for pc', 'версия для пк'],
  'nav.play': ['Play', 'Играть'], 'nav.gfx': ['Graphics', 'Графика'], 'nav.mods': ['Mods', 'Моды'],
  'nav.keys': ['Controls', 'Управление'], 'nav.sys': ['System', 'Система'],

  'play.ready': ['Ready to play', 'Можно играть'],
  'play.ready.lead': ['Everything is built. Pick up your save from the game’s own menu, or start over. Settings from the other tabs are applied when the game starts.',
                      'Всё собрано. Сохранение выбирается в обычном меню игры, либо можно начать заново. Настройки с других вкладок применяются при запуске.'],
  'play.need': ['First, build the game', 'Сначала соберите игру'],
  'play.need.lead': ['The game is put together on this computer from your own disc image. It takes a while, usually 5–15 minutes, and is done once. Nothing from the disc leaves your PC.',
                     'Игра собирается на этом компьютере из вашего образа диска. Это занимает время, обычно 5–15 минут, и делается один раз. Ничего с диска не покидает ваш ПК.'],
  'play.busy': ['Building…', 'Идёт сборка…'],
  'play.busy.lead': ['This is a one-time step. You can look through the other tabs meanwhile.',
                     'Это разовый шаг. Пока идёт сборка, можно посмотреть другие вкладки.'],
  'play.running': ['The game is running', 'Игра запущена'],
  'play.running.lead': ['The launcher steps aside and comes back when you close the game.',
                        'Лаунчер сворачивается и возвращается, когда вы закроете игру.'],
  'play.failed': ['The build failed', 'Сборка не удалась'],
  'play.failed.lead': ['Open the output below to see where it stopped, then try again.',
                       'Откройте вывод ниже, там видно, где всё остановилось, и попробуйте ещё раз.'],
  'play.go': ['Play', 'Играть'], 'play.new': ['New game', 'Новая игра'],
  'play.newhint': ['After “New game” the opening scene runs for about a minute before you can walk around.',
                   'После «Новой игры» вступительная сцена идёт около минуты, потом можно ходить.'],
  'play.setup': ['Current setup', 'Текущие настройки'],
  'kv.window': ['Window', 'Окно'], 'kv.picture': ['Picture', 'Картинка'], 'kv.mods': ['Mods', 'Моды'], 'kv.sound': ['Sound', 'Звук'],

  'disc.label': ['Disc image', 'Образ диска'],
  'disc.ph': ['Path to your Alundra 2 (USA) image', 'Путь к образу Alundra 2 (USA)'],
  'disc.browse': ['Browse…', 'Выбрать…'], 'disc.change': ['Change…', 'Изменить…'],
  'disc.hint': ['A raw 2352-byte .bin (the usual PS1 dump), a .cue next to it, or a .zip / .7z with one inside. It has to be the US release, SLUS-010.17.',
                'Сырой .bin с секторами по 2352 байта (обычный дамп PS1), .cue рядом с ним или .zip / .7z с таким файлом внутри. Нужна американская версия, SLUS-010.17.'],
  'build.go': ['Build the game', 'Собрать игру'], 'build.back': ['Back', 'Назад'], 'build.stop': ['Stop', 'Остановить'],
  'step.1': ['Get the tools (Python, compiler)', 'Подготовка инструментов (Python, компилятор)'],
  'step.2': ['Unpack the disc image', 'Распаковка образа диска'],
  'step.3': ['Translate the game code to C', 'Перевод кода игры в C'],
  'step.4': ['Compile', 'Компиляция'],
  'step.5': ['Final touches', 'Завершение'],
  'build.tools': ['If this PC has no Python or compiler, they are downloaded first (about 110 MB, one time). Progress and every step show up below, so a quiet moment does not mean it froze.',
                  'Если на ПК нет Python или компилятора, они сначала скачаются (около 110 МБ, один раз). Прогресс и каждый шаг видны ниже, так что пауза не значит, что всё зависло.'],
  'toast.prep': ['Could not get the tools. The reason is in the output below.', 'Не удалось получить инструменты. Причина в блоке \u00abВывод\u00bb ниже.'],
  'sys.py.none': ['not found, it will be downloaded when you build', 'не найден, скачается при сборке'],
  'st.wait': ['waiting', 'ждёт'], 'st.work': ['working', 'идёт'], 'st.done': ['done', 'готово'],
  'clock': ['Elapsed', 'Прошло'],
  'out.title': ['Output', 'Вывод'],
  'toast.nodisc': ['Pick the disc image first.', 'Сначала выберите образ диска.'],
  'toast.built': ['The game is built.', 'Игра собрана.'],
  'toast.nologs': ['No logs yet. A log appears only if the game runs into trouble.', 'Логов пока нет. Файл появляется, только если в игре что-то пошло не так.'],
  'toast.stopped': ['Build stopped.', 'Сборка остановлена.'],
  'toast.nopy': ['Could not start Python. Check the System tab.', 'Не удалось запустить Python. Загляните во вкладку «Система».'],
  'toast.exit': ['The game closed with an error. See the output on the Play tab.', 'Игра завершилась с ошибкой. Подробности во вкладке «Играть», в блоке «Вывод».'],

  'gfx.title': ['Graphics', 'Графика'],
  'gfx.lead': ['How the picture is drawn and shown. The defaults suit most computers.', 'Как рисуется и показывается картинка. Значения по умолчанию подходят большинству компьютеров.'],
  'size.t': ['Window size', 'Размер окна'],
  'size.d': ['The game window in pixels. The picture is 4:3 like on the original console. In a window of another shape the game adds black bars, unless the Widescreen mod is on with the same shape. Not used in full screen.',
             'Размер игрового окна в пикселях. Картинка 4:3, как на оригинальной приставке. В окне другой формы игра добавляет чёрные полосы, если не включён мод «Широкий экран» с тем же форматом. В полноэкранном режиме не используется.'],
  'mode.t': ['Display mode', 'Режим экрана'],
  'mode.d': ['Window is a normal window. Borderless is a frameless window of the size above. Full screen takes the whole screen at the desktop resolution. F11 toggles full screen while you play.',
             'Окно: обычное окно. Без рамки: окно без обрамления выбранного размера. Полный экран: весь экран в разрешении рабочего стола. Во время игры полный экран включается клавишей F11.'],
  'mode.window': ['Window', 'Окно'], 'mode.borderless': ['Borderless', 'Без рамки'], 'mode.fullscreen': ['Full screen', 'Полный экран'],
  'smooth.t': ['Smooth scaling', 'Мягкое масштабирование'],
  'smooth.d': ['Smooths the pixels when the picture is enlarged but keeps edges crisp (sharp-bilinear). It runs on the video card when possible. Off gives hard, blocky pixels.',
               'Сглаживает пиксели при увеличении картинки, но оставляет края чёткими (sharp-bilinear). По возможности работает на видеокарте. Если выключить, пиксели будут резкими и «квадратными».'],
  'texfilter.t': ['Texture filtering', 'Фильтрация текстур'],
  'texfilter.d': ['Blends the texels on 3D surfaces instead of showing the PlayStation’s square blocks. Costs almost nothing. Turn it off for the original look.',
                  'Смешивает текселы на поверхностях в 3D вместо «квадратиков» PlayStation. Почти ничего не стоит. Выключите, чтобы было как в оригинале.'],
  'hires.t': ['Internal 3D resolution', 'Внутреннее разрешение 3D'],
  'hires.d': ['Draws the 3D scenery and characters on a finer grid, 2× to 4× the original, for cleaner edges. The processor does this work, so every step up costs real speed. At 1× the game is lightest, and smooth scaling still keeps it clean on a big screen.',
              'Рисует 3D-сцены и персонажей на более частой сетке, в 2–4 раза подробнее оригинала, края становятся чище. Эту работу делает процессор, поэтому каждая ступень заметно нагружает ПК. На 1× игра самая лёгкая, а мягкое масштабирование всё равно держит картинку чистой на большом экране.'],
  'hires.load': ['CPU load', 'Нагрузка на процессор'],
  'showfps.t': ['FPS counter', 'Счётчик FPS'],
  'showfps.d': ['Shows frames per second in a corner of the window. Handy for checking the frame-rate mod.',
                'Показывает кадры в секунду в углу окна. Удобно, чтобы проверить мод частоты кадров.'],

  'mods.title': ['Mods', 'Моды'],
  'mods.lead': ['Optional changes to how the game looks and runs. They never touch your saves or the disc image. Turn any of them off to get the original behaviour.',
                'Необязательные изменения того, как игра выглядит и работает. Они не трогают сохранения и образ диска. Любой мод можно выключить и получить оригинальное поведение.'],
  'wide.t': ['Widescreen', 'Широкий экран'],
  'wide.d': ['Shows more of the world to the left and right instead of black bars. Menus and backgrounds are stretched to fit, and objects can pop in or out near the edges because the game never expected them to be seen.',
             'Показывает больше мира слева и справа вместо чёрных полос. Меню и фоны растягиваются, а у краёв могут появляться и пропадать объекты, потому что игра не рассчитывала, что их увидят.'],
  'wfmt.t': ['Aspect ratio', 'Формат экрана'],
  'wfmt.d': ['Use the shape of your window or screen. Pick a preset or type any ratio between 1.35 and 8, for example 17:9 or 1.85.',
             'Укажите форму вашего окна или экрана: готовый формат или любое своё соотношение от 1,35 до 8, например 17:9 или 1.85.'],
  'wfmt.ph': ['custom', 'своё'],
  'wfmt.bad': ['That is not a usable ratio. Try something like 16:9.', 'Такое соотношение не подойдёт. Попробуйте, например, 16:9.'],
  'wfmt.ok': ['The window and the format have the same shape.', 'Форма окна и формат совпадают.'],
  'wfmt.mismatch': ['The window is %w but the format is %f, so there will be black bars.%s', 'Окно %w, а формат %f, поэтому будут чёрные полосы.%s'],
  'wfmt.try': [' A matching size: %s.', ' Подходящий размер: %s.'],
  'wfmt.screen': ['Your screen is %w but the format is %f, so there will be black bars. Pick %w to fill the screen.',
                  'У вашего экрана формат %w, а выбран %f, поэтому будут чёрные полосы. Выберите %w, чтобы заполнить экран.'],
  'unlock.t': ['Frame rate unlock', 'Разблокировка частоты кадров'],
  'unlock.d': ['The game itself moves 30 steps a second (60 in menus). This mod adds in-between pictures built from the game’s own 3D data, so motion looks smooth on 60–240 Hz screens. The interface and 2D sprites are never touched, so nothing ghosts. Game speed never changes: if the PC falls behind, the extra pictures are simply skipped.',
             'Сама игра двигается 30 шагов в секунду (в меню 60). Мод добавляет промежуточные картинки, построенные из 3D-данных самой игры, поэтому движение плавное на экранах 60–240 Гц. Интерфейс и 2D-спрайты не трогаются, двоения нет. Скорость игры не меняется: если ПК не успевает, лишние картинки просто пропускаются.'],
  'unlock.off': ['Off', 'Выкл.'],
  'unlock.hint': ['Choose your monitor’s refresh rate, or a little under it.', 'Выберите частоту вашего монитора или чуть меньше.'],
  'unlock.load': ['Extra load', 'Доп. нагрузка'],

  'keys.title': ['Controls', 'Управление'],
  'keys.lead': ['Keyboard layout. A game controller works too and is picked up automatically, with sticks and vibration.',
                'Раскладка клавиатуры. Геймпад тоже работает и подхватывается автоматически, со стиками и вибрацией.'],
  'or': ['or', 'или'],
  'g.char': ['Character', 'Персонаж'], 'g.cam': ['Camera and menus', 'Камера и меню'], 'g.win': ['Window', 'Окно'],
  'k.move': ['Move', 'Движение'],
  'k.cross': ['Cross', 'Крестик'], 'k.cross.n': ['jump, confirm', 'прыжок, подтвердить'],
  'k.square': ['Square', 'Квадрат'], 'k.square.n': ['attack', 'атака'],
  'k.circle': ['Circle', 'Круг'], 'k.circle.n': ['talk, pick up', 'говорить, поднять'],
  'k.tri': ['Triangle', 'Треугольник'], 'k.tri.n': ['items, magic', 'предметы, магия'],
  'k.l1r1': ['L1 / R1', 'L1 / R1'], 'k.l1r1.n': ['camera', 'камера'],
  'k.l2': ['L2', 'L2'], 'k.l2.n': ['item menu', 'меню предметов'],
  'k.r2': ['R2', 'R2'], 'k.r2.n': ['run', 'бег'],
  'k.start': ['Start', 'Start'], 'k.select': ['Select', 'Select'],
  'k.full': ['Full screen', 'На весь экран'], 'k.quit': ['Quit the game', 'Выйти из игры'],

  'sys.title': ['System', 'Система'],
  'sys.lead': ['Disc image, folders and a few other things.', 'Образ диска, папки и несколько других мелочей.'],
  'sys.disc.t': ['Disc image', 'Образ диска'],
  'sys.disc.d': ['Used only to build the game. Once it is built the game reads its data from the game folder, so the image is not needed any more. Pick it again only if the launcher asks for the disc.',
                 'Нужен только для сборки. Когда игра собрана, она читает данные из папки game, и образ больше не требуется. Выбирайте его снова, только если лаунчер попросит диск.'],
  'sys.rebuild': ['Build from disc', 'Собрать с диска'],
  'sys.none': ['not set', 'не выбран'],
  'sound.t': ['Sound', 'Звук'],
  'sound.d': ['Music, effects and voices. Turn it off to play silently.', 'Музыка, эффекты и озвучка. Выключите, чтобы играть без звука.'],
  'sys.dirs.t': ['Folders', 'Папки'],
  'sys.dirs.d': ['Saves live in one memory-card image, saves/memcard1.mcr, the same kind of file as on a PS1. Copy it to keep your progress safe. A log is written to Desktop/alundra2logs only when something goes wrong; attach it when you report a problem.',
                 'Сохранения лежат в одном образе карты памяти, saves/memcard1.mcr, такой же файл, как на PS1. Скопируйте его, чтобы прогресс не пропал. Лог пишется в Рабочий стол/alundra2logs, только если что-то пошло не так; приложите его, когда сообщаете о проблеме.'],
  'sys.saves': ['Saves', 'Сохранения'], 'sys.logs': ['Logs', 'Логи'], 'sys.game': ['Game folder', 'Папка игры'],
  'sys.py.d': ['The interpreter the launcher uses to build and start the game.', 'Интерпретатор, через который лаунчер собирает и запускает игру.'],
  'upd.t': ['Updates', 'Обновления'],
  'upd.d': ['The launcher looks at the project page on GitHub. When there is a newer version it can install it and rebuild only the parts of the game that changed. Your saves, settings and disc data are never touched.',
            'Лаунчер заглядывает на страницу проекта на GitHub. Если там есть новая версия, он может установить её и пересобрать только изменившиеся части игры. Сохранения, настройки и данные с диска не затрагиваются.'],
  'upd.check': ['Check now', 'Проверить'], 'upd.page': ['Project page', 'Страница проекта'],
  'upd.log': ['Update log', 'Журнал обновлений'],
  'autoupdate.t': ['Check for updates at start', 'Проверять обновления при запуске'],
  'autoupdate.d': ['Looks at GitHub when the launcher opens. Nothing is installed without your click.',
                   'Заглядывает на GitHub при запуске лаунчера. Без вашего согласия ничего не устанавливается.'],
  'upd.v': ['version %v', 'версия %v'],
  'upd.st.checking': ['checking…', 'проверяю…'],
  'upd.st.latest': ['you have the latest version', 'у вас последняя версия'],
  'upd.st.avail': ['update available: %v', 'есть обновление: %v'],
  'upd.st.err': ['could not check: %e', 'не удалось проверить: %e'],
  'upd.st.none': ['not checked yet', 'ещё не проверялось'],
  'upd.st.empty': ['nothing is published on GitHub yet', 'на GitHub пока ничего не опубликовано'],
  'upd.noanswer': ['The update did not report back. See the update log in the System tab.', 'Обновление не ответило. Загляните в журнал обновлений во вкладке «Система».'],
  'gb.t': ['Game build', 'Сборка игры'],
  'gb.d': ['Translates and compiles the game again from the files unpacked earlier. No disc image needed. Only changed parts are rebuilt, so it is quick.',
           'Заново переводит и собирает игру из ранее распакованных файлов. Образ диска не нужен. Пересобирается только изменившееся, поэтому быстро.'],
  'gb.go': ['Rebuild game', 'Пересобрать игру'],
  'gb.ok': ['up to date', 'актуальна'], 'gb.none': ['not built yet', 'ещё не собрана'],
  'gb.need': ['needs a rebuild: %w', 'нужна пересборка: %w'],
  'gb.full': ['needs the disc image again', 'снова нужен образ диска'],
  'gb.why.translate': ['the game code translation changed', 'изменился перевод кода игры'],
  'gb.why.compile': ['the game runtime changed', 'изменилась среда выполнения'],
  'bn.upd': ['Update available', 'Доступно обновление'],
  'bn.upd.t': ['Version %v, changed files: %n.', 'Версия %v, изменено файлов: %n.'],
  'bn.upd.t2': ['Changed files: %n.', 'Изменено файлов: %n.'],
  'bn.crash.fix': ['A newer version is out and may fix the problem. ', 'Есть новая версия, она может исправить проблему. '],
  'bn.install': ['Update', 'Обновить'], 'bn.later': ['Later', 'Позже'], 'bn.dismiss': ['Hide', 'Скрыть'],
  'bn.applying': ['Installing the update…', 'Устанавливаю обновление…'],
  'bn.restart': ['Update installed', 'Обновление установлено'],
  'bn.restart.t': ['Restart the launcher to start using the new version.', 'Перезапустите лаунчер, чтобы начать пользоваться новой версией.'],
  'bn.restart.b': ['Restart', 'Перезапустить'],
  'bn.rebuild': ['The game needs a rebuild', 'Игре нужна пересборка'],
  'bn.rebuild.t': ['The update changed the game code. Only what changed is rebuilt, usually in under a minute. Your disc image is not needed.',
                   'Обновление изменило код игры. Пересобирается только изменившееся, обычно меньше минуты. Образ диска не нужен.'],
  'bn.rebuild.b': ['Rebuild', 'Пересобрать'],
  'bn.full': ['The game needs the disc again', 'Игре снова нужен образ диска'],
  'bn.full.t': ['This update changes how the game is unpacked from the disc. Pick your disc image and build once more.',
                'Это обновление меняет распаковку игры с диска. Выберите образ и соберите игру ещё раз.'],
  'bn.full.b': ['Open build', 'Открыть сборку'],
  'bn.err': ['The update did not install', 'Обновление не установилось'],
  'bn.err.b': ['Details', 'Подробности'], 'bn.retry': ['Try again', 'Повторить'],
  'bn.crash': ['The game ran into code it cannot run yet', 'Игра наткнулась на код, который пока не работает'],
  'bn.crash.t': ['Address: %a. Checking whether a fix is out…', 'Адрес: %a. Проверяю, нет ли исправления…'],
  'bn.crash.none': ['Address: %a. No fix is published yet. Please report it on the project page and paste the details.',
                    'Адрес: %a. Исправления пока нет. Сообщите о нём на странице проекта и приложите подробности.'],
  'bn.noaddr': ['not recorded', 'не записан'],
  'bn.copy': ['Copy details', 'Скопировать'],
  'toast.copied': ['Copied.', 'Скопировано.'],
  'toast.latest': ['You have the latest version.', 'У вас последняя версия.'],
  'toast.chkerr': ['Could not check for updates: %e', 'Не удалось проверить обновления: %e'],
  'toast.updated': ['Update installed.', 'Обновление установлено.'],
  'toast.nopyupd': ['Updates need Python, which is installed during the first build.', 'Для обновлений нужен Python, он ставится при первой сборке.'],
  'play.rebuilding': ['Rebuilding…', 'Идёт пересборка…'],
  'play.rebuilding.lead': ['Only the parts that changed are rebuilt. Your disc image is not needed.', 'Пересобирается только изменившееся. Образ диска не нужен.'],
  'sys.about': ['Alundra 2 PC is a static recompilation: the game’s own code is translated to C and built on your computer from your disc. No game data is included. This window runs on Microsoft Edge WebView2; the fonts are Spectral and IBM Plex (SIL Open Font License).',
                'Alundra 2 PC это статическая рекомпиляция. Код самой игры переводится в C и собирается на вашем компьютере из вашего диска, данных игры внутри нет. Окно работает на Microsoft Edge WebView2, шрифты Spectral и IBM Plex (SIL Open Font License).']
};

var SIZES = ['640x480', '960x720', '1024x768', '1280x960', '1600x1200', '1920x1440', '1280x720', '1600x900', '1920x1080', '2560x1440', '3840x2160'];
var WFMTS = ['16:9', '16:10', '21:9', '32:9', '3:2'];
var UNLOCKS = [0, 60, 75, 90, 120, 144, 165, 240];
var LOADS = { 0: 0, 60: 1, 75: 1, 90: 2, 120: 2, 144: 3, 165: 3, 240: 4 };

var state = {
  lang: 'en',
  size: '1280x960', mode: 'window', smooth: true, texfilter: true, hires: 1,
  showfps: false, wide: false, wfmt: '16:9', unlock: 0, sound: true, disc: '', autoupdate: true, dismissed: ''
};
var env = { built: false, python: '', version: '', status: null };
var ui = { tab: 'play', building: false, playing: false, failed: false, showBuild: false, stage: 0, t0: 0, timer: 0, rebuild: false, autoRebuild: false, crashHidden: false };

function t(key) { var v = S[key]; return v ? v[state.lang === 'ru' ? 1 : 0] : key; }
function el(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }

// ---- helpers -----------------------------------------------------------

function parseRatio(s) {
  s = String(s || '').trim().replace(',', '.');
  var m = /^(\d+(?:\.\d+)?)\s*[:x/]\s*(\d+(?:\.\d+)?)$/.exec(s);
  var r = m ? parseFloat(m[1]) / parseFloat(m[2]) : (/^\d+(\.\d+)?$/.test(s) ? parseFloat(s) : NaN);
  return r > 1.34 && r < 8 ? r : NaN;
}
var KNOWN = [[4 / 3, '4:3'], [16 / 9, '16:9'], [16 / 10, '16:10'], [21 / 9, '21:9'], [32 / 9, '32:9'], [3 / 2, '3:2'], [5 / 4, '5:4']];
function shape(r) {
  for (var i = 0; i < KNOWN.length; i++) if (Math.abs(r / KNOWN[i][0] - 1) < 0.02) return KNOWN[i][1];
  return r.toFixed(2).replace(/0$/, '');
}
function sizeRatio(sz) { var p = sz.split('x'); return p[0] / p[1]; }
function sizeLabel(sz) { var p = sz.split('x'); return p[0] + ' × ' + p[1] + '  (' + shape(p[0] / p[1]) + ')'; }

function toast(msg) {
  var n = $('#toast'); n.textContent = msg; n.classList.add('on');
  clearTimeout(toast.h); toast.h = setTimeout(function () { n.classList.remove('on'); }, 3800);
}

// every choice is written to launcher.json right away; only typing is delayed a little
var saveTimer = 0;
function save(now) {
  clearTimeout(saveTimer);
  var go = function () { send('save', JSON.stringify(state)); };
  if (now) go(); else saveTimer = setTimeout(go, 250);
}
function set(k, v) { state[k] = v; save(true); render(); }

// ---- rows --------------------------------------------------------------

var OPTS = {
  mode: [['window', 'mode.window'], ['borderless', 'mode.borderless'], ['fullscreen', 'mode.fullscreen']],
  hires: [[1, '1×'], [2, '2×'], [3, '3×'], [4, '4×']],
  unlock: UNLOCKS.map(function (u) { return [u, u ? String(u) : 'unlock.off']; })
};

function meter(label, n, of) {
  var m = el('div', 'meter'); m.appendChild(el('span', null, label));
  for (var i = 0; i < of; i++) m.appendChild(el('i', i < n ? 'f' : ''));
  return m;
}

function buildRow(row) {
  var k = row.getAttribute('data-k'), type = row.getAttribute('data-c');
  row.textContent = '';
  var left = el('div'); left.appendChild(el('div', 't')); left.appendChild(el('div', 'd'));
  var ctl = el('div', 'ctl');
  row.appendChild(left); row.appendChild(ctl);
  if (type === 'switch') {
    var b = el('button', 'sw'); b.setAttribute('role', 'switch');
    b.addEventListener('click', function () { set(k, !state[k]); });
    ctl.appendChild(b);
  } else if (type === 'seg') {
    var g = el('div', 'seg');
    OPTS[k].forEach(function (o) {
      var bb = el('button'); bb.setAttribute('data-v', o[0]);
      bb.addEventListener('click', function () { set(k, o[0]); });
      g.appendChild(bb);
    });
    ctl.appendChild(g);
  } else if (type === 'select') {
    var s = el('select', 'sel');
    SIZES.forEach(function (z) { var o = el('option', null, sizeLabel(z)); o.value = z; s.appendChild(o); });
    s.addEventListener('change', function () { set(k, s.value); });
    ctl.appendChild(s);
  }
  var extra = el('div', 'extra'); ctl.appendChild(extra);
}

function paintRow(row) {
  var k = row.getAttribute('data-k'), type = row.getAttribute('data-c');
  $('.t', row).textContent = t(k + '.t');
  $('.d', row).textContent = t(k + '.d');
  var v = state[k];
  if (type === 'switch') $('.sw', row).setAttribute('aria-checked', v ? 'true' : 'false');
  if (type === 'select') $('.sel', row).value = v;
  if (type === 'seg') {
    $$('.seg button', row).forEach(function (b, i) {
      var o = OPTS[k][i], label = /^[a-z]+\./.test(String(o[1])) ? t(o[1]) : o[1];
      b.textContent = label;
      b.classList.toggle('on', String(o[0]) === String(v));
    });
  }
  var extra = $('.extra', row); extra.textContent = '';
  if (k === 'hires') extra.appendChild(meter(t('hires.load'), v, 4));
  if (k === 'unlock') {
    extra.appendChild(meter(t('unlock.load'), LOADS[v] || 0, 4));
    extra.appendChild(el('div', 'hint', t('unlock.hint')));
    extra.lastChild.style.textAlign = 'right'; extra.lastChild.style.maxWidth = '230px';
  }
  if (k === 'size') row.classList.toggle('off', state.mode === 'fullscreen');
}

function paintWide() {
  var on = !!state.wide;
  $('#wide-more').style.display = on ? '' : 'none';
  var seg = $('#wfmt-seg');
  if (!seg.children.length) {
    WFMTS.forEach(function (f) {
      var b = el('button', null, f); b.addEventListener('click', function () { set('wfmt', f); });
      seg.appendChild(b);
    });
  }
  $$('button', seg).forEach(function (b) { b.classList.toggle('on', b.textContent === state.wfmt); });
  var inp = $('#wfmt-in');
  if (document.activeElement !== inp) inp.value = WFMTS.indexOf(state.wfmt) >= 0 ? '' : state.wfmt;
  $('#wfmt-in').placeholder = t('wfmt.ph');

  var note = $('#wfmt-note'); note.className = 'note'; note.textContent = '';
  if (!on) return;
  var inp2 = $('#wfmt-in'), raw = document.activeElement === inp2 && inp2.value.trim() ? inp2.value : state.wfmt;
  var f = parseRatio(raw);
  if (isNaN(f)) { note.className = 'note bad'; note.textContent = t('wfmt.bad'); return; }
  var wr, fullscreen = state.mode === 'fullscreen';
  wr = fullscreen ? screen.width / screen.height : sizeRatio(state.size);
  if (Math.abs(wr / f - 1) < 0.03) { note.textContent = t('wfmt.ok'); return; }
  var tip = '';
  if (fullscreen) {
    note.className = 'note bad';
    note.textContent = t('wfmt.screen').replace(/%w/g, shape(wr)).replace('%f', shape(f));
    return;
  } else {
    var match = SIZES.filter(function (z) { return Math.abs(sizeRatio(z) / f - 1) < 0.03; });
    if (match.length) tip = t('wfmt.try').replace('%s', match[Math.min(match.length - 1, 1)].replace('x', '×'));
  }
  note.className = 'note bad';
  note.textContent = t('wfmt.mismatch').replace('%w', shape(wr)).replace('%f', shape(f)).replace('%s', tip);
}

// ---- play page ---------------------------------------------------------

function modeWord() { return t('mode.' + state.mode).toLowerCase(); }
function setupLines() {
  var ru = state.lang === 'ru';
  var win = state.mode === 'fullscreen' ? (ru ? 'на весь экран, разрешение рабочего стола' : 'full screen, desktop resolution')
          : state.size.replace('x', ' × ') + ', ' + (ru ? { window: 'в окне', borderless: 'без рамки' }[state.mode] : modeWord());
  var pic = [ru ? (state.smooth ? 'мягкое масштабирование' : 'чёткие пиксели') : (state.smooth ? 'smooth scaling' : 'sharp pixels'),
             ru ? (state.texfilter ? 'текстуры сглажены' : 'текстуры как в оригинале') : (state.texfilter ? 'textures filtered' : 'original textures'),
             state.hires > 1 ? '3D ×' + state.hires : (ru ? 'родное 3D' : 'native 3D')];
  var mods = [];
  if (state.wide) mods.push((ru ? 'широкий экран ' : 'widescreen ') + state.wfmt);
  if (state.unlock >= 60) mods.push(state.unlock + (ru ? ' кадров/с' : ' fps'));
  return [
    ['window', 'gfx', 'kv.window', win],
    ['picture', 'gfx', 'kv.picture', pic.join(' · ')],
    ['mods', 'mods', 'kv.mods', mods.length ? mods.join(' · ') : (ru ? 'нет' : 'none')],
    ['sound', 'sys', 'kv.sound', state.sound ? (ru ? 'включён' : 'on') : (ru ? 'выключен' : 'off')]
  ];
}

var STEPS = ['step.1', 'step.2', 'step.3', 'step.4', 'step.5'];
function paintSteps() {
  var ol = $('#steps'); ol.textContent = '';
  STEPS.forEach(function (k, i) {
    var cls = i < ui.stage ? 'done' : (i === ui.stage && ui.building ? 'act' : '');
    var li = el('li', cls);
    li.appendChild(el('span', 'n', '0' + (i + 1)));
    li.appendChild(el('span', null, t(k)));
    li.appendChild(el('span', 's', t(i < ui.stage ? 'st.done' : (cls === 'act' ? 'st.work' : 'st.wait'))));
    ol.appendChild(li);
  });
  ol.style.display = ui.building || ui.stage > 0 ? '' : 'none';
}

function paintPlay() {
  var showBuild = !env.built || ui.building || ui.showBuild;
  var key = ui.playing ? 'play.running' : ui.building ? (ui.rebuild ? 'play.rebuilding' : 'play.busy') : ui.failed ? 'play.failed' : env.built ? 'play.ready' : 'play.need';
  $('#play-title').textContent = t(key);
  $('#play-lead').textContent = t(key + '.lead');
  $('#play-ready').style.display = env.built && !ui.building && !ui.showBuild ? '' : 'none';
  $('#play-build').style.display = showBuild ? '' : 'none';
  $('#b-play').disabled = ui.playing; $('#b-new').disabled = ui.playing;
  $('#b-build').hidden = ui.building; $('#b-stop').hidden = !ui.building;
  $('#b-back').hidden = !(env.built && !ui.building);
  $('#disc').disabled = ui.building; $('#b-browse').disabled = ui.building;
  $('#bar').classList.toggle('still', !ui.building);
  $$('#play-build .discpart').forEach(function (n) { n.style.display = ui.building && ui.rebuild ? 'none' : ''; });
  if (!ui.building && !ui.failed) $('#status').textContent = '';
  if (document.activeElement !== $('#disc')) $('#disc').value = state.disc || '';
  paintSteps();

  var kv = $('#setup'); kv.textContent = '';
  setupLines().forEach(function (r) {
    var b = el('button'); b.appendChild(el('span', 'k', t(r[2]))); b.appendChild(el('span', 'v', r[3]));
    b.addEventListener('click', function () { tab(r[1]); });
    kv.appendChild(b);
  });
}

function tickClock() {
  var s = Math.floor((Date.now() - ui.t0) / 1000), m = Math.floor(s / 60);
  $('#clock').textContent = t('clock') + ' ' + (m < 10 ? '0' : '') + m + ':' + (s % 60 < 10 ? '0' : '') + (s % 60);
}

function startBuild() {
  var d = (state.disc || '').trim().replace(/^"|"$/g, '');
  if (!d) { toast(t('toast.nodisc')); return; }
  state.disc = d; save(true);
  ui.failed = false; ui.rebuild = false; ui.stage = 0; $('#log').textContent = '';
  send('build', d);
}

// ---- log ---------------------------------------------------------------

function addLog(line) {
  var log = $('#log');
  var near = log.scrollHeight - log.scrollTop - log.clientHeight < 40;
  var d = el('div', /error|traceback|failed/i.test(line) ? 'e' : '', line);
  log.appendChild(d);
  while (log.childNodes.length > 800) log.removeChild(log.firstChild);
  if (near) log.scrollTop = log.scrollHeight;

  if (ui.playing) collectReport(line);
  if (!ui.building) return;
  var m = /^\[a2pc\] \[(\d)\/5\]/.exec(line);
  if (m) ui.stage = Math.max(ui.stage, parseInt(m[1], 10) - 1);
  else if (/^\[a2pc\] DONE/.test(line)) ui.stage = 5;
  if (/^\[a2pc\]/.test(line)) $('#status').textContent = line.slice(8).trim().slice(0, 120);
  paintSteps();
}

// ---- messages from the launcher ---------------------------------------

function onMessage(raw) {
  var i = raw.indexOf('\t'), type = i < 0 ? raw : raw.slice(0, i), body = i < 0 ? '' : raw.slice(i + 1);
  if (type === 'init') {
    var o = JSON.parse(body), c = null;
    if (typeof o.cfg === 'string' && o.cfg) { try { c = JSON.parse(o.cfg); } catch (e) { c = null; } }
    if (c && typeof c === 'object') for (var k in c) if (k in state && typeof c[k] === typeof state[k]) state[k] = c[k];
    if (state.lang !== 'ru') state.lang = 'en';
    env.built = !!o.built; env.python = o.python || '';
    ui.building = !!o.busy; ui.playing = !!o.playing;
    send('lang', state.lang);
    render();
    startUpdates();
  } else if (type === 'log') addLog(body);
  else if (type === 'disc') { state.disc = body; save(true); render(); }
  else if (type === 'build') {
    if (body === 'start') {
      ui.building = true; ui.failed = false; ui.stage = 0; ui.t0 = Date.now();
      $('#out').open = true; clearInterval(ui.timer); ui.timer = setInterval(tickClock, 1000); tickClock();
    } else {
      var code = parseInt(body.split(':')[1], 10);
      ui.building = false; clearInterval(ui.timer);
      $('#clock').textContent = '';
      util('status');
      env.built = code === 0 ? true : env.built;
      if (code === 0) { ui.stage = 5; ui.showBuild = false; toast(t('toast.built')); setTimeout(function () { ui.stage = 0; render(); }, 1200); }
      else if (code === 1 && ui.stopping) { ui.stopping = false; ui.stage = 0; toast(t('toast.stopped')); }
      else { ui.failed = true; $('#out').open = true; if (code === -1) toast(t('toast.nopy')); else if (code === -3) toast(t('toast.prep')); }
    }
    render();
  } else if (type === 'game') {
    if (body === 'start') { ui.playing = true; $('#log').textContent = ''; reports = []; ui.crashHidden = false; }
    else {
      ui.playing = false;
      var c = parseInt(body.split(':')[1], 10);
      if (c === -1) toast(t('toast.nopy'));
      else if (c > 0) { toast(t('toast.exit')); $('#out').open = true; }
      if (c !== -1 && c !== -2 && (reports.length || c > 0)) util('check-force');
    }
    render();
  } else if (type === 'nologs') toast(t('toast.nologs'));
  else if (type === 'result') { var d = null; try { d = JSON.parse(body); } catch (e) { d = null; } onResult(d); }
  else if (type === 'ulog') addUpdLog(body);
  else if (type === 'util') onUtil(body);
}
if (host) host.addEventListener('message', function (e) { onMessage(String(e.data)); });

// ---- updates -----------------------------------------------------------

// helper scripts (update check, status) run one at a time; the launcher answers each with a result line and a done message
var uq = { busy: false, q: [], cur: '', wd: 0 };
var upd = { phase: 'idle', info: null, error: '', manual: false, checking: false, launcherTried: false, timer: 0 };
var reports = [];

function short(e) { e = String(e || ''); return e.length > 110 ? e.slice(0, 107) + '...' : e; }

function util(name) {
  if (uq.cur === name || uq.q.indexOf(name) >= 0) return;
  uq.q.push(name); pumpUtil();
}
function pumpUtil() {
  if (uq.busy || !uq.q.length) return;
  uq.cur = uq.q.shift(); uq.busy = true;
  if (/^check/.test(uq.cur)) upd.checking = true;
  clearTimeout(uq.wd); uq.wd = setTimeout(utilDone, 15 * 60 * 1000);
  send('update', uq.cur);
  render();
}
function utilDone() {
  clearTimeout(uq.wd);
  var was = uq.cur; uq.busy = false; uq.cur = '';
  if (/^check/.test(was)) upd.checking = false;
  if (was === 'apply' && upd.phase === 'applying') { upd.phase = 'error'; upd.error = upd.error || t('upd.noanswer'); ui.autoRebuild = false; }
  pumpUtil(); render();
}
function onUtil(body) {
  if (body === 'busy') { if (upd.manual) { upd.manual = false; } utilDone(); return; }
  if (body === 'restart-failed') { upd.phase = 'error'; upd.error = t('upd.noanswer'); render(); return; }
  var code = parseInt(body.split(':')[1], 10);
  if (code === -1) {
    upd.error = t('toast.nopyupd');
    if (upd.manual) { upd.manual = false; toast(upd.error); }
  }
  utilDone();
}

function addUpdLog(line) {
  var log = $('#ulog'); log.appendChild(el('div', /error|failed/i.test(line) ? 'e' : '', line));
  while (log.childNodes.length > 200) log.removeChild(log.firstChild);
  log.scrollTop = log.scrollHeight;
}

function onResult(d) {
  if (!d || !d.cmd) return;
  if (d.cmd === 'status') {
    env.status = d; if (d.version) env.version = d.version;
    if (d.launcher_stale && !upd.launcherTried) { upd.launcherTried = true; util('launcher'); }
    if (ui.autoRebuild && (d.need === 'compile' || d.need === 'translate') && !ui.building && !ui.playing) { ui.autoRebuild = false; startRebuild(''); }
    else if (ui.autoRebuild && d.need !== 'compile' && d.need !== 'translate') ui.autoRebuild = false;
  } else if (d.cmd === 'update-check') {
    if (d.local_version) env.version = d.local_version;
    if (d.ok) { upd.info = d; upd.error = ''; } else { upd.error = d.error || ''; if (!upd.info) upd.info = d; }
    if (upd.manual) {
      upd.manual = false;
      if (!d.ok) toast(t('toast.chkerr').replace('%e', short(d.error)));
      else if (!d.available) toast(t('toast.latest'));
    }
  } else if (d.cmd === 'update-apply') {
    if (d.ok) {
      upd.info = null; upd.error = ''; upd.launcherTried = false;
      upd.phase = d.restart ? 'restart' : 'idle';
      if (d.applied) toast(t('toast.updated'));
      util('status');
    } else { upd.phase = 'error'; upd.error = d.error || ''; ui.autoRebuild = false; }
  } else if (d.cmd === 'launcher-build') {
    if (d.ok) upd.phase = 'restart';
  }
  render();
}

function startUpdates() {
  if (!env.python) return;          // Python arrives with the first build, updates can start then
  if (env.built) util('status');
  if (state.autoupdate) util('check');
  clearInterval(upd.timer);
  upd.timer = setInterval(function () {
    if (state.autoupdate && !ui.building && !ui.playing && upd.phase === 'idle') util('check');
  }, 30 * 60 * 1000);
}

function doUpdate() {
  if (ui.building || ui.playing) return;
  upd.phase = 'applying'; upd.error = ''; ui.autoRebuild = true;
  util('apply'); render();
}

function startRebuild(flag) {
  if (ui.building || ui.playing) return;
  ui.failed = false; ui.rebuild = true; ui.stage = 0; $('#log').textContent = '';
  send('rebuild', flag || '');
  tab('play');
}

function collectReport(line) {
  var m = /unknown indirect (?:call|jump) to ([0-9a-f]{8})|\bindirect (?:call|jump) to ([0-9a-f]{8})/i.exec(line);
  if (m) {
    var a = (m[1] || m[2]).toLowerCase();
    if (reports.length < 30 && reports.every(function (r) { return r.addr !== a; })) reports.push({ addr: a, line: line });
  } else if (/\[a2pc\] (CRASH|STOP):/.test(line) && reports.length < 30) reports.push({ addr: '', line: line });
}

function copyText(txt) {
  var done = function () { toast(t('toast.copied')); };
  var fallback = function () {
    var ta = el('textarea'); ta.value = txt; ta.style.position = 'fixed'; ta.style.opacity = '0';
    document.body.appendChild(ta); ta.select();
    try { document.execCommand('copy'); done(); } catch (e) {}
    document.body.removeChild(ta);
  };
  try { navigator.clipboard.writeText(txt).then(done, fallback); } catch (e) { fallback(); }
}

function paintBanner() {
  var b = $('#banner'), info = upd.info, st = env.status;
  var busy = ui.building || ui.playing;
  var title = '', text = '', notes = '', cls = '', main = null, alt = null, x = null;
  var pending = upd.checking || uq.q.some(function (n) { return /^check/.test(n); });
  var addrs = reports.map(function (r) { return r.addr; }).filter(Boolean);

  if (ui.building) { b.hidden = true; return; }
  if (upd.phase === 'applying') { title = t('bn.applying'); }
  else if (upd.phase === 'restart') {
    title = t('bn.restart'); text = t('bn.restart.t'); cls = 'good';
    main = [t('bn.restart.b'), function () { send('restart'); }];
  } else if (upd.phase === 'error') {
    title = t('bn.err'); text = short(upd.error); cls = 'warn';
    main = [t('bn.retry'), doUpdate];
    alt = [t('bn.err.b'), function () { tab('sys'); $('#ulog-box').open = true; }];
    x = [t('bn.dismiss'), function () { upd.phase = 'idle'; render(); }];
  } else if (info && info.ok && info.available && state.dismissed !== info.sha) {
    title = t('bn.upd');
    var v = info.version;
    text = (v && v !== env.version ? t('bn.upd.t').replace('%v', v) : t('bn.upd.t2')).replace('%n', info.changed);
    if (reports.length) text = t('bn.crash.fix') + text;
    notes = (info.notes && (info.notes[state.lang] || info.notes.en)) || '';
    main = [t('bn.install'), doUpdate];
    alt = [t('bn.later'), function () { state.dismissed = info.sha; save(true); render(); }];
  } else if (st && st.built && st.need === 'full') {
    title = t('bn.full'); text = t('bn.full.t');
    main = [t('bn.full.b'), function () { ui.showBuild = true; ui.rebuild = false; ui.stage = 0; tab('play'); render(); }];
  } else if (st && st.built && (st.need === 'compile' || st.need === 'translate')) {
    title = t('bn.rebuild'); text = t('bn.rebuild.t');
    main = [t('bn.rebuild.b'), function () { startRebuild(''); }];
  } else if (reports.length && !ui.crashHidden) {
    title = t('bn.crash'); cls = 'warn';
    var a = addrs.length ? addrs.slice(0, 3).join(', ') + (addrs.length > 3 ? ' +' + (addrs.length - 3) : '') : t('bn.noaddr');
    text = t(pending ? 'bn.crash.t' : 'bn.crash.none').replace('%a', a);
    main = [t('bn.copy'), function () {
      copyText('Alundra 2 PC ' + (env.version || '') + '\n' + reports.map(function (r) { return r.line; }).join('\n'));
    }];
    alt = [t('upd.page'), function () { send('open', 'repo'); }];
    x = [t('bn.dismiss'), function () { ui.crashHidden = true; render(); }];
  } else { b.hidden = true; return; }

  b.hidden = false; b.className = 'banner' + (cls ? ' ' + cls : '');
  $('#bn-title').textContent = title; $('#bn-text').textContent = text; $('#bn-notes').textContent = notes;
  [['#bn-main', main, true], ['#bn-alt', alt, false], ['#bn-x', x, false]].forEach(function (r) {
    var btn = $(r[0]);
    btn.hidden = !r[1];
    if (!r[1]) { btn.onclick = null; return; }
    btn.textContent = r[1][0]; btn.onclick = r[1][1];
    btn.classList.toggle('go', r[2]);
    btn.disabled = busy && (r[1][1] === doUpdate || r[1][0] === t('bn.rebuild.b') || r[1][0] === t('bn.restart.b'));
  });
}

function paintUpd() {
  $('#ver').textContent = env.version ? 'v' + env.version : '';
  var info = upd.info, s;
  if (upd.checking) s = t('upd.st.checking');
  else if (upd.phase === 'applying') s = t('bn.applying');
  else if (info && info.ok && info.available) s = t('upd.st.avail').replace('%v', info.version || String(info.changed));
  else if (info && info.ok && info.empty) s = t('upd.st.empty');
  else if (info && info.ok) s = t('upd.st.latest');
  else if (upd.error) s = t('upd.st.err').replace('%e', short(upd.error));
  else s = t('upd.st.none');
  var box = $('#upd-val'); box.textContent = '';
  if (env.version) box.appendChild(el('div', null, t('upd.v').replace('%v', env.version)));
  box.appendChild(el('div', null, s));
  $('#s-check').disabled = upd.checking || upd.phase === 'applying' || ui.building;

  var gs = env.status, g;
  if (!env.built) g = t('gb.none');
  else if (!gs) g = '';
  else if (gs.need === 'none') g = t('gb.ok');
  else if (gs.need === 'full') g = t('gb.full');
  else g = t('gb.need').replace('%w', t('gb.why.' + gs.need));
  $('#gb-val').textContent = g;
  $('#s-gb').disabled = !env.built || ui.building || ui.playing || !gs || !gs.can_rebuild;
}

// ---- tabs / language / render -----------------------------------------

var TABS = [['play', 'nav.play'], ['gfx', 'nav.gfx'], ['mods', 'nav.mods'], ['keys', 'nav.keys'], ['sys', 'nav.sys']];
function tab(id) {
  ui.tab = id;
  $$('.page').forEach(function (p) { p.classList.toggle('on', p.id === 'p-' + id); });
  $$('#nav button').forEach(function (b) { b.classList.toggle('on', b.getAttribute('data-tab') === id); });
  $('#main').scrollTop = 0;
}

var KEYS = [
  ['g.char', [
    ['k.move', null, [['↑', '←', '↓', '→'], ['W', 'A', 'S', 'D']]],
    ['k.cross', 'k.cross.n', [['K'], ['Space']]],
    ['k.square', 'k.square.n', [['J']]],
    ['k.circle', 'k.circle.n', [['L']]],
    ['k.tri', 'k.tri.n', [['I']]]]],
  ['g.cam', [
    ['k.l1r1', 'k.l1r1.n', [['Q', 'E']]],
    ['k.l2', 'k.l2.n', [['Tab']]],
    ['k.r2', 'k.r2.n', [['Shift']]],
    ['k.start', null, [['Enter']]],
    ['k.select', null, [['Backspace']]]]],
  ['g.win', [
    ['k.full', null, [['F11']]],
    ['k.quit', null, [['F10']]]]]
];
function paintKeys() {
  var box = $('#keys'); box.textContent = '';
  KEYS.forEach(function (g) {
    var wrap = el('div', 'kgroup');
    wrap.appendChild(el('h3', 'sub', t(g[0])));
    wrap.lastChild.style.marginTop = '0';
    var list = el('div', 'keys'); list.style.marginTop = '12px';
    g[1].forEach(function (r) {
      var row = el('div', 'krow'), a = el('div', 'a', t(r[0]));
      if (r[1]) a.appendChild(el('small', null, t(r[1])));
      var cap = el('div', 'cap');
      r[2].forEach(function (alt, i) {
        if (i) cap.appendChild(el('span', null, t('or')));
        alt.forEach(function (k) { cap.appendChild(el('kbd', null, k)); });
      });
      row.appendChild(a); row.appendChild(cap); list.appendChild(row);
    });
    wrap.appendChild(list); box.appendChild(wrap);
  });
}

function render() {
  document.documentElement.lang = state.lang;
  $$('[data-t]').forEach(function (n) { n.textContent = t(n.getAttribute('data-t')); });
  $$('[data-ph]').forEach(function (n) { n.placeholder = t(n.getAttribute('data-ph')); });
  $$('#nav button').forEach(function (b, i) { $('.l', b).textContent = t(TABS[i][1]); });
  $$('#langs button').forEach(function (b) { b.classList.toggle('on', b.getAttribute('data-lang') === state.lang); });
  $$('.row[data-k]').forEach(paintRow);
  paintWide();
  paintPlay();
  paintKeys();
  $('#disc-val').textContent = state.disc || t('sys.none');
  $('#py-val').textContent = env.python ? env.python.replace(/"/g, '') : t('sys.py.none');
  $('#s-rebuild').disabled = ui.building || ui.playing;
  paintBanner();
  paintUpd();
}

function init() {
  var nav = $('#nav');
  TABS.forEach(function (tb, i) {
    var li = el('li'), b = el('button');
    b.setAttribute('data-tab', tb[0]);
    b.appendChild(el('span', 'n', '0' + (i + 1))); b.appendChild(el('span', 'l'));
    b.addEventListener('click', function () { tab(tb[0]); });
    li.appendChild(b); nav.appendChild(li);
  });
  $$('.row[data-k]').forEach(buildRow);

  $$('#langs button').forEach(function (b) {
    b.addEventListener('click', function () { state.lang = b.getAttribute('data-lang'); send('lang', state.lang); save(true); render(); });
  });
  $('#b-play').addEventListener('click', function () { send('play', 'continue'); });
  $('#b-new').addEventListener('click', function () { send('play', 'new'); });
  $('#b-browse').addEventListener('click', function () { send('browse'); });
  $('#s-browse').addEventListener('click', function () { send('browse'); });
  $('#b-build').addEventListener('click', startBuild);
  $('#b-stop').addEventListener('click', function () { ui.stopping = true; send('cancel'); });
  $('#s-rebuild').addEventListener('click', function () {
    ui.showBuild = true; ui.rebuild = false; ui.stage = 0; tab('play'); render();
  });
  $('#s-check').addEventListener('click', function () { upd.manual = true; util('check-force'); });
  $('#s-gb').addEventListener('click', function () {
    var gs = env.status;
    if (gs && gs.need === 'full') { ui.showBuild = true; ui.rebuild = false; ui.stage = 0; tab('play'); render(); }
    else startRebuild(gs && gs.need === 'none' ? '--translate' : '');
  });
  $('#b-back').addEventListener('click', function () { ui.showBuild = false; render(); });
  $('#disc').addEventListener('input', function (e) { state.disc = e.target.value; save(); });
  $('#disc').addEventListener('keydown', function (e) { if (e.key === 'Enter') startBuild(); });
  $$('[data-open]').forEach(function (b) { b.addEventListener('click', function () { send('open', b.getAttribute('data-open')); }); });
  $('#wfmt-in').addEventListener('input', function (e) {
    var v = e.target.value.trim();
    if (!v) { state.wfmt = '16:9'; save(); paintWide(); return; }
    if (!isNaN(parseRatio(v))) { state.wfmt = v; save(); }
    paintWide();
    $$('#wfmt-seg button').forEach(function (b) { b.classList.toggle('on', b.textContent === state.wfmt && b.textContent === v); });
  });
  // the page is never zoomed or dragged, whatever the user tries
  document.addEventListener('contextmenu', function (e) { e.preventDefault(); });
  document.addEventListener('dragover', function (e) { e.preventDefault(); });
  document.addEventListener('drop', function (e) { e.preventDefault(); });

  tab('play');
  render();
  send('ready');
}
init();
})();
