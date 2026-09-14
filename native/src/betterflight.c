/*
 * BetterFlight.dll - native half of the Better Flight mod for No Man's Sky.
 *
 * Deployed as Binaries/winmm.dll. NMS.exe statically imports winmm for
 * timeBeginPeriod/timeEndPeriod, so this loads before any game code runs.
 * Both exports forward to the real system winmm.
 *
 * Adds 6DOF translation (strafe / vertical thrust) on top of the vanilla flight
 * model by post-hooking cGcSpaceshipComponent::UpdateControlled: after the game
 * has done its own flight update for the frame, read the ship's velocity, add
 * thruster delta-v along the ship's own right/up axes, write it back.
 *
 * Nothing is tied to a specific game build. Functions are found by unique byte
 * signature; struct offsets are parsed out of the matched machine code.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "MinHook.h"
#include "signatures.h"

#define BF_VERSION "1.0.1"

/* ======================================================================== */
/*  winmm proxy                                                             */
/* ======================================================================== */
/*
 * Once this DLL is loaded as "winmm.dll", EVERY module in the process that
 * imports winmm binds to it - not just NMS.exe. Steam's steamclient64.dll uses
 * the waveOut, waveIn and mixer families; overlays and frame-generation DLLs
 * use the timer functions. Windows refuses to start the process if any imported name is
 * missing (Wine only logs it - which is why 1.0.0 worked on Linux and failed to
 * launch on Windows). So we export the complete winmm interface: the union of
 * real Windows 10/11 and Wine exports, generated into winmm_exports.inc.
 *
 * Each export is a one-instruction stub:   jmp [bf_winmm_table + index*8]
 * The table is filled from the system winmm.dll during DLL_PROCESS_ATTACH -
 * before any importing module can run. As a safety net, every slot starts out
 * pointing at a lazy resolver (which fills the table, then continues into the
 * real function) and names the system DLL lacks fall back to a stub returning 0.
 * Nothing ever jumps to address zero.
 */

static HMODULE g_self;

enum {
#define WINMM_EXPORT(idx, name)    WINMM_IDX_##name,
#define WINMM_EXPORT_ORD(idx, ord) WINMM_IDX_ord_##ord,
#include "winmm_exports.inc"
#undef WINMM_EXPORT
#undef WINMM_EXPORT_ORD
    WINMM_COUNT
};

/* GetProcAddress key: a name, or MAKEINTRESOURCE(ordinal) for unnamed exports. */
static const char *const WINMM_KEYS[WINMM_COUNT] = {
#define WINMM_EXPORT(idx, name)    #name,
#define WINMM_EXPORT_ORD(idx, ord) (const char *)(uintptr_t)(ord),
#include "winmm_exports.inc"
#undef WINMM_EXPORT
#undef WINMM_EXPORT_ORD
};

#define WINMM_EXPORT(idx, name)    void bf_lazy_##name(void);
#define WINMM_EXPORT_ORD(idx, ord) void bf_lazy_ord_##ord(void);
#include "winmm_exports.inc"
#undef WINMM_EXPORT
#undef WINMM_EXPORT_ORD

__attribute__((used)) void *bf_winmm_table[WINMM_COUNT] = {
#define WINMM_EXPORT(idx, name)    bf_lazy_##name,
#define WINMM_EXPORT_ORD(idx, ord) bf_lazy_ord_##ord,
#include "winmm_exports.inc"
#undef WINMM_EXPORT
#undef WINMM_EXPORT_ORD
};

static int g_winmm_resolved, g_winmm_missing;

static uint64_t bf_winmm_missing(void) { return 0; }

static BOOL CALLBACK resolve_winmm_once(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    (void)once; (void)param; (void)ctx;
    HMODULE real = NULL;
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n && n < MAX_PATH - 16) {
        wcscat(path, L"\\winmm.dll");
        real = LoadLibraryW(path);
    }
    /* Under Wine with winmm=n (instead of n,b) this resolves to ourselves. */
    if (real == g_self) real = NULL;
    for (int i = 0; i < WINMM_COUNT; i++) {
        void *fn = real ? (void *)GetProcAddress(real, WINMM_KEYS[i]) : NULL;
        if (fn) g_winmm_resolved++;
        else  { g_winmm_missing++; fn = (void *)bf_winmm_missing; }
        bf_winmm_table[i] = fn;
    }
    return TRUE;
}

