#!/usr/bin/env python3
"""Builds and starts the Alundra 2 (USA, SLUS-010.17) PC version.

    python a2pc.py                         open the launcher window
    python a2pc.py "Alundra 2.bin"         build the game from your disc image (a .bin, a .cue or a .zip/.7z with one inside)
    python a2pc.py rebuild                 build again from the files already unpacked, no disc needed
    python a2pc.py status                  does the built game match these scripts?
    python a2pc.py play [options]          start the game
        --newgame  --size 1920x1080  --fullscreen | --borderless  --smooth | --sharp  --texfilter | --no-texfilter
        --hires 1..4  --wide 16:9  --fps 60..240  --mute
    python a2update.py check | apply       look for a newer version on GitHub / install it

Everything made from your disc (game/, extracted/, recomp/, saves/) stays on this computer.
Needs Python 3.8+. A C compiler and SDL2.dll are fetched automatically when they are missing."""
import os, sys, json, struct, subprocess, shutil, glob, re, time, zipfile, hashlib, threading, urllib.request
from concurrent.futures import ThreadPoolExecutor
HERE = os.path.dirname(os.path.abspath(__file__)); os.chdir(HERE); os.environ['A2WORK'] = HERE; sys.path.insert(0, HERE)
GAME = os.path.join(HERE, 'game'); REC = os.path.join(HERE, 'recomp'); EXT = os.path.join(HERE, 'extracted')
WIN = os.name == 'nt'; EXE = 'a2pc.exe' if WIN else 'a2pc'
P1 = [1, 2, 11, 13, 17, 18, 21, 23, 24, 25, 26, 30, 31, 32, 33, 34, 35]
MODS = ['main', 'game00', 'event00', 'game01', 'game02', 'game03', 'al22', 'al22b', 'al22c', 'al22d'] + ['p1_%d' % i for i in P1]
RT = ['boot_test.c', 'a2cd.c', 'gpu.c', 'sio.c', 'a2card.c', 'a2thr.c', 'a2log.c', 'a2_win.c', 'a2ov.c', 'gte.c', 'spu.c']
SDL_URL = 'https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9-win32-x64.zip'
# output goes to the console and, during a build, to build.log; the launcher shows the same lines
_out = {'last': time.time(), 'file': None}
def say(*a):
    msg = ' '.join(str(x) for x in a)
    print('[a2pc]', msg, flush=True); _out['last'] = time.time()
    if _out['file']: _out['file'].write('[a2pc] ' + msg + '\n'); _out['file'].flush()
def die(*a):
    say('ERROR:', *a); sys.exit(1)
