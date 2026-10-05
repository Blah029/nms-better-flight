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
/* Types only: XInput and DirectInput are loaded at runtime (controller
 * diagnostics), so the import table doesn't change. */
#define DIRECTINPUT_VERSION 0x0800
#define COBJMACROS
#include <dinput.h>
#include <xinput.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "MinHook.h"
#include "signatures.h"

#define BF_VERSION "1.3.4"

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
    {"Mouse1", VK_LBUTTON}, {"Mouse2", VK_RBUTTON}, {"Mouse3", VK_MBUTTON},
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

/* An action can be bound to several inputs: "KeyA, Mouse4". */
#define MAX_BINDS 4
typedef struct { int vk[MAX_BINDS]; int n; } keyset_t;

static struct {
    keyset_t strafe_left, strafe_right, strafe_up, strafe_down, toggle, decouple;
    keyset_t thrust, brake, boost, autopilot_keys;
    float lateral_accel, vertical_accel, max_strafe_speed;
    float autopilot_accel, low_speed_autopilot_accel, autopilot_seconds, impact_accel;
    float drift_cap, momentum_min_speed;
    int world_momentum;
    int invert_lateral, invert_vertical, debug;
    int flight_retune;
    int input_diag;
} cfg;

static wchar_t g_ini[MAX_PATH];
static FILETIME g_ini_mtime;

static void ini_str(const wchar_t *sec, const wchar_t *key, char *out, size_t cap)
{
    wchar_t w[160];
    GetPrivateProfileStringW(sec, key, L"", w, 160, g_ini);
    for (wchar_t *p = w; *p; p++)                   /* strip inline comment */
        if (*p == L';' || *p == L'#') { *p = 0; break; }
    size_t len = wcslen(w);
    while (len && (w[len - 1] == L' ' || w[len - 1] == L'\t')) w[--len] = 0;
    wchar_t *b = w;
    while (*b == L' ' || *b == L'\t') b++;
    WideCharToMultiByte(CP_UTF8, 0, b, -1, out, (int)cap, NULL, NULL);
}

/* Parse "KeyA, Mouse4" (commas and/or spaces). Unknown names are skipped. */
static int parse_keys(const char *s, keyset_t *ks, const wchar_t *key_for_log)
{
    ks->n = 0;
    char tok[32];
    while (*s) {
        while (*s == ',' || *s == ' ' || *s == '\t') s++;
        if (!*s) break;
        size_t n = 0;
        while (s[n] && s[n] != ',' && s[n] != ' ' && s[n] != '\t') n++;
        if (n >= sizeof tok) n = sizeof tok - 1;
        memcpy(tok, s, n); tok[n] = 0;
        s += n;
        int vk = key_from_name(tok);
        if (vk && ks->n < MAX_BINDS) ks->vk[ks->n++] = vk;
        else if (!vk && key_for_log) logmsg("config: unknown key name '%s' for %ls - ignored", tok, key_for_log);
    }
    return ks->n;
}

static keyset_t ini_keys(const wchar_t *sec, const wchar_t *key, int fallback)
{
    char s[128];
    ini_str(sec, key, s, sizeof s);
    keyset_t ks = { {0}, 0 };
    if (!parse_keys(s, &ks, key) && fallback) { ks.vk[0] = fallback; ks.n = 1; }
    return ks;
}

static const char *fmt_keys(const keyset_t *k, char *out, size_t cap)
{
    out[0] = 0;
    for (int i = 0; i < k->n; i++) {
        size_t l = strlen(out);
        snprintf(out + l, cap - l, "%s%#x", i ? "+" : "", k->vk[i]);
    }
    if (!k->n) snprintf(out, cap, "none");
    return out;
}

static float ini_float(const wchar_t *sec, const wchar_t *key, float fallback)
{
    char s[64];
    ini_str(sec, key, s, sizeof s);
    return *s ? (float)atof(s) : fallback;
}

static void load_config(void)
{
    cfg.strafe_left      = ini_keys(L"flight",  L"StrafeLeft",  'A');
    cfg.strafe_right     = ini_keys(L"flight",  L"StrafeRight", 'D');
    cfg.strafe_up        = ini_keys(L"flight",  L"StrafeUp",    VK_SPACE);
    cfg.strafe_down      = ini_keys(L"flight",  L"StrafeDown",  VK_LCONTROL);
    cfg.decouple         = ini_keys(L"flight",  L"DecoupleKey", 'Z');
    cfg.toggle           = ini_keys(L"flight",  L"ToggleKey",   VK_F8);
    cfg.thrust           = ini_keys(L"coupled", L"ThrustKey",   'W');
    cfg.brake            = ini_keys(L"coupled", L"BrakeKey",    'S');
    cfg.boost            = ini_keys(L"coupled", L"BoostKey",    VK_LSHIFT);
    cfg.autopilot_keys   = ini_keys(L"coupled", L"AutopilotKeys", 0);
    cfg.autopilot_seconds         = ini_float(L"coupled", L"AutopilotSeconds",       6.0f);
    cfg.autopilot_accel           = ini_float(L"coupled", L"AutopilotAccel",         300.0f);
    cfg.low_speed_autopilot_accel = ini_float(L"coupled", L"LowSpeedAutopilotAccel", 20.0f);
    cfg.impact_accel              = ini_float(L"coupled", L"ImpactAccel",            6000.0f);
    cfg.lateral_accel    = ini_float(L"tuning", L"LateralAccel",   70.0f);
    cfg.vertical_accel   = ini_float(L"tuning", L"VerticalAccel",  55.0f);
    cfg.max_strafe_speed = ini_float(L"tuning", L"MaxStrafeSpeed", 140.0f);
    cfg.drift_cap          = ini_float(L"tuning", L"DriftSpeedCap",    3500.0f);
    cfg.momentum_min_speed = ini_float(L"tuning", L"MomentumMinSpeed", 30.0f);
    cfg.world_momentum     = ini_float(L"tuning", L"WorldMomentum",    1) != 0;
    cfg.invert_lateral   = ini_float(L"tuning", L"InvertLateral",  0) != 0;
    cfg.invert_vertical  = ini_float(L"tuning", L"InvertVertical", 0) != 0;
    cfg.debug            = ini_float(L"tuning", L"Debug",          0) != 0;
    cfg.flight_retune    = ini_float(L"tuning", L"FlightRetune",   1) != 0;
    cfg.input_diag       = ini_float(L"input",  L"Diagnostics",    0) != 0;
    char k1[40], k2[40], k3[40], k4[40], k5[40], k6[40], k7[40], k8[40], k9[40], k10[40];
    logmsg("config: strafe L=%s R=%s U=%s D=%s decouple=%s toggle=%s | coupled throttle keys thrust=%s brake=%s boost=%s | "
           "hand-off keys=%s %.0fs, fwd push>%.0f, impact>%.0f, >%.0f below momentum speed | "
           "accel lat=%.1f vert=%.1f max=%.1f | world momentum=%d (decoupled, above %.0f m/s) drift cap=%.0f "
           "| invert lat=%d vert=%d | debug=%d",
           fmt_keys(&cfg.strafe_left, k1, 40), fmt_keys(&cfg.strafe_right, k2, 40),
           fmt_keys(&cfg.strafe_up, k3, 40), fmt_keys(&cfg.strafe_down, k4, 40),
           fmt_keys(&cfg.decouple, k5, 40), fmt_keys(&cfg.toggle, k6, 40),
           fmt_keys(&cfg.thrust, k7, 40), fmt_keys(&cfg.brake, k8, 40), fmt_keys(&cfg.boost, k9, 40),
           fmt_keys(&cfg.autopilot_keys, k10, 40), cfg.autopilot_seconds,
           cfg.autopilot_accel, cfg.impact_accel, cfg.low_speed_autopilot_accel,
           cfg.lateral_accel, cfg.vertical_accel, cfg.max_strafe_speed,
           cfg.world_momentum, cfg.momentum_min_speed, cfg.drift_cap,
           cfg.invert_lateral, cfg.invert_vertical, cfg.debug);
    logmsg("config: flight retune=%d | controller diagnostics=%d", cfg.flight_retune, cfg.input_diag);
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
    int32_t off_controller;         /* cGcSpaceshipComponent -> controller handle (-1 = unknown) */
    int32_t off_controller_active;  /* cGcSpaceshipComponent -> controller-active flag (byte)     */
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

    /* Pilot check (see signatures.h). Optional: without it the mod behaves as
     * before, including on a corvette nobody is flying. Either site is enough;
     * if both are found they must agree. */
    G.off_controller = G.off_controller_active = -1;
    {
        int na, nb;
        uint8_t *a = scan_unique(SIG_PILOT_CHECK, &na), *b = scan_unique(SIG_PILOT_BRANCH, &nb);
        int32_t ca = 0, fa = 0, cb = 0, fb = 0;
        if (a) { memcpy(&ca, a + 3, 4); memcpy(&fa, a + 18, 4); }
        if (b) {
            int32_t rel; memcpy(&rel, b + 34, 4);
            memcpy(&cb, b + 3, 4); memcpy(&fb, b + 20, 4);
            if (b + 38 + rel != s[0].addr) {
                logmsg("  note: PilotBranch does not call UpdateControlled - ignored");
                b = NULL;
            }
        }
        logmsg("  sig %-22s %s  (%d match%s)  [either is enough]", "PilotCheck",
               a ? "OK  " : "FAIL", na, na == 1 ? "" : "es");
        logmsg("  sig %-22s %s  (%d match%s)  [either is enough]", "PilotBranch",
               b ? "OK  " : "FAIL", nb, nb == 1 ? "" : "es");
        if (a && b && (ca != cb || fa != fb))
            logmsg("  WARNING: pilot-check sites disagree (%#x,%#x) vs (%#x,%#x)", ca, fa, cb, fb);
        else if (a || b) {
            G.off_controller        = a ? ca : cb;
            G.off_controller_active = a ? fa : fb;
        }
        if (G.off_controller < 0)
            logmsg("  pilot check unavailable - strafe/coupled will also act on a corvette nobody is flying");
    }

    uint8_t *base = g_image;
    logmsg("resolved: UpdateControlled=+%#llx GetVelocity=+%#llx SetLinearVelocity=+%#llx GetTransform=+%#llx",
           (unsigned long long)(s[0].addr - base), (unsigned long long)(s[1].addr - base),
           (unsigned long long)(s[2].addr - base), (unsigned long long)(xform - base));
    logmsg("offsets:  ship->physics=%#x  physics->rigidbody=%#x  rigidbody->state=%#x  ship->controller=%#x active=%#x",
           G.off_physics, G.off_rigid_body, G.off_state, G.off_controller, G.off_controller_active);
    return 1;
}

/* ======================================================================== */
/*  flight                                                                  */
/* ======================================================================== */

static int g_enabled = 1;       /* ToggleKey: every native flight feature on/off */
static int g_decoupled = 0;     /* DecoupleKey */
#ifdef BF_SIMTEST
static double sim_ms;                           /* simulated clock */
static ULONGLONG bf_ticks(void) { return (ULONGLONG)sim_ms; }
#else
static ULONGLONG bf_ticks(void) { return GetTickCount64(); }
#endif
static ULONGLONG g_next_ini_check, g_next_warn;
/* One key press is seen by every ship processed that frame: act on it once. */
#define TOGGLE_DEBOUNCE_MS 150
static ULONGLONG g_last_toggle_press, g_last_decouple_press;
static unsigned g_skipped;
static float g_last_ix = 0, g_last_iy = 0;
static int g_logged_first_apply;

/* Above this the game is in pulse drive / warp: hands off entirely. */
#define PULSE_SPEED 4000.0f
/* A velocity change this large between two frames is scripted (teleport,
 * warp exit, docking): accept it rather than fight it. */
#define JUMP_DV     1500.0f

/* ---- per-ship state ----
 * The game runs UpdateControlled for more than one ship per frame: corvettes
 * with nobody aboard, and other players' ships in multiplayer. Nothing about
 * "the ship" may be global. Up to 1.2.0 it was: one ship's velocity memory got
 * written into another (random velocities), an unpiloted corvette marked Z/F8
 * as held every frame (stuck in coupled mode), and it reset the piloted ship's
 * memory every frame (coupled braking never engaged). */
typedef struct {
    void     *ship;
    ULONGLONG last_seen;
    int       piloted;                  /* last pilot-check result, for logging transitions */
    int       announce;                 /* log this ship's census line on its first frame */
    int       toggle_was_down, decouple_was_down;
    ULONGLONG hard_until;               /* hand-off not cancelled by pilot input (impact, pulse exit) */
    ULONGLONG handoff_until;            /* coupled mode stands down while the game drives the ship */
    ULONGLONG strafe_window;            /* strafe-release / mode-switch window active until this tick */
    int       switch_to_coupled;        /* one-frame flag: decouple key switched us to coupled */
    ULONGLONG next_diag;
    struct {                            /* what we left the ship with last frame, in world space */
        int valid;
        ULONGLONG t;
        float v[3];
        float right[3], up[3], at[3];
    } m;
    struct {                            /* per-second diagnostics (sums over the diag window) */
        unsigned frames;
        float ga_r, ga_u, ga_f;         /* game's own push per ship axis, mean |m/s^2| */
        float discarded;                /* m/s of game push discarded on uncommanded axes */
        unsigned handoff_frames;
        int capped;
    } d;
} ship_state_t;

#define MAX_SHIPS 8
static ship_state_t g_ships[MAX_SHIPS];
static ship_state_t g_ship_none;               /* until the first ship is seen */
static ship_state_t *g_cur = &g_ship_none;     /* the ship flight_tick is handling */

/* The flight code reads these as if there were one ship; they are the current one's. */
#define M                   (g_cur->m)
#define D                   (g_cur->d)
#define g_piloted           (g_cur->piloted)
#define g_toggle_was_down   (g_cur->toggle_was_down)
#define g_decouple_was_down (g_cur->decouple_was_down)
#define g_hard_until        (g_cur->hard_until)
#define g_handoff_until     (g_cur->handoff_until)
#define g_strafe_window     (g_cur->strafe_window)
#define g_switch_to_coupled (g_cur->switch_to_coupled)
#define g_next_diag         (g_cur->next_diag)