static INIT_ONCE g_winmm_once = INIT_ONCE_STATIC_INIT;

__attribute__((used)) void bf_winmm_resolve(void)
{
    InitOnceExecuteOnce(&g_winmm_once, resolve_winmm_once, NULL, NULL);
}

/* Export stubs + lazy entry points. */
__asm__(".text\n");
#define BF_STUB(idx, sym)                                         \
    __asm__(".globl bf_x_" sym "\n"                              \
            ".p2align 3\n"                                       \
            "bf_x_" sym ":\n"                                    \
            "    jmpq *bf_winmm_table+" #idx "*8(%rip)\n"        \
            ".globl bf_lazy_" sym "\n"                           \
            "bf_lazy_" sym ":\n"                                 \
            "    movl $" #idx ", %eax\n"                         \
            "    jmp bf_lazy_common\n");
#define WINMM_EXPORT(idx, name)    BF_STUB(idx, #name)
#define WINMM_EXPORT_ORD(idx, ord) BF_STUB(idx, "ord_" #ord)
#include "winmm_exports.inc"
#undef WINMM_EXPORT
#undef WINMM_EXPORT_ORD
#undef BF_STUB

/* Save every argument register (incl. xmm0-3), resolve, restore, and continue
 * into the real function with the caller's stack untouched. On entry rsp is
 * 8 mod 16; five pushes + 0x60 keeps the call to C 16-byte aligned. */
__asm__(".text\n"
        ".seh_proc bf_lazy_common\n"
        "bf_lazy_common:\n"
        "    pushq %rcx\n"   ".seh_pushreg %rcx\n"
        "    pushq %rdx\n"   ".seh_pushreg %rdx\n"
        "    pushq %r8\n"    ".seh_pushreg %r8\n"
        "    pushq %r9\n"    ".seh_pushreg %r9\n"
        "    pushq %rax\n"   ".seh_pushreg %rax\n"
        "    subq $0x60, %rsp\n" ".seh_stackalloc 0x60\n"
        ".seh_endprologue\n"
        "    movdqu %xmm0, 0x20(%rsp)\n"
        "    movdqu %xmm1, 0x30(%rsp)\n"
        "    movdqu %xmm2, 0x40(%rsp)\n"
        "    movdqu %xmm3, 0x50(%rsp)\n"
        "    call bf_winmm_resolve\n"
        "    movdqu 0x20(%rsp), %xmm0\n"
        "    movdqu 0x30(%rsp), %xmm1\n"
        "    movdqu 0x40(%rsp), %xmm2\n"
        "    movdqu 0x50(%rsp), %xmm3\n"
        "    addq $0x60, %rsp\n"
        "    popq %rax\n"
        "    popq %r9\n"
        "    popq %r8\n"
        "    popq %rdx\n"
        "    popq %rcx\n"
        "    leaq bf_winmm_table(%rip), %r11\n"
        "    jmpq *(%r11,%rax,8)\n"
        ".seh_endproc\n");

/* ======================================================================== */
/*  logging                                                                 */
/* ======================================================================== */

static CRITICAL_SECTION g_log_cs;
static FILE *g_log;
static wchar_t g_dir[MAX_PATH];