def step(n, text): say('[%d/5] %s' % (n, text))
def start_build_log():
    _out['file'] = open(os.path.join(HERE, 'build.log'), 'w', encoding='utf-8')
    t0 = time.time()
    def beat():
        while True:
            time.sleep(5)
            if time.time() - _out['last'] >= 20:
                e = int(time.time() - t0); say('still working, %d:%02d since the start (translating and compiling are the slow parts)' % (e // 60, e % 60))
    threading.Thread(target=beat, daemon=True).start()
def download(url, dst, label):
    os.makedirs(os.path.dirname(dst) or '.', exist_ok=True)
    req = urllib.request.Request(url, headers={'User-Agent': 'a2pc'})
    with urllib.request.urlopen(req, timeout=60) as r, open(dst + '.part', 'wb') as f:
        total = int(r.headers.get('Content-Length') or 0); got = 0; t = time.time()
        say('downloading %s%s' % (label, ' (%d MB)' % (total >> 20) if total else ''))
        while True:
            b = r.read(1 << 20)
            if not b: break
            f.write(b); got += len(b)
            if time.time() - t >= 3:
                t = time.time(); say('  %s: %d%s MB' % (label, got >> 20, ' of %d' % (total >> 20) if total else ''))
    os.replace(dst + '.part', dst)

# --- disc image
def resolve_disc(p):
    p = os.path.abspath(p.strip('"'))
    if not os.path.exists(p): die('file not found:', p)
    low = p.lower()
    if low.endswith('.cue'):
        txt = open(p, errors='replace').read(); m = re.search(r'FILE\s+"?([^"\r\n]+?)"?\s+BINARY', txt, re.I)
        if not m: die('no FILE ... BINARY line in the .cue')
        b = os.path.join(os.path.dirname(p), m.group(1))
        if not os.path.exists(b): die('the .cue points to a missing file:', b)
        if txt.upper().count('TRACK ') > 1: say('note: the .cue has several tracks; only the first .bin (data track) is used')
        return b
    if low.endswith('.zip') or low.endswith('.7z'):
        import discimg as B; out = os.path.join(HERE, 'alundra2_unpacked.bin')
        if not os.path.exists(out) or os.path.getsize(out) < 10 ** 8:
            say('unpacking archive ...'); (B.extract_zip if low.endswith('.zip') else B.extract_7z)(p, out)
        return out
    return p
def check_raw(p):
    sz = os.path.getsize(p)
    if sz % 2352: die('this is not a raw 2352-byte-sector image (size %d is not a multiple of 2352). Use the .bin of a MODE2/2352 dump '
                      '(a 2048-byte .iso is not supported yet).' % sz)
    with open(p, 'rb') as f: f.seek(16 * 2352 + 24); pvd = f.read(8)
    if pvd[1:6] != b'CD001': die('no ISO9660 volume at sector 16 - is this a PS1 data-track .bin?')
def pack_game(disc, files):
    """unpack the game's data from the disc image into the folder game/ (the exe reads it from there; the disc image is not needed afterwards)"""
    idx = os.path.join(GAME, 'index.txt'); total = os.path.getsize(disc) // 2352
    if os.path.exists(idx) and open(idx).read().rstrip().endswith('end'): say('game/ folder already unpacked'); return
    os.makedirs(GAME, exist_ok=True); lines = ['total %d' % total]; cov = set(); raw = open(disc, 'rb'); CH = 512
    for path, lba, size in sorted(files, key=lambda x: x[1]):
        name = path.split('/')[-1].split(';')[0]; cnt = (size + 2047) // 2048; rawkind = name.upper().endswith(('.XA', '.STR'))
        say('  unpacking %s (%d MB)' % (name, size // 1048576))
        with open(os.path.join(GAME, name), 'wb') as out:
            for a in range(0, cnt, CH):
                n = min(CH, cnt - a); raw.seek((lba + a) * 2352); blk = raw.read(n * 2352)
                if len(blk) != n * 2352: die('the disc image is shorter than expected (truncated file?)')
                if rawkind: out.write(blk)
                else: out.write(b''.join(blk[i * 2352 + 24:i * 2352 + 2072] for i in range(n)))
        lines.append('%s %d %d %s' % ('r' if rawkind else 'c', lba, cnt, name)); cov.update(range(lba, lba + cnt))
    sysd = bytearray()
    for l in range(total):                       # sectors that belong to no file
        if l in cov: continue
        raw.seek(l * 2352 + 24); d = raw.read(2048)
        if any(d): sysd += struct.pack('<I', l) + d
    open(os.path.join(GAME, 'system.dat'), 'wb').write(sysd); lines.append('s 0 1 system.dat'); lines.append('end')
    open(idx, 'w').write('\n'.join(lines) + '\n'); say('game data unpacked into game/ (%d files)' % (len(lines) - 3))
def extract(disc):
    os.environ['A2DISC'] = disc
    import discimg as B
    files = B.parse_iso(disc); json.dump(files, open('files.json', 'w'))
    names = {p.split('/')[-1].split(';')[0]: (l, s) for p, l, s in files}
    for need in B.SMALL[:-1] + ['PACK0.PAK', 'GRP.GP']:
        if need not in names: die('file %s not found on the disc - wrong game/region? (needs Alundra 2 USA, SLUS-010.17)' % need)
    os.makedirs(EXT, exist_ok=True); raw = open(disc, 'rb')
    def read_file(lba, size):
        out = bytearray()
        for i in range((size + 2047) // 2048): raw.seek((lba + i) * 2352 + 24); out += raw.read(2048)
        return bytes(out[:size])
    for nm in B.SMALL:
        l, s = names[nm]; open(os.path.join(EXT, nm), 'wb').write(read_file(l, s))
    exe = open(os.path.join(EXT, 'SLUS_010.17'), 'rb').read()
    if exe[:8] != b'PS-X EXE': die('SLUS_010.17 is not a PS-X EXE')
    if struct.unpack('<I', exe[0x10:0x14])[0] != 0x8003CF4C: die('unexpected entry point - this tool only knows the USA release SLUS-010.17')
    os.makedirs(REC, exist_ok=True)
    open(os.path.join(REC, 'main.bin'), 'wb').write(exe[0x800:])
    for m, f in (('game00', 'GAME00.BIN'), ('event00', 'EVENT00.BIN'), ('game01', 'GAME01.BIN'), ('game02', 'GAME02.BIN'), ('game03', 'GAME03.BIN')):
        shutil.copy(os.path.join(EXT, f), os.path.join(REC, m + '.bin'))
    import lib
    for nm, rr in (('al22', 76), ('al22b', 77), ('al22c', 78), ('al22d', 79)): open(os.path.join(REC, nm + '.bin'), 'wb').write(lib.rec('PACK0.PAK', rr))
    for i in P1: open(os.path.join(REC, 'p1_%d.bin' % i), 'wb').write(lib.rec('PACK1.PAK', i))
    say('extracted %d files from the disc into extracted/ and recomp/' % (len(B.SMALL) + 2 + len(P1)))
    pack_game(disc, files)

# --- translating to C
def translate():
    t0 = time.time()
    def one(m):
        r = subprocess.run([sys.executable, 'a2recomp.py', m, 'recomp'], capture_output=True, text=True)
        if r.returncode: die('translation of %s failed:\n%s' % (m, r.stderr[-800:]))
        p = os.path.join(REC, m + '.c'); s = open(p).read()
        s = s.replace('__attribute__((weak)) ', '')            # PE/COFF has no usable weak symbols
        if m == 'main': s = s.replace('void f_80045a04(CPU*c){', 'void f_80045a04_lib(CPU*c){', 1)   # the pad function is replaced in rt/boot_test.c
        open(p, 'w').write(s)
    n = [0]; lock = threading.Lock()
    def counted(m):
        one(m)
        with lock: n[0] += 1; say('  translated %d/%d (%s)' % (n[0], len(MODS), m))
    with ThreadPoolExecutor(max(1, (os.cpu_count() or 2))) as ex: list(ex.map(counted, MODS))
    say('translated %d modules to C in %.0f s' % (len(MODS), time.time() - t0))

# --- build stamps
# build.json remembers what the game was built from. After an update this tells what has to be redone:
# translator or seeds.txt changed: translate again; only rt/ changed: just compile again
TRANSLATE_REV = 1                     # raise it when translate() changes the generated C
BUILD = os.path.join(HERE, 'build.json')
TRANSLATOR = ['a2recomp.py', 'mips.py', 'dasm.py', 'lib.py', 'seeds.txt']
LEVELS = {'none': 0, 'compile': 1, 'translate': 2, 'full': 3}
def fhash(paths):
    h = hashlib.sha1()
    for p in paths:
        h.update(os.path.basename(p).encode())
        try:
            with open(p, 'rb') as f: h.update(f.read().replace(b'\r\n', b'\n'))
        except OSError: h.update(b'-')
    return h.hexdigest()[:16]
def stamp_translate(): return fhash([os.path.join(HERE, f) for f in TRANSLATOR]) + '.%d' % TRANSLATE_REV
def stamp_compile(): return fhash(sorted(glob.glob(os.path.join(HERE, 'rt', '*.c')) + glob.glob(os.path.join(HERE, 'rt', '*.h'))))
def read_json(p):
    try:
        with open(p, encoding='utf-8') as f: return json.load(f)
    except Exception: return {}
def write_json(p, d):
    tmp = p + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f: json.dump(d, f, indent=1)
    os.replace(tmp, p)
def set_stamps(**kw):
    b = read_json(BUILD); b.update(kw); write_json(BUILD, b)
def local_version(): return read_json(os.path.join(HERE, 'version.json')).get('version')
def have_unpacked(): return all(os.path.exists(os.path.join(REC, m + '.bin')) for m in MODS)
def build_status():
    """what the built game needs: none / compile / translate / full (full = unpack the disc again)"""
    built = os.path.exists(os.path.join(REC, EXE)); b = read_json(BUILD); u = read_json(os.path.join(HERE, 'update.json'))
    r = {'cmd': 'status', 'built': built, 'need': 'none', 'why': '', 'version': local_version(), 'can_rebuild': have_unpacked(),
         'launcher_stale': bool(u.get('launcher_stale')), 'force': u.get('force')}
    if not built: return r
    if not b:                                            # built before build.json existed: take the current state as the baseline
        set_stamps(translate=stamp_translate(), compile=stamp_compile(), adopted=True); b = read_json(BUILD)
    need, why = 'none', ''
    if b.get('translate') != stamp_translate(): need, why = 'translate', 'the translator or seeds.txt changed'
    elif b.get('compile') != stamp_compile(): need, why = 'compile', 'the runtime (rt/) changed'
    f = u.get('force')
    if f in LEVELS and LEVELS[f] > LEVELS[need]: need, why = f, 'the update asks for a rebuild'
    if need in ('compile', 'translate') and not have_unpacked(): need, why = 'full', 'the unpacked game files are missing'
    r['need'] = need; r['why'] = why
    return r
def clear_force(level):
    u = read_json(os.path.join(HERE, 'update.json'))
    if u.get('force') and LEVELS.get(u['force'], 0) <= LEVELS[level]:
        u.pop('force', None); write_json(os.path.join(HERE, 'update.json'), u)

# --- compiling
TOOLS = os.path.join(HERE, 'tools')
def zig_win():
    """Zig compiler from the ziglang wheel on PyPI (a plain zip), unpacked into tools/zig. No pip needed."""
    exe = os.path.join(TOOLS, 'zig', 'ziglang', 'zig.exe')
    if os.path.exists(exe): return exe
    say('no C compiler on this PC: getting Zig (one time, about 100 MB)')
    try:
        meta = json.load(urllib.request.urlopen('https://pypi.org/pypi/ziglang/json', timeout=60))
        best = None
        for files in meta['releases'].values():
            for f in files:
                if f['filename'].endswith('win_amd64.whl') and 'dev' not in f['filename'] and not f.get('yanked'):
                    if best is None or f['upload_time'] > best['upload_time']: best = f
        if not best: die('no Windows build of ziglang on PyPI')
        whl = os.path.join(TOOLS, best['filename']); download(best['url'], whl, 'zig ' + best['filename'].split('-')[1])
        h = hashlib.sha256()
        with open(whl, 'rb') as f:
            for b in iter(lambda: f.read(1 << 20), b''): h.update(b)
        if h.hexdigest() != best['digests']['sha256']: os.remove(whl); die('the downloaded zig archive is damaged, run again')
        say('unpacking zig (about 1 minute) ...')
        with zipfile.ZipFile(whl) as z:
            names = [n for n in z.namelist() if n.startswith('ziglang/')]
            for i, n in enumerate(names):
                z.extract(n, os.path.join(TOOLS, 'zig'))
                if i % 1500 == 1499: say('  unpacking zig: %d%%' % (100 * i // len(names)))
        os.remove(whl)
    except SystemExit: raise
    except Exception as e:
        die('could not get the Zig compiler (%s). Check the internet connection, or install MSYS2 / w64devkit (gcc) and run again.' % e)
    if not os.path.exists(exe): die('zig.exe is missing after unpacking')
    return exe
def zig(sub, why):
    if WIN: return [zig_win()] + sub
    zc = [sys.executable, '-m', 'ziglang'] + sub
    if subprocess.run(zc + ['--version'], capture_output=True).returncode != 0:
        say(why + ' - installing ziglang with pip (about 80 MB, one time) ...')
        subprocess.run([sys.executable, '-m', 'pip', 'install', '--user', 'ziglang'])
        if subprocess.run(zc + ['--version'], capture_output=True).returncode != 0:
            die('no C compiler. Install gcc or run: pip install ziglang')
    return zc
def find_cc():
    for c in (['gcc'], ['cc'], ['clang']):
        if shutil.which(c[0]):
            r = subprocess.run(c + ['-dumpmachine'], capture_output=True, text=True)
            if r.returncode == 0 and (not WIN or 'mingw' in r.stdout): return c      # on Windows only a 64-bit MinGW compiler works
    return zig(['cc'], 'no C compiler found')
def compile_all(cc):
    say('compiler:', ' '.join(cc))
    srcs = [os.path.join('recomp', m + '.c') for m in MODS]; rts = [os.path.join('rt', f) for f in RT]
    objdir = os.path.join(HERE, 'obj'); os.makedirs(objdir, exist_ok=True); objs = []; jobs = []
    sp = os.path.join(objdir, 'stamps.json'); old = read_json(sp); new = {}
    hdr = fhash([os.path.join('rt', 'a2rt.h')])
    def key(src, flags): return fhash([src]) + hdr + hashlib.sha1((' '.join(cc + flags)).encode()).hexdigest()[:8]
    for s in srcs:
        o = os.path.join(objdir, os.path.basename(s)[:-2] + '.o'); objs.append(o); fl = ['-O1', '-w', '-Irt']
        new[o] = key(s, fl); jobs.append((o, cc + fl + ['-c', s, '-o', o], s))
    for s in rts:
        o = os.path.join(objdir, 'rt_' + os.path.basename(s)[:-2] + '.o'); objs.append(o); fl = ['-O2', '-w', '-Irt', '-Irecomp']
        new[o] = key(s, fl); jobs.append((o, cc + fl + ['-c', s, '-o', o], s))
    todo = [j for j in jobs if not (os.path.exists(j[0]) and old.get(j[0]) == new[j[0]])]
    if len(todo) < len(jobs): say('%d of %d files are unchanged, compiling only the %d that changed' % (len(jobs) - len(todo), len(jobs), len(todo)))
    t0 = time.time()
    n = [0]; lock = threading.Lock(); done = {}
    def run(j):
        r = subprocess.run(j[1], capture_output=True, text=True)
        with lock:
            n[0] += 1; say('  compiled %d/%d (%s)' % (n[0], len(todo), os.path.basename(j[2])))
            if r.returncode == 0: done[j[0]] = new[j[0]]
        return r.returncode, j[2], r.stderr[-600:]
    with ThreadPoolExecutor(max(1, (os.cpu_count() or 2))) as ex: res = list(ex.map(run, todo))
    keep = {o: k for o, k in old.items() if o in new and os.path.exists(o)}
    keep.update({o: k for o, k in new.items() if o not in {j[0] for j in todo}}); keep.update(done)
    write_json(sp, keep)
    bad = [r for r in res if r[0]]
    if bad: die('compile failed for %s:\n%s' % (bad[0][1], bad[0][2]))
    say('compiled %d files in %.0f s' % (len(todo), time.time() - t0))
    out = os.path.join(REC, EXE)
    say('linking the game ...')
    r = subprocess.run(cc + ['-o', out] + objs + ([] if WIN else ['-ldl']), capture_output=True, text=True)
    if r.returncode: die('link failed:\n' + r.stderr[-1500:])
    say('built', out)
def get_sdl():
    if not WIN: return
    dst = os.path.join(REC, 'SDL2.dll')
    if os.path.exists(dst): return
    os.makedirs(REC, exist_ok=True)
    for p in (os.path.join(HERE, 'SDL2.dll'),):
        if os.path.exists(p): shutil.copy(p, dst); return
    try:
        zp = os.path.join(HERE, 'sdl2.zip'); download(SDL_URL, zp, 'SDL2.dll')
        with zipfile.ZipFile(zp) as z: open(dst, 'wb').write(z.read('SDL2.dll'))
        os.remove(zp)
    except Exception as e:
        say('could not download SDL2.dll (%s). Download SDL2-2.x-win32-x64.zip from https://github.com/libsdl-org/SDL/releases and put SDL2.dll into %s' % (e, REC))

# --- settings and starting the game
DEFAULTS = {'size': '1280x960', 'mode': 'window', 'smooth': True, 'texfilter': True, 'hires': 1, 'showfps': False, 'wide': False, 'wfmt': '16:9', 'unlock': 0, 'sound': True, 'start': 'continue'}
LCFG = os.path.join(HERE, 'launcher.json')
def launcher_cfg():
    try:
        with open(LCFG, encoding='utf-8') as f: return json.load(f)
    except Exception: return {}
def ui_settings():
    st = dict(DEFAULTS)
    st.update({k: v for k, v in launcher_cfg().items() if k in DEFAULTS})
    return st
def remember_disc(disc):
    lc = launcher_cfg()
    if not lc.get('disc'):
        lc['disc'] = disc
        write_json(LCFG, lc)
def settings_env(st):
    """launcher settings -> environment variables the game reads"""
    w, h = (st.get('size') or '1280x960').split('x'); e = {'A2WINW': w, 'A2WINH': h}
    mode = st.get('mode') or ('fullscreen' if st.get('fullscreen') else 'window')
    if mode == 'fullscreen': e['A2FULLSCREEN'] = '1'
    if mode == 'borderless': e['A2BORDERLESS'] = '1'
    if st.get('smooth'):
        e['A2LINEAR'] = '1'; e['A2SHARP'] = '3' if int(h) >= 1000 or mode == 'fullscreen' else '2'   # whole-number prescale, then smoothing
    if st.get('texfilter'): e['A2TEXFILTER'] = '1'
    if int(st.get('hires', 1)) > 1: e['A2HIRES'] = str(int(st['hires'])); e.pop('A2SHARP', None)
    if st.get('showfps'): e['A2FPSSHOW'] = '1'
    if int(st.get('unlock') or 0) >= 60: e['A2UNLOCK'] = str(int(st['unlock']))
    if st.get('wide'): e['A2WIDE'] = str(st.get('wfmt') or '16:9')
    if not st.get('sound', True): e['A2MUTE'] = '1'
    return e
def play(newgame=False, extra=(), st=None, wait=True):
    if not os.path.exists(os.path.join(GAME, 'index.txt')) or not os.path.exists(os.path.join(REC, EXE)): die('build first:  python a2pc.py "path\\to\\disc.bin"')
    st = st or ui_settings()
    env = dict(os.environ, A2MEMCARD=os.path.join(HERE, 'saves', 'memcard1.mcr'), A2GAMEDIR=GAME, A2WINDOW='1', A2SKIPINTRO='1', A2MAXEV='2000000000', A2SOFT='1')
    env.pop('A2DISC', None); env.update(settings_env(st)); os.makedirs(os.path.join(HERE, 'saves'), exist_ok=True)
    if newgame: env['A2PAD'] = '1400:3:UP,1450:4:X,2000:4:X'
    say('starting. Keys: arrows/WASD move, K/Space jump and confirm, J attack, L talk and pick up, I items and magic, Q/E camera, Tab item menu, Shift run, Enter Start, Backspace Select, F11 full screen, F10 quit')
    say('saves go to ' + os.path.join(HERE, 'saves', 'memcard1.mcr'))
    if newgame: say('New Game is pressed for you, the opening scene takes about a minute')
    cmd = [os.path.join(REC, EXE), 'main.bin'] + list(extra)
    if not wait: return subprocess.Popen(cmd, cwd=REC, env=env)
    sys.exit(subprocess.call(cmd, cwd=REC, env=env))

# --- launcher window
LDIR = os.path.join(HERE, 'launcher'); LEXE = os.path.join(HERE, 'A2Launcher.exe')
LLIBS = ['ole32', 'oleaut32', 'uuid', 'shell32', 'shlwapi', 'user32', 'comdlg32', 'gdi32', 'advapi32', 'dwmapi', 'urlmon']
def find_cxx(download=True):
    for c in (['g++'], ['clang++']):
        if shutil.which(c[0]):
            r = subprocess.run(c + ['-dumpmachine'], capture_output=True, text=True)
            if r.returncode == 0 and 'x86_64' in r.stdout and (not WIN or 'mingw' in r.stdout): return c, False
    if not download and WIN and not os.path.exists(os.path.join(TOOLS, 'zig', 'ziglang', 'zig.exe')): return None, False
    return zig(['c++'], 'no 64-bit C++ compiler found'), True
def ensure_launcher(force=False, out=None, download=True):
    if not WIN: return None
    out = out or LEXE
    if os.path.exists(out) and not force: return out
    src = os.path.join(LDIR, 'launcher.cpp')
    if not os.path.exists(src): return None
    try:
        say('building the launcher window (A2Launcher.exe, about 20 seconds) ...')
        cxx, is_zig = find_cxx(download)
        if not cxx: say('no C++ compiler here yet, the launcher will be rebuilt after the first game build'); return None
        cmd = cxx + ['-std=c++17', '-O2', '-s', '-I' + os.path.join(LDIR, 'inc'), src, '-o', out]
        cmd += ['-Wl,--subsystem,windows'] if is_zig else ['-mwindows', '-static', '-static-libgcc', '-static-libstdc++']
        cmd += ['-l' + l for l in LLIBS]
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=HERE)
        if r.returncode:
            say('launcher build failed:\n' + (r.stderr or r.stdout)[-1200:]); return None
        say('launcher built')
        return out
    except (SystemExit, Exception) as e:
        say('launcher build failed: %s' % e); return None
def launcher(force=False):
    exe = ensure_launcher(force)
    if exe:
        py = sys.executable
        if os.path.basename(py).lower() == 'pythonw.exe': py = os.path.join(os.path.dirname(py), 'python.exe')
        try:
            p = subprocess.Popen([exe], cwd=HERE, env=dict(os.environ, A2PY=py))
            try: rc = p.wait(timeout=6)
            except subprocess.TimeoutExpired: return
            if rc == 0: return
        except OSError as e:
            say('could not start the launcher: %s' % e)
        say('the launcher window could not start (it needs Windows 10/11 and Microsoft WebView2). The game can still be built and started from the command line: python a2pc.py --help')

def result(d): print('[a2pc-result] ' + json.dumps(d, ensure_ascii=False), flush=True)
def disc_build(a):
    start_build_log()
    disc = resolve_disc(a[0]); check_raw(disc); say('disc image:', disc)
    check_rt()
    step(1, 'Tools: compiler and SDL2')
    cc = find_cc(); get_sdl()
    step(2, 'Unpacking the disc image'); extract(disc)
    run_translate(); run_compile(cc)
    step(5, 'Final touches')
    remember_disc(disc); clear_force('full')
    if disc.endswith('alundra2_unpacked.bin'):
        try: os.remove(disc)
        except OSError: pass
    say('DONE. Game: %s   (data unpacked into %s - the disc image is no longer needed)' % (os.path.join(REC, EXE), GAME))
    say('Run:  python a2pc.py        (launcher window)  or   python a2pc.py play --newgame')
    if '--play' in a: play('--newgame' in a)
def check_rt():
    for need in RT + ['a2rt.h']:
        if not os.path.exists(os.path.join('rt', need)): die('missing rt/' + need + ' - unpack the whole archive')
def run_translate():
    step(3, 'Translating the game code to C')
    set_stamps(translate=None, compile=None)
    st = stamp_translate(); translate(); shutil.copy(os.path.join('rt', 'a2rt.h'), REC)
    set_stamps(translate=st)
def run_compile(cc):
    step(4, 'Compiling')
    set_stamps(compile=None)
    st = stamp_compile(); compile_all(cc)
    set_stamps(compile=st, version=local_version(), built=int(time.time()))
def rebuild(a):
    """translate again and/or compile again from the files unpacked earlier: no disc, no re-download of tools"""
    start_build_log()
    b = build_status(); need = b['need']
    if '--translate' in a: need = 'translate'
    elif '--compile' in a and LEVELS[need] < 1: need = 'compile'
    if need == 'full' or not have_unpacked():
        disc = launcher_cfg().get('disc')
        if not disc or not os.path.exists(disc): die('this update needs your disc image again: pick it in the launcher and press Build the game')
        say('this update needs the disc again, using the remembered one'); disc_build([disc]); return
    if need == 'none': say('the game is already up to date'); return
    check_rt()
    if read_json(os.path.join(HERE, 'update.json')).get('force'):
        try: os.remove(os.path.join(HERE, 'obj', 'stamps.json'))
        except OSError: pass
    step(1, 'Tools: compiler and SDL2')
    cc = find_cc(); get_sdl()
    step(2, 'Unpacking the disc image'); say('using the game files unpacked earlier, no disc needed')
    if need == 'translate': run_translate()
    else: step(3, 'Translating the game code to C'); say('the translator did not change, keeping the translated code')
    run_compile(cc)
    step(5, 'Final touches'); clear_force('translate' if need == 'translate' else 'compile')
    say('DONE. The game was rebuilt: %s' % os.path.join(REC, EXE))

def main():
    a = sys.argv[1:]
    if not a: launcher(); return
    if a[0] in ('-h', '--help'): print(__doc__); return
    if a[0] == 'status': result(build_status()); return
    if a[0] == 'rebuild': rebuild(a[1:]); return
    if a[0] == 'rebuild-launcher' and '--new' in a:
        new = os.path.join(HERE, 'A2Launcher.new.exe'); ok = ensure_launcher(True, new, download='--download' in a)
        good = bool(ok and os.path.exists(new))
        if good:
            u = read_json(os.path.join(HERE, 'update.json')); u['launcher_stale'] = False; write_json(os.path.join(HERE, 'update.json'), u)
        result({'cmd': 'launcher-build', 'ok': good}); return
    if a[0] in ('launcher', 'rebuild-launcher'): launcher(a[0] != 'launcher' or '--rebuild' in a); return
    if a[0] == 'play':
        st = ui_settings()
        for i, x in enumerate(a):
            if x == '--size' and i + 1 < len(a): st['size'] = a[i + 1]
            if x == '--fullscreen': st['mode'] = 'fullscreen'
            if x == '--borderless': st['mode'] = 'borderless'
            if x == '--texfilter': st['texfilter'] = True
            if x == '--no-texfilter': st['texfilter'] = False
            if x == '--sharp': st['smooth'] = False
            if x == '--hires' and i + 1 < len(a): st['hires'] = max(1, min(4, int(a[i + 1])))
            if x == '--smooth': st['smooth'] = True
            if x == '--mute': st['sound'] = False
            if x == '--wide' and i + 1 < len(a): st['wide'] = True; st['wfmt'] = a[i + 1]
            if x == '--fps' and i + 1 < len(a): st['unlock'] = int(a[i + 1])
        play('--newgame' in a, st=st); return
    disc_build(a)
if __name__ == '__main__': main()