static ship_state_t *ship_state(void *ship, ULONGLONG now)
{
    ship_state_t *e = NULL, *oldest = &g_ships[0];
    for (int i = 0; i < MAX_SHIPS; i++) {
        if (g_ships[i].ship == ship) { g_ships[i].last_seen = now; return &g_ships[i]; }
        if (!e && !g_ships[i].ship) e = &g_ships[i];
        if (g_ships[i].last_seen < oldest->last_seen) oldest = &g_ships[i];
    }
    if (!e) e = oldest;                        /* forget the ship not seen for longest */
    memset(e, 0, sizeof *e);
    e->ship = ship;
    e->last_seen = now;
    e->piloted = 1;
    e->announce = 1;
    e->toggle_was_down = e->decouple_was_down = 1;   /* a key already held when a ship appears isn't a press */
    return e;
}

static int ship_index_of(const void *ship)
{
    for (int i = 0; i < MAX_SHIPS; i++) if (g_ships[i].ship == ship) return i;
    return -1;
}

static int ship_index(void) { return g_cur >= g_ships && g_cur < g_ships + MAX_SHIPS ? (int)(g_cur - g_ships) : -1; }

static inline float dot3(const float *a, const float *b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
static inline float len3(const float *a) { return sqrtf(dot3(a, a)); }

static int normalize3(float *v)
{
    float l = len3(v);
    if (!(l > 1e-4f)) return 0;
    v[0] /= l; v[1] /= l; v[2] /= l;
    return 1;
}

#ifdef BF_SIMTEST
static int sim_keys[256];
static inline int down(int vk) { return vk && sim_keys[vk & 0xff]; }
static int game_focused(void) { return 1; }
static int down_set(const keyset_t *k) { for (int i = 0; i < k->n; i++) if (down(k->vk[i])) return 1; return 0; }
#else
static inline int down(int vk) { return vk && (GetAsyncKeyState(vk) & 0x8000); }

static int game_focused(void)
{
    HWND fg = GetForegroundWindow();
    if (!fg) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

static int down_set(const keyset_t *k) { for (int i = 0; i < k->n; i++) if (down(k->vk[i])) return 1; return 0; }
#endif

static int plausible(void *p)
{
    uintptr_t v = (uintptr_t)p;
    return v >= 0x10000 && v < 0x00007FFFFFFFFFFFull;
}

/* Rate-limited warning: says what failed and how many frames it has cost. */
static void warn(ULONGLONG now, const char *fmt, ...)
{
    g_skipped++;
    M.valid = 0;
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

/* ======================================================================== */
/*  flight data retune                                                      */
/* ======================================================================== */
/* Up to 1.1.x the flight retune shipped as a replacement
 * GCSPACESHIPGLOBALS.GLOBAL.MBIN, which conflicted with every other mod that
 * touches that file. The DLL now applies the same changes in memory, on top of
 * whatever the game loaded - vanilla or another mod's file.
 *
 * Fields are found by NAME through the game's own reflection metadata
 * (cTkMetaDataClass 0x28 / cTkMetaDataMember 0x58, layouts as documented by
 * NMS.py tools/extract.py). The loaded GcSpaceshipGlobals instance is found
 * through the code that loads it:
 *   mov rdx,[rip+slot -> "/GcSpaceshipGlobals.global.mbin"]; lea rcx,[rip+instance]
 * No offsets from a particular game build are used.
 *
 * Applying is idempotent: each value remembers what we last wrote. If memory
 * holds anything else, the game (or a reload) put it there, and the change is
 * applied again on top of that value. */

typedef struct meta_class {
    const char         *name;
    uint64_t            name_hash, template_hash;
    struct meta_member *members;
    int32_t             num_members, size;
} meta_class_t;

typedef struct meta_member {
    const char   *name;
    uint32_t      name_hash, type, inner_type;
    int32_t       size, count, offset;
    meta_class_t *cls;
    uint8_t       rest_[0x30];
} meta_member_t;

_Static_assert(sizeof(meta_class_t)  == 0x28, "cTkMetaDataClass is 0x28 bytes");
_Static_assert(sizeof(meta_member_t) == 0x58, "cTkMetaDataMember is 0x58 bytes");

#define META_CUSTOM 0x03
#define META_FLOAT  0x0E

enum { BF_SET, BF_MUL };
enum { TUNE_ALWAYS, TUNE_DECOUPLED };   /* always: both flight modes; decoupled: decoupled mode only */
typedef struct { const char *engine, *field; int op; float value; unsigned scope; } engine_tune_t;
typedef struct { const char *field; int op; float value; } global_tune_t;

static const engine_tune_t ENGINE_TUNES[] = {
#define BF_ENGINE_TUNE_ALWAYS(e, f, o, v) { e, f, o, v, TUNE_ALWAYS },
#define BF_ENGINE_TUNE(e, f, o, v) { e, f, o, v, TUNE_DECOUPLED },
#define BF_GLOBAL_TUNE(f, o, v)
#include "flight_tuning.inc"
#undef BF_ENGINE_TUNE_ALWAYS
#undef BF_ENGINE_TUNE
#undef BF_GLOBAL_TUNE
};
static const global_tune_t GLOBAL_TUNES[] = {
#define BF_ENGINE_TUNE_ALWAYS(e, f, o, v)
#define BF_ENGINE_TUNE(e, f, o, v)
#define BF_GLOBAL_TUNE(f, o, v) { f, o, v },
#include "flight_tuning.inc"
#undef BF_ENGINE_TUNE_ALWAYS
#undef BF_ENGINE_TUNE
#undef BF_GLOBAL_TUNE
};
#define N_ENGINE_TUNES (sizeof ENGINE_TUNES / sizeof ENGINE_TUNES[0])
#define N_GLOBAL_TUNES (sizeof GLOBAL_TUNES / sizeof GLOBAL_TUNES[0])

#define MAX_RETUNE 512
typedef struct {
    int32_t  offset;
    int      op, applied;
    float    value, baseline;
    uint32_t written;                 /* bit pattern we last wrote */
    unsigned scope;                   /* TUNE_ALWAYS / TUNE_DECOUPLED */
    char     label[72];
} retune_t;

static struct {
    meta_class_t *globals, *control, *engine;
    uint8_t      *instance;
    int           found, blocked, n;
    volatile LONG ready;              /* set once, by the init thread */
    retune_t      e[MAX_RETUNE];
} RT;

static IMAGE_NT_HEADERS64 *image_nt(void)
{
    return (IMAGE_NT_HEADERS64 *)(g_image + ((IMAGE_DOS_HEADER *)g_image)->e_lfanew);
}

static int image_section(const char *name, uint8_t **start, size_t *vsize, size_t *rawsize)
{
    IMAGE_NT_HEADERS64 *nt = image_nt();
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    size_t len = strlen(name);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
        if (memcmp(sec->Name, name, len) == 0 && (len == 8 || sec->Name[len] == 0)) {
            *start = g_image + sec->VirtualAddress;
            *vsize = sec->Misc.VirtualSize;
            *rawsize = sec->SizeOfRawData < sec->Misc.VirtualSize ? sec->SizeOfRawData : sec->Misc.VirtualSize;
            return 1;
        }
    return 0;
}

/* Pointers stored in the image are relocated in the running game, but keep the
 * preferred base in an image mapped for the offline selftest. Accept both. */
static void *image_ptr(void *p)
{
    if (!g_image) return p;
    uint64_t q = (uintptr_t)p, pref = image_nt()->OptionalHeader.ImageBase;
    uint64_t size = image_nt()->OptionalHeader.SizeOfImage;
    if (pref != (uintptr_t)g_image && q >= pref && q < pref + size)
        return g_image + (q - pref);
    return p;
}

static int is_ptr_to(uint64_t q, const uint8_t *target)
{
    return q == (uintptr_t)target || q == image_nt()->OptionalHeader.ImageBase + (uint64_t)(target - g_image);
}

/* Exact NUL-terminated string that starts on a string boundary. */
static const uint8_t *find_cstr(const uint8_t *sec, size_t size, const char *s)
{
    size_t n = strlen(s) + 1;
    if (size < n) return NULL;
    const uint8_t *end = sec + size - n + 1;
    for (const uint8_t *p = sec; p < end; p++) {
        p = memchr(p, (unsigned char)s[0], (size_t)(end - p));
        if (!p) break;
        if ((p == sec || p[-1] == 0) && memcmp(p, s, n) == 0) return p;
    }
    return NULL;
}

static meta_member_t *meta_members(const meta_class_t *c) { return image_ptr(c->members); }
static const char *meta_name(const meta_member_t *m)      { return image_ptr((void *)m->name); }

/* The member table of a class may be filled in by the game's startup code. */
static int meta_populated(const meta_class_t *c)
{
    if (!c || c->num_members <= 0) return 0;
    meta_member_t *m = meta_members(c);
    return plausible(m) && plausible((void *)meta_name(&m[0])) &&
           plausible((void *)meta_name(&m[c->num_members - 1]));
}

static const meta_member_t *meta_member(const meta_class_t *c, const char *name)
{
    meta_member_t *m = meta_members(c);
    if (!plausible(m)) return NULL;
    for (int i = 0; i < c->num_members; i++) {
        const char *n = meta_name(&m[i]);
        if (plausible((void *)n) && strcmp(n, name) == 0) return &m[i];
    }
    return NULL;
}

static meta_class_t *meta_find_class(const char *name)
{
    uint8_t *rd; size_t rdv, rdr;
    if (!image_section(".rdata", &rd, &rdv, &rdr)) return NULL;
    const uint8_t *s = find_cstr(rd, rdr, name);
    if (!s) return NULL;
    for (size_t i = 0; i + sizeof(meta_class_t) <= rdr; i += 8) {
        uint64_t q; memcpy(&q, rd + i, 8);
        if (!is_ptr_to(q, s)) continue;
        meta_class_t *c = (meta_class_t *)(rd + i);
        if (c->num_members > 0 && c->num_members < 10000 && c->size > 0 && c->size < 0x100000 &&
            plausible(meta_members(c)))
            return c;
    }
    return NULL;
}

/* mov rdx,[rip+slot]; lea rcx,[rip+instance] - where slot points at the path. */
static uint8_t *find_globals_instance(const char *path, int32_t size)
{
    uint8_t *rd, *da; size_t rdv, rdr, dav, dar;
    if (!image_section(".rdata", &rd, &rdv, &rdr) || !image_section(".data", &da, &dav, &dar)) return NULL;
    const uint8_t *s = find_cstr(rd, rdr, path);
    if (!s) return NULL;
    uint8_t *slot = NULL; int slots = 0;
    for (size_t i = 0; i + 8 <= dar; i += 8) {
        uint64_t q; memcpy(&q, da + i, 8);
        if (is_ptr_to(q, s)) { slot = da + i; slots++; }
    }
    if (slots != 1) return NULL;
    uint8_t *hit = NULL; int hits = 0;
    const uint8_t *end = g_text + g_text_size - 14;
    for (uint8_t *p = g_text; p < end; p++) {
        p = memchr(p, 0x48, (size_t)(end - p));
        if (!p) break;
        if (p[1] != 0x8B || p[2] != 0x15 || p[7] != 0x48 || p[8] != 0x8D || p[9] != 0x0D) continue;
        int32_t d; memcpy(&d, p + 3, 4);
        if (p + 7 + d == slot) { hit = p; hits++; }
    }
    if (hits != 1) return NULL;
    int32_t d2; memcpy(&d2, hit + 10, 4);
    uint8_t *inst = hit + 14 + d2;
    return inst >= da && inst + size <= da + dav ? inst : NULL;
}

/* Static part: classes and the instance. All of it is in the file. */
static int retune_find(int verbose)
{
    if (!RT.globals) RT.globals = meta_find_class("cGcSpaceshipGlobals");
    if (!RT.control) RT.control = meta_find_class("cGcPlayerSpaceshipControlData");
    if (!RT.engine)  RT.engine  = meta_find_class("cGcPlayerSpaceshipEngineData");
    if (RT.globals && !RT.instance)
        RT.instance = find_globals_instance("/GcSpaceshipGlobals.global.mbin", RT.globals->size);
    RT.found = RT.globals && RT.control && RT.engine && RT.instance;
    if (verbose || RT.found)
        logmsg("  data cGcSpaceshipGlobals %s  ControlData %s  EngineData %s  loaded instance %s",
               RT.globals ? "OK" : "FAIL", RT.control ? "OK" : "FAIL", RT.engine ? "OK" : "FAIL",
               RT.instance ? "OK" : "FAIL");
    if (RT.found)
        logmsg("flight data: GcSpaceshipGlobals at +%#llx (%d fields, %#x bytes)",
               (unsigned long long)(RT.instance - g_image), RT.globals->num_members, RT.globals->size);
    return RT.found;
}

static void retune_add(int32_t offset, int op, float value, unsigned scope, const char *fmt, ...)
{
    if (RT.n >= MAX_RETUNE || offset < 0 || offset + 4 > RT.globals->size) return;
    retune_t *e = &RT.e[RT.n++];
    memset(e, 0, sizeof *e);
    e->offset = offset; e->op = op; e->value = value; e->scope = scope;
    va_list ap; va_start(ap, fmt); vsnprintf(e->label, sizeof e->label, fmt, ap); va_end(ap);
}

/* Name -> offset for every change. Needs the member tables filled in. */
static int retune_expand(int verbose)
{
    if (!meta_populated(RT.globals) || !meta_populated(RT.control) || !meta_populated(RT.engine)) {
        if (verbose) logmsg("  flight data: field metadata not filled in yet");
        return 0;
    }
    RT.n = 0;
    int blocks = 0, missing = 0;
    const meta_member_t *gm = meta_members(RT.globals);
    for (int i = 0; i < RT.globals->num_members; i++) {
        const meta_member_t *blk = &gm[i];
        if (blk->type != META_CUSTOM || image_ptr(blk->cls) != (void *)RT.control) continue;
        blocks++;
        for (size_t t = 0; t < N_ENGINE_TUNES; t++) {
            const meta_member_t *eng = meta_member(RT.control, ENGINE_TUNES[t].engine);
            const meta_member_t *fld = meta_member(RT.engine, ENGINE_TUNES[t].field);
            if (!eng || eng->type != META_CUSTOM || !fld || fld->type != META_FLOAT || fld->size != 4) {
                if (blocks == 1) logmsg("  flight data: %s.%s not in this game version - skipped",
                                        ENGINE_TUNES[t].engine, ENGINE_TUNES[t].field);
                missing++;
                continue;
            }
            retune_add(blk->offset + eng->offset + fld->offset, ENGINE_TUNES[t].op, ENGINE_TUNES[t].value,
                       ENGINE_TUNES[t].scope, "%s.%s.%s", meta_name(blk), ENGINE_TUNES[t].engine, ENGINE_TUNES[t].field);
        }
    }
    for (size_t t = 0; t < N_GLOBAL_TUNES; t++) {
        const meta_member_t *m = meta_member(RT.globals, GLOBAL_TUNES[t].field);
        if (!m || m->type != META_FLOAT || m->size != 4) {
            logmsg("  flight data: %s not in this game version - skipped", GLOBAL_TUNES[t].field);
            missing++;
            continue;
        }
        retune_add(m->offset, GLOBAL_TUNES[t].op, GLOBAL_TUNES[t].value, TUNE_DECOUPLED, "%s", GLOBAL_TUNES[t].field);
    }
    logmsg("flight data: %d values to retune across %d ship control blocks (%d missing)", RT.n, blocks, missing);
    if (RT.n == 0) return 0;
    InterlockedExchange(&RT.ready, 1);
    return 1;
}

/* Called every flight frame: apply, re-apply after the game changed a value,
 * or put the loaded values back when the retune is switched off.
 * Scope: TUNE_ALWAYS entries apply in both flight modes (zero space min-speed);
 * TUNE_DECOUPLED entries apply only while decoupled - in coupled mode the
 * game's loaded flight data is left untouched for those fields. */
static void retune_tick(void)
{
    if (!RT.ready) return;
    int on = cfg.flight_retune && g_enabled && !RT.blocked, changed = 0;
    for (int i = 0; i < RT.n; i++) {
        retune_t *e = &RT.e[i];
        uint8_t *at = RT.instance + e->offset;
        uint32_t bits; memcpy(&bits, at, 4);
        int want = on && (e->scope == TUNE_ALWAYS || g_decoupled);
        if (!want) {
            if (e->applied) {
                if (bits == e->written) memcpy(at, &e->baseline, 4);
                e->applied = 0; changed++;
            }
            continue;
        }
        if (e->applied && bits == e->written) continue;
        float cur; memcpy(&cur, &bits, 4);
        float nv = e->op == BF_MUL ? cur * e->value : e->value;
        memcpy(at, &nv, 4);
        memcpy(&e->written, &nv, 4);
        if (cfg.debug)
            logmsg("  retune %-48s %12g -> %g%s", e->label, cur, nv, e->applied ? "  (game changed it)" : "");
        e->baseline = cur; e->applied = 1; changed++;
    }
    if (changed)
        logmsg(on ? "flight data: retuned %d of %d values on top of the loaded flight data"
                  : "flight data: put back %d of %d loaded values (retune off)", changed, RT.n);
}

/* Init thread, after the flight hook is in. Waits for the game's startup code
 * to fill in the field metadata. */
static void retune_init(void)
{
    wchar_t old[MAX_PATH];
    swprintf(old, MAX_PATH, L"%ls..\\GAMEDATA\\MODS\\BetterFlight\\GCSPACESHIPGLOBALS.GLOBAL.MBIN", g_dir);
    if (GetFileAttributesW(old) != INVALID_FILE_ATTRIBUTES) {
        RT.blocked = 1;
        logmsg("flight data: the old Better Flight data mod is still installed (GAMEDATA/MODS/BetterFlight). "
               "Delete that folder - the retune is skipped so it isn't applied twice.");
        return;
    }
    const int tries = 600;
    for (int i = 0; i < tries; i++) {
        if ((RT.found || retune_find(i == 0)) && retune_expand(i == tries - 1)) return;
        Sleep(1000);
    }
    logmsg("flight data: could not find the ship flight data - retune off. Strafe and coupled flight still work.");
}

/* The game's own test before it flies a ship for the player: controller handle
 * valid and controller active. Corvettes skip it and get UpdateControlled with
 * nobody at the controls (walking around inside, EVA). */
static int ship_piloted(void *ship)
{
    if (G.off_controller < 0) return 1;                  /* check unavailable: old behaviour */
    void **controller = *(void ***)((uint8_t *)ship + G.off_controller);
    return plausible(controller) && *controller != NULL &&
           *((uint8_t *)ship + G.off_controller_active) != 0;
}

/* ---- ship census (multiplayer research) ----
 * Which ships the game runs through the flight update, and what controls them.
 * Logged once per ship, plus a rate-limited note when two ships are at the
 * controls in the same frame - the situation where another player's ship would
 * get this player's keys. Every pointer is checked readable first. */
static int readable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!plausible((void *)p) || !VirtualQuery(p, &mbi, sizeof mbi)) return 0;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                         PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return 0;
    return (const uint8_t *)p + n <= (const uint8_t *)mbi.BaseAddress + mbi.RegionSize;
}

/* MSVC x64 RTTI: vtable[-1] -> CompleteObjectLocator { u32 signature (1), offset,
 * cdOffset, typeDescriptor RVA, classDescriptor RVA, self RVA }; the type
 * descriptor's decorated name (".?AVcGcPlayerController@@") is at +0x10. */
static const char *rtti_name(const void *obj)
{
    if (!readable(obj, 8)) return NULL;
    const uint8_t *vt = *(const uint8_t *const *)obj;
    if (!readable(vt - 8, 8)) return NULL;
    const uint8_t *col = *(const uint8_t *const *)(vt - 8);
    if (!readable(col, 24)) return NULL;
    uint32_t sig, td, self;
    memcpy(&sig, col, 4); memcpy(&td, col + 12, 4); memcpy(&self, col + 20, 4);
    if (sig != 1) return NULL;
    const char *name = (const char *)(col - self + td + 16);
    if (!readable(name, 64) || memcmp(name, ".?AV", 4) != 0 || !memchr(name, 0, 64)) return NULL;
    return name;
}

static void log_ship_census(void *ship, int piloted)
{
    const char *cls = "unknown";
    void *ctrl = NULL;
    if (G.off_controller >= 0) {
        void **h = *(void ***)((uint8_t *)ship + G.off_controller);
        ctrl = h;
        const char *n = rtti_name(h);                           /* the field is the object... */
        if (!n && readable(h, 8)) n = rtti_name(*h);            /* ...or a handle to it */
        if (n) cls = n;
    }
    logmsg("ship %d: first seen %s | controller %p class %s", ship_index(),
           piloted ? "with a pilot at the controls" : "with no pilot", ctrl, cls);
}

static void *g_last_pilot_ship;
static ULONGLONG g_last_pilot_t, g_next_multi_note;

static void flight_tick(void *ship, float dt)
{
    ULONGLONG now = bf_ticks();
    g_cur = ship_state(ship, now);

    if (now >= g_next_ini_check) {
        g_next_ini_check = now + 1000;
        if (ini_changed()) { logmsg("config changed on disk - reloading"); load_config(); }
    }

    retune_tick();

    /* Nobody flying this ship: leave it to the game and don't read keys - they
     * belong to whatever the player is doing instead (jetpack, walking). */
    if (!ship_piloted(ship)) {
        if (g_cur->announce) { log_ship_census(ship, 0); g_cur->announce = 0; }
        if (g_piloted && cfg.debug) logmsg("ship %d: no pilot at the controls (corvette interior / EVA) - standing by", ship_index());
        g_piloted = 0;
        M.valid = 0;
        g_toggle_was_down = g_decouple_was_down = 1;    /* this ship only: a key held while sitting down isn't a press */
        return;
    }
    if (g_cur->announce) { log_ship_census(ship, 1); g_cur->announce = 0; }
    if (g_last_pilot_ship && g_last_pilot_ship != ship && now - g_last_pilot_t < 50 && now >= g_next_multi_note) {
        g_next_multi_note = now + 10000;
        logmsg("NOTE: ships %d and %d both have a pilot at the controls in the same frame "
               "(another player's ship?) - please include this log in multiplayer reports",
               ship_index_of(g_last_pilot_ship), ship_index());
    }
    g_last_pilot_ship = ship;
    g_last_pilot_t = now;
    if (!g_piloted && cfg.debug) logmsg("ship %d: pilot at the controls", ship_index());
    g_piloted = 1;

    /* ---- input (only while the game has focus) ---- */
    float ix = 0, iy = 0;
    int fwd_in = 0, auto_key = 0, thr_in = 0, brk_in = 0, bst_in = 0;
    if (game_focused()) {
        int t = down_set(&cfg.toggle);
        if (t && !g_toggle_was_down && now - g_last_toggle_press >= TOGGLE_DEBOUNCE_MS) {
            g_last_toggle_press = now;
            g_enabled = !g_enabled;
            logmsg("Better Flight native features %s (toggle key)", g_enabled ? "ENABLED" : "DISABLED");
        }
        g_toggle_was_down = t;
        int d = down_set(&cfg.decouple);
        if (d && !g_decouple_was_down && now - g_last_decouple_press >= TOGGLE_DEBOUNCE_MS) {
            g_last_decouple_press = now;
            g_decoupled = !g_decoupled;
            if (!g_decoupled) g_switch_to_coupled = 1;
            logmsg("flight mode: %s", g_decoupled ? "DECOUPLED" : "COUPLED");
        }
        g_decouple_was_down = d;
        ix = (float)(down_set(&cfg.strafe_right) - down_set(&cfg.strafe_left));
        iy = (float)(down_set(&cfg.strafe_up)    - down_set(&cfg.strafe_down));
        thr_in   = down_set(&cfg.thrust);
        brk_in   = down_set(&cfg.brake);
        bst_in   = down_set(&cfg.boost);
        fwd_in   = thr_in || brk_in || bst_in;
        auto_key = down_set(&cfg.autopilot_keys);
    }
    if (cfg.debug && (ix != g_last_ix || iy != g_last_iy))
        logmsg("input: lateral=%+.0f vertical=%+.0f", ix, iy);
    g_last_ix = ix; g_last_iy = iy;

    if (!g_enabled) { M.valid = 0; return; }
    if (!(dt > 0.0f && dt < 0.25f)) { warn(now, "implausible frame time dt=%f", dt); return; }

    /* ---- ship state ---- */
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

    _Alignas(16) float vg[4] = { 0 };
    G.get_velocity(ship, vg);
    float v[3] = { vg[0], vg[1], vg[2] };
    float speed = len3(v);

    /* ---- what the game did to the ship since our last write ---- */
    int resync = !M.valid || now - M.t > 500 || speed > PULSE_SPEED;
    float g[3] = { 0, 0, 0 }, gr = 0, gu = 0, gf = 0;
    if (!resync) {
        for (int i = 0; i < 3; i++) g[i] = v[i] - M.v[i];
        if (len3(g) > JUMP_DV) resync = 1;
        else { gr = dot3(g, right) / dt; gu = dot3(g, up) / dt; gf = dot3(g, at) / dt; }
    }
    float sx = cfg.invert_lateral  ? -ix : ix;
    float sy = cfg.invert_vertical ? -iy : iy;
    int piloting = sx != 0 || sy != 0 || fwd_in;
    if (sx != 0 || sy != 0) g_strafe_window = now + 5000;
    /* ^ release window (1.3.2): the game converts residual drift into forward
     * speed for a few seconds AFTER the strafe key is released, so the
     * nose-axis cancellation below stays armed until 5 s after the last
     * strafe frame. The drift bleeds off in ~2-3 s, well inside the window. */

    /* Pulse drive: hands off while above pulse speed and for a while after, so the
     * game's pulse-exit deceleration happens even if the pilot is holding W. */
    if (speed > PULSE_SPEED) {
        if (cfg.debug && now >= g_hard_until) logmsg("hand-off (pulse drive speed %.0f m/s)", speed);
        g_hard_until = now + (ULONGLONG)(cfg.autopilot_seconds * 1000.0f);
    }

    /* ---- hand-off: let the game fly when something automated is flying ----
     * A push only counts if rotating the ship can't explain it - whether the game
     * carries velocity round with the ship or not - so hard turns never look like
     * an autopilot. Rules:
     *   autopilot keys (land / pulse / follow)          -> AutopilotSeconds
     *   forward push, no throttle input (pulse spool)    -> AutopilotSeconds
     *   impact on any axis                               -> 1s, not cancellable
     *   below MomentumMinSpeed, push on an unused axis   -> AutopilotSeconds (take-off, landing)
     * Steering and throttle wind-down never hand off. */
    if (!resync) {
        float lr = dot3(M.v, M.right), lu = dot3(M.v, M.up), la = dot3(M.v, M.at), c[3];
        for (int i = 0; i < 3; i++) c[i] = lr * right[i] + lu * up[i] + la * at[i] - M.v[i];
        float cr = dot3(c, right) / dt, cu = dot3(c, up) / dt, cf = dot3(c, at) / dt;
#define BF_PUSH(gv, cv) (fabsf(gv) < fabsf((gv) - (cv)) ? (gv) : ((gv) - (cv)))
        float pr = fabsf(BF_PUSH(gr, cr)), pu = fabsf(BF_PUSH(gu, cu));
        float pf_signed = BF_PUSH(gf, cf), pf = fabsf(pf_signed);
#undef BF_PUSH
        int slow = len3(M.v) < cfg.momentum_min_speed;
        float lo = cfg.low_speed_autopilot_accel;
        float mv = len3(M.v), gmag = len3(g), vhat[3] = { at[0], at[1], at[2] };
        if (!slow && mv > 1e-3f) for (int i = 0; i < 3; i++) vhat[i] = M.v[i] / mv;
        float speedup    = dot3(g, vhat) / dt;                     /* rate total speed is rising */
        float nose_align = gmag > 1e-6f ? dot3(g, at) / gmag : 0;  /* push direction vs nose */
        int pulse_like   = speedup > cfg.autopilot_accel && nose_align > 0.9f;
        const char *why = NULL;
        if (pr > cfg.impact_accel || pu > cfg.impact_accel || pf > cfg.impact_accel) {
            if (cfg.debug && now >= g_hard_until)
                logmsg("hand-off (impact): push right=%.0f up=%.0f fwd=%.0f m/s^2", pr, pu, pf);
            g_hard_until = now + 1000;
        } else if (auto_key) {
            why = "autopilot key";
        } else if (!fwd_in && pulse_like) {
            /* Pulse spool-up: the game is raising TOTAL speed hard, along the nose.
             * Its steering (swinging velocity toward the nose) and its reverse-speed
             * limit both push along the nose too, but never raise total speed -
             * so manoeuvring at speed can't be mistaken for pulse drive. */
            why = "pulse spool-up";
        } else if (slow && now >= g_strafe_window &&
                    ((sx == 0 && pr > lo) || (sy == 0 && pu > lo))) {
            /* take-off lifts vertically; low-speed forward pushes are just the
             * throttle. Not while the strafe-release window is armed: there the
             * low-speed side push is the known drift bleed, not take-off/landing. */
            why = "low-speed push - take-off / landing";
        }
        if (why) {
            if (cfg.debug && now >= g_handoff_until)
                logmsg("hand-off (%s): push right=%.0f up=%.0f fwd=%+.0f m/s^2", why, pr, pu, pf_signed);
            g_handoff_until = now + (ULONGLONG)(cfg.autopilot_seconds * 1000.0f);
        } else if (piloting && now < g_handoff_until) {
            if (cfg.debug) logmsg("hand-off cancelled: pilot input");
            g_handoff_until = 0;
        }
    }
    int handoff = now < g_handoff_until || now < g_hard_until;
    D.ga_r += fabsf(gr); D.ga_u += fabsf(gu); D.ga_f += fabsf(gf);
    if (handoff) D.handoff_frames++;

    int changed = 0;

    /* 1.3.3: switching to coupled with large off-nose velocity (from
     * decoupled momentum flight) hands the ship to the game, which converts
     * the drift into forward/backwards velocity - at 1600 m/s sideways the
     * live game applied ~1200 m/s^2 of brake and a ~440 m/s^2 nose push. Arm
     * the release window so the nose-axis part is cancelled; the drift
     * bleeds off at the game's own (vanilla) rate. */
    if (g_switch_to_coupled) {
        g_switch_to_coupled = 0;
        float smvr = dot3(M.v, right), smvu = dot3(M.v, up);
        if (smvr * smvr + smvu * smvu > 25.0f) {
            g_strafe_window = now + 5000;
            if (cfg.debug)
                logmsg("coupled with %.0f m/s off-nose drift - arming release window",
                       sqrtf(smvr * smvr + smvu * smvu));
        }
    }

    /* 1. Momentum.
     * Decoupled: above MomentumMinSpeed, whatever the game did to the velocity
     * on an axis the pilot isn't commanding is discarded - its steering toward
     * the nose, drift pull, throttle wind-down. The velocity stays fixed in
     * space no matter HOW the game would have bent it. W/S/boost still pass the
     * game's throttle through along the nose.
     * Coupled while strafing, and for 5 s after the last strafe frame: the
     * game's flight model converts sustained sideways/vertical drift into
     * forward/backwards velocity - it steers the velocity vector toward the
     * nose (live logs: a pure strafe in space drew up to ~30 m/s^2 of
     * nose-axis push from the game). Cancel only that nose-axis change: the
     * game's own bleed of the drift keeps running (the vanilla "gentle push"
     * feel), and the drift stays sideways. The release window covers the
     * residual drift after key release; the strafe-activity requirement keeps
     * vanilla slide-turn recovery (no recent strafe) untouched. The real-drift
     * requirement leaves hover/landing alone (nose-axis lift with ~no drift);
     * hand-off already covers autopilot, pulse drive, impacts, take-off.
     * In coupled mode otherwise the game's flight model (steering, braking,
     * min-speed) is left to run: only the strafe thrust below is added. */
    if (!resync && !handoff) {
        int decon = g_decoupled && cfg.world_momentum && len3(M.v) > cfg.momentum_min_speed;
        float mvr = dot3(M.v, right), mvu = dot3(M.v, up);
        int strafe_conv = !g_decoupled && !fwd_in &&
                          mvr * mvr + mvu * mvu > 25.0f && now < g_strafe_window;
        if (decon || strafe_conv) {
            /* Each keep_* is the part of the game's change along that axis we
             * add back on top of our last write (a velocity delta, not a factor). */
            float gcr = dot3(g, right), gcu = dot3(g, up), gfc = dot3(g, at);
            float keep_r = 0.0f, keep_u = 0.0f, keep_f = 0.0f;
            if (strafe_conv) {
                keep_r = gcr; keep_u = gcu;   /* let the game bleed the drift */
            } else {
                /* Pass the game's forward change only in the direction the pilot
                 * pushes: W/boost keep accelerating, S keeps braking - so holding
                 * W above normal speed isn't dragged down by the overspeed brake. */
                int push = thr_in || bst_in;
                if (push && !brk_in)      keep_f = gfc > 0 ? gfc : 0;
                else if (brk_in && !push) keep_f = gfc < 0 ? gfc : 0;
                else if (fwd_in)          keep_f = gfc;
            }
            float dropped[3];
            for (int i = 0; i < 3; i++) {
                float nv = M.v[i] + right[i] * keep_r + up[i] * keep_u + at[i] * keep_f;
                dropped[i] = v[i] - nv;
                v[i] = nv;
            }
            float dl = len3(dropped);
            if (dl > 1e-6f) { D.discarded += dl; changed = 1; }
        }
    }

    float vr = dot3(v, right), vu = dot3(v, up), vf = dot3(v, at);

    /* 2. Strafe thrusters (both modes). In coupled mode this is the ONLY
     * flight change on top of the vanilla model: a push along the ship's
     * right/up axes. The game's flight assist bleeds off sideways velocity,
     * so coupled strafe is a gentle push against it - not free 6DOF drift.
     * Raise LateralAccel/VerticalAccel in BetterFlight.ini for more. */
    float dr = thrust(sx, vr, cfg.lateral_accel,  dt, cfg.max_strafe_speed);
    float du = thrust(sy, vu, cfg.vertical_accel, dt, cfg.max_strafe_speed);

    if (dr != 0 || du != 0) {
        for (int i = 0; i < 3; i++) v[i] += right[i] * dr + up[i] * du;
        vr += dr; vu += du;
        changed = 1;
    }

    /* 4. Safety: cap drift (the part of velocity not along the nose). Forward
     * speed is left to the game, so boost and pulse drive are unaffected. */
    float drift = sqrtf(vr * vr + vu * vu);
    if (!resync && cfg.drift_cap > 0 && drift > cfg.drift_cap) {
        float k = cfg.drift_cap / drift;
        for (int i = 0; i < 3; i++) v[i] -= (right[i] * vr + up[i] * vu) * (1 - k);
        vr *= k; vu *= k;
        D.capped++;
        changed = 1;
    }

    if (changed) {
        _Alignas(16) float out[4] = { v[0], v[1], v[2], vg[3] };
        G.set_linear_velocity(rigid_body, out, 0);
        if (!g_logged_first_apply) {
            g_logged_first_apply = 1;
            logmsg("first velocity write (mode %s)", g_decoupled ? "DECOUPLED" : "COUPLED");
        }
    }

    D.frames++;
    if (cfg.debug && now >= g_next_diag) {
        g_next_diag = now + 1000;
        logmsg("diag: ship %d %s |v|=%6.1f right=%7.1f up=%7.1f at=%7.1f in x=%+.0f y=%+.0f fwd=%d dt=%.4f | "
               "discarded game push %.0f m/s%s%s%s",
               ship_index(), g_decoupled ? "DECOUP" : "COUPLED", speed, vr, vu, vf, ix, iy, fwd_in, dt,
               D.discarded,
               D.capped ? " | DRIFT CAPPED" : "", resync ? " | resync" : "", handoff ? " | hand-off" : "");
        logmsg("measure: game push mean |right|=%.0f |up|=%.0f |fwd|=%.0f m/s^2 | hand-off %u/%u frames",
               D.ga_r / D.frames, D.ga_u / D.frames, D.ga_f / D.frames, D.handoff_frames, D.frames);
        memset(&D, 0, sizeof D);
    }

    memcpy(M.v, v, sizeof M.v);
    memcpy(M.right, right, sizeof M.right);
    memcpy(M.up, up, sizeof M.up);
    memcpy(M.at, at, sizeof M.at);
    M.t = now;
    M.valid = 1;
}

static uint64_t __fastcall hook_update_controlled(void *ship, float dt)
{
    uint64_t r = orig_update_controlled(ship, dt);
    flight_tick(ship, dt);
    return r;
}

/* ======================================================================== */
/*  controller diagnostics                                                  */
/* ======================================================================== */
/* Research aid for controller and HOSAS support. With [input] Diagnostics = 1,
 * log every controller the DLL can see - XInput pads and DirectInput game
 * controllers - and their input changes. Read-only.
 *
 * XInput and DirectInput are loaded at runtime, so the DLL's import table stays
 * exactly as it is (a missing import is what broke 1.0.0 on Windows). Devices
 * are opened non-exclusive and in the background, so the game's own input is
 * unaffected. */

typedef DWORD   (WINAPI *fn_xinput_get_state)(DWORD, XINPUT_STATE *);
typedef HRESULT (WINAPI *fn_di8_create)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);