static void logmsg(const char *fmt, ...)
{
    if (!g_log)
        return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    EnterCriticalSection(&g_log_cs);
    fprintf(g_log, "[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_log_cs);
}

/* ======================================================================== */
/*  config (BetterFlight.ini next to this DLL; same format as controls.ini) */
/* ======================================================================== */

typedef struct { const char *name; int vk; } keyname_t;

static const keyname_t KEYS[] = {
    {"Space", VK_SPACE}, {"Tab", VK_TAB}, {"Enter", VK_RETURN}, {"Escape", VK_ESCAPE},
    {"Backspace", VK_BACK}, {"Insert", VK_INSERT}, {"Delete", VK_DELETE},
    {"Home", VK_HOME}, {"End", VK_END}, {"PageUp", VK_PRIOR}, {"PageDown", VK_NEXT},
    {"Up", VK_UP}, {"Down", VK_DOWN}, {"Left", VK_LEFT}, {"Right", VK_RIGHT},
    {"LShift", VK_LSHIFT}, {"RShift", VK_RSHIFT}, {"Shift", VK_SHIFT},
    {"LCtrl", VK_LCONTROL}, {"RCtrl", VK_RCONTROL}, {"Ctrl", VK_CONTROL},
    {"LAlt", VK_LMENU}, {"RAlt", VK_RMENU}, {"Alt", VK_MENU},
    {"Comma", VK_OEM_COMMA}, {"Period", VK_OEM_PERIOD}, {"Slash", VK_OEM_2},
    {"Semicolon", VK_OEM_1}, {"Apostrophe", VK_OEM_7}, {"Grave", VK_OEM_3},
    {"LeftSquare", VK_OEM_4}, {"RightSquare", VK_OEM_6}, {"BackSlash", VK_OEM_5},
    {"Hyphen", VK_OEM_MINUS}, {"Equals", VK_OEM_PLUS},
    {"Mouse4", VK_XBUTTON1}, {"Mouse5", VK_XBUTTON2},
};

static int key_from_name(const char *s)
{
    if (!s || !*s)
        return 0;
    if (strlen(s) == 4 && strncmp(s, "Key", 3) == 0) {
        char c = s[3];
        if (c >= 'A' && c <= 'Z') return c;
        if (c >= 'a' && c <= 'z') return c - 32;
        if (c >= '0' && c <= '9') return c;
    }
    if ((s[0] == 'F' || s[0] == 'f') && s[1] >= '1' && s[1] <= '9') {
        int n = atoi(s + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
        if (_stricmp(KEYS[i].name, s) == 0)
            return KEYS[i].vk;
    return 0;
}

static struct {
    int strafe_left, strafe_right, strafe_up, strafe_down, toggle;
    float lateral_accel, vertical_accel, max_strafe_speed;
    int invert_lateral, invert_vertical, debug;
} cfg;

static wchar_t g_ini[MAX_PATH];
static FILETIME g_ini_mtime;

static void ini_str(const wchar_t *sec, const wchar_t *key, char *out, size_t cap)
{
    wchar_t w[128];
    GetPrivateProfileStringW(sec, key, L"", w, 128, g_ini);
    for (wchar_t *p = w; *p; p++)                   /* strip inline comment */
        if (*p == L';' || *p == L'#') { *p = 0; break; }
    size_t len = wcslen(w);
    while (len && (w[len - 1] == L' ' || w[len - 1] == L'\t')) w[--len] = 0;
    wchar_t *b = w;
    while (*b == L' ' || *b == L'\t') b++;
    WideCharToMultiByte(CP_UTF8, 0, b, -1, out, (int)cap, NULL, NULL);
}

static int ini_key(const wchar_t *sec, const wchar_t *key, int fallback)
{
    char s[64];
    ini_str(sec, key, s, sizeof s);
    int vk = key_from_name(s);
    if (*s && !vk)
        logmsg("config: unknown key name '%s' for %ls - using default", s, key);
    return vk ? vk : fallback;
}

static float ini_float(const wchar_t *sec, const wchar_t *key, float fallback)
{
    char s[64];
    ini_str(sec, key, s, sizeof s);
    return *s ? (float)atof(s) : fallback;
}

static void load_config(void)
{
    cfg.strafe_left      = ini_key(L"flight", L"StrafeLeft",  'A');
    cfg.strafe_right     = ini_key(L"flight", L"StrafeRight", 'D');
    cfg.strafe_up        = ini_key(L"flight", L"StrafeUp",    VK_SPACE);
    cfg.strafe_down      = ini_key(L"flight", L"StrafeDown",  VK_LCONTROL);
    cfg.toggle           = ini_key(L"flight", L"ToggleKey",   VK_F8);
    cfg.lateral_accel    = ini_float(L"tuning", L"LateralAccel",   70.0f);
    cfg.vertical_accel   = ini_float(L"tuning", L"VerticalAccel",  55.0f);
    cfg.max_strafe_speed = ini_float(L"tuning", L"MaxStrafeSpeed", 140.0f);
    cfg.invert_lateral   = ini_float(L"tuning", L"InvertLateral",  0) != 0;
    cfg.invert_vertical  = ini_float(L"tuning", L"InvertVertical", 0) != 0;
    cfg.debug            = ini_float(L"tuning", L"Debug",          0) != 0;
    logmsg("config: keys L=%#x R=%#x U=%#x D=%#x toggle=%#x | accel lat=%.1f vert=%.1f max=%.1f"
           " | invert lat=%d vert=%d | debug=%d",
           cfg.strafe_left, cfg.strafe_right, cfg.strafe_up, cfg.strafe_down, cfg.toggle,
           cfg.lateral_accel, cfg.vertical_accel, cfg.max_strafe_speed,
           cfg.invert_lateral, cfg.invert_vertical, cfg.debug);
}

static int ini_changed(void)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(g_ini, GetFileExInfoStandard, &a))
        return 0;
    if (CompareFileTime(&a.ftLastWriteTime, &g_ini_mtime) != 0) {
        g_ini_mtime = a.ftLastWriteTime;
        return 1;
    }
    return 0;
}

/* ======================================================================== */
/*  signature scanning                                                      */
/* ======================================================================== */

typedef struct { uint8_t byte[160]; uint8_t mask[160]; int len; } pattern_t;

static int parse_pattern(const char *s, pattern_t *p)
{
    p->len = 0;
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        if (p->len >= (int)sizeof p->byte) return 0;
        if (*s == '?') {
            p->byte[p->len] = 0; p->mask[p->len] = 0;
            while (*s == '?') s++;
        } else {
            char hex[3] = { s[0], s[1], 0 };
            p->byte[p->len] = (uint8_t)strtoul(hex, NULL, 16);
            p->mask[p->len] = 1;
            s += 2;
        }
        p->len++;
    }
    return p->len > 0;
}

