#!/usr/bin/env python3
"""a2update.py - update check and apply for the Alundra 2 PC package, fed by a GitHub repository.

    python a2update.py check [--force]     is there a newer commit with different package files?
    python a2update.py apply               download it and replace the changed files (with a backup, rolled back on any error)

Prints human lines as "[a2pc] ..." and one machine line "[a2pc-result] {json}" at the end (the launcher reads that).
Only package files are ever replaced. Your game data, saves, settings and downloaded tools are protected, see PROTECTED."""
import os, sys, json, time, shutil, hashlib, zipfile, posixpath, urllib.request, urllib.error

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.environ.get('A2REPO', 'pashadarkgemplay/Alundra2-recompilation')
API = os.environ.get('A2UPDATE_API', 'https://api.github.com')
ZIPHOST = os.environ.get('A2UPDATE_ZIP', 'https://github.com')
UPD = os.path.join(HERE, 'tools', 'update')
STATE = os.path.join(HERE, 'update.json')
PKG = os.path.join(UPD, 'package.zip')
MIN_GAP = 300                       # seconds between automatic checks (a manual check ignores it)
MAX_BYTES, MAX_FILES = 600 << 20, 6000

# what an update may write: top-level scripts and notes, rt/, launcher/, the bootstrap script
ALLOW_TOP = ('.py', '.txt', '.md', '.bat', '.ps1')
ALLOW_NAMES = {'version.json', '.gitignore', 'LICENSE', 'A2Launcher.exe'}
ALLOW_DIRS = ('rt/', 'launcher/')
ALLOW_FILES = {'tools/bootstrap.ps1'}
# and what it never touches, even if the repository happens to contain it
PROTECTED = ('game/', 'extracted/', 'recomp/', 'obj/', 'saves/', 'tools/python/', 'tools/zig/', 'tools/update/', '__pycache__/')
PROTECTED_NAMES = {'launcher.json', 'launcher.python', 'a2pc.cfg', 'update.json', 'build.log', 'SDL2.dll', 'files.json', 'funcs_main.json', 'cmd_table.json'}
EXE, EXE_NEW = 'A2Launcher.exe', 'A2Launcher.new.exe'    # a running exe cannot be overwritten, so the new one waits for the next start
LAUNCHER_SRC = ('launcher/launcher.cpp', 'launcher/inc/')


def log(*a): print('[a2pc]', *a, flush=True)
def result(d): print('[a2pc-result] ' + json.dumps(d, ensure_ascii=False), flush=True)

def load():
    try:
        with open(STATE, encoding='utf-8') as f: return json.load(f)
    except Exception: return {}
def save(st):
    tmp = STATE + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f: json.dump(st, f, indent=1)
    os.replace(tmp, STATE)

TEXT = ('.py', '.txt', '.md', '.bat', '.ps1', '.json', '.cpp', '.h', '.c', '.js', '.html', '.css', '.gitignore')
def sha256(path):
    """text files are hashed without CR, so a Windows checkout with CRLF endings does not look like an update"""
    h = hashlib.sha256()
    if path.lower().endswith(TEXT):
        with open(path, 'rb') as f: h.update(f.read().replace(b'\r\n', b'\n'))
        return h.hexdigest()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''): h.update(b)
    return h.hexdigest()

def allowed(rel):
    if rel in PROTECTED_NAMES or rel.startswith(PROTECTED): return False
    if rel in ALLOW_FILES or rel in ALLOW_NAMES or rel.startswith(ALLOW_DIRS): return True
    return '/' not in rel and (rel.endswith(ALLOW_TOP) or (rel.startswith('overrides') and rel.endswith('.json')))

def local_version():
    try:
        with open(os.path.join(HERE, 'version.json'), encoding='utf-8') as f: return json.load(f)
    except Exception: return {}

# --- network

def http(url, headers=None, timeout=25):
    req = urllib.request.Request(url, headers=dict({'User-Agent': 'a2pc-updater'}, **(headers or {})))
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r: return r.status, dict(r.headers), r.read()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read()[:300]

def remote_sha(st):
    """Newest commit of the default branch. A conditional request (ETag) that answers 304 does not count against GitHub's rate limit."""
    h = {'Accept': 'application/vnd.github.sha'}
    if st.get('etag') and st.get('remote_sha'): h['If-None-Match'] = st['etag']
    code, hd, body = http('%s/repos/%s/commits/HEAD' % (API, REPO), h)
    if code == 304 and st.get('remote_sha'): return st['remote_sha'], 'ok'
    if code == 200:
        st['etag'] = hd.get('ETag') or hd.get('Etag') or ''
        return body.decode().strip(), 'ok'
    if code in (404, 409): return None, 'empty'        # nothing published yet (409 = empty repository)
    return None, 'http %d' % code