#define DIAG_MAX_DEV 16
#define DIAG_MAX_LINES_PER_SEC 40

typedef struct {
    IDirectInputDevice8W *dev;
    GUID  instance;
    char  name[128];
    int   present, gone;
    char  last[256];
} diag_dev_t;

static struct {
    fn_xinput_get_state xget;
    int        x_connected[4];
    char       x_last[4][96];
    IDirectInput8W *di;
    diag_dev_t dev[DIAG_MAX_DEV];
    int        ndev;
    HWND       hwnd;
    ULONGLONG  next_scan, line_window;
    int        lines;
} DG;

static void diag_log(const char *fmt, ...)
{
    ULONGLONG now = GetTickCount64();
    if (now - DG.line_window >= 1000) {
        if (DG.lines > DIAG_MAX_LINES_PER_SEC)
            logmsg("input: (%d lines suppressed)", DG.lines - DIAG_MAX_LINES_PER_SEC);
        DG.line_window = now;
        DG.lines = 0;
    }
    if (++DG.lines > DIAG_MAX_LINES_PER_SEC) return;
    char buf[400];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    logmsg("%s", buf);
}

/* -range..range -> -10..10 */
static int diag_q10(long v, long range)
{
    long q = lround(10.0 * (double)v / (double)range);
    return q < -10 ? -10 : q > 10 ? 10 : (int)q;
}