static uint8_t *g_image, *g_text;
static size_t g_text_size;

static int find_text_section_in(uint8_t *base)
{
    g_image = base;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (memcmp(sec->Name, ".text", 5) == 0) {
            g_text = base + sec->VirtualAddress;
            g_text_size = sec->Misc.VirtualSize;
            return 1;
        }
    }
    return 0;
}

static int find_text_section(void)
{
    return find_text_section_in((uint8_t *)GetModuleHandleW(NULL));
}

/* Returns the match only if the pattern occurs exactly once. */
static uint8_t *scan_unique(const char *sig, int *count)
{
    pattern_t p;
    *count = 0;
    if (!parse_pattern(sig, &p) || !p.mask[0])
        return NULL;
    uint8_t *hit = NULL;
    uint8_t *cur = g_text, *end = g_text + g_text_size - p.len;
    while (cur < end) {
        cur = memchr(cur, p.byte[0], (size_t)(end - cur));
        if (!cur) break;
        int ok = 1;
        for (int i = 1; i < p.len; i++)
            if (p.mask[i] && cur[i] != p.byte[i]) { ok = 0; break; }
        if (ok) {
            if (++*count > 1) return NULL;
            hit = cur;
        }
        cur++;
    }
    return hit;
}

/* ======================================================================== */
/*  game interface                                                          */
/* ======================================================================== */

typedef uint64_t (__fastcall *fn_update_controlled)(void *ship, float dt);
typedef void *   (__fastcall *fn_get_velocity)(void *ship, float *out);
typedef void     (__fastcall *fn_set_linear_velocity)(void *rigid_body, const float *vel, uint8_t no_sync);
typedef float *  (__fastcall *fn_get_transform)(void *rigid_body, float *out);

static struct {
    fn_update_controlled   update_controlled;
    fn_get_velocity        get_velocity;
    fn_set_linear_velocity set_linear_velocity;
    fn_get_transform       get_transform;
    int32_t off_physics;     /* cGcSpaceshipComponent -> cTkPhysicsComponent*  */
    int32_t off_rigid_body;  /* cTkPhysicsComponent   -> cTkRigidBody (inline)  */
    int32_t off_state;       /* cTkRigidBody -> physics state ptr (-1 = unknown) */
} G;

static fn_update_controlled orig_update_controlled;