def download(ref, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    url = '%s/%s/archive/%s.zip' % (ZIPHOST, REPO, ref)
    req = urllib.request.Request(url, headers={'User-Agent': 'a2pc-updater'})
    with urllib.request.urlopen(req, timeout=60) as r, open(dst + '.part', 'wb') as f:
        total = int(r.headers.get('Content-Length') or 0); got = 0; t = time.time()
        log('downloading the update%s' % (' (%.1f MB)' % (total / 1048576) if total else ''))
        while True:
            b = r.read(1 << 20)
            if not b: break
            f.write(b); got += len(b)
            if got > MAX_BYTES: raise RuntimeError('the update archive is too big')
            if time.time() - t >= 3: t = time.time(); log('  update: %.1f MB' % (got / 1048576))
    os.replace(dst + '.part', dst)

# --- package archive

def safe_extract(zpath, dest):
    shutil.rmtree(dest, ignore_errors=True); os.makedirs(dest)
    total = 0
    with zipfile.ZipFile(zpath) as z:
        infos = z.infolist()
        if len(infos) > MAX_FILES: raise RuntimeError('too many files in the update archive')
        for i in infos:
            name = i.filename
            if name.endswith('/') or ((i.external_attr >> 16) & 0o170000) == 0o120000: continue     # folders, symlinks
            norm = posixpath.normpath(name)
            if norm.startswith(('..', '/')) or ':' in norm or '\\' in name: raise RuntimeError('unsafe path in the archive: ' + name)
            total += i.file_size
            if total > MAX_BYTES: raise RuntimeError('the update archive is too big')
            out = os.path.join(dest, *norm.split('/'))
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with z.open(i) as s, open(out, 'wb') as d: shutil.copyfileobj(s, d)

def find_root(base, depth=3):
    """The folder with a2pc.py. Accepts the repository as plain files, or a2pc.zip uploaded as a file."""
    def walk(d, n):
        if os.path.exists(os.path.join(d, 'a2pc.py')): return d
        if n == 0: return None
        for e in sorted(os.listdir(d)):
            p = os.path.join(d, e)
            if os.path.isdir(p):
                r = walk(p, n - 1)
                if r: return r
        return None
    r = walk(base, depth)
    if r: return r
    for dp, _, fs in os.walk(base):
        for f in sorted(fs):
            if f.lower().endswith('.zip'):
                zp = os.path.join(dp, f)
                try:
                    with zipfile.ZipFile(zp) as z:
                        if not any(n.endswith('a2pc.py') and n.count('/') <= 1 for n in z.namelist()): continue
                except zipfile.BadZipFile: continue
                inner = os.path.join(UPD, 'inner')
                safe_extract(zp, inner)
                r = walk(inner, depth)
                if r: return r
    return None

def scan(root):
    out = {}
    for dp, dn, fs in os.walk(root):
        dn[:] = [d for d in dn if d != '.git']
        for f in fs:
            p = os.path.join(dp, f); rel = os.path.relpath(p, root).replace(os.sep, '/')
            if allowed(rel): out[rel] = p
    return out

def dest_of(rel):
    return os.path.join(HERE, *(EXE_NEW if rel == EXE else rel).split('/'))

def diff(files):
    """files that differ from what is installed now"""
    changed = []
    for rel, p in sorted(files.items()):
        cur = os.path.join(HERE, *rel.split('/'))
        if rel == EXE:
            if os.path.exists(cur) and sha256(cur) == sha256(p): continue
            if os.path.exists(dest_of(rel)) and sha256(dest_of(rel)) == sha256(p): continue
        elif os.path.exists(cur) and sha256(cur) == sha256(p): continue
        changed.append(rel)
    return changed

def read_meta(root):
    try:
        with open(os.path.join(root, 'version.json'), encoding='utf-8') as f: return json.load(f)
    except Exception: return {}

def prepare(ref):
    """unpack the package archive for ref (a commit sha, or HEAD) and describe it"""
    stage = os.path.join(UPD, 'stage')
    safe_extract(PKG, stage)
    root = find_root(stage)
    if not root: raise RuntimeError('no a2pc.py in the repository archive')
    files = scan(root)
    return root, files, diff(files), read_meta(root)

# --- commands

def report(st, err=None, cached=False):
    last = st.get('last') or {}
    lv = local_version()
    r = {'cmd': 'update-check', 'ok': err is None, 'error': err, 'cached': cached,
         'available': bool(last.get('changed')), 'sha': st.get('remote_sha'),
         'version': last.get('version'), 'rev': last.get('rev'), 'notes': last.get('notes') or {},
         'changed': len(last.get('changed') or []), 'files': (last.get('changed') or [])[:40],
         'local_version': lv.get('version'), 'checked': st.get('checked'), 'repo': REPO, 'empty': bool(st.get('empty'))}
    result(r)

def check(force=False):
    st = load(); now = time.time()
    if not force and st.get('last') is not None and now - st.get('checked', 0) < MIN_GAP:
        return report(st, cached=True)
    sha, why = None, 'ok'
    try:
        sha, why = remote_sha(st)
    except Exception as e:
        why = str(e)
    st['empty'] = why == 'empty'
    if why == 'empty':
        st['checked'] = now; st['last'] = {}; st['remote_sha'] = None; save(st)
        log('the update repository has nothing published yet')
        return report(st)
    ref = sha
    if not sha:                          # the api is unreachable or rate limited: fetch the archive of the default branch directly
        log('could not ask GitHub for the latest commit (%s), trying the archive directly' % why)
        ref = 'HEAD'
    try:
        if sha and st.get('pkg_sha') == sha and os.path.exists(PKG) and st.get('last') is not None and st.get('last_for') == sha:
            pass                         # this commit was already looked at
        else:
            download(ref, PKG)
            root, files, changed, meta = prepare(ref)
            st['pkg_sha'] = sha or 'HEAD'; st['last_for'] = sha
            st['last'] = {'changed': changed, 'version': meta.get('version'), 'rev': meta.get('rev'), 'notes': meta.get('notes') or {},
                          'rebuild': meta.get('rebuild'), 'rebuild_id': meta.get('rebuild_id')}
            if not changed: st['applied_sha'] = sha       # same files as ours, only another commit
        st['remote_sha'] = sha; st['checked'] = now; save(st)
    except Exception as e:
        log('update check failed: %s' % e)
        return report(st, err=str(e))
    ch = (st['last'] or {}).get('changed') or []
    log('update available: %d changed files' % len(ch) if ch else 'you have the latest version')
    report(st)

def backup_and_apply(plan, stale_delete):
    bak = os.path.join(UPD, 'backup')
    shutil.rmtree(bak, ignore_errors=True); os.makedirs(bak)
    done = []                                   # (dst, backed_up)
    try:
        for rel, src, dst in plan:
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            had = os.path.exists(dst)
            if had:
                b = os.path.join(bak, *rel.split('/')); os.makedirs(os.path.dirname(b), exist_ok=True); shutil.copy2(dst, b)
            done.append((dst, rel, had))
            tmp = dst + '.new-tmp'
            shutil.copyfile(src, tmp); os.replace(tmp, dst)
        for rel in stale_delete:
            dst = os.path.join(HERE, *rel.split('/'))
            if os.path.exists(dst):
                b = os.path.join(bak, *rel.split('/')); os.makedirs(os.path.dirname(b), exist_ok=True); shutil.copy2(dst, b)
                done.append((dst, rel, True)); os.remove(dst)
    except Exception:
        for dst, rel, had in reversed(done):
            try:
                if had: shutil.copy2(os.path.join(bak, *rel.split('/')), dst)
                elif os.path.exists(dst): os.remove(dst)
            except Exception: pass
        raise

def apply():
    st = load()
    sha = st.get('remote_sha')
    try:
        if not (os.path.exists(PKG) and st.get('pkg_sha') == (sha or 'HEAD')):
            download(sha or 'HEAD', PKG); st['pkg_sha'] = sha or 'HEAD'
        root, files, changed, meta = prepare(sha or 'HEAD')
        if not changed:
            st['applied_sha'] = sha; st['last'] = {}; save(st)
            log('nothing to update'); result({'cmd': 'update-apply', 'ok': True, 'applied': False, 'files': 0}); return
        log('updating %d files' % len(changed))
        for rel in changed:                       # a broken script would lock the updater out for good, so syntax is checked first
            if rel.endswith('.py'):
                with open(files[rel], 'rb') as f: compile(f.read(), rel, 'exec')
        plan = [(rel, files[rel], dest_of(rel)) for rel in changed]
        old = st.get('manifest') or {}
        stale = [rel for rel in old if rel not in files and allowed(rel) and rel != EXE]
        backup_and_apply(plan, stale)
        for rel in changed: log('  updated ' + rel)
        for rel in stale: log('  removed ' + rel)
        restart = any(r.startswith('launcher/ui/') or r == EXE for r in changed)
        src_changed = any(r == LAUNCHER_SRC[0] or r.startswith(LAUNCHER_SRC[1]) for r in changed)
        if EXE in changed: st['launcher_stale'] = False
        elif src_changed: st['launcher_stale'] = True       # source changed, no ready exe came with it: a2pc.py rebuild builds it
        # a one-off rebuild request from the maintainer: version.json "rebuild": "translate"|"compile"|"full" with a higher "rebuild_id"
        rid = meta.get('rebuild_id') or 0
        if rid > (st.get('rebuild_id') or 0):
            if meta.get('rebuild') in ('compile', 'translate', 'full'): st['force'] = meta['rebuild']
            st['rebuild_id'] = rid
        st['applied_sha'] = sha; st['last'] = {}
        st['manifest'] = {rel: None for rel in files}
        save(st)
        shutil.rmtree(os.path.join(UPD, 'stage'), ignore_errors=True)
        log('update applied: version %s' % (meta.get('version') or '?'))
        result({'cmd': 'update-apply', 'ok': True, 'applied': True, 'files': len(changed), 'restart': restart,
                'version': meta.get('version'), 'launcher_stale': bool(st.get('launcher_stale'))})
    except Exception as e:
        log('ERROR: the update was not applied: %s' % e)
        result({'cmd': 'update-apply', 'ok': False, 'error': str(e)})

def main(a):
    if not a or a[0] in ('-h', '--help'): print(__doc__); return
    if a[0] == 'check': check('--force' in a)
    elif a[0] == 'apply': apply()
    else: print(__doc__)

if __name__ == '__main__': main(sys.argv[1:])