static void diag_xinput_init(void)
{
    static const wchar_t *const dlls[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
    for (int i = 0; i < 3 && !DG.xget; i++) {
        HMODULE m = LoadLibraryW(dlls[i]);
        if (m) DG.xget = (fn_xinput_get_state)GetProcAddress(m, "XInputGetState");
        if (DG.xget) logmsg("input: XInput from %ls", dlls[i]);
    }
    if (!DG.xget) logmsg("input: no XInput DLL available");
}

static void diag_xinput_poll(void)
{
    if (!DG.xget) return;
    for (DWORD i = 0; i < 4; i++) {
        XINPUT_STATE s;
        memset(&s, 0, sizeof s);
        int on = DG.xget(i, &s) == ERROR_SUCCESS;
        if (on != DG.x_connected[i]) {
            diag_log("input: XInput pad %lu %s", (unsigned long)i, on ? "CONNECTED" : "disconnected");
            DG.x_connected[i] = on;
            DG.x_last[i][0] = 0;
        }
        if (!on) continue;
        const XINPUT_GAMEPAD *g = &s.Gamepad;
        char line[96];
        snprintf(line, sizeof line, "buttons=%04x LX=%+d LY=%+d RX=%+d RY=%+d LT=%d RT=%d",
                 g->wButtons, diag_q10(g->sThumbLX, 32767), diag_q10(g->sThumbLY, 32767),
                 diag_q10(g->sThumbRX, 32767), diag_q10(g->sThumbRY, 32767),
                 diag_q10(g->bLeftTrigger, 255), diag_q10(g->bRightTrigger, 255));
        if (strcmp(line, DG.x_last[i]) != 0) {
            diag_log("input: XInput pad %lu %s", (unsigned long)i, line);
            strcpy(DG.x_last[i], line);
        }
    }
}

static BOOL CALLBACK diag_find_window(HWND h, LPARAM unused)
{
    (void)unused;
    DWORD pid;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) {
        DG.hwnd = h;
        return FALSE;
    }
    return TRUE;
}

static const char *diag_dev_type(DWORD t)
{
    switch (GET_DIDEVICE_TYPE(t)) {
    case DI8DEVTYPE_JOYSTICK:     return "joystick";
    case DI8DEVTYPE_GAMEPAD:      return "gamepad";
    case DI8DEVTYPE_FLIGHT:       return "flight";
    case DI8DEVTYPE_DRIVING:      return "driving";
    case DI8DEVTYPE_1STPERSON:    return "1st-person";
    case DI8DEVTYPE_SUPPLEMENTAL: return "supplemental";
    default:                      return "other";
    }
}

