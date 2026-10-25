// W3MultislotQuickSave.mix - rotating quick saves and a quick load key for Warcraft III 1.26a, 1.27b and 1.28.5
// (Game.dll builds 6401, 7085 and 7680)
// Build: i686-w64-mingw32-gcc -O2 -Wall -shared -static-libgcc -s -o W3MultislotQuickSave.mix W3MultislotQuickSave.c -lversion
//
// The game's own quick save (F6) always writes the same file. Here it writes QuickSave_1 .. QuickSave_N in turn,
// and a quick load key loads the newest of them. Both go through the game's own code: the quick save is the game's
// (with its own checks: single player only etc.), and the quick load is the game's unused quick load routine,
// rebuilt here to take the file name.
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef uint32_t u32;
static u32 g_base;
static FILE* g_log;
static int g_slots = 10, g_saveKey = VK_F6, g_loadKey = VK_F8, g_debug;
static HHOOK g_hook;

// addresses (RVAs) per Game.dll build
typedef struct {
    u32 build;
    u32 quickSaveCall;   // F6 handler: call QuickSave
    u32 quickSave;
    u32 saveGameFile;    // fastcall(name): save the running game as name (relative, ".w3z")
    u32 gameObj;         // the running game
    u32 checkA, checkB;  // checks the unused quick load does before loading
    u32 baseDir;         // fastcall(buf, size): the game's folder
    u32 savePath;        // fastcall(name, out, size): name -> path of the save, relative to it
    u32 fileExists;      // fastcall(path)
    u32 loadSave;        // thiscall(game, path, a, b): load a saved game
    int nativeLoad;      // load as the game's LoadGame does: relative path, (path, 0, 1); else as its quick load
} Addrs;
static const Addrs kAddrs[] = {
    { 6401, 0x2FDF7D, 0x338140, 0x43FAD0, 0xAB65F4, 0x53E670, 0x53F160, 0x6C6000, 0x5BCB10, 0x6D7DE0, 0x3A4C70, 0 },  // 1.26a
    { 7085, 0x36FCEE, 0x3C5A70, 0x270520, 0xD687A8, 0x32C080, 0x32C110, 0x091C20, 0x2BFC90, 0x093AD0, 0x1E7630, 1 },  // 1.27b
    { 7680, 0x3A3DAE, 0x3F9B80, 0x2A29F0, 0xD305E0, 0x35FD40, 0x35FDD0, 0x0BB6B0, 0x2F2490, 0x0BD970, 0x219F90, 1 },  // 1.28.5
};
static const Addrs* A;
#define RVA_QuickSaveCall (A->quickSaveCall)
#define RVA_QuickSave     (A->quickSave)
#define RVA_SaveGameFile  (A->saveGameFile)
#define RVA_GameObj       (A->gameObj)
#define RVA_CheckA        (A->checkA)
#define RVA_CheckB        (A->checkB)
#define RVA_BaseDir       (A->baseDir)
#define RVA_SavePath      (A->savePath)
#define RVA_FileExists    (A->fileExists)
#define RVA_LoadSave      (A->loadSave)

static void logf_(const char* f, ...)
{
    if (!g_log) return;
    va_list a; va_start(a, f); vfprintf(g_log, f, a); va_end(a);
    fputc('\n', g_log); fflush(g_log);
}

