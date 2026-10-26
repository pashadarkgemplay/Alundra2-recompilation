// Launcher for the Alundra 2 PC build.
// A plain Win32 window with a WebView2 page inside (ui/index.html). The page draws
// everything; this side only starts python/the game and passes text messages back and forth.

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <urlmon.h>
#include <dwmapi.h>
#include <commdlg.h>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <string>
#include "WebView2.h"
#include "iids.h"

#define WM_POSTED   (WM_APP + 1)   // text for the page, lParam = new std::wstring*
#define WM_WV_INIT  (WM_APP + 2)   // runtime is there, create the webview
#define WM_WV_FAIL  (WM_APP + 3)   // lParam = new std::wstring* with the reason
#define WM_PROC_END (WM_APP + 4)   // wParam = 0 game / 1 build / 2 helper script (update check, status), lParam = exit code
#define WM_STATUS   (WM_APP + 5)   // lParam = new std::wstring*, text for the bare window

static const wchar_t* kHost = L"a2.launcher";
static const wchar_t* kBootstrapUrl = L"https://go.microsoft.com/fwlink/p/?LinkId=2124703";
static const wchar_t* kPythonUrl = L"https://www.python.org/ftp/python/3.12.8/python-3.12.8-embed-amd64.zip";
static const wchar_t* kRepoUrl = L"https://github.com/pashadarkgemplay/Alundra2-recompilation";

static HWND g_wnd;
static HINSTANCE g_inst;
static std::wstring g_dir;          // folder with the exe (= folder with a2pc.py)
static std::wstring g_self;         // full path of this exe
static std::wstring g_status;       // shown on the plain window until the page is up
static std::wstring g_py;           // python command line prefix
static std::string g_lang = "en";
static bool g_ready, g_building, g_playing, g_util, g_applying;
static volatile bool g_cancel;
static HANDLE g_buildJob;

static bool g_fileMode;            // page is loaded from file:// (runtime too old for host mapping)
static ICoreWebView2Environment* g_env;
static ICoreWebView2Controller* g_ctl;
static ICoreWebView2* g_wv;
static HMODULE g_loader;
typedef HRESULT (STDAPICALLTYPE *CreateEnvFn)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
typedef HRESULT (STDAPICALLTYPE *GetVerFn)(PCWSTR, LPWSTR*);
static CreateEnvFn g_createEnv;

static std::wstring W(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), 0, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::string U(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), 0, 0, 0, 0);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, 0, 0);
    return s;
}
static bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static std::string readFile(const std::wstring& p) {
    HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (f == INVALID_HANDLE_VALUE) return "";
    std::string s; char buf[8192]; DWORD n;
    while (ReadFile(f, buf, sizeof buf, &n, 0) && n) s.append(buf, n);
    CloseHandle(f);
    return s;
}
// written next to the target and moved over it, so a crash or power loss never leaves half a file
static bool writeFile(const std::wstring& p, const std::string& s) {
    std::wstring tmp = p + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD n; bool ok = WriteFile(f, s.data(), (DWORD)s.size(), &n, 0) && n == s.size();
    if (ok) FlushFileBuffers(f);
    CloseHandle(f);
    if (ok) ok = MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(tmp.c_str());
    return ok;
}

static std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) { char b[8]; wsprintfA(b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

// thread-safe: the page can only be touched from the ui thread, so queue it
static void post(const char* type, const std::string& body = "") {
    auto* m = new std::wstring(W(std::string(type) + "\t" + body));
    if (!PostMessageW(g_wnd, WM_POSTED, 0, (LPARAM)m)) delete m;
}

static std::wstring T(const char* en, const char* ru) { return W(g_lang == "ru" ? ru : en); }

static std::wstring knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = 0; std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, 0, &p))) r = p;
    CoTaskMemFree(p);
    return r;
}

// ---- python -------------------------------------------------------------

// runs a command line hidden and returns its exit code (-1 if it could not start or timed out)
static int runQuiet(std::wstring cmd, DWORD timeoutMs) {
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(0, &cmd[0], 0, 0, FALSE, CREATE_NO_WINDOW, 0, 0, &si, &pi)) return -1;
    DWORD code = (DWORD)-1;
    if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    else TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return (int)code;
}