static BOOL CALLBACK diag_enum_device(const DIDEVICEINSTANCEW *inst, LPVOID unused)
{
    (void)unused;
    for (int i = 0; i < DG.ndev; i++)
        if (IsEqualGUID(&DG.dev[i].instance, &inst->guidInstance)) {
            DG.dev[i].present = 1;
            if (DG.dev[i].gone) {
                logmsg("input: DirectInput device %d '%s' is back", i, DG.dev[i].name);
                DG.dev[i].gone = 0;
                DG.dev[i].last[0] = 0;
            }
            return DIENUM_CONTINUE;
        }
    if (DG.ndev >= DIAG_MAX_DEV) return DIENUM_STOP;

    diag_dev_t *d = &DG.dev[DG.ndev];
    memset(d, 0, sizeof *d);
    d->instance = inst->guidInstance;
    d->present = 1;
    WideCharToMultiByte(CP_UTF8, 0, inst->tszProductName, -1, d->name, sizeof d->name, NULL, NULL);
    DWORD vidpid = inst->guidProduct.Data1;
    logmsg("input: DirectInput device %d: '%s' type=%s VID=%04lx PID=%04lx", DG.ndev, d->name,
           diag_dev_type(inst->dwDevType), (unsigned long)(vidpid & 0xFFFF), (unsigned long)((vidpid >> 16) & 0xFFFF));

    if (FAILED(IDirectInput8_CreateDevice(DG.di, &inst->guidInstance, &d->dev, NULL))) {
        logmsg("input:   could not open it");
        d->dev = NULL;
        DG.ndev++;
        return DIENUM_CONTINUE;
    }
    IDirectInputDevice8_SetDataFormat(d->dev, &c_dfDIJoystick2);
    HRESULT co = IDirectInputDevice8_SetCooperativeLevel(d->dev, DG.hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(co))
        co = IDirectInputDevice8_SetCooperativeLevel(d->dev, NULL, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    DIPROPRANGE range;
    range.diph.dwSize = sizeof range;
    range.diph.dwHeaderSize = sizeof range.diph;
    range.diph.dwHow = DIPH_DEVICE;
    range.diph.dwObj = 0;
    range.lMin = -1000;
    range.lMax = 1000;
    IDirectInputDevice8_SetProperty(d->dev, DIPROP_RANGE, &range.diph);
    IDirectInputDevice8_Acquire(d->dev);
    DIDEVCAPS caps;
    caps.dwSize = sizeof caps;
    if (SUCCEEDED(IDirectInputDevice8_GetCapabilities(d->dev, &caps)))
        logmsg("input:   %lu axes, %lu buttons, %lu hats%s", (unsigned long)caps.dwAxes,
               (unsigned long)caps.dwButtons, (unsigned long)caps.dwPOVs,
               FAILED(co) ? " (could not set background access)" : "");
    DG.ndev++;
    return DIENUM_CONTINUE;
}

static void diag_dinput_scan(void)
{
    if (!DG.di) return;
    for (int i = 0; i < DG.ndev; i++) DG.dev[i].present = 0;
    IDirectInput8_EnumDevices(DG.di, DI8DEVCLASS_GAMECTRL, diag_enum_device, NULL, DIEDFL_ATTACHEDONLY);
    for (int i = 0; i < DG.ndev; i++)
        if (!DG.dev[i].present && !DG.dev[i].gone) {
            logmsg("input: DirectInput device %d '%s' removed", i, DG.dev[i].name);
            DG.dev[i].gone = 1;
        }
}

static void diag_dinput_poll(void)
{
    for (int i = 0; i < DG.ndev; i++) {
        diag_dev_t *d = &DG.dev[i];
        if (!d->dev || !d->present) continue;
        IDirectInputDevice8_Poll(d->dev);
        DIJOYSTATE2 js;
        HRESULT hr = IDirectInputDevice8_GetDeviceState(d->dev, sizeof js, &js);
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
            IDirectInputDevice8_Acquire(d->dev);
            continue;
        }
        if (FAILED(hr)) continue;
        char line[256];
        int n = snprintf(line, sizeof line, "X=%+d Y=%+d Z=%+d RX=%+d RY=%+d RZ=%+d S0=%+d S1=%+d",
                         diag_q10(js.lX, 1000), diag_q10(js.lY, 1000), diag_q10(js.lZ, 1000),
                         diag_q10(js.lRx, 1000), diag_q10(js.lRy, 1000), diag_q10(js.lRz, 1000),
                         diag_q10(js.rglSlider[0], 1000), diag_q10(js.rglSlider[1], 1000));
        for (int h = 0; h < 4 && n < (int)sizeof line - 16; h++)
            if (LOWORD(js.rgdwPOV[h]) != 0xFFFF)
                n += snprintf(line + n, sizeof line - n, " hat%d=%lu", h, (unsigned long)(js.rgdwPOV[h] / 100));
        n += snprintf(line + n, sizeof line - n, " buttons=");
        int any = 0;
        for (int b = 0; b < 128 && n < (int)sizeof line - 8; b++)
            if (js.rgbButtons[b] & 0x80) {
                n += snprintf(line + n, sizeof line - n, "%s%d", any ? "," : "", b);
                any = 1;
            }
        if (!any) snprintf(line + n, sizeof line - n, "-");
        if (strcmp(line, d->last) != 0) {
            diag_log("input: DirectInput %d '%s' %s", i, d->name, line);
            strcpy(d->last, line);
        }
    }
}