static int WriteMem(u32 addr, const void* p, size_t n)
{
    DWORD old;
    if (!VirtualProtect((void*)addr, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy((void*)addr, p, n);
    VirtualProtect((void*)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, n);
    return 1;
}

static u32 GetGameBuild(void)
{
    DWORD h; DWORD sz = GetFileVersionInfoSizeA("Game.dll", &h);
    if (!sz) return 0;
    char* buf = (char*)malloc(sz); u32 build = 0;
    if (GetFileVersionInfoA("Game.dll", h, sz, buf)) {
        VS_FIXEDFILEINFO* vi; UINT l;
        if (VerQueryValueA(buf, "\\", (LPVOID*)&vi, &l)) build = vi->dwFileVersionLS & 0xFFFF;
    }
    free(buf);
    return build;
}

typedef void (__attribute__((fastcall)) *SaveGameFile_t)(const char* name);
typedef int (*Check_t)(void);
typedef void (__attribute__((fastcall)) *BaseDir_t)(char* buf, int size);
typedef void (__attribute__((fastcall)) *SavePath_t)(const char* name, char* out, int size);
typedef int (__attribute__((fastcall)) *FileExists_t)(const char* path);
typedef void (__attribute__((fastcall)) *LoadSave_t)(void* game, void* unused, const char* path, int a, int b);
#define FN(t, rva) ((t)(g_base + (rva)))

static void SlotName(int i, char* out, size_t n) { snprintf(out, n, "QuickSave_%d.w3z", i); }

// full path of a save, as the game builds it
static void FullPath(const char* name, char* out, size_t n)
{
    char dir[MAX_PATH] = "", rel[MAX_PATH] = "";
    FN(BaseDir_t, RVA_BaseDir)(dir, MAX_PATH);
    FN(SavePath_t, RVA_SavePath)(name, rel, MAX_PATH);
    snprintf(out, n, "%s%s", dir, rel);
}

// write time of slot i, 0 if there is no such save
static ULONGLONG SlotTime(int i)
{
    char name[64], path[MAX_PATH]; WIN32_FILE_ATTRIBUTE_DATA fa;
    SlotName(i, name, sizeof name); FullPath(name, path, sizeof path);
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) return 0;
    return ((ULONGLONG)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
}

static int NewestSlot(void)
{
    int best = 0; ULONGLONG bt = 0;
    for (int i = 1; i <= g_slots; i++) { ULONGLONG t = SlotTime(i); if (t > bt) { bt = t; best = i; } }
    return best;
}

// replaces the game's quick save (called from its F6 handler, after the handler's own checks)
static void QuickSave(void)
{
    int n = NewestSlot();
    int next = n % g_slots + 1;
    char name[64]; SlotName(next, name, sizeof name);
    logf_("quick save -> %s", name);
    FN(SaveGameFile_t, RVA_SaveGameFile)(name);
}

static void QuickLoad(void)
{
    void* game = *(void**)(g_base + RVA_GameObj);
    if (!game) return;                                       // not in a game
    if (FN(Check_t, RVA_CheckA)() || FN(Check_t, RVA_CheckB)()) { logf_("quick load: not here"); return; }
    int n = NewestSlot();
    char name[64], path[MAX_PATH];
    if (n) SlotName(n, name, sizeof name); else snprintf(name, sizeof name, "QuickSave.w3z");   // the game's own one
    FullPath(name, path, sizeof path);
    if (!FN(FileExists_t, RVA_FileExists)(path)) { logf_("quick load: no %s", path); return; }
    if (A->nativeLoad) {
        char rel[MAX_PATH] = "";
        FN(SavePath_t, RVA_SavePath)(name, rel, MAX_PATH);
        logf_("quick load <- %s", rel);
        FN(LoadSave_t, RVA_LoadSave)(game, NULL, rel, 0, 1);
    } else {
        logf_("quick load <- %s", path);
        FN(LoadSave_t, RVA_LoadSave)(game, NULL, path, 0, 0);
    }
}

// keys arrive as window messages on the game's thread
static LRESULT CALLBACK GetMsgHook(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && wp == PM_REMOVE) {
        MSG* m = (MSG*)lp;
        if ((m->message == WM_KEYDOWN || m->message == WM_KEYUP) && !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
            if ((int)m->wParam == g_loadKey) {
                if (m->message == WM_KEYDOWN && !(m->lParam & (1 << 30))) QuickLoad();
                m->message = WM_NULL;                        // the key does nothing else
            } else if ((int)m->wParam == g_saveKey && g_saveKey != VK_F6) {
                m->wParam = VK_F6;                           // the game's quick save, with its own checks
                m->lParam = (m->lParam & ~(0xFF << 16)) | (MapVirtualKeyA(VK_F6, MAPVK_VK_TO_VSC) << 16);
            }
        }
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

static int ParseKey(const char* s, int def)
{
    while (*s == ' ') s++;
    if (!*s) return def;
    if ((s[0] == 'F' || s[0] == 'f') && s[1] >= '1' && s[1] <= '9') { int n = atoi(s + 1); if (n >= 1 && n <= 24) return VK_F1 + n - 1; }
    if (!s[1] && ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= '0' && s[0] <= '9'))) return s[0];
    if (!s[1] && s[0] >= 'a' && s[0] <= 'z') return s[0] - 32;
    if (!_strnicmp(s, "0x", 2)) return (int)strtol(s, NULL, 16);
    return def;
}

static void Install(HINSTANCE self)
{
    char dir[MAX_PATH], path[MAX_PATH], v[32];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    char* sl = strrchr(dir, '\\'); if (sl) *(sl + 1) = 0;
    snprintf(path, sizeof path, "%sW3MultislotQuickSave.ini", dir);
    g_slots = GetPrivateProfileIntA("W3MultislotQuickSave", "Slots", 10, path);
    if (g_slots < 1) g_slots = 1;
    if (g_slots > 99) g_slots = 99;
    GetPrivateProfileStringA("W3MultislotQuickSave", "QuickSaveKey", "F6", v, sizeof v, path); g_saveKey = ParseKey(v, VK_F6);
    GetPrivateProfileStringA("W3MultislotQuickSave", "QuickLoadKey", "F8", v, sizeof v, path); g_loadKey = ParseKey(v, VK_F8);
    g_debug = GetPrivateProfileIntA("W3MultislotQuickSave", "Debug", 0, path);
    if (g_debug) { snprintf(path, sizeof path, "%sW3MultislotQuickSave.log", dir); g_log = fopen(path, "w"); }

    g_base = (u32)GetModuleHandleA("Game.dll");
    u32 build = GetGameBuild();
    logf_("W3MultislotQuickSave 1.2  Game.dll build %u, %d slots, save key %02X, load key %02X", build, g_slots, g_saveKey, g_loadKey);
    for (size_t i = 0; i < sizeof kAddrs / sizeof kAddrs[0]; i++) if (kAddrs[i].build == build) A = &kAddrs[i];
    if (!g_base || !A) { logf_("unsupported game version, doing nothing (need 1.26a / 6401, 1.27b / 7085 or 1.28.5 / 7680)"); return; }

    uint8_t* c = (uint8_t*)(g_base + RVA_QuickSaveCall);
    if (c[0] != 0xE8 || g_base + RVA_QuickSaveCall + 5 + *(int32_t*)(c + 1) != g_base + RVA_QuickSave) { logf_("quick save call not found"); return; }
    uint8_t j[5]; j[0] = 0xE8; *(int32_t*)(j + 1) = (int32_t)((u32)QuickSave - (g_base + RVA_QuickSaveCall + 5));
    if (!WriteMem(g_base + RVA_QuickSaveCall, j, 5)) { logf_("could not patch the quick save"); return; }
    g_hook = SetWindowsHookExA(WH_GETMESSAGE, GetMsgHook, self, GetCurrentThreadId());
    logf_(g_hook ? "active" : "key hook failed (%u)", GetLastError());
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        // the Miles sound library loads the .mix files and unloads / reloads them when the sound provider changes
        // (e.g. EAX switched on); the patches in Game.dll point into this module, so it must stay loaded
        { HMODULE self; GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCSTR)DllMain, &self); }
        Install(h);
    }
    if (reason == DLL_PROCESS_DETACH && g_hook) UnhookWindowsHookEx(g_hook);
    return TRUE;
}