// the windows store ships a fake python.exe that only opens the store, so just finding one is not enough
static bool pythonWorks(const std::wstring& prefix) {
    return runQuiet(prefix + L" -c \"import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)\"", 20000) == 0;
}

static void usePython(const std::wstring& exe, const wchar_t* extra = L"") {
    g_py = L"\"" + exe + L"\"" + extra;
    writeFile(g_dir + L"\\launcher.python", U(exe));
}

static std::wstring ownPython() { return g_dir + L"\\tools\\python\\python.exe"; }

static void findPython() {
    wchar_t env[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(L"A2PY", env, MAX_PATH * 2);
    if (n && n < MAX_PATH * 2) { usePython(env); return; }
    if (exists(ownPython()) && pythonWorks(L"\"" + ownPython() + L"\"")) { usePython(ownPython()); return; }
    std::string saved = readFile(g_dir + L"\\launcher.python");
    while (!saved.empty() && (saved.back() == '\n' || saved.back() == '\r' || saved.back() == ' ')) saved.pop_back();
    if (!saved.empty() && exists(W(saved)) && pythonWorks(L"\"" + W(saved) + L"\"")) { usePython(W(saved)); return; }
    wchar_t found[MAX_PATH];
    if (SearchPathW(0, L"py.exe", 0, MAX_PATH, found, 0) && pythonWorks(L"\"" + std::wstring(found) + L"\" -3")) { usePython(found, L" -3"); return; }
    if (SearchPathW(0, L"python.exe", 0, MAX_PATH, found, 0) && pythonWorks(L"\"" + std::wstring(found) + L"\"")) { usePython(found); return; }
    g_py.clear();
}

// download progress goes to the log so the page never looks frozen
struct Progress final : IBindStatusCallback {
    std::string label; DWORD last = 0; LONG refs = 1;
    explicit Progress(const char* l) : label(l) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** o) override {
        if (IsEqualIID(r, IID_IUnknown) || IsEqualIID(r, IID_IBindStatusCallback)) { *o = this; AddRef(); return S_OK; }
        *o = 0; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG c = InterlockedDecrement(&refs); if (!c) delete this; return c; }
    HRESULT STDMETHODCALLTYPE OnStartBinding(DWORD, IBinding*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPriority(LONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OnLowResource(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnProgress(ULONG now, ULONG total, ULONG, LPCWSTR) override {
        if (g_cancel) return E_ABORT;
        DWORD t = GetTickCount();
        if (t - last >= 2500 && now) {
            last = t;
            std::string m = "[a2pc]   " + label + ": " + std::to_string(now >> 20) + (total ? " of " + std::to_string(total >> 20) : std::string()) + " MB";
            post("log", m);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnStopBinding(HRESULT, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetBindInfo(DWORD*, BINDINFO*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OnDataAvailable(DWORD, DWORD, FORMATETC*, STGMEDIUM*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnObjectAvailable(REFIID, IUnknown*) override { return S_OK; }
};

static bool download(const wchar_t* url, const std::wstring& dst, const char* label) {
    DeleteFileW(dst.c_str());
    Progress* p = new Progress(label);
    HRESULT hr = URLDownloadToFileW(0, url, dst.c_str(), 0, p);
    p->Release();
    return SUCCEEDED(hr) && exists(dst);
}

// no python on this pc: fetch the official embeddable build (about 10 MB) into tools/python
static bool installPython() {
    std::wstring root = g_dir + L"\\tools", dir = root + L"\\python", zip = root + L"\\python-embed.zip";
    CreateDirectoryW(root.c_str(), 0);
    post("log", "[a2pc] [1/5] Tools: Python was not found on this PC, getting it (one time, about 10 MB)");
    if (!download(kPythonUrl, zip, "Python")) {
        post("log", g_cancel ? "[a2pc] stopped" : "[a2pc] ERROR: could not download Python from python.org. Check the internet connection and try again.");
        return false;
    }
    post("log", "[a2pc] unpacking Python ...");
    CreateDirectoryW(dir.c_str(), 0);
    int rc = runQuiet(L"tar.exe -xf \"" + zip + L"\" -C \"" + dir + L"\"", 120000);
    if (rc != 0)   // old windows without tar
        rc = runQuiet(L"powershell.exe -NoProfile -Command \"Expand-Archive -Force -LiteralPath '" + zip + L"' -DestinationPath '" + dir + L"'\"", 180000);
    DeleteFileW(zip.c_str());
    if (rc != 0 || !exists(ownPython())) { post("log", "[a2pc] ERROR: could not unpack Python"); return false; }

    // the embeddable build only looks at what its ._pth file lists; add the package folder so a2recomp.py can import its siblings
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\python*._pth").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        FindClose(h);
        std::wstring name = fd.cFileName;
        std::string base = U(name.substr(0, name.size() - 5));
        writeFile(dir + L"\\" + name, base + ".zip\r\n.\r\n..\\..\r\n");
    }
    if (!pythonWorks(L"\"" + ownPython() + L"\"")) { post("log", "[a2pc] ERROR: the downloaded Python does not start"); return false; }
    usePython(ownPython());
    post("log", "[a2pc] Python is ready");
    return true;
}

struct Pump { HANDLE pipe, proc; int kind; };   // kind: 0 game, 1 build, 2 helper script

static DWORD WINAPI pumpThread(void* p) {
    Pump* c = (Pump*)p;
    std::string buf; char tmp[4096]; DWORD n;
    while (ReadFile(c->pipe, tmp, sizeof tmp, &n, 0) && n) {
        buf.append(tmp, n);
        size_t nl;
        while ((nl = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            static const char tag[] = "[a2pc-result] ";
            if (c->kind == 2) {
                if (line.compare(0, sizeof tag - 1, tag) == 0) post("result", line.substr(sizeof tag - 1));
                else post("ulog", line);
            } else post("log", line);
            buf.erase(0, nl + 1);
        }
    }
    if (!buf.empty()) post(c->kind == 2 ? "ulog" : "log", buf);
    WaitForSingleObject(c->proc, INFINITE);
    DWORD code = 1; GetExitCodeProcess(c->proc, &code);
    PostMessageW(g_wnd, WM_PROC_END, c->kind, (LPARAM)code);
    CloseHandle(c->pipe); CloseHandle(c->proc);
    delete c;
    return 0;
}

// runs "<python> -u <script> <args>" without a console, output goes to the page
static bool runScript(const wchar_t* script, const std::wstring& args, int kind) {
    SECURITY_ATTRIBUTES sa = { sizeof sa, 0, TRUE };
    HANDLE rd, wr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    SetEnvironmentVariableW(L"PYTHONUTF8", L"1");
    SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");

    std::wstring cmd = g_py + L" -u \"" + g_dir + L"\\" + script + L"\" " + args;
    STARTUPINFOW si = { sizeof si };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr; si.hStdError = wr;
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, 0);
    si.hStdInput = nul;
    PROCESS_INFORMATION pi;
    DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
    BOOL ok = CreateProcessW(0, &cmd[0], 0, 0, TRUE, flags, 0, g_dir.c_str(), &si, &pi);
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) { CloseHandle(rd); return false; }

    if (kind == 1) {
        // the whole compiler tree has to die together on cancel
        g_buildJob = CreateJobObjectW(0, 0);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_buildJob, JobObjectExtendedLimitInformation, &li, sizeof li);
        AssignProcessToJobObject(g_buildJob, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    Pump* c = new Pump{ rd, pi.hProcess, kind };
    HANDLE t = CreateThread(0, 0, pumpThread, c, 0, 0);
    if (t) CloseHandle(t);
    return true;
}

// ---- messages from the page ---------------------------------------------

static bool built() {
    return exists(g_dir + L"\\game\\index.txt") && exists(g_dir + L"\\recomp\\a2pc.exe");
}

static DWORD WINAPI buildThread(void* p) {
    std::wstring* args = (std::wstring*)p;
    int fail = 0;
    if (g_py.empty() && !installPython()) fail = -3;
    else if (g_cancel) fail = 1;
    else if (!runScript(L"a2pc.py", *args, 1)) { post("log", "[a2pc] ERROR: could not start Python"); fail = -1; }
    delete args;
    if (fail) PostMessageW(g_wnd, WM_PROC_END, 1, (LPARAM)fail);
    return 0;
}

static void sendInit() {
    std::string cfg = readFile(g_dir + L"\\launcher.json");   // sent as text, the page parses it and falls back to defaults if it is damaged
    std::string j = "{\"built\":" + std::string(built() ? "true" : "false") +
        ",\"busy\":" + (g_building ? "true" : "false") +
        ",\"playing\":" + (g_playing ? "true" : "false") +
        ",\"python\":" + jstr(U(g_py)) +
        ",\"cfg\":" + jstr(cfg) + "}";
    post("init", j);
}

static void browseDisc() {
    wchar_t file[MAX_PATH * 2] = L"";
    OPENFILENAMEW o = { sizeof o };
    o.hwndOwner = g_wnd;
    std::wstring filter = T("Disc images", "Образы дисков") + std::wstring(L" (bin, cue, zip, 7z)") + std::wstring(1, L'\0') +
                          L"*.bin;*.cue;*.zip;*.7z;*.img" + std::wstring(1, L'\0') +
                          T("All files", "Все файлы") + std::wstring(1, L'\0') + L"*.*" + std::wstring(2, L'\0');
    o.lpstrFilter = filter.c_str();
    o.lpstrFile = file; o.nMaxFile = MAX_PATH * 2;
    std::wstring title = T("Alundra 2 (USA) disc image", "Образ диска Alundra 2 (USA)");
    o.lpstrTitle = title.c_str();
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&o)) post("disc", U(file));
}

static void openFolder(const std::string& what) {
    std::wstring dir;
    if (what == "saves") {
        dir = g_dir + L"\\saves";
        CreateDirectoryW(dir.c_str(), 0);
    } else if (what == "logs") {
        dir = knownFolder(FOLDERID_Desktop) + L"\\alundra2logs";
        if (!exists(dir)) { post("nologs"); return; }
    } else {
        dir = g_dir;
    }
    ShellExecuteW(0, L"open", dir.c_str(), 0, 0, SW_SHOWNORMAL);
}

// A2Launcher.new.exe is the update waiting to be used. A running exe can be renamed but not overwritten, so the old one steps aside first.
static bool swapExe() {
    std::wstring nw = g_dir + L"\\A2Launcher.new.exe";
    if (!exists(nw)) return false;
    std::string head = readFile(nw);
    if (head.size() < 65536 || head[0] != 'M' || head[1] != 'Z') { DeleteFileW(nw.c_str()); return false; }
    std::wstring old = g_self + L".old";
    DeleteFileW(old.c_str());
    if (!MoveFileExW(g_self.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) return false;
    if (!MoveFileExW(nw.c_str(), g_self.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(old.c_str(), g_self.c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }
    return true;
}

static bool startSelf() {
    std::wstring cmd = L"\"" + g_self + L"\" --restarted " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(0, &cmd[0], 0, 0, FALSE, 0, 0, g_dir.c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
}

static void helper(const std::string& what) {
    if (g_util || g_building || (g_playing && what == "apply")) { post("util", "busy"); return; }
    if (g_py.empty()) { post("util", "done:-1"); return; }
    const wchar_t* script = L"a2update.py"; std::wstring args;
    if (what == "check") args = L"check";
    else if (what == "check-force") args = L"check --force";
    else if (what == "apply") args = L"apply";
    else if (what == "status") { script = L"a2pc.py"; args = L"status"; }
    else if (what == "launcher") { script = L"a2pc.py"; args = L"rebuild-launcher --new"; }
    else return;
    g_util = true; g_applying = what == "apply";
    if (!runScript(script, args, 2)) { g_util = g_applying = false; post("util", "done:-1"); }
}

static void onPage(const std::string& msg) {
    size_t t = msg.find('\t');
    std::string cmd = msg.substr(0, t);
    std::string arg = t == std::string::npos ? "" : msg.substr(t + 1);

    if (cmd == "ready") { g_ready = true; DeleteFileW((g_self + L".old").c_str()); sendInit(); }
    else if (cmd == "save") writeFile(g_dir + L"\\launcher.json", arg);
    else if (cmd == "lang") g_lang = arg == "ru" ? "ru" : "en";
    else if (cmd == "browse") browseDisc();
    else if (cmd == "open") { if (arg == "repo") ShellExecuteW(0, L"open", kRepoUrl, 0, 0, SW_SHOWNORMAL); else openFolder(arg); }
    else if (cmd == "update") helper(arg);
    else if (cmd == "restart") {
        if (g_building || g_playing || g_util) return;
        swapExe();
        if (startSelf()) DestroyWindow(g_wnd);
        else post("util", "restart-failed");
    }
    else if (cmd == "cancel") { if (g_building) { g_cancel = true; if (g_buildJob) TerminateJobObject(g_buildJob, 1); } }
    else if (cmd == "build" || cmd == "rebuild") {
        if (g_building || g_playing || g_applying) return;
        g_building = true; g_cancel = false;
        post("build", "start");
        // "build": arg is the disc image; "rebuild": translate/compile again from the files unpacked earlier
        std::wstring args = cmd == "build" ? L"\"" + W(arg) + L"\"" : std::wstring(L"rebuild") + (arg == "--translate" ? L" --translate" : arg == "--compile" ? L" --compile" : L"");
        HANDLE t = CreateThread(0, 0, buildThread, new std::wstring(args), 0, 0);
        if (t) CloseHandle(t);
    }
    else if (cmd == "play") {
        if (g_building || g_playing || g_applying) return;
        if (!built()) { post("game", "done:-2"); return; }
        if (g_py.empty()) { post("log", "[a2pc] ERROR: Python is missing, build the game again to get it"); post("game", "done:-1"); return; }
        g_playing = true;
        post("game", "start");
        if (runScript(L"a2pc.py", arg == "new" ? L"play --newgame" : L"play", 0)) {
            ShowWindow(g_wnd, SW_MINIMIZE);
        } else {
            g_playing = false;
            post("game", "done:-1");
        }
    }
}

// ---- webview2 -----------------------------------------------------------

template <class I, class... A>
struct Cb final : I {
    std::function<HRESULT(A...)> fn;
    LONG refs = 0;   // the api takes its own reference
    explicit Cb(std::function<HRESULT(A...)> f) : fn(std::move(f)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** o) override {
        if (IsEqualIID(r, IID_IUnknown) || IsEqualIID(r, iidOf<I>())) { *o = this; AddRef(); return S_OK; }
        *o = 0; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG c = InterlockedDecrement(&refs);
        if (!c) delete this;
        return c;
    }
    HRESULT STDMETHODCALLTYPE Invoke(A... a) override { return fn(a...); }
};
template <class I, class... A, class F>
static Cb<I, A...>* cb(F f) { return new Cb<I, A...>(std::function<HRESULT(A...)>(std::move(f))); }

static void fail(const std::wstring& why) {
    PostMessageW(g_wnd, WM_WV_FAIL, 0, (LPARAM)new std::wstring(why));
}

static void paintStatus(const std::wstring& s) {
    PostMessageW(g_wnd, WM_STATUS, 0, (LPARAM)new std::wstring(s));
}

static void attachPage(ICoreWebView2Controller* ctl) {
    g_ctl = ctl; ctl->AddRef();
    ctl->get_CoreWebView2(&g_wv);

    ICoreWebView2Controller2* c2 = 0;
    if (SUCCEEDED(ctl->QueryInterface(iidOf<ICoreWebView2Controller2>(), (void**)&c2))) {
        COREWEBVIEW2_COLOR bg = { 255, 20, 18, 15 };
        c2->put_DefaultBackgroundColor(bg);
        c2->Release();
    }
    RECT rc; GetClientRect(g_wnd, &rc);
    ctl->put_Bounds(rc);

    ICoreWebView2Settings* s = 0;
    if (SUCCEEDED(g_wv->get_Settings(&s))) {
        bool dbg = GetEnvironmentVariableW(L"A2UIDEBUG", 0, 0) != 0;
        s->put_AreDevToolsEnabled(dbg);
        s->put_AreDefaultContextMenusEnabled(dbg);
        s->put_IsStatusBarEnabled(FALSE);
        s->put_IsZoomControlEnabled(FALSE);
        s->put_IsBuiltInErrorPageEnabled(FALSE);
        ICoreWebView2Settings3* s3 = 0;
        if (SUCCEEDED(s->QueryInterface(iidOf<ICoreWebView2Settings3>(), (void**)&s3))) {
            s3->put_AreBrowserAcceleratorKeysEnabled(dbg);   // no F5/ctrl+P etc. in a launcher
            s3->Release();
        }
        s->Release();
    }

    EventRegistrationToken tok;
    g_wv->add_WebMessageReceived(
        cb<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>(
            [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* a) -> HRESULT {
                LPWSTR m = 0;
                if (SUCCEEDED(a->TryGetWebMessageAsString(&m)) && m) {
                    onPage(U(m));
                    CoTaskMemFree(m);
                }
                return S_OK;
            }), &tok);

    // the page must stay the only thing this window ever shows
    g_wv->add_NavigationStarting(
        cb<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>(
            [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* a) -> HRESULT {
                LPWSTR u = 0;
                if (SUCCEEDED(a->get_Uri(&u)) && u) {
                    std::wstring s = u;
                    bool ours = s.compare(0, 8, L"https://") == 0 && s.find(kHost) == 8;
                    if (g_fileMode && s.compare(0, 8, L"file:///") == 0) ours = true;
                    if (!ours) a->put_Cancel(TRUE);
                    CoTaskMemFree(u);
                }
                return S_OK;
            }), &tok);
    g_wv->add_NewWindowRequested(
        cb<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>(
            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* a) -> HRESULT {
                a->put_Handled(TRUE);
                return S_OK;
            }), &tok);

    std::wstring ui = g_dir + L"\\launcher\\ui";
    std::wstring url = std::wstring(L"https://") + kHost + L"/index.html";
    ICoreWebView2_3* w3 = 0;
    if (SUCCEEDED(g_wv->QueryInterface(iidOf<ICoreWebView2_3>(), (void**)&w3))) {
        w3->SetVirtualHostNameToFolderMapping(kHost, ui.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        w3->Release();
        g_wv->Navigate(url.c_str());
    } else {
        // very old runtime without host mapping
        g_fileMode = true;
        std::wstring f = L"file:///" + ui + L"\\index.html";
        for (auto& ch : f) if (ch == L'\\') ch = L'/';
        g_wv->Navigate(f.c_str());
    }
    ctl->put_IsVisible(TRUE);
    ctl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    g_status.clear();
}

static void startWebView() {
    std::wstring data = knownFolder(FOLDERID_LocalAppData) + L"\\Alundra2PC";
    CreateDirectoryW(data.c_str(), 0);
    data += L"\\webview";
    HRESULT hr = g_createEnv(0, data.c_str(), 0,
        cb<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
            [](HRESULT r, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(r) || !env) { fail(L"environment " + std::to_wstring((long)r)); return r; }
                g_env = env; env->AddRef();   // keep it alive until the controller exists
                env->CreateCoreWebView2Controller(g_wnd,
                    cb<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
                        [](HRESULT r2, ICoreWebView2Controller* ctl) -> HRESULT {
                            if (FAILED(r2) || !ctl) { fail(L"controller " + std::to_wstring((long)r2)); return r2; }
                            attachPage(ctl);
                            return S_OK;
                        }));
                return S_OK;
            }));
    if (FAILED(hr)) fail(L"CreateCoreWebView2EnvironmentWithOptions " + std::to_wstring((long)hr));
}

static bool runtimePresent() {
    GetVerFn gv = (GetVerFn)GetProcAddress(g_loader, "GetAvailableCoreWebView2BrowserVersionString");
    if (!gv) return true;   // very old loader, just try
    LPWSTR v = 0;
    HRESULT hr = gv(0, &v);
    bool ok = SUCCEEDED(hr) && v && *v;
    CoTaskMemFree(v);
    return ok;
}

// worker: make sure the evergreen runtime is installed, then tell the ui thread to go on
static DWORD WINAPI prepareThread(void*) {
    findPython();
    std::wstring dll = g_dir + L"\\launcher\\lib\\WebView2Loader.dll";
    g_loader = LoadLibraryW(dll.c_str());
    if (!g_loader) g_loader = LoadLibraryW(L"WebView2Loader.dll");
    if (!g_loader) { fail(L"WebView2Loader.dll not found"); return 0; }
    g_createEnv = (CreateEnvFn)GetProcAddress(g_loader, "CreateCoreWebView2EnvironmentWithOptions");
    if (!g_createEnv) { fail(L"bad WebView2Loader.dll"); return 0; }

    if (!runtimePresent()) {
        wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        std::wstring setup = std::wstring(tmp) + L"MicrosoftEdgeWebview2Setup.exe";
        paintStatus(T("Downloading Microsoft WebView2 (one time, about 2 MB)...",
                      "Скачиваю Microsoft WebView2 (один раз, около 2 МБ)..."));
        DeleteFileW(setup.c_str());
        if (!download(kBootstrapUrl, setup, "WebView2")) { fail(L"download failed"); return 0; }

        paintStatus(T("Installing WebView2. Windows may ask for permission.",
                      "Устанавливаю WebView2. Windows может запросить разрешение."));
        // ShellExecuteEx so that windows can show its permission prompt if the installer wants one
        SHELLEXECUTEINFOW ex = { sizeof ex };
        ex.fMask = SEE_MASK_NOCLOSEPROCESS;
        ex.lpFile = setup.c_str();
        ex.lpParameters = L"/silent /install";
        ex.nShow = SW_HIDE;
        if (!ShellExecuteExW(&ex) || !ex.hProcess) { fail(L"cannot run the WebView2 installer"); return 0; }
        WaitForSingleObject(ex.hProcess, 10 * 60 * 1000);
        CloseHandle(ex.hProcess);
        DeleteFileW(setup.c_str());
        bool ok = false;
        for (int i = 0; i < 15 && !ok; i++) { ok = runtimePresent(); if (!ok) Sleep(1000); }
        if (!ok) { fail(L"WebView2 did not install"); return 0; }
    }
    PostMessageW(g_wnd, WM_WV_INIT, 0, 0);
    return 0;
}

// ---- window -------------------------------------------------------------

static const COLORREF kBg = RGB(20, 18, 15);
static const COLORREF kFg = RGB(233, 226, 211);

static UINT dpiFor(HWND w) {
    typedef UINT (WINAPI *Fn)(HWND);
    static Fn f = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    return f && w ? f(w) : 96;
}

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        BOOL on = TRUE;
        DwmSetWindowAttribute(h, 20, &on, sizeof on);          // dark caption
        COLORREF c = kBg, t = kFg;
        DwmSetWindowAttribute(h, 35, &c, sizeof c);            // win11: caption colour
        DwmSetWindowAttribute(h, 34, &c, sizeof c);            // border
        DwmSetWindowAttribute(h, 36, &t, sizeof t);            // caption text
        return 0;
    }
    case WM_SIZE:
        if (g_ctl) { RECT rc; GetClientRect(h, &rc); g_ctl->put_Bounds(rc); }
        return 0;
    case WM_SETFOCUS:
        if (g_ctl) g_ctl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        return 0;
    case WM_GETMINMAXINFO: {
        UINT d = dpiFor(h);
        MINMAXINFO* mi = (MINMAXINFO*)lp;
        mi->ptMinTrackSize.x = 900 * d / 96;
        mi->ptMinTrackSize.y = 620 * d / 96;
        return 0;
    }
    case WM_DPICHANGED: {
        RECT* r = (RECT*)lp;
        SetWindowPos(h, 0, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_ERASEBKGND: {
        HDC dc = (HDC)wp; RECT rc; GetClientRect(h, &rc);
        HBRUSH b = CreateSolidBrush(kBg);
        FillRect(dc, &rc, b);
        DeleteObject(b);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        if (!g_status.empty()) {
            RECT rc; GetClientRect(h, &rc);
            int px = -18 * (int)dpiFor(h) / 96;
            HFONT f = CreateFontW(px, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            HGDIOBJ old = SelectObject(dc, f);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, kFg);
            DrawTextW(dc, g_status.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, old); DeleteObject(f);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_POSTED: {
        std::wstring* s = (std::wstring*)lp;
        if (g_wv) g_wv->PostWebMessageAsString(s->c_str());
        delete s;
        return 0;
    }
    case WM_STATUS: {
        std::wstring* s = (std::wstring*)lp;
        g_status = *s;
        delete s;
        InvalidateRect(h, 0, TRUE);
        return 0;
    }
    case WM_WV_INIT:
        g_status = T("Starting...", "Запуск...");
        InvalidateRect(h, 0, TRUE);
        startWebView();
        return 0;
    case WM_WV_FAIL: {
        std::wstring* s = (std::wstring*)lp;
        std::wstring text = T(
            "The launcher window could not start (",
            "Окно лаунчера не удалось запустить (") + *s + T(
            ").\n\nThe game can still be built and started from the command line:  py a2pc.py --help",
            ").\n\nИгру всё равно можно собрать и запустить из командной строки:  py a2pc.py --help");
        delete s;
        MessageBoxW(h, text.c_str(), L"Alundra 2 PC", MB_OK | MB_ICONWARNING);
        ExitProcess(3);
    }
    case WM_PROC_END:
        if (wp == 2) {
            g_util = g_applying = false;
            post("util", "done:" + std::to_string((int)lp));
        } else if (wp) {
            g_building = false;
            if (g_buildJob) { CloseHandle(g_buildJob); g_buildJob = 0; }
            post("build", "done:" + std::to_string((int)lp));
        } else {
            g_playing = false;
            if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
            post("game", "done:" + std::to_string((int)lp));
        }
        return 0;
    case WM_CLOSE:
        if (g_applying) {
            std::wstring q = T("An update is being installed. Please wait a few seconds.",
                               "Устанавливается обновление. Подождите несколько секунд.");
            MessageBoxW(h, q.c_str(), L"Alundra 2 PC", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        if (g_building) {
            std::wstring q = T("The game is still being built. Stop it and close the launcher?",
                               "Игра ещё собирается. Остановить сборку и закрыть лаунчер?");
            if (MessageBoxW(h, q.c_str(), L"Alundra 2 PC", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        }
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        if (g_buildJob) TerminateJobObject(g_buildJob, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show) {
    g_inst = inst;

    // started by the previous copy after an update: wait until it has gone, it still holds the single-instance lock
    {
        const wchar_t* cl = GetCommandLineW();
        const wchar_t* r = wcsstr(cl, L"--restarted ");
        if (r) {
            DWORD pid = (DWORD)_wtoi(r + 12);
            HANDLE ph = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : 0;
            if (ph) { WaitForSingleObject(ph, 15000); CloseHandle(ph); }
        }
    }
    HANDLE once = CreateMutexW(0, FALSE, L"Local\\Alundra2PCLauncher");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(L"A2LauncherWnd", 0);
        if (other) {
            if (IsIconic(other)) ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }

    // per-monitor dpi, falls back silently on old windows
    typedef BOOL (WINAPI *DpiCtx)(HANDLE);
    DpiCtx setCtx = (DpiCtx)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
    if (setCtx) setCtx((HANDLE)-4); else SetProcessDPIAware();

    CoInitializeEx(0, COINIT_APARTMENTTHREADED);

    wchar_t path[MAX_PATH * 2];
    GetModuleFileNameW(0, path, MAX_PATH * 2);
    g_self = path;
    g_dir = path;
    g_dir.resize(g_dir.find_last_of(L'\\'));

    // an update that arrived while the launcher was closed: put the new exe in place and start it
    if (!wcsstr(GetCommandLineW(), L"--restarted") && swapExe() && startSelf()) { CloseHandle(once); return 0; }

    std::wstring ico = g_dir + L"\\launcher\\icon.ico";
    HICON big = 0, small = 0;
    if (exists(ico)) {
        big = (HICON)LoadImageW(0, ico.c_str(), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_LOADFROMFILE);
        small = (HICON)LoadImageW(0, ico.c_str(), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);
    }

    WNDCLASSEXW wc = { sizeof wc };
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = 0;
    wc.lpszClassName = L"A2LauncherWnd";
    wc.hIcon = big; wc.hIconSm = small;
    RegisterClassExW(&wc);

    UINT d = 96;
    { HDC sdc = GetDC(0); d = GetDeviceCaps(sdc, LOGPIXELSX); ReleaseDC(0, sdc); }
    int w = 1100 * d / 96, hgt = 720 * d / 96;
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    w = std::min<int>(w, wa.right - wa.left); hgt = std::min<int>(hgt, wa.bottom - wa.top);
    int x = wa.left + (wa.right - wa.left - w) / 2, y = wa.top + (wa.bottom - wa.top - hgt) / 2;

    g_wnd = CreateWindowExW(0, L"A2LauncherWnd", L"Alundra 2 PC", WS_OVERLAPPEDWINDOW, x, y, w, hgt, 0, 0, inst, 0);
    if (!g_wnd) return 1;
    g_status = T("Starting...", "Запуск...");
    ShowWindow(g_wnd, show);
    UpdateWindow(g_wnd);

    HANDLE t = CreateThread(0, 0, prepareThread, 0, 0, 0);
    if (t) CloseHandle(t);

    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CloseHandle(once);
    return (int)msg.wParam;
}