static DWORD WINAPI input_diag_thread(LPVOID unused)
{
    (void)unused;
    int started = 0;
    for (;;) {
        if (!cfg.input_diag) { Sleep(500); continue; }
        if (!started) {
            started = 1;
            logmsg("input: controller diagnostics ON - logging XInput pads and DirectInput game controllers (read-only)");
            diag_xinput_init();
            EnumWindows(diag_find_window, 0);
            HMODULE m = LoadLibraryW(L"dinput8.dll");
            fn_di8_create create = m ? (fn_di8_create)GetProcAddress(m, "DirectInput8Create") : NULL;
            if (!create || FAILED(create(g_self, DIRECTINPUT_VERSION, &IID_IDirectInput8W, (LPVOID *)&DG.di, NULL))) {
                DG.di = NULL;
                logmsg("input: DirectInput unavailable");
            }
        }
        ULONGLONG now = GetTickCount64();
        if (now >= DG.next_scan) {
            DG.next_scan = now + 5000;
            if (!DG.hwnd) EnumWindows(diag_find_window, 0);
            diag_dinput_scan();
        }
        diag_xinput_poll();
        diag_dinput_poll();
        Sleep(50);
    }
    return 0;
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
            HANDLE diag = CreateThread(NULL, 0, input_diag_thread, NULL, 0, NULL);
            if (diag) CloseHandle(diag);
            retune_init();
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
    int ok = resolve(1) && G.off_controller >= 0;
    /* Flight data: classes, instance and the engine/control member tables are
     * in the file. cGcSpaceshipGlobals' own member table is filled in by the
     * game's startup code, so that part is only checkable in game. */
    int rt = retune_find(1), nested = 0;
    for (size_t t = 0; rt && t < N_ENGINE_TUNES; t++) {
        const meta_member_t *eng = meta_member(RT.control, ENGINE_TUNES[t].engine);
        const meta_member_t *fld = meta_member(RT.engine, ENGINE_TUNES[t].field);
        nested += eng && eng->type == META_CUSTOM && fld && fld->type == META_FLOAT && fld->size == 4;
    }
    logmsg("  flight data: %d/%d engine fields found by name; top-level field table %s", nested,
           (int)N_ENGINE_TUNES, meta_populated(RT.globals) ? "filled" : "filled at runtime (checked in game)");
    ok = ok && rt && nested == (int)N_ENGINE_TUNES;
    logmsg("SELFTEST %s", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
#endif

#ifdef BF_SIMTEST
/* ==========================================================================
 * Offline flight-logic simulator. Replaces the game functions with a model
 * ship that behaves the way No Man's Sky's does (velocity carried round with
 * the ship when it rotates) and runs the real flight_tick() against it.
 * ========================================================================== */
static float W[3];                 /* world velocity */
static float sim_game_accel[3];    /* game-applied accel in ship axes: right, up, at */
static float sim_steer;            /* game steers velocity toward the nose (m/s^2), force-based model */
static float sim_reverse_brake;    /* game's reverse-speed limit: pushes forward when going backwards > 134 m/s */
static float sim_swing;            /* game swings velocity toward the nose, keeping speed (m/s^2) */
static float R[3][3];              /* rows: right, up, at (world space) */
static int   sim_writes;
static uint8_t sim_ship[0x100], sim_phys[0x100], sim_state_blob[0x10];

/* Second ship (tests M*): another ship the game also runs UpdateControlled on -
 * a parked corvette, or another player's ship in multiplayer. */
static float Wb[3], Rb[3][3];
static int   sim_writes_b;
static uint8_t sim_ship_b[0x100], sim_phys_b[0x100];

static void * __fastcall sim_get_velocity(void *ship, float *out)
{
    const float *w = ship == sim_ship_b ? Wb : W;
    out[0] = w[0]; out[1] = w[1]; out[2] = w[2]; out[3] = 0; return out;
}
static void __fastcall sim_set_linear_velocity(void *rb, const float *v, uint8_t n)
{
    (void)n;
    if (rb == sim_phys_b + 0x20) { Wb[0] = v[0]; Wb[1] = v[1]; Wb[2] = v[2]; sim_writes_b++; return; }
    W[0] = v[0]; W[1] = v[1]; W[2] = v[2]; sim_writes++;
}
static float * __fastcall sim_get_transform(void *rb, float *out)
{
    float (*r)[3] = rb == sim_phys_b + 0x20 ? Rb : R;
    for (int i = 0; i < 3; i++) { out[i] = -r[0][i]; out[4 + i] = r[1][i]; out[8 + i] = r[2][i]; }
    return out;                                    /* row 0 = LEFT, as in the game */
}

/* one frame of two ships, in either order the game might update them */
static void sim_run2(int frames, float dt, int b_first)
{
    for (int f = 0; f < frames; f++) {
        sim_ms += dt * 1000.0;
        if (b_first) { flight_tick(sim_ship_b, dt); flight_tick(sim_ship, dt); }
        else         { flight_tick(sim_ship, dt);   flight_tick(sim_ship_b, dt); }
    }
}

static void sim_reset(float v_at, float v_right, float v_up)
{
    float I[3][3] = { {1,0,0}, {0,1,0}, {0,0,1} };
    memcpy(R, I, sizeof R);
    for (int i = 0; i < 3; i++) W[i] = v_right * R[0][i] + v_up * R[1][i] + v_at * R[2][i];
    memset(g_ships, 0, sizeof g_ships); memset(&g_ship_none, 0, sizeof g_ship_none); g_cur = &g_ship_none;
    memset(sim_keys, 0, sizeof sim_keys);
    memset(sim_game_accel, 0, sizeof sim_game_accel); sim_steer = 0; sim_reverse_brake = 0; sim_swing = 0;
    g_decoupled = 0; g_enabled = 1; sim_writes = 0; g_handoff_until = 0; g_hard_until = 0;
}

/* rotate the ship: axis 0 = yaw (about up), 1 = roll (about at) */
static void sim_rotate(int axis, float ang, int carry)
{
    float old[3][3]; memcpy(old, R, sizeof R);
    float c = cosf(ang), sn = sinf(ang);
    int a = axis == 0 ? 0 : 0, b = axis == 0 ? 2 : 1;   /* yaw mixes right/at, roll mixes right/up */
    for (int i = 0; i < 3; i++) {
        R[a][i] = c * old[a][i] - sn * old[b][i];
        R[b][i] = sn * old[a][i] + c * old[b][i];
    }
    if (carry) {                                   /* the game's behaviour */
        float lr = dot3(W, old[0]), lu = dot3(W, old[1]), la = dot3(W, old[2]);
        for (int i = 0; i < 3; i++) W[i] = lr * R[0][i] + lu * R[1][i] + la * R[2][i];
    }
}

static float angle_deg(const float *a, const float *b)
{
    float la = len3(a), lb = len3(b);
    if (la < 1e-3f || lb < 1e-3f) return 0;
    float c = dot3(a, b) / (la * lb); if (c > 1) c = 1; if (c < -1) c = -1;
    return acosf(c) * 57.29578f;
}

static int fails;
static void check(const char *name, int ok, const char *fmt, ...)
{
    char msg[256]; va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    printf("  %s  %-52s %s\n", ok ? "PASS" : "FAIL", name, msg);
    if (!ok) fails++;
}

/* Retune test: how many entries don't hold their expected value. With the
 * retune on, always-scope entries are applied in both flight modes and
 * decoupled-scope entries only while decoupled; with it off everything is
 * restored to the loaded (base) value. */
static int sim_retune_bad(float base, int on, int decoupled)
{
    int bad = 0;
    for (int i = 0; i < RT.n; i++) {
        float v; memcpy(&v, RT.instance + RT.e[i].offset, 4);
        int want = on && (decoupled || RT.e[i].scope == TUNE_ALWAYS);
        float q = want ? (RT.e[i].op == BF_MUL ? base * RT.e[i].value : RT.e[i].value) : base;
        if (fabsf(v - q) > 1e-5f) bad++;
    }
    return bad;
}

/* run n frames of: game rotates (with carry), then our hook */
static void sim_run(int frames, float dt, int axis, float rate, int carry)
{
    for (int f = 0; f < frames; f++) {
        if (rate != 0) sim_rotate(axis, rate * dt, carry);
        for (int i = 0; i < 3; i++)
            W[i] += (R[0][i] * sim_game_accel[0] + R[1][i] * sim_game_accel[1] + R[2][i] * sim_game_accel[2]) * dt;
        if (sim_steer > 0) {                       /* force-based: pull the off-nose part toward zero */
            float fa = dot3(W, R[2]), perp[3];
            for (int i = 0; i < 3; i++) perp[i] = W[i] - R[2][i] * fa;
            float pm = len3(perp), st = sim_steer * dt;
            if (pm > 1e-4f) { if (st > pm) st = pm; for (int i = 0; i < 3; i++) W[i] -= perp[i] / pm * st; }
        }
        if (sim_reverse_brake > 0 && dot3(W, R[2]) < -134.0f)
            for (int i = 0; i < 3; i++) W[i] += R[2][i] * sim_reverse_brake * dt;
        if (sim_swing > 0) {
            float sp = len3(W);
            if (sp > 1e-3f) {
                float vh[3] = { W[0] / sp, W[1] / sp, W[2] / sp }, d = dot3(R[2], vh), pp[3];
                for (int i = 0; i < 3; i++) pp[i] = R[2][i] - vh[i] * d;
                float pm = len3(pp);
                if (pm > 1e-4f) {
                    for (int i = 0; i < 3; i++) W[i] += pp[i] / pm * sim_swing * dt;
                    float ns = len3(W);
                    for (int i = 0; i < 3; i++) W[i] *= sp / ns;
                }
            }
        }
        sim_ms += dt * 1000.0;
        flight_tick(sim_ship, dt);
    }
}

int main(void)
{
    InitializeCriticalSection(&g_log_cs);
    G.get_velocity = sim_get_velocity;
    G.set_linear_velocity = sim_set_linear_velocity;
    G.get_transform = sim_get_transform;
    G.off_physics = 0x10; G.off_rigid_body = 0x20; G.off_state = 0x08;
    G.off_controller = G.off_controller_active = -1;        /* test P turns the pilot check on */
    *(void **)(sim_ship + 0x10) = sim_phys;
    *(void **)(sim_phys + 0x20 + 0x08) = sim_state_blob;
#define KS1(x) ((keyset_t){ {x}, 1 })
    cfg.strafe_left = KS1('A'); cfg.strafe_right = KS1('D'); cfg.strafe_up = KS1(VK_SPACE); cfg.strafe_down = KS1(VK_LCONTROL);
    cfg.toggle = KS1(VK_F8); cfg.decouple = KS1('Z');
    cfg.thrust = KS1('W'); cfg.brake = KS1('S'); cfg.boost = KS1(VK_LSHIFT);
    cfg.autopilot_keys = KS1('N'); cfg.autopilot_seconds = 6; cfg.autopilot_accel = 300; cfg.low_speed_autopilot_accel = 20;
    cfg.impact_accel = 6000;
    cfg.lateral_accel = 70; cfg.vertical_accel = 55; cfg.max_strafe_speed = 140;
    cfg.drift_cap = 2500; cfg.momentum_min_speed = 30; cfg.world_momentum = 1; cfg.debug = 0;
    const float dt = 1.0f / 60, HALF_PI = 1.5707963f;
    float w0[3];

    printf("Better Flight %s - offline flight-logic simulation\n", BF_VERSION);
    printf("Two models of the game: CARRY (velocity carried round with the ship) and\n"
           "STEER (forces pull velocity toward the nose). The mod must be right under both.\n\n");

    /* A. reference: mod OFF, both models bend velocity with the ship */
    sim_reset(500, 0, 0); memcpy(w0, W, sizeof w0); g_enabled = 0;
    sim_run(60, dt, 0, HALF_PI, 1);
    check("A vanilla CARRY: 90deg yaw turns velocity with ship", fabsf(angle_deg(W, w0) - 90) < 1,
          "turned %.1f deg", angle_deg(W, w0));
    sim_reset(500, 0, 0); memcpy(w0, W, sizeof w0); g_enabled = 0; sim_steer = 400;
    sim_run(120, dt, 0, HALF_PI / 2, 0);
    check("A' vanilla STEER: velocity bent toward the nose", angle_deg(W, w0) > 30,
          "turned %.1f deg", angle_deg(W, w0));

    /* B. decoupled: velocity stays fixed in the world under both models */
    sim_reset(500, 0, 0); memcpy(w0, W, sizeof w0); g_decoupled = 1;
    sim_run(60, dt, 0, HALF_PI, 1);
    check("B decoupled CARRY: 90deg yaw keeps world velocity", angle_deg(W, w0) < 2 && fabsf(len3(W) - 500) < 5,
          "turned %.2f deg, |v|=%.1f", angle_deg(W, w0), len3(W));
    sim_reset(500, 0, 0); memcpy(w0, W, sizeof w0); g_decoupled = 1; sim_steer = 400;
    sim_run(120, dt, 0, HALF_PI / 2, 0);
    check("B' decoupled STEER: 90deg yaw keeps world velocity", angle_deg(W, w0) < 2 && fabsf(len3(W) - 500) < 5,
          "turned %.2f deg, |v|=%.1f", angle_deg(W, w0), len3(W));

    /* Q. the asteroid (decoupled): coasting at it, look left -> still heading
     * straight at it */
    for (int model = 0; model < 2; model++) {
        sim_reset(300, 0, 0); memcpy(w0, W, sizeof w0); g_decoupled = 1;
        if (model) sim_steer = 400;
        sim_run(60, dt, 0, HALF_PI / 2, model == 0);    /* 45 deg left over 1s */
        check(model ? "Q' asteroid STEER: look left, keep heading at it" : "Q asteroid CARRY: look left, keep heading at it",
              angle_deg(W, w0) < 2 && fabsf(len3(W) - 300) < 5,
              "velocity turned %.2f deg, |v| 300 -> %.1f", angle_deg(W, w0), len3(W));
    }

    /* C. coupled: the game's flight model runs untouched - under CARRY the
     * velocity turns with the ship and the mod never writes (no braking, no
     * momentum discard). The real game's own assists then slow it down. */
    sim_reset(500, 0, 0); memcpy(w0, W, sizeof w0);
    sim_writes = 0;
    sim_run(60, dt, 0, HALF_PI, 1);
    sim_run(600, dt, 0, 0, 1);
    check("C coupled: velocity turns with the ship, mod writes nothing",
          fabsf(angle_deg(W, w0) - 90) < 1 && fabsf(len3(W) - 500) < 5 && sim_writes == 0,
          "turned %.1f deg, |v|=%.1f, writes=%d", angle_deg(W, w0), len3(W), sim_writes);

    /* D. 2000 m/s sideways, rolling hard, coupled: the game's OWN flight
     * assist (simulated) shrinks the drift - the mod writes nothing */
    sim_reset(0, 2000, 0); sim_steer = 100;
    sim_writes = 0;
    sim_run(1, dt, 1, 0, 1);
    sim_run(300, dt, 1, HALF_PI, 1);
    check("D coupled: game's own assist shrinks the drift while rolling",
          len3(W) <= 1750 && len3(W) > 0 && sim_writes == 0,
          "|v| 2000 -> after 5s %.0f, writes=%d", len3(W), sim_writes);
    sim_reset(0, 2000, 0); g_decoupled = 1;
    sim_run(1, dt, 1, 0, 1); memcpy(w0, W, sizeof w0);
    sim_run(300, dt, 1, HALF_PI, 1);
    check("D' decoupled: drift stays world-fixed while rolling", angle_deg(W, w0) < 2 && len3(W) <= 2000.5f,
          "turned %.2f deg, |v|=%.1f", angle_deg(W, w0), len3(W));

    /* E. pulse drive speed: hands off */
    sim_reset(5000, 0, 0); memcpy(w0, W, sizeof w0);
    sim_run(60, dt, 0, HALF_PI, 1);
    check("E pulse speed: no writes, game keeps control", sim_writes == 0, "writes=%d", sim_writes);

    /* F. strafe */
    sim_reset(0, 0, 0); sim_keys['D'] = 1;
    sim_run(180, dt, 0, 0, 1);
    check("F strafe: D builds rightward speed to the limit", fabsf(dot3(W, R[0]) - 140) < 1,
          "sideways after 3s = %.1f", dot3(W, R[0]));
    sim_keys['D'] = 0; sim_writes = 0; sim_run(240, dt, 0, 0, 1);
    check("F' coupled: releasing D, the sideways speed coasts (no mod braking)",
          fabsf(dot3(W, R[0]) - 140) < 1 && sim_writes == 0,
          "sideways 4s after release = %.2f, writes=%d", dot3(W, R[0]), sim_writes);

    /* AC. coupled strafe vs the vanilla flight assist: the assist bleeds off
     * sideways velocity faster than the strafe thrust (70 m/s^2) builds it, so
     * at full assist strength coupled strafe can't build sideways speed at all
     * - it is a gentle push, not free drift. With the decoupled retune the
     * assist is cut 10x and the same thrust builds to the cap. Accepted
     * design: users who want more raise LateralAccel/VerticalAccel. */
    sim_reset(0, 0, 0); sim_steer = 150; sim_keys['D'] = 1;
    sim_run(180, dt, 0, 0, 1);
    float ac_held = dot3(W, R[0]);
    sim_keys['D'] = 0; sim_run(60, dt, 0, 0, 1);
    check("AC coupled strafe vs full vanilla assist: assist eats the thrust",
          ac_held < 5 && fabsf(dot3(W, R[0])) < 5,
          "sideways while held = %.2f (cap 140)", ac_held);
    sim_reset(0, 0, 0); sim_steer = 15; sim_keys['D'] = 1;
    sim_run(180, dt, 0, 0, 1);
    check("AC' decoupled strafe vs cut assist (x0.1): builds to the cap",
          fabsf(dot3(W, R[0]) - 140) < 1, "sideways after 3s = %.1f", dot3(W, R[0]));

    /* S. the 1.3.0 bug: the game steers the velocity toward the nose
     * (sim_swing) in response to sustained drift, converting it into forward
     * speed. In coupled strafe the mod cancels only the nose-axis part of
     * that push - the drift builds (and bleeds) as before, no forward gained. */
    sim_reset(0, 100, 0); g_enabled = 0; sim_swing = 40;
    sim_run(180, dt, 0, 0, 0);
    check("S0 vanilla: nose-steering converts drift into forward speed",
          dot3(W, R[2]) > 50, "fwd after 3s = %.1f", dot3(W, R[2]));
    g_enabled = 1; sim_swing = 0;
    sim_reset(0, 30, 0); sim_swing = 40; sim_keys['D'] = 1;
    sim_run(180, dt, 0, 0, 0);
    check("S coupled strafe vs nose-steering: drift builds, no forward gained",
          dot3(W, R[0]) > 50 && fabsf(dot3(W, R[2])) < 2,
          "side=%.1f fwd=%.1f after 3s", dot3(W, R[0]), dot3(W, R[2]));
    sim_keys['D'] = 0; sim_swing = 0;
    sim_reset(0, 30, 0); sim_game_accel[2] = 30; sim_keys['D'] = 1;
    sim_run(180, dt, 0, 0, 0);
    check("S' coupled strafe: game's nose-axis push cancelled, drift kept",
          dot3(W, R[0]) > 50 && fabsf(dot3(W, R[2])) < 1,
          "side=%.1f fwd=%.1f after 3s", dot3(W, R[0]), dot3(W, R[2]));
    sim_keys['D'] = 0; sim_game_accel[2] = 0;
    sim_reset(0, 0, 0); sim_game_accel[1] = 40; sim_keys['D'] = 1;
    sim_run(120, dt, 0, 0, 0);
    check("S'' coupled strafe + vertical lift: lift not cancelled",
          dot3(W, R[1]) > 20, "up after 2s = %.1f", dot3(W, R[1]));
    sim_keys['D'] = 0; sim_game_accel[1] = 0;
    /* S3. the 1.3.1 bug: releasing the strafe key leaves residual drift that
     * the game then converts into forward speed. The cancellation stays armed
     * 5 s after the last strafe frame, so the drift bleeds off sideways and no
     * forward kick is gained. */
    sim_reset(0, 30, 0); sim_swing = 40; sim_steer = 25; sim_keys['D'] = 1;
    sim_run(90, dt, 0, 0, 0);              /* 1.5s holding: drift builds */
    sim_keys['D'] = 0;                     /* release; the game keeps converting */
    sim_run(300, dt, 0, 0, 0);             /* 5s coasting */
    check("S3 release: residual drift bleeds off, no forward kick",
          fabsf(dot3(W, R[2])) < 6 && dot3(W, R[0]) < 10,
          "side=%.1f fwd=%.1f after 5s coast", dot3(W, R[0]), dot3(W, R[2]));
    sim_swing = 0; sim_steer = 0;
    /* S4. no strafe: a sliding turn leaves off-nose velocity, and the game
     * steering it back to the nose is vanilla feel - the release window only
     * arms from strafe activity, so the mod must not interfere. */
    sim_reset(150, 150, 0); sim_swing = 40; sim_writes = 0;
    sim_run(180, dt, 0, 0, 0);
    check("S4 slide turn, no strafe: game aligns velocity to nose, no writes",
          dot3(W, R[2]) > 180 && sim_writes == 0,
          "fwd=%.1f writes=%d", dot3(W, R[2]), sim_writes);
    sim_swing = 0;
    /* S5. the 1.3.3 case: decoupled momentum flight leaves large off-nose
     * velocity; switching to coupled (via the decouple key) arms the release
     * window, so the game's nose-axis conversion is cancelled and the drift
     * bleeds off at the game's own rate (sim_steer models the direct bleed). */
    sim_reset(0, 1600, 0); g_decoupled = 1;
    sim_run(30, dt, 0, 0, 0);             /* establish decoupled state */
    sim_swing = 40; sim_steer = 400;
    sim_keys[0x5A] = 1; sim_run(1, dt, 0, 0, 0); sim_keys[0x5A] = 0;  /* switch */
    sim_run(299, dt, 0, 0, 0);            /* 5s in coupled */
    check("S5 coupled switch with 1600 m/s sideways: no nose kick, drift bleeds",
          fabsf(dot3(W, R[2])) < 20 && dot3(W, R[0]) < 50,
          "side=%.1f fwd=%.1f after 5s", dot3(W, R[0]), dot3(W, R[2]));
    sim_swing = 0; sim_steer = 0;
    /* S6. switch to coupled with no off-nose drift: nothing arms, vanilla */
    sim_reset(300, 0, 0); g_decoupled = 1;
    sim_run(30, dt, 0, 0, 0);
    sim_swing = 40;
    sim_keys[0x5A] = 1; sim_run(1, dt, 0, 0, 0); sim_keys[0x5A] = 0;  /* switch */
    sim_writes = 0;
    sim_run(180, dt, 0, 0, 0);
    check("S6 coupled switch, no drift: vanilla, no writes",
          sim_writes == 0, "writes=%d", sim_writes);
    sim_swing = 0;

    /* G. slow flight: game changes pass through below MomentumMinSpeed */
    sim_reset(20, 0, 0); memcpy(w0, W, sizeof w0); g_decoupled = 1;
    sim_run(60, dt, 0, HALF_PI, 1);
    check("G below 30 m/s: game still turns velocity (landing/hover)", fabsf(angle_deg(W, w0) - 90) < 2,
          "turned %.1f deg", angle_deg(W, w0));

    /* H. F8 (fly a frame first: a key already down on the frame a ship is first
     *    seen counts as held, not pressed - as when switching ships) */
    sim_reset(500, 0, 0); sim_run(1, dt, 0, 0, 1); memcpy(w0, W, sizeof w0);
    sim_keys[VK_F8] = 1; sim_run(1, dt, 0, 0, 1); sim_keys[VK_F8] = 0;
    sim_writes = 0; sim_run(60, dt, 0, HALF_PI, 1);
    check("H F8 off: no writes, vanilla behaviour", sim_writes == 0 && fabsf(angle_deg(W, w0) - 90) < 1,
          "writes=%d turned %.1f deg", sim_writes, angle_deg(W, w0));
    g_enabled = 1;

    /* J. decoupled coasts */
    sim_reset(300, 0, 0); g_decoupled = 1;
    sim_run(480, dt, 0, 0, 1);
    check("J decoupled: coasts with throttle released", fabsf(len3(W) - 300) < 0.5f, "|v| after 8s = %.1f", len3(W));

    /* K. sliding turn in decoupled, then couple: the mod stops writing - the
     * velocity is left to the game, nothing is converted into forward speed */
    sim_reset(0, 300, 0); g_decoupled = 1;
    sim_run(30, dt, 0, 1.5707963f, 1);
    float k_fwd = dot3(W, R[2]);
    sim_keys['Z'] = 1; sim_run(1, dt, 0, 0, 1); sim_keys['Z'] = 0;
    sim_writes = 0;
    sim_run(600, dt, 0, 0, 1);
    check("K coupled after sliding turn: velocity left to the game, no writes",
          fabsf(dot3(W, R[2]) - k_fwd) < 0.5f && fabsf(len3(W) - 300) < 0.5f && sim_writes == 0,
          "fwd at switch %.1f -> %.1f, |v|=%.1f, writes=%d", k_fwd, dot3(W, R[2]), len3(W), sim_writes);

    /* L. W held: the game's throttle passes through */
    sim_reset(0, 0, 0); sim_keys['W'] = 1; sim_game_accel[2] = 50;
    sim_run(180, dt, 0, 0, 1);
    check("L coupled + W: game throttle accelerates normally", fabsf(dot3(W, R[2]) - 150) < 3 && g_handoff_until == 0,
          "fwd after 3s = %.1f (expect 150)", dot3(W, R[2]));

    /* M. take-off from rest: low-speed push -> hand-off, lift not fought */
    sim_reset(0, 0, 0); sim_game_accel[1] = 40;
    sim_run(120, dt, 0, 0, 1);
    check("M take-off hand-off: game lift not fought", dot3(W, R[1]) > 75, "vertical after 2s = %.1f", dot3(W, R[1]));

    /* N. pulse spool-up */
    sim_reset(200, 0, 0); sim_game_accel[2] = 2500;
    sim_run(180, dt, 0, 0, 1);
    check("N pulse drive: accelerates past pulse threshold", len3(W) > 4000, "|v| after 3s = %.0f", len3(W));

    /* R. land key: the game's landing manoeuvre is not overridden */
    sim_reset(100, 0, 0);
    sim_run(5, dt, 0, 0, 1);
    float r0 = dot3(W, R[2]);
    sim_keys['N'] = 1; sim_run(1, dt, 0, 0, 1); sim_keys['N'] = 0;
    sim_game_accel[2] = -20;
    sim_run(119, dt, 0, 0, 1);
    check("R land key hand-off: game's approach drives the ship", fabsf(dot3(W, R[2]) - (r0 - 40)) < 3,
          "fwd %.1f -> %.1f after 2s (game-only would be %.1f)", r0, dot3(W, R[2]), r0 - 40);

    /* U. decoupled boost release: keep boost speed and direction, no hand-off */
    sim_reset(1300, 0, 0); g_decoupled = 1;
    sim_run(1, dt, 0, 0, 1); memcpy(w0, W, sizeof w0);  /* mod already running before release */
    sim_game_accel[2] = -1000;                        /* game's post-boost overspeed brake */
    sim_run(240, dt, 0, 0, 1);
    check("U decoupled boost release: speed and heading kept", fabsf(len3(W) - 1300) < 2 && angle_deg(W, w0) < 0.5f && g_handoff_until == 0,
          "|v| 1300 -> %.1f after 4s, heading change %.2f deg, hand-off=%d", len3(W), angle_deg(W, w0), g_handoff_until != 0);
    sim_reset(1300, 0, 0); g_decoupled = 1; sim_keys['W'] = 1;
    sim_run(1, dt, 0, 0, 1); sim_game_accel[2] = -1000;
    sim_run(240, dt, 0, 0, 1);
    check("U' holding W at boost speed isn't dragged down", fabsf(len3(W) - 1300) < 2, "|v| 1300 -> %.1f after 4s", len3(W));
    sim_reset(1300, 0, 0);
    sim_run(1, dt, 0, 0, 1); sim_game_accel[2] = -1000;
    sim_writes = 0;
    sim_run(36, dt, 0, 0, 1);
    check("U'' coupled boost release: game's own overspeed brake passes through, mod writes nothing",
          fabsf(dot3(W, R[2]) - 700) < 5 && sim_writes == 0,
          "|v| 1300 -> %.1f after 0.6s (game-only 700), writes=%d", dot3(W, R[2]), sim_writes);

    /* V. pulse exit: the game's deceleration below pulse speed still happens */
    sim_reset(4500, 0, 0); sim_game_accel[2] = -2000;
    sim_run(90, dt, 0, 0, 1);
    check("V pulse exit: game deceleration not discarded", fabsf(dot3(W, R[2]) - 1500) < 40,
          "fwd 4500 -> %.0f after 1.5s (game-only 1500)", dot3(W, R[2]));

    /* W. turn at full boost speed in decoupled: speed kept */
    sim_reset(1400, 0, 0); g_decoupled = 1; memcpy(w0, W, sizeof w0);
    sim_run(60, dt, 0, HALF_PI, 1);
    check("W decoupled 90deg turn at 1400 m/s: speed and heading kept", fabsf(len3(W) - 1400) < 2 && angle_deg(W, w0) < 2,
          "|v| %.1f, heading change %.2f deg", len3(W), angle_deg(W, w0));

    /* X. low-speed throttle wind-down is not a hand-off */
    sim_reset(25, 0, 0); sim_game_accel[2] = -50;
    sim_run(10, dt, 0, 0, 1);
    check("X low-speed forward wind-down: no hand-off", g_handoff_until == 0, "hand-off=%d", g_handoff_until != 0);

    /* Y. the requested manoeuvre: 1400 m/s decoupled, flip 180 deg (game braking
     *    and steering against it), keep going backwards at 1400, then couple -> stop */
    sim_reset(1400, 0, 0); g_decoupled = 1;
    sim_run(1, dt, 0, 0, 0);                             /* one quiet frame: mod has a previous velocity */
    memcpy(w0, W, sizeof w0); sim_steer = 100; sim_game_accel[2] = -500;
    sim_run(120, dt, 0, 1.5707963f, 0);                  /* 180 deg over 2s */
    check("Y decoupled 180deg flip at 1400 m/s: still 1400 the same way", angle_deg(W, w0) < 1 && fabsf(len3(W) - 1400) < 5 && dot3(W, R[2]) < -1390,
          "direction change %.2f deg, |v|=%.1f, along nose %.1f", angle_deg(W, w0), len3(W), dot3(W, R[2]));
    sim_game_accel[2] = 0; sim_steer = 0;
    sim_keys['Z'] = 1; sim_run(1, dt, 0, 0, 0); sim_keys['Z'] = 0;
    sim_writes = 0;
    sim_run(1800, dt, 0, 0, 0);
    check("Y' then coupled: coasts at full speed, mod writes nothing "
          "(the real game's flight assist would slow it)",
          fabsf(len3(W) - 1400) < 5 && sim_writes == 0,
          "|v| 30s after coupling = %.2f, writes=%d", len3(W), sim_writes);

    /* Z. log 17:13:20 - travelling backwards, the game's reverse-speed limit pushes
     *    forward along the nose. Must not hand off; speed and heading kept. */
    sim_reset(1300, 0, 0); g_decoupled = 1;
    sim_run(1, dt, 0, 0, 0);
    memcpy(w0, W, sizeof w0); sim_reverse_brake = 600;
    sim_run(120, dt, 0, 1.5707963f, 0);            /* flip 180 deg over 2s */
    sim_run(120, dt, 0, 0, 0);                     /* coast backwards 2s */
    check("Z decoupled flip vs game reverse-speed limit: speed kept, no hand-off",
          fabsf(len3(W) - 1300) < 5 && angle_deg(W, w0) < 1 && dot3(W, R[2]) < -1290 && g_handoff_until == 0 && g_hard_until == 0,
          "|v|=%.1f, direction change %.2f deg, along nose %.1f, hand-off=%d", len3(W), angle_deg(W, w0), dot3(W, R[2]),
          g_handoff_until != 0 || g_hard_until != 0);

    /* Z'. log 17:12:47 - turning away at speed, the game swings velocity toward the
     *     nose (a big push along the nose). Must not hand off; speed and heading kept. */
    sim_reset(1344, 0, 0); g_decoupled = 1;
    sim_run(1, dt, 0, 0, 0);
    memcpy(w0, W, sizeof w0); sim_swing = 600;
    sim_run(120, dt, 0, 0.8f, 0);
    check("Z' decoupled turn vs game swinging velocity to nose: no hand-off",
          fabsf(len3(W) - 1344) < 5 && angle_deg(W, w0) < 1 && g_handoff_until == 0 && g_hard_until == 0,
          "|v|=%.1f, direction change %.2f deg, hand-off=%d", len3(W), angle_deg(W, w0),
          g_handoff_until != 0 || g_hard_until != 0);

    /* T. collision: a bounce is not braked or pushed back into the rock */
    sim_reset(200, 0, 0);
    sim_run(5, dt, 0, 0, 1);
    for (int i = 0; i < 3; i++) W[i] += R[2][i] * -400.0f;     /* hit: 200 -> -200 in one frame */
    sim_run(30, dt, 0, 0, 1);
    check("T impact hand-off: bounce kept, not undone", dot3(W, R[2]) < -150, "fwd 0.5s after impact = %.1f (bounce -200)", dot3(W, R[2]));

    /* O. bindings */
    keyset_t ks;
    int n1 = parse_keys("KeyA, Mouse4", &ks, NULL); int ok1 = n1 == 2 && ks.vk[0] == 'A' && ks.vk[1] == VK_XBUTTON1;
    int n2 = parse_keys("Mouse1 Mouse3,Mouse5", &ks, NULL); int ok2 = n2 == 3 && ks.vk[0] == VK_LBUTTON && ks.vk[1] == VK_MBUTTON && ks.vk[2] == VK_XBUTTON2;
    int n3 = parse_keys("Bogus", &ks, NULL);
    check("O parse: 'KeyA, Mouse4' / 'Mouse1 Mouse3,Mouse5' / bad name", ok1 && ok2 && n3 == 0, "counts %d %d %d", n1, n2, n3);
    sim_reset(0, 0, 0); cfg.strafe_right = (keyset_t){ {'D', VK_XBUTTON2}, 2 }; sim_keys[VK_XBUTTON2] = 1;
    sim_run(180, dt, 0, 0, 1);
    check("O' strafe right on Mouse5 (second binding)", fabsf(dot3(W, R[0]) - 140) < 1, "sideways after 3s = %.1f", dot3(W, R[0]));
    cfg.strafe_right = KS1('D');

    /* P. corvettes: the game runs the flight update with nobody at the controls
     *    (walking inside, EVA). Jetpack/roll keys must not fly the empty ship. */
    static void *sim_controller;
    sim_controller = sim_state_blob;
    G.off_controller = 0x40; G.off_controller_active = 0x48;
    *(void **)(sim_ship + 0x40) = &sim_controller;
    sim_reset(300, 0, 0); sim_ship[0x48] = 0;
    sim_keys['D'] = 1; sim_keys[VK_SPACE] = 1; sim_keys['Z'] = 1;
    sim_run(120, dt, 0, 0, 1);
    check("P corvette, pilot away: keys ignored, ship left alone",
          sim_writes == 0 && fabsf(dot3(W, R[2]) - 300) < 0.01f && fabsf(dot3(W, R[0])) < 0.01f && !g_decoupled,
          "writes=%d fwd=%.1f side=%.1f decoupled=%d", sim_writes, dot3(W, R[2]), dot3(W, R[0]), g_decoupled);
    sim_ship[0x48] = 1;                                      /* back in the seat, Z still held */
    sim_run(60, dt, 0, 0, 1);
    check("P' back at the controls: strafe works, held Z not a press",
          dot3(W, R[0]) > 50 && !g_decoupled, "side after 1s=%.1f decoupled=%d", dot3(W, R[0]), g_decoupled);
    sim_reset(300, 0, 0); sim_ship[0x48] = 1; *(void **)(sim_ship + 0x40) = NULL; sim_keys['D'] = 1;
    sim_run(60, dt, 0, 0, 1);
    check("P'' no controller handle: ship left alone", sim_writes == 0, "writes=%d", sim_writes);
    G.off_controller = G.off_controller_active = -1;

    /* R. flight data retune (replaces the GCSPACESHIPGLOBALS data mod), on fake
     *    reflection metadata laid out like the game's: two control blocks, a
     *    member of another class that must be skipped, global floats. */
    {
        static meta_member_t eng_m[16], ctl_m[4], glob_m[16];
        static meta_class_t  eng_c, ctl_c, glob_c;
        static uint8_t       inst[0x400];
        const char *engines[4] = { "AtmosCombatEngine", "CombatEngine", "PlanetEngine", "SpaceEngine" };
        int ne = 0, turn_idx = -1;
        for (size_t t = 0; t < N_ENGINE_TUNES; t++) {
            int dup = 0;
            for (int k = 0; k < ne; k++) dup |= strcmp(eng_m[k].name, ENGINE_TUNES[t].field) == 0;
            if (dup) continue;
            if (strcmp(ENGINE_TUNES[t].field, "TurnStrength") == 0) turn_idx = ne;
            eng_m[ne] = (meta_member_t){ .name = ENGINE_TUNES[t].field, .type = META_FLOAT, .size = 4, .count = 1, .offset = 4 * ne };
            ne++;
        }
        eng_m[ne] = (meta_member_t){ .name = "ThrustForce", .type = META_FLOAT, .size = 4, .count = 1, .offset = 4 * ne };
        ne++;
        eng_c = (meta_class_t){ "cGcPlayerSpaceshipEngineData", 0, 0, eng_m, ne, 0x40 };
        for (int k = 0; k < 4; k++)
            ctl_m[k] = (meta_member_t){ .name = engines[k], .type = META_CUSTOM, .size = 0x40, .count = 1, .offset = 0x40 * k, .cls = &eng_c };
        ctl_c = (meta_class_t){ "cGcPlayerSpaceshipControlData", 0, 0, ctl_m, 4, 0x100 };
        int ng = 0;
        glob_m[ng++] = (meta_member_t){ .name = "Control",            .type = META_CUSTOM, .size = 0x100, .count = 1, .offset = 0x000, .cls = &ctl_c };
        glob_m[ng++] = (meta_member_t){ .name = "ControlCorvette",    .type = META_CUSTOM, .size = 0x100, .count = 1, .offset = 0x100, .cls = &ctl_c };
        glob_m[ng++] = (meta_member_t){ .name = "HoverShipDataNames", .type = META_CUSTOM, .size = 0x40,  .count = 1, .offset = 0x300, .cls = &eng_c };
        for (size_t t = 0; t < N_GLOBAL_TUNES; t++)
            glob_m[ng++] = (meta_member_t){ .name = GLOBAL_TUNES[t].field, .type = META_FLOAT, .size = 4, .count = 1, .offset = 0x200 + 4 * (int)t };
        glob_c = (meta_class_t){ "cGcSpaceshipGlobals", 0, 0, glob_m, ng, (int32_t)sizeof inst };
#define SIM_FILL(v) do { for (int k = 0; k < (int)sizeof inst; k += 4) { float f_ = (v); memcpy(inst + k, &f_, 4); } } while (0)

        memset(&RT, 0, sizeof RT);
        RT.globals = &glob_c; RT.control = &ctl_c; RT.engine = &eng_c; RT.instance = inst; RT.found = 1;
        cfg.flight_retune = 1; g_enabled = 1; g_decoupled = 1;
        SIM_FILL(2.0f);
        int want_n = (int)(2 * N_ENGINE_TUNES + N_GLOBAL_TUNES);
        int ok_expand = retune_expand(0);
        check("R retune: every field found by name, both control blocks", ok_expand && RT.n == want_n,
              "resolved %d, expected %d", RT.n, want_n);

        retune_tick();
        float turn = 0, turn_mul = 0; int turn_off = -1;
        for (int i = 0; i < RT.n; i++)
            if (strcmp(RT.e[i].label, "ControlCorvette.SpaceEngine.TurnStrength") == 0) {
                turn_off = RT.e[i].offset; turn_mul = RT.e[i].value; memcpy(&turn, inst + turn_off, 4);
            }
        check("R' applied on top of the loaded values", sim_retune_bad(2.0f, 1, 1) == 0 &&
              turn_off == 0x100 + 0xC0 + 4 * turn_idx && fabsf(turn - 2.0f * turn_mul) < 1e-5f,
              "%d wrong; ControlCorvette.SpaceEngine.TurnStrength @%#x = %.3f", sim_retune_bad(2.0f, 1, 1), turn_off, turn);

        retune_tick(); retune_tick();
        check("R'' not applied twice", sim_retune_bad(2.0f, 1, 1) == 0, "%d wrong", sim_retune_bad(2.0f, 1, 1));

        /* The reload value must not equal anything we wrote (2 x 1.5 = 3 would):
         * a loaded value bit-identical to our last write reads as "already
         * applied". Deliberate - a file that already carries Better Flight's
         * values must not get them twice. */

        SIM_FILL(7.0f); retune_tick();
        check("R''' game reloads its data: applied again on the new values", sim_retune_bad(7.0f, 1, 1) == 0,
              "%d wrong", sim_retune_bad(7.0f, 1, 1));

        float other = 5.0f; memcpy(inst + turn_off, &other, 4); retune_tick();
        memcpy(&turn, inst + turn_off, 4);
        check("R4 another mod's value is scaled, the rest left as they are",
              fabsf(turn - 5.0f * turn_mul) < 1e-5f && sim_retune_bad(7.0f, 1, 1) == 1, "TurnStrength %.3f, others wrong %d",
              turn, sim_retune_bad(7.0f, 1, 1) - 1);

        SIM_FILL(7.0f); retune_tick();
        cfg.flight_retune = 0; retune_tick();
        int off_bad = sim_retune_bad(7.0f, 0, 0);
        cfg.flight_retune = 1; retune_tick();
        int on_bad = sim_retune_bad(7.0f, 1, 1);
        g_enabled = 0; retune_tick();
        int f8_bad = sim_retune_bad(7.0f, 0, 0);
        g_enabled = 1; retune_tick();
        check("R5 FlightRetune=0 and F8 put the loaded values back, and on again",
              off_bad == 0 && on_bad == 0 && f8_bad == 0 && sim_retune_bad(7.0f, 1, 1) == 0,
              "off %d, on %d, F8 %d wrong", off_bad, on_bad, f8_bad);

        /* R6: mode toggle WITH the retune on - decoupled-scope entries follow
         * the mode (restored to the loaded values in coupled), always-scope
         * entries stay applied in both. */
        g_decoupled = 0; retune_tick();
        float minspd = 0, ts2 = 0; int minspd_off = -1, ts2_off = -1;
        for (int i = 0; i < RT.n; i++) {
            if (strcmp(RT.e[i].label, "Control.SpaceEngine.MinSpeed") == 0)
                { minspd_off = RT.e[i].offset; memcpy(&minspd, inst + minspd_off, 4); }
            if (strcmp(RT.e[i].label, "Control.SpaceEngine.TurnStrength") == 0)
                { ts2_off = RT.e[i].offset; memcpy(&ts2, inst + ts2_off, 4); }
        }
        check("R6 coupled with retune on: always-scope applied, decoupled-scope restored",
              sim_retune_bad(7.0f, 1, 0) == 0 && minspd_off >= 0 && ts2_off >= 0 &&
              fabsf(minspd) < 1e-6f && fabsf(ts2 - 7.0f) < 1e-6f,
              "%d wrong; MinSpeed=%.3f TurnStrength=%.3f", sim_retune_bad(7.0f, 1, 0), minspd, ts2);
        g_decoupled = 1; retune_tick();
        check("R6' back to decoupled: everything applied again", sim_retune_bad(7.0f, 1, 1) == 0,
              "%d wrong", sim_retune_bad(7.0f, 1, 1));
#undef SIM_FILL
        memset(&RT, 0, sizeof RT);
        cfg.flight_retune = 0;
    }

    /* M. several ships through UpdateControlled in one frame. The game runs it for
     *    corvettes with nobody aboard, and (likely) other players' ships in
     *    multiplayer. Player reports: Z stuck in coupled; sent in random directions. */
    {
        static void *ctl;
        ctl = sim_state_blob;
        G.off_controller = 0x40; G.off_controller_active = 0x48;
        *(void **)(sim_ship_b + 0x10) = sim_phys_b;
        *(void **)(sim_phys_b + 0x20 + 0x08) = sim_state_blob;
        float I3[3][3] = { {1,0,0}, {0,1,0}, {0,0,1} };

        for (int order = 0; order < 2; order++) {
            /* M1: parked corvette (no pilot) updated every frame - Z must still toggle */
            sim_reset(300, 0, 0);
            *(void **)(sim_ship + 0x40) = &ctl; sim_ship[0x48] = 1;
            *(void **)(sim_ship_b + 0x40) = NULL; sim_ship_b[0x48] = 0;
            memcpy(Rb, I3, sizeof Rb); memset(Wb, 0, sizeof Wb);
            sim_run2(30, dt, order);
            sim_keys['Z'] = 1; sim_run2(5, dt, order);
            sim_keys['Z'] = 0; sim_run2(5, dt, order);
            check(order ? "M1 unpiloted ship updated first: Z still toggles" : "M1 unpiloted ship updated after: Z still toggles",
                  g_decoupled == 1, "decoupled=%d", g_decoupled);

            /* M3: same scene - 1.3.x expectation: no mod braking, so the
             *     piloted ship coasts untouched; the parked corvette is
             *     left alone (no velocity crossing between ships) */
            sim_reset(300, 0, 0);
            *(void **)(sim_ship + 0x40) = &ctl; sim_ship[0x48] = 1;
            memcpy(Rb, I3, sizeof Rb); memset(Wb, 0, sizeof Wb);
            sim_writes = 0; sim_writes_b = 0;
            sim_run2(60 * 8, dt, order);
            check(order ? "M3 unpiloted ship first: coupled coasts, parked ship untouched"
                        : "M3 unpiloted ship after: coupled coasts, parked ship untouched",
                  fabsf(len3(W) - 300) < 1 && len3(Wb) < 1 &&
                  sim_writes == 0 && sim_writes_b == 0,
                  "ship A |v| %.1f (was 300), ship B |v| %.1f, writes A %d B %d",
                  len3(W), len3(Wb), sim_writes, sim_writes_b);
        }

        /* M2: two piloted ships at nearby speeds - one's velocity must never be
         *     written into the other (decoupled, no input: nothing should change) */
        sim_reset(60, 0, 0); g_decoupled = 1;
        *(void **)(sim_ship + 0x40) = &ctl; sim_ship[0x48] = 1;
        *(void **)(sim_ship_b + 0x40) = &ctl; sim_ship_b[0x48] = 1;
        memcpy(Rb, I3, sizeof Rb); memset(Wb, 0, sizeof Wb); sim_writes_b = 0;
        sim_run2(60, dt, 0);
        check("M2 two piloted ships: no velocity crosses between them",
              fabsf(len3(W) - 60) < 1 && len3(Wb) < 1 && sim_writes_b == 0,
              "ship A |v| %.1f (was 60), ship B |v| %.1f (was 0), writes to B %d", len3(W), len3(Wb), sim_writes_b);

        /* M4: two piloted ships see the same Z press - it must toggle once, not twice */
        sim_reset(0, 0, 0);
        *(void **)(sim_ship + 0x40) = &ctl; sim_ship[0x48] = 1;
        *(void **)(sim_ship_b + 0x40) = &ctl; sim_ship_b[0x48] = 1;
        memcpy(Rb, I3, sizeof Rb); memset(Wb, 0, sizeof Wb);
        sim_run2(30, dt, 0);
        sim_keys['Z'] = 1; sim_run2(5, dt, 0);
        sim_keys['Z'] = 0; sim_run2(20, dt, 0);
        int once = g_decoupled == 1;
        sim_keys['Z'] = 1; sim_run2(5, dt, 1);
        sim_keys['Z'] = 0; sim_run2(20, dt, 1);
        check("M4 two ships, one Z press each time: toggles exactly once per press",
              once && g_decoupled == 0, "after 1st press decoupled=%d, after 2nd %d", once, g_decoupled);

        /* M5: the fix must not break sitting down with Z held (P' in a two-ship scene) */
        sim_reset(300, 0, 0);
        *(void **)(sim_ship + 0x40) = &ctl; sim_ship[0x48] = 0;          /* A: pilot away */
        *(void **)(sim_ship_b + 0x40) = NULL; sim_ship_b[0x48] = 0;      /* B: parked corvette */
        memcpy(Rb, I3, sizeof Rb); memset(Wb, 0, sizeof Wb);
        sim_keys['Z'] = 1; sim_run2(30, dt, 0);
        sim_ship[0x48] = 1; sim_run2(30, dt, 0);                          /* sit down, Z still held */
        check("M5 sitting down with Z held is not a press (with a parked corvette)", g_decoupled == 0, "decoupled=%d", g_decoupled);
        sim_keys['Z'] = 0;

        *(void **)(sim_ship + 0x40) = NULL; *(void **)(sim_ship_b + 0x40) = NULL;
        G.off_controller = G.off_controller_active = -1;
    }

    /* RT. ship census: controller class name read through MSVC x64 RTTI, with the
     *     field holding the object itself or a handle to it */
    {
        static struct { uint8_t pad[64]; uint32_t col[6]; uint8_t pad2[32]; struct { void *vft, *spare; char name[40]; } td; } img;
        static const void *vtbl[2];
        static struct { const void *vt; int x; } obj;
        static const void *handle;
        uint8_t *base = (uint8_t *)&img;
        img.col[0] = 1;                                                /* signature: x64, image-relative */
        img.col[3] = (uint32_t)((uint8_t *)&img.td - base);            /* type descriptor RVA */
        img.col[5] = (uint32_t)((uint8_t *)img.col - base);            /* self RVA */
        strcpy(img.td.name, ".?AVcGcPlayerController@@");
        vtbl[0] = img.col; obj.vt = &vtbl[1]; handle = &obj;
        const char *direct = rtti_name(&obj);
        const char *via = rtti_name(&handle);
        if (!via) via = rtti_name(handle);                             /* same fallback as the census */
        int bad = rtti_name(NULL) != NULL || rtti_name((void *)0x20) != NULL;
        check("RT census: controller class via RTTI (object, handle, bad pointers)",
              direct && via && !strcmp(direct, img.td.name) && !strcmp(via, img.td.name) && !bad,
              "object=%s handle=%s bad-pointer-safe=%d", direct ? direct : "NULL", via ? via : "NULL", !bad);
    }

    printf("\n%s (%d failed)\n", fails ? "SIMTEST FAIL" : "SIMTEST PASS", fails);
    return fails ? 1 : 0;
}
#endif