static int resolve(int verbose)
{
    struct { const char *name; const char *sig; uint8_t *addr; int n; } s[] = {
        {"UpdateControlled",      SIG_UPDATE_CONTROLLED,      NULL, 0},
        {"GetVelocity",           SIG_GET_VELOCITY,           NULL, 0},
        {"SetLinearVelocity",     SIG_SET_LINEAR_VELOCITY,    NULL, 0},
        {"GetTransform@callsite", SIG_GET_TRANSFORM_CALLSITE, NULL, 0},
        {"GetTransform",          SIG_GET_TRANSFORM,          NULL, 0},
    };
    const size_t N = sizeof s / sizeof s[0];
    for (size_t i = 0; i < N; i++)
        s[i].addr = scan_unique(s[i].sig, &s[i].n);

    /* GetTransform: prefer resolving it from the flight code's own call. */
    uint8_t *xform = NULL;
    if (s[3].addr) {
        int32_t rel;
        memcpy(&rel, s[3].addr + 19, 4);
        xform = s[3].addr + 23 + rel;
        if (s[4].addr && s[4].addr != xform)
            logmsg("  note: GetTransform callsite target %p != function signature %p; using callsite",
                   (void *)xform, (void *)s[4].addr);
    } else if (s[4].addr) {
        xform = s[4].addr;
    }

    int all = s[0].addr && s[1].addr && s[2].addr && xform;
    if (verbose || all)
        for (size_t i = 0; i < N; i++)
            logmsg("  sig %-22s %s  (%d match%s)%s", s[i].name,
                   s[i].addr ? "OK  " : "FAIL", s[i].n, s[i].n == 1 ? "" : "es",
                   i >= 3 ? "  [either is enough]" : "");
    if (!all)
        return 0;

    const uint8_t *gv = s[1].addr;
    /* 40 53 | 48 83 EC xx | 48 8B 89 <disp32> | 48 8B DA | 48 83 C1 <imm8> */
    if (!(gv[6] == 0x48 && gv[7] == 0x8B && gv[8] == 0x89 &&
          gv[16] == 0x48 && gv[17] == 0x83 && gv[18] == 0xC1)) {
        logmsg("GetVelocity layout changed - cannot extract physics offsets");
        return 0;
    }
    G.update_controlled   = (fn_update_controlled)s[0].addr;
    G.get_velocity        = (fn_get_velocity)s[1].addr;
    G.set_linear_velocity = (fn_set_linear_velocity)s[2].addr;
    G.get_transform       = (fn_get_transform)xform;
    memcpy(&G.off_physics, gv + 9, 4);
    G.off_rigid_body = (int8_t)gv[19];

    /* 48 89 74 24 10 | 57 | 48 83 EC 70 | 48 8B B1 <disp32> : rigid body -> state */
    G.off_state = -1;
    if (xform[10] == 0x48 && xform[11] == 0x8B && xform[12] == 0xB1)
        memcpy(&G.off_state, xform + 13, 4);

    /* The callsite also encodes the physics/rigid-body offsets: cross-check. */
    if (s[3].addr) {
        int32_t cs_phys; memcpy(&cs_phys, s[3].addr + 3, 4);
        int8_t cs_rb = (int8_t)s[3].addr[17];
        if (cs_phys != G.off_physics || cs_rb != G.off_rigid_body)
            logmsg("  WARNING: offset mismatch  GetVelocity(%#x,%#x) vs flight callsite(%#x,%#x)",
                   G.off_physics, G.off_rigid_body, cs_phys, cs_rb);
    }

    uint8_t *base = g_image;
    logmsg("resolved: UpdateControlled=+%#llx GetVelocity=+%#llx SetLinearVelocity=+%#llx GetTransform=+%#llx",
           (unsigned long long)(s[0].addr - base), (unsigned long long)(s[1].addr - base),
           (unsigned long long)(s[2].addr - base), (unsigned long long)(xform - base));
    logmsg("offsets:  ship->physics=%#x  physics->rigidbody=%#x  rigidbody->state=%#x",
           G.off_physics, G.off_rigid_body, G.off_state);
    return 1;
}

/* ======================================================================== */
/*  flight                                                                  */
/* ======================================================================== */

static int g_enabled = 1;
static int g_toggle_was_down;
static ULONGLONG g_next_ini_check, g_next_diag, g_next_warn;
static unsigned g_skipped;
static float g_last_ix = 0, g_last_iy = 0;
static int g_logged_first_apply;

static inline float dot3(const float *a, const float *b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

static int normalize3(float *v)
{
    float l = sqrtf(dot3(v, v));
    if (!(l > 1e-4f)) return 0;
    v[0] /= l; v[1] /= l; v[2] /= l;
    return 1;
}

static inline int down(int vk) { return vk && (GetAsyncKeyState(vk) & 0x8000); }

static int game_focused(void)
{
    HWND fg = GetForegroundWindow();
    if (!fg) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

static int plausible(void *p)
{
    uintptr_t v = (uintptr_t)p;
    return v >= 0x10000 && v < 0x00007FFFFFFFFFFFull;
}

/* Rate-limited warning: says what failed and how many frames it has cost. */
static void warn(ULONGLONG now, const char *fmt, ...)
{
    g_skipped++;
    if (now < g_next_warn) return;
    g_next_warn = now + 5000;
    char msg[256];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    logmsg("SKIP (%u frames so far): %s", g_skipped, msg);
}

/* Delta-v along one axis, stopping at the speed cap in the pushed direction. */
static float thrust(float input, float current, float accel, float dt, float cap)
{
    if (input == 0) return 0;
    float dir = input > 0 ? 1.0f : -1.0f;
    float room = cap - current * dir;
    if (room <= 0) return 0;
    float dv = accel * dt;
    return dir * (dv < room ? dv : room);
}

static void flight_tick(void *ship, float dt)
{
    ULONGLONG now = GetTickCount64();

    if (now >= g_next_ini_check) {
        g_next_ini_check = now + 1000;
        if (ini_changed()) { logmsg("config changed on disk - reloading"); load_config(); }
    }

    if (!game_focused())
        return;

    int tdown = down(cfg.toggle);
    if (tdown && !g_toggle_was_down) {
        g_enabled = !g_enabled;
        logmsg("strafe %s (toggle key)", g_enabled ? "ENABLED" : "DISABLED");
    }
    g_toggle_was_down = tdown;

    float ix = (float)(down(cfg.strafe_right) - down(cfg.strafe_left));
    float iy = (float)(down(cfg.strafe_up)    - down(cfg.strafe_down));
    if (cfg.debug && (ix != g_last_ix || iy != g_last_iy))
        logmsg("input: lateral=%+.0f vertical=%+.0f%s", ix, iy, g_enabled ? "" : "  (strafe disabled)");
    g_last_ix = ix; g_last_iy = iy;

    int diag = cfg.debug && now >= g_next_diag;
    if (ix == 0 && iy == 0 && !diag)
        return;
    if (diag) g_next_diag = now + 1000;
    if (!(dt > 0.0f && dt < 0.25f)) {
        warn(now, "implausible frame time dt=%f", dt);
        return;
    }

    void *physics = *(void **)((uint8_t *)ship + G.off_physics);
    if (!plausible(physics)) { warn(now, "ship physics pointer %p", physics); return; }
    void *rigid_body = (uint8_t *)physics + G.off_rigid_body;
    if (G.off_state >= 0) {
        void *state = *(void **)((uint8_t *)rigid_body + G.off_state);
        if (!plausible(state)) { warn(now, "rigid body has no physics state (%p)", state); return; }
    }

    _Alignas(16) float buf[32] = { 0 };
    float *t = G.get_transform(rigid_body, buf);
    if (!plausible(t)) t = buf;
    /* Row 0 of the rigid-body transform points to the ship's LEFT (confirmed
     * in game: strafe was mirrored). Negate it so "right" means right. */
    float right[3] = { -t[0], -t[1], -t[2] };
    float up[3]    = { t[4], t[5], t[6] };
    float at[3]    = { t[8], t[9], t[10] };
    if (!normalize3(right) || !normalize3(up) || !normalize3(at) ||
        fabsf(dot3(right, up)) > 0.2f || fabsf(dot3(right, at)) > 0.2f) {
        warn(now, "ship transform not a rotation: r=(%.2f %.2f %.2f) u=(%.2f %.2f %.2f) a=(%.2f %.2f %.2f)",
             t[0], t[1], t[2], t[4], t[5], t[6], t[8], t[9], t[10]);
        return;
    }

    _Alignas(16) float v[4] = { 0 };
    G.get_velocity(ship, v);
    float vr = dot3(v, right), vu = dot3(v, up), vf = dot3(v, at);

    if (diag)
        logmsg("diag: |v|=%6.1f  right=%7.1f up=%7.1f at=%7.1f  input x=%+.0f y=%+.0f  dt=%.4f%s",
               sqrtf(dot3(v, v)), vr, vu, vf, ix, iy, dt, g_enabled ? "" : "  (disabled)");
    if (!g_enabled || (ix == 0 && iy == 0))
        return;

    if (cfg.invert_lateral)  ix = -ix;
    if (cfg.invert_vertical) iy = -iy;
    float dr = thrust(ix, vr, cfg.lateral_accel,  dt, cfg.max_strafe_speed);
    float du = thrust(iy, vu, cfg.vertical_accel, dt, cfg.max_strafe_speed);
    if (dr == 0 && du == 0)
        return;

    for (int i = 0; i < 3; i++)
        v[i] += right[i] * dr + up[i] * du;
    G.set_linear_velocity(rigid_body, v, 0);

    if (!g_logged_first_apply) {
        g_logged_first_apply = 1;
        logmsg("first strafe applied: dv right=%+.3f up=%+.3f (velocity before: right=%.1f up=%.1f)",
               dr, du, vr, vu);
    }
}

static uint64_t __fastcall hook_update_controlled(void *ship, float dt)
{
    uint64_t r = orig_update_controlled(ship, dt);
    flight_tick(ship, dt);
    return r;
}

/* ======================================================================== */
/*  startup                                                                 */
/* ======================================================================== */

static int host_is_nms(void)
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *name = wcsrchr(exe, L'\\');
    name = name ? name + 1 : exe;
    return _wcsicmp(name, L"NMS.exe") == 0;
}

static DWORD WINAPI init_thread(LPVOID unused)
{
    (void)unused;
    wchar_t logpath[MAX_PATH];
    swprintf(logpath, MAX_PATH, L"%lsBetterFlight.log", g_dir);
    g_log = _wfopen(logpath, L"w");

    const char *(*wine_get_version)(void) =
        (void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version");
    logmsg("BetterFlight %s loaded into NMS.exe (%s)", BF_VERSION,
           wine_get_version ? wine_get_version() : "native Windows");
    bf_winmm_resolve();
    logmsg("winmm proxy: %d exports forwarded to the system winmm.dll, %d not present there",
           g_winmm_resolved, g_winmm_missing);

    swprintf(g_ini, MAX_PATH, L"%lsBetterFlight.ini", g_dir);
    if (GetFileAttributesW(g_ini) == INVALID_FILE_ATTRIBUTES)
        logmsg("config: %ls not found - using built-in defaults", g_ini);
    ini_changed();
    load_config();

    if (!find_text_section()) {
        logmsg("no .text section in host - disabled");
        return 0;
    }

    /* Retry for a while: the Steam DRM stub may still be unpacking. */
    const int tries = 90;
    for (int i = 0; i < tries; i++) {
        if (resolve(i == 0 || i == tries - 1)) {
            if (MH_Initialize() != MH_OK ||
                MH_CreateHook(G.update_controlled, hook_update_controlled,
                              (LPVOID *)&orig_update_controlled) != MH_OK ||
                MH_EnableHook(G.update_controlled) != MH_OK) {
                logmsg("MinHook failed to install the UpdateControlled hook - disabled");
                return 0;
            }
            logmsg("hook installed. Strafe: fly a ship and use the keys above. Toggle with the toggle key.");
            return 0;
        }
        Sleep(1000);
    }
    logmsg("signatures never resolved - game build changed? Strafe disabled; the game runs normally.");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
#ifndef BF_TEST_LAZY_WINMM
        /* Fill the winmm forwarding table now: modules importing winmm are
         * initialized after us, so nothing can call a stub before this. */
        bf_winmm_resolve();
#endif
        if (!host_is_nms())
            return TRUE;                     /* be a plain winmm proxy anywhere else */
        InitializeCriticalSection(&g_log_cs);
        GetModuleFileNameW(inst, g_dir, MAX_PATH);
        wchar_t *slash = wcsrchr(g_dir, L'\\');
        if (slash) slash[1] = 0;
        HANDLE t = CreateThread(NULL, 0, init_thread, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}

#ifdef BF_SELFTEST
/* Offline check: map a real NMS.exe as an image (never executed) and run the
 * exact signature + offset extraction the DLL uses in-game. */
int wmain(int argc, wchar_t **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: selftest.exe <path-to-NMS.exe>\n"); return 2; }
    InitializeCriticalSection(&g_log_cs);
    g_log = stdout;
    HMODULE img = LoadLibraryExW(argv[1], NULL, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!img) { logmsg("could not map %ls (err %lu)", argv[1], GetLastError()); return 2; }
    uint8_t *base = (uint8_t *)((ULONG_PTR)img & ~(ULONG_PTR)3);
    if (!find_text_section_in(base)) { logmsg("no .text"); return 1; }
    logmsg("mapped %ls  .text %zu bytes", argv[1], g_text_size);
    int ok = resolve(1);
    logmsg("SELFTEST %s", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
#endif
