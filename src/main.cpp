// Camp Funds Withdraw: adds "Withdraw Camp Funds" to Carl's menu (Greymane camp provisions) and converts
// camp funds into silver. Every address/offset used here is resolved by signature in Resolve().
#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>

#define MOD_VERSION "2.04.06"

// ---------------------------------------------------------------------------------------------
// Logging and configuration

static char g_dir[MAX_PATH];
static FILE *g_log;
static CRITICAL_SECTION g_logLock;

static void Log(const char *fmt, ...) {
    if (!g_log) return;
    EnterCriticalSection(&g_logLock);
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_logLock);
}

struct Config {
    bool enabled = true;
    bool showCostPreview = true;
    char menuLabel[128] = "Withdraw Camp Funds";
    char title[256] = "Withdraw Camp Funds";
    char text[512] = "Convert Camp Funds into silver: %RATE% Camp Funds = 1 copper (100 copper = 1 silver).\n"
                     "Available: %FUNDS% Camp Funds (up to %MAX% copper = %MAXSILVER% silver).";
} g_cfg;

static void ReadText(const char *sec, const char *key, char *out, size_t n, const char *ini) {
    char buf[512];
    GetPrivateProfileStringA(sec, key, "", buf, sizeof buf, ini);
    if (!buf[0]) return;
    // INI values cannot hold newlines: accept a literal \n.
    char *o = out;
    for (const char *s = buf; *s && o < out + n - 1; s++) {
        if (s[0] == '\\' && s[1] == 'n') { *o++ = '\n'; s++; }
        else *o++ = *s;
    }
    *o = 0;
}

static void LoadConfig() {
    char ini[MAX_PATH];
    snprintf(ini, sizeof ini, "%s\\CampFundsWithdraw.ini", g_dir);
    g_cfg.enabled = GetPrivateProfileIntA("General", "Enabled", 1, ini) != 0;
    g_cfg.showCostPreview = GetPrivateProfileIntA("Modal", "ShowCostPreview", 1, ini) != 0;
    ReadText("General", "MenuLabel", g_cfg.menuLabel, sizeof g_cfg.menuLabel, ini);
    ReadText("Modal", "Title", g_cfg.title, sizeof g_cfg.title, ini);
    ReadText("Modal", "Text", g_cfg.text, sizeof g_cfg.text, ini);
}

// ---------------------------------------------------------------------------------------------
// Image and pattern helpers

static uint8_t *g_base;
static size_t g_size;
struct Section { uint8_t *p; size_t n; };
static Section g_text, g_rdata, g_data;  // game code, read-only data, writable data

// Section names are shuffled by the protection on every build (2.04.03: .trace/.xtls/.reloc,
// 2.04.04: .xtext/.shared/.sdata), so sections are picked by role: the largest executable
// non-writable one (game code; the rwx protection VM is excluded), the largest read-only
// initialized one, and the largest writable non-executable one.
static bool FindSections() {
    if (!g_base) g_base = (uint8_t *)GetModuleHandleA(nullptr);
    auto dos = (IMAGE_DOS_HEADER *)g_base;
    auto nt = (IMAGE_NT_HEADERS64 *)(g_base + dos->e_lfanew);
    g_size = nt->OptionalHeader.SizeOfImage;
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        DWORD ch = sec->Characteristics;
        bool exec = ch & IMAGE_SCN_MEM_EXECUTE, write = ch & IMAGE_SCN_MEM_WRITE;
        bool init = ch & IMAGE_SCN_CNT_INITIALIZED_DATA, discard = ch & IMAGE_SCN_MEM_DISCARDABLE;
        Section s{g_base + sec->VirtualAddress, sec->Misc.VirtualSize};
        if (exec && !write) { if (s.n > g_text.n) g_text = s; }
        else if (!exec && !write && init && !discard) { if (s.n > g_rdata.n) g_rdata = s; }
        else if (!exec && write && init) { if (s.n > g_data.n) g_data = s; }
    }
    Log("image %p size %zu MB; code %p+%zx rdata %p+%zx data %p+%zx", g_base, g_size >> 20, g_text.p,
        g_text.n, g_rdata.p, g_rdata.n, g_data.p, g_data.n);
    return g_text.p && g_rdata.p && g_data.p;
}

// Pattern: hex bytes separated by spaces, "??" = wildcard.
struct Pat { uint8_t b[96]; bool m[96]; int n; };
static Pat P(const char *s) {
    Pat p{};
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        if (s[0] == '?') { p.m[p.n] = false; p.b[p.n++] = 0; s += 2; continue; }
        unsigned v;
        sscanf(s, "%2x", &v);
        p.m[p.n] = true;
        p.b[p.n++] = (uint8_t)v;
        s += 2;
    }
    return p;
}
static uint8_t *Find(uint8_t *lo, size_t n, const Pat &p, uint8_t *after = nullptr) {
    uint8_t *start = after ? after + 1 : lo;
    uint8_t *end = lo + n - p.n;
    for (uint8_t *c = start; c <= end; c++) {
        if (c[0] != p.b[0]) continue;
        int i = 1;
        while (i < p.n && (!p.m[i] || c[i] == p.b[i])) i++;
        if (i == p.n) return c;
    }
    return nullptr;
}
static uint8_t *Rel(uint8_t *at, int immAfter = 0) {  // rel32 at `at`, instruction ends at+4+immAfter
    return at + 4 + immAfter + *(int32_t *)at;
}
static uint8_t *FindBytes(Section s, const void *b, size_t n, uint8_t *from = nullptr) {
    uint8_t *c = from ? from + 1 : s.p;
    uint8_t *end = s.p + s.n - n;
    for (; c <= end; c++)
        if (*c == *(const uint8_t *)b && !memcmp(c, b, n)) return c;
    return nullptr;
}
static bool Contains(uint8_t *fn, size_t n, const Pat &p) { return Find(fn, n, p) != nullptr; }

// MSVC RTTI: TypeDescriptor name at +0x10 (in .data); COL {sig=1, ..., tdRva @+0xC, selfRva @+0x14}
// in rdata; vtable[-1] = &COL.
static void **FindVtable(const char *mangled) {
    size_t n = strlen(mangled) + 1;
    for (uint8_t *name = FindBytes(g_data, mangled, n); name; name = FindBytes(g_data, mangled, n, name)) {
        uint32_t tdRva = (uint32_t)(name - 0x10 - g_base);
        for (uint8_t *c = g_rdata.p; c + 24 <= g_rdata.p + g_rdata.n; c += 4) {
            if (*(uint32_t *)(c + 0xC) != tdRva || *(uint32_t *)c != 1 || *(uint32_t *)(c + 4) != 0) continue;
            if (*(uint32_t *)(c + 0x14) != (uint32_t)(c - g_base)) continue;
            uint64_t col = (uint64_t)c;
            for (uint8_t *v = g_rdata.p; v + 16 <= g_rdata.p + g_rdata.n; v += 8)
                if (*(uint64_t *)v == col) return (void **)(v + 8);
        }
    }
    return nullptr;
}

// First `lea rdx, [rip+X]` in code whose target is the given C string in rdata.
static uint8_t *FindLeaRdxToString(const char *s) {
    uint8_t *str = FindBytes(g_rdata, s, strlen(s) + 1);
    if (!str) return nullptr;
    Pat lea = P("48 8D 15");
    for (uint8_t *a = Find(g_text.p, g_text.n, lea); a; a = Find(g_text.p, g_text.n, lea, a))
        if (Rel(a + 3) == str) return a;
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Game functions and data (resolved by Resolve())

using WidgetFn = void(__fastcall *)(void *w);
using FindViewFn = void *(__fastcall *)(void *root, const char *name);
using ActorHandleFn = void *(__fastcall *)(void *unused, void *out);
using HandleReleaseFn = void(__fastcall *)(void *h);
using GetInvFn = void *(__fastcall *)(void *actor);
using GetCountFn = int64_t(__fastcall *)(void *inv, void *actor, uint32_t itemIdx);
using InfoFn = uint8_t *(__fastcall *)(void *unused, uint32_t idx);
using CfgFn = void(__fastcall *)(void *cfg);
using SetTextFn = void(__fastcall *)(char *dst, uint32_t cap, const char *src);
using CreateModalFn = void *(__fastcall *)(void *owner, void *view, void *cfg, int64_t max, int64_t min,
                                           uint32_t itemKey, uint64_t p7, uint8_t p8, uint8_t p9);
using SendFn = int *(__fastcall *)(void *net, int *rv, uint32_t channel, const void *pkt, uint16_t len,
                                   uint8_t flag, uint16_t prio);
using VaryFn = int *(__fastcall *)(void *comp, int *rv, uint32_t faction, uint32_t item, uint64_t count,
                                   uint8_t flag);
using ItemValueFn = void *(__fastcall *)(void *elem, const uint16_t *idx, int64_t count);
using RecordFn = void(__fastcall *)(void *p, uint32_t key, int64_t count);
using AddButtonFn = void *(__fastcall *)(void *list, const char *cmd, const char *label, void *r9, int64_t a5,
                                         int64_t a6);
using AddExtraButtonFn = void(__fastcall *)(void *panel, const char *cmd, const char *label);
using ActivateFn = void(__fastcall *)(void *panel, uint32_t index);
using LocHashFn = uint32_t *(__fastcall *)(uint32_t *out, const char *name);
using LocIdFn = uint16_t *(__fastcall *)(uint16_t *out, uint32_t hash);
using LocTextFn = uint8_t *(__fastcall *)(void *unused, uint32_t id);
using VFn = void *(__fastcall *)(void *self, ...);

static struct {
    void **vtWarehouse, **vtMenu;
    void *origWhRefresh, *origMenuOpen, *origMenuConfirm;
    const char *modalViewName;
    FindViewFn findView;
    ActorHandleFn actorHandle;
    void *handleVt;
    HandleReleaseFn handleRelease;
    GetInvFn getInv;
    GetCountFn getCount;
    InfoFn itemInfo, factionInfo, typeInfo;
    CfgFn cfgCtor, cfgDtor;
    SetTextFn setText;
    CreateModalFn createModal;
    int32_t *minCount;
    void **cfgRootA, **cfgRootB;
    void **netMgr;
    SendFn send;
    uint16_t opcode;
    uint8_t *rateFlag;
    uint8_t *siteVary, *siteItemValue, *siteRecord;  // server path call sites
    VaryFn vary;
    ItemValueFn itemValue;
    RecordFn record;
    uint8_t *siteAddButton, *siteActivateKey, *siteActivateMouse;  // menu call sites
    AddButtonFn addButton;
    AddExtraButtonFn addExtraButton;
    ActivateFn activate;
    LocHashFn locHash;
    LocIdFn locId;
    LocTextFn locText;
} G;

static constexpr int WH_SLOT_REFRESH = 17, WH_SLOT_OPEN = 146, MENU_SLOT_CONFIRM = 118, MENU_SLOT_OPEN = 146;
static constexpr uint8_t MARKER = 0xA7;
static constexpr uint32_t CHANNEL_SERVER = 4;
static const char *const MENU_CMD = "CampFundsWithdraw";
static const char *const CARL_LABEL_KEY = "UI_NpcStore_WareHouse";  // used only by Carl's stage in 2.04

#define NEED(x, what)                                   \
    do {                                                \
        if (!(x)) { Log("signature missing: %s", what); return false; } \
    } while (0)

static bool Resolve() {
    // Warehouse2 (camp provisions panel): the modal helpers and the camp faction cache.
    G.vtWarehouse = FindVtable(".?AVUIGamePlayControlRootWarehouse2@uiCommonScript@pa@@");
    NEED(G.vtWarehouse, "Warehouse2 vtable");
    uint8_t *whRefresh = (uint8_t *)G.vtWarehouse[WH_SLOT_REFRESH];
    uint8_t *whEvent = (uint8_t *)G.vtWarehouse[115];
    uint8_t *whConfirm = (uint8_t *)G.vtWarehouse[118];
    uint8_t *whOpen = (uint8_t *)G.vtWarehouse[WH_SLOT_OPEN];
    NEED(Contains(whRefresh, 0x300, P("80 BF FB 04 00 00 00")), "warehouse refresh: island flag");
    NEED(Contains(whRefresh, 0x300, P("66 39 B7 CA 04 00 00")), "warehouse refresh: donation item");
    uint8_t *a = Find(whEvent, 0x800, P("48 3B 9F 60 04 00 00 0F 85 ?? ?? ?? ?? 48 8B 07 48 8B CF FF 90 18 01 00 00 48 8D 15 ?? ?? ?? ?? 48 8B C8 E8"));
    NEED(a, "warehouse event: donation branch");
    G.modalViewName = (const char *)Rel(a + 28);
    G.findView = (FindViewFn)Rel(a + 36);
    NEED(!strcmp(G.modalViewName, "ModalMessageView"), "warehouse event: ModalMessageView string");
    a = Find(whEvent, 0x800, P("44 0F B7 8F C8 04 00 00 48 8B 97 D0 01 00 00 48 8D 8C 24 ?? ?? ?? ?? E8"));
    NEED(a, "warehouse event: donation modal helper");
    uint8_t *fModal = Rel(a + 24);
    a = Find(whConfirm, 0x300, P("45 0F B7 86 C8 04 00 00 48 8D 94 24 ?? ?? ?? ?? 48 8B 49 18 E8"));
    NEED(a, "warehouse confirm: donate helper call");
    uint8_t *fDonate = Rel(a + 21);
    NEED(whOpen, "warehouse open command");

    // Donation modal helper (FUN_1411fe070).
    a = Find(fModal, 0x100, P("48 8D 54 24 50 E8 ?? ?? ?? ?? 90 80 7C 24 60 00"));
    NEED(a, "modal: actor handle");
    G.actorHandle = (ActorHandleFn)Rel(a + 6);
    a = Find(fModal, 0x100, P("41 BC EB 01 00 00 65 48 8B 04 25 58 00 00 00 4C 03 20 48 8B 05 ?? ?? ?? ?? 41 80 3C 24 00 48 0F 45 05 ?? ?? ?? ?? 48 8B 00 48 8B 50 08 4C 8B 42 30"));
    NEED(a, "modal: config roots");
    G.cfgRootA = (void **)Rel(a + 21);
    G.cfgRootB = (void **)Rel(a + 34);
    a = Find(fModal, 0x200, P("48 8D 4D 80 E8 ?? ?? ?? ?? 90 48 8B 8D"));
    NEED(a, "modal: cfg ctor");
    G.cfgCtor = (CfgFn)Rel(a + 5);
    a = Find(fModal, 0x200, P("0F B7 11 E8 ?? ?? ?? ?? 48 8B 48 10 48 8B 11 4C 8B 42 08"));
    NEED(a, "modal: localized text");
    G.locText = (LocTextFn)Rel(a + 4);
    a = Find(fModal, 0x200, P("8B 95 A8 08 00 00 E8"));
    NEED(a, "modal: set text");
    G.setText = (SetTextFn)Rel(a + 7);
    a = Find(fModal, 0x300, P("48 8B CE E8 ?? ?? ?? ?? 0F B7 9D 00 14 00 00"));
    NEED(a, "modal: inventory");
    G.getInv = (GetInvFn)Rel(a + 4);
    a = Find(fModal, 0x300, P("48 8B C8 E8 ?? ?? ?? ?? 48 8B F0 48 85 C0"));
    NEED(a, "modal: item count");
    G.getCount = (GetCountFn)Rel(a + 4);
    a = Find(fModal, 0x300, P("8B D3 E8 ?? ?? ?? ?? 48 63 15"));
    NEED(a, "modal: item info, min count");
    G.itemInfo = (InfoFn)Rel(a + 3);
    G.minCount = (int32_t *)Rel(a + 10);
    a = Find(fModal, 0x300, P("49 8B CF E8 ?? ?? ?? ?? 48 8B 8D 10 14 00 00"));
    NEED(a, "modal: create");
    G.createModal = (CreateModalFn)Rel(a + 4);
    a = Find(fModal, 0x300, P("48 8D 4D 80 E8 ?? ?? ?? ?? 90 EB"));
    NEED(a, "modal: cfg dtor");
    G.cfgDtor = (CfgFn)Rel(a + 5);
    a = Find(fModal, 0x300, P("48 8D 05 ?? ?? ?? ?? 48 89 44 24 50 48 8D 4C 24 50 E8"));
    NEED(a, "modal: handle release");
    G.handleVt = Rel(a + 3);
    G.handleRelease = (HandleReleaseFn)Rel(a + 18);

    // Client donate helper (FUN_1404cd190) and the VaryResource sender (FUN_140c91fc0).
    a = Find(fDonate, 0x100, P("E8 ?? ?? ?? ?? 48 8D 58 08 8B D6 E8 ?? ?? ?? ?? 4C 8D 40 08"));
    NEED(a, "donate: infos");
    NEED(Rel(a + 1) == (uint8_t *)G.itemInfo, "donate: item info matches");
    G.factionInfo = (InfoFn)Rel(a + 12);
    a = Find(fDonate, 0x120, P("48 8B 4D 10 E8"));
    NEED(a, "donate: sender");
    uint8_t *fSend = Rel(a + 5);
    NEED(Contains(fSend, 0x200, P("FF 90 68 01 00 00 3C FF")), "sender: peer vfunc");
    a = Find(fSend, 0x200, P("B9 ?? ?? 00 00 66 89 4D 90"));
    NEED(a, "sender: opcode");
    G.opcode = *(uint16_t *)(a + 1);
    a = Find(fSend, 0x200, P("48 8B 0D ?? ?? ?? ?? 80 B9 12 01 00 00 00"));
    NEED(a, "sender: net manager");
    G.netMgr = (void **)Rel(a + 3);
    a = Find(fSend, 0x200, P("41 B8 04 00 00 00 48 8D 95 98 7F 00 00 E8"));
    NEED(a, "sender: send");
    G.send = (SendFn)Rel(a + 14);

    // Server handler of TrVaryResourceReq and the donation logic it calls.
    void **vtVary = FindVtable(".?AVTrocTrVaryResourceReq@pa@@");
    NEED(vtVary, "VaryResource handler vtable");
    a = Find((uint8_t *)vtVary[2], 0x400, P("44 8B 4D F7 44 8B 45 7F 48 8D 55 7F 48 8B 49 18 E8"));
    NEED(a, "server: vary call site");
    G.siteVary = a + 16;
    G.vary = (VaryFn)Rel(a + 17);
    a = Find((uint8_t *)G.vary, 0x600, P("80 3D ?? ?? ?? ?? 00 74 07 48 8D 3C BF 48 03 FF 4C 8B C7 48 8D 54 24 ?? 48 8D 4D ?? E8"));
    NEED(a, "server: rate multiplier");
    G.rateFlag = Rel(a + 2, 1);
    G.siteItemValue = a + 28;
    G.itemValue = (ItemValueFn)Rel(a + 29);
    a = Find((uint8_t *)G.vary, 0x800, P("48 8B 48 68 4C 8B 46 10 0F B7 56 08 48 8B 49 28 E8"));
    NEED(a, "server: donation record");
    G.siteRecord = a + 16;
    G.record = (RecordFn)Rel(a + 17);
    // Per-faction table of the camp component (entry = idx*0x1c8 + *(comp+0x20), type key at +0x18) and
    // the type info whose +0xe == 3 marks a camp faction (the branch that applies the x10 rate).
    NEED(Contains((uint8_t *)G.vary, 0x400, P("48 69 D0 C8 01 00 00 49 03 57 20 74 05 44 0F B7 72 18")), "server: faction table layout");
    a = Find((uint8_t *)G.vary, 0x400, P("89 44 24 78 8B D0 E8 ?? ?? ?? ?? 4C 8B 75 D0 80 78 0E 03"));
    NEED(a, "server: faction type info");
    G.typeInfo = (InfoFn)Rel(a + 7);

    // NPC function menu (ButtonListPanel).
    G.vtMenu = FindVtable(".?AVUIGamePlayControl_Root_ButtonList@uiCommonScript@pa@@");
    NEED(G.vtMenu, "ButtonList vtable");
    uint8_t *mEvent = (uint8_t *)G.vtMenu[115], *mMouse = (uint8_t *)G.vtMenu[140];
    uint8_t *mOpen = (uint8_t *)G.vtMenu[MENU_SLOT_OPEN];
    a = Find(mEvent, 0x200, P("48 8B 93 80 01 00 00 8B 92 C8 00 00 00 48 8B CB E8"));
    NEED(a, "menu event: activate call");
    G.siteActivateKey = a + 16;
    G.activate = (ActivateFn)Rel(a + 17);
    a = Find(mMouse, 0x40, P("48 39 91 80 01 00 00 75 0F 41 80 F8 02 75 09 8B 54 24 28 E9"));
    NEED(a, "menu mouse: activate jump");
    G.siteActivateMouse = a + 19;
    NEED(Rel(a + 20) == (uint8_t *)G.activate, "menu mouse: same activate");
    a = Find(mOpen, 0x1000, P("48 8B 8D 70 01 00 00 48 8B 89 80 01 00 00 E8"));
    NEED(a, "menu open: add button call");
    G.siteAddButton = a + 14;
    G.addButton = (AddButtonFn)Rel(a + 15);
    a = Find(mOpen, 0x1200, P("48 8D 15 ?? ?? ?? ?? 48 8B CE E8"));
    while (a && strcmp((const char *)Rel(a + 3), "ChangeName"))
        a = Find(a + 1, mOpen + 0x1200 - (a + 1), P("48 8D 15 ?? ?? ?? ?? 48 8B CE E8"));
    NEED(a, "menu open: extra button (ChangeName)");
    G.addExtraButton = (AddExtraButtonFn)Rel(a + 11);

    // Localization: name -> hash -> id, as done for the UI_WareHouse_* keys.
    a = FindLeaRdxToString("UI_WareHouse_DonationModal");
    NEED(a, "localization: key reference");
    uint8_t *c1 = Find(a, 0x30, P("E8"));
    NEED(c1, "localization: hash call");
    uint8_t *c2 = Find(c1 + 5, 0x20, P("8B 10 E8"));
    NEED(c2, "localization: id call");
    G.locHash = (LocHashFn)Rel(c1 + 1);
    G.locId = (LocIdFn)Rel(c2 + 3);

    Log("resolved: modal %p createModal %p send %p opcode %#x net %p rate flag %p", fModal, G.createModal, G.send,
        G.opcode, G.netMgr, G.rateFlag);
    Log("server: vary %p site %p, itemValue site %p, record site %p", G.vary, G.siteVary, G.siteItemValue,
        G.siteRecord);
    Log("menu: vtable %p, activate %p (sites %p %p), add button %p (site %p), extra button %p, faction type %p", G.vtMenu,
        G.activate, G.siteActivateKey, G.siteActivateMouse, G.addButton, G.siteAddButton, G.addExtraButton,
        G.typeInfo);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Game state helpers

static uint8_t *GameCfg() {
    uint8_t *tls0 = *(uint8_t **)__readgsqword(0x58);
    void *root = tls0[0x1eb] ? *G.cfgRootB : *G.cfgRootA;
    if (!root) return nullptr;
    uint8_t *p = *(uint8_t **)root;
    if (!p) return nullptr;
    p = *(uint8_t **)(p + 8);
    return p ? *(uint8_t **)(p + 0x30) : nullptr;
}
static uint16_t SilverIdx() { uint8_t *c = GameCfg(); return c ? *(uint16_t *)(c + 0xf0) : 0xffff; }
static uint16_t CampMoneyIdx() { uint8_t *c = GameCfg(); return c ? *(uint16_t *)(c + 0xf2) : 0xffff; }
static int64_t Rate() { return *G.rateFlag ? 10 : 1; }

struct ActorHandle {
    void *vt;
    void *actor;
    uint8_t ok;
    uint8_t pad[0x40];
    ActorHandle() { memset(this, 0, sizeof *this); G.actorHandle(nullptr, this); }
    ~ActorHandle() { vt = G.handleVt; G.handleRelease(this); }
};

static void *CallV(void *self, int off) { return (*(VFn **)self)[off / 8](self); }

static const char *LocalizedText(const char *key) {
    uint32_t h = 0;
    uint16_t id = 0xffff;
    G.locHash(&h, key);
    G.locId(&id, h);
    if (id == 0xffff) return nullptr;
    uint8_t *obj = G.locText(&id, id);  // the game calls it with rcx pointing at the id
    void *o10 = obj ? *(void **)(obj + 0x10) : nullptr;
    if (!o10) return nullptr;
    return (const char *)((void *(__fastcall *)(void *, int))(*(void ***)o10)[1])(o10, 0);
}

// Camp faction index: cached from the camp provisions panel (exactly what the game uses for donations),
// otherwise the camp-type faction of the player's camp component (the one whose donations apply the rate).
static int g_campFactionIdx = -1;

static uint8_t *CampComponent(void *actor) {
    uint8_t *p = *(uint8_t **)((uint8_t *)actor + 0x68);
    p = p ? *(uint8_t **)(p + 0x188) : nullptr;
    return p ? *(uint8_t **)(p + 0x18) : nullptr;
}

static int CampFactionIdx() {
    if (g_campFactionIdx >= 0) return g_campFactionIdx;
    ActorHandle h;
    if (!h.ok || !h.actor) return -1;
    uint8_t *comp = CampComponent(h.actor);
    if (!comp) return -1;
    uint32_t n = *(uint32_t *)(comp + 0x28);
    uint8_t *table = *(uint8_t **)(comp + 0x20);
    int found = -1, count = 0;
    for (uint32_t i = 0; table && i < n && i < 0x10000; i++) {
        uint16_t type = *(uint16_t *)(table + i * 0x1c8 + 0x18);
        if (type == 0xffff) continue;
        uint8_t *t = G.typeInfo(nullptr, type);
        uint8_t *f = G.factionInfo(nullptr, i);
        if (!t || t[0xe] != 3 || !f || !f[0x10]) continue;
        Log("camp faction candidate: idx %u key %u type %u", i, *(uint32_t *)(f + 8), type);
        if (found < 0) found = (int)i;
        count++;
    }
    if (count > 1) Log("camp faction: %d candidates, using idx %d", count, found);
    if (found >= 0) g_campFactionIdx = found;
    return found;
}

static void FormatText(char *out, size_t n, int64_t rate, int64_t funds, int64_t maxSilver) {
    char tmp[64];
    size_t o = 0;
    for (const char *s = g_cfg.text; *s && o + 1 < n;) {
        const char *rep = nullptr;
        size_t skip = 0;
        if (!strncmp(s, "%RATE%", 6)) { snprintf(tmp, sizeof tmp, "%lld", (long long)rate); rep = tmp; skip = 6; }
        else if (!strncmp(s, "%FUNDS%", 7)) { snprintf(tmp, sizeof tmp, "%lld", (long long)funds); rep = tmp; skip = 7; }
        else if (!strncmp(s, "%MAXSILVER%", 11)) {
            // The silver item counts copper (100 copper = 1 silver, shown with two decimals in game).
            snprintf(tmp, sizeof tmp, "%lld.%02lld", (long long)(maxSilver / 100), (long long)(maxSilver % 100));
            rep = tmp; skip = 11;
        }
        else if (!strncmp(s, "%MAX%", 5)) { snprintf(tmp, sizeof tmp, "%lld", (long long)maxSilver); rep = tmp; skip = 5; }
        if (rep) { for (; *rep && o + 1 < n; rep++) out[o++] = *rep; s += skip; }
        else out[o++] = *s++;
    }
    out[o] = 0;
}

// ---------------------------------------------------------------------------------------------
// Withdraw flow (client side)

static volatile bool g_serverReady;
static void *g_menuPanel;     // Carl's menu instance that holds our entry
static int64_t g_menuIndex = -1;
static void *g_menuModal;
static int g_menuFaction = -1;

static void *OpenWithdrawModal(uint8_t *panel, void *owner) {
    ActorHandle h;
    if (!h.ok || !h.actor) { Log("withdraw: no local actor"); return nullptr; }
    uint16_t campIdx = CampMoneyIdx(), silverIdx = SilverIdx();
    if (campIdx == 0xffff || silverIdx == 0xffff) { Log("withdraw: config not ready"); return nullptr; }
    void *inv = G.getInv(h.actor);
    int64_t funds = inv ? G.getCount(inv, h.actor, campIdx) : 0;
    int64_t rate = Rate();
    int64_t maxSilver = funds / rate;
    Log("withdraw: open, funds %lld rate %lld max %lld silver", (long long)funds, (long long)rate,
        (long long)maxSilver);
    if (maxSilver < 1) return nullptr;
    void *root = CallV(panel, 0x118);
    void *view = root ? G.findView(root, G.modalViewName) : nullptr;
    if (!view) { Log("withdraw: ModalMessageView not found"); return nullptr; }

    alignas(16) static uint8_t cfg[0x2000];
    memset(cfg, 0, sizeof cfg);
    G.cfgCtor(cfg);
    char text[640];
    FormatText(text, sizeof text, rate, funds, maxSilver);
    char *t1 = *(char **)(cfg + 0x920) ? *(char **)(cfg + 0x920) : (char *)(cfg + 0x118);
    *(uint32_t *)(cfg + 0x92c) = 0xffffffff;
    G.setText(t1, *(uint32_t *)(cfg + 0x928), g_cfg.title);
    char *t2 = *(char **)(cfg + 0xa38) ? *(char **)(cfg + 0xa38) : (char *)(cfg + 0x930);
    *(uint32_t *)(cfg + 0xa44) = 0xffffffff;
    G.setText(t2, *(uint32_t *)(cfg + 0xa40), text);
    uint32_t silverKey = *(uint32_t *)(G.itemInfo(nullptr, silverIdx) + 8);
    void *modal = G.createModal(owner, view, cfg, maxSilver, (int64_t)*G.minCount, silverKey, 0, 1,
                                g_cfg.showCostPreview ? 1 : 0);
    G.cfgDtor(cfg);
    if (!modal) { Log("withdraw: modal creation failed"); return nullptr; }
    *(void **)((uint8_t *)modal + 0x28) = panel;  // the modal reports to this panel's vtable slot 118
    return modal;
}

static void SendWithdraw(int factionIdx, int64_t silver) {
    if (!g_serverReady) return;
    ActorHandle h;
    if (!h.ok || !h.actor) { Log("withdraw: no local actor"); return; }
    uint8_t *comp = CampComponent(h.actor);
    void *sender = comp ? *(void **)(comp + 0x10) : nullptr;
    if (!sender) { Log("withdraw: no camp component"); return; }
    uint8_t peer = (uint8_t)(uintptr_t)CallV(sender, 0x168);
    void *net = *G.netMgr;
    if (peer == 0xff || !net || !*(uint8_t *)((uint8_t *)net + 0x112)) { Log("withdraw: not connected"); return; }
    uint32_t factionKey = *(uint32_t *)(G.factionInfo(nullptr, (uint32_t)factionIdx) + 8);
    uint32_t campKey = *(uint32_t *)(G.itemInfo(nullptr, CampMoneyIdx()) + 8);
    uint64_t funds = (uint64_t)(silver * Rate());

    uint8_t pkt[5 + 17];
    *(uint16_t *)pkt = G.opcode;
    pkt[2] = peer;
    *(uint16_t *)(pkt + 3) = 17;
    *(uint32_t *)(pkt + 5) = factionKey;
    *(uint32_t *)(pkt + 9) = campKey;
    *(uint64_t *)(pkt + 13) = funds;
    pkt[21] = MARKER;
    int rv = -1;
    G.send(net, &rv, CHANNEL_SERVER, pkt, sizeof pkt, 0, 0x10);
    Log("withdraw: sent %lld funds -> %lld silver (faction idx %d key %u, camp money key %u), rv %d",
        (long long)funds, (long long)silver, factionIdx, factionKey, campKey, rv);
}

// ---------------------------------------------------------------------------------------------
// Warehouse2 hook: remember the camp faction set by SetDonationFaction (camp provisions panel).

static void __fastcall HkWhRefresh(uint8_t *self) {
    ((void(__fastcall *)(void *))G.origWhRefresh)(self);
    if (!*(uint8_t *)(self + 0x4fb) && *(int16_t *)(self + 0x4ca) != -1 && g_campFactionIdx != *(uint16_t *)(self + 0x4c8)) {
        g_campFactionIdx = *(uint16_t *)(self + 0x4c8);
        Log("camp faction idx %d (from camp provisions)", g_campFactionIdx);
    }
}

// ---------------------------------------------------------------------------------------------
// ButtonList (NPC function menu) hooks

static void *g_building;      // menu being rebuilt by its open-command handler
static bool g_buildIsCarl, g_buildAdded;
static const char *g_carlLabel;

static void AddMenuEntry(uint8_t *panel) {
    G.addExtraButton(panel, MENU_CMD, g_cfg.menuLabel);
    uint32_t n = *(uint32_t *)(panel + 0x1c8);  // extra-button index list: u32 array at +0x1c0
    g_menuIndex = n ? (*(uint32_t **)(panel + 0x1c0))[n - 1] : -1;
    g_menuPanel = panel;
    g_buildAdded = true;
    Log("menu: added \"%s\" at index %lld", g_cfg.menuLabel, (long long)g_menuIndex);
}

static void *__fastcall HkAddButton(void *list, const char *cmd, const char *label, void *r9, int64_t a5, int64_t a6) {
    void *r = G.addButton(list, cmd, label, r9, a5, a6);
    if (g_building && !g_buildAdded && label && g_carlLabel && !strcmp(label, g_carlLabel)) {
        g_buildIsCarl = true;
        if (g_cfg.enabled && g_serverReady) AddMenuEntry((uint8_t *)g_building);  // right after Camp Provisions
    }
    return r;
}

static void __fastcall HkMenuOpen(uint8_t *self, void *a2, void *a3, char *ev) {
    bool rebuild = ev && *ev == 0x05;  // button list event
    if (rebuild) {
        if (!g_carlLabel) {
            g_carlLabel = LocalizedText(CARL_LABEL_KEY);
            Log("menu: Carl's entry label \"%s\"", g_carlLabel ? g_carlLabel : "(not found)");
        }
        g_building = self;
        g_buildIsCarl = g_buildAdded = false;
        if (g_menuPanel == self) { g_menuPanel = nullptr; g_menuIndex = -1; }
    }
    ((void(__fastcall *)(void *, void *, void *, char *))G.origMenuOpen)(self, a2, a3, ev);
    if (rebuild) {
        if (g_buildIsCarl && !g_buildAdded && g_cfg.enabled && g_serverReady) AddMenuEntry(self);
        g_building = nullptr;
    }
}

static void __fastcall HkActivate(uint8_t *self, uint32_t index) {
    if (self != g_menuPanel || (int64_t)index != g_menuIndex) { G.activate(self, index); return; }
    int faction = CampFactionIdx();
    if (faction < 0) { Log("withdraw: camp faction not found (open Camp Provisions once and retry)"); return; }
    void *owner = *(void **)(self + 0x158);
    g_menuModal = OpenWithdrawModal(self, owner);
    g_menuFaction = faction;
}

static void __fastcall HkMenuConfirm(uint8_t *self, void *modal, char cancelled, int64_t count) {
    if (!modal || modal != g_menuModal) {
        ((void(__fastcall *)(void *, void *, char, int64_t))G.origMenuConfirm)(self, modal, cancelled, count);
        return;
    }
    // Our modal: the original would forward a cancel to the stage chart, so it is not called.
    g_menuModal = nullptr;
    Log("withdraw: modal closed, cancelled %d count %lld", cancelled, (long long)count);
    if (!cancelled && count > 0) SendWithdraw(g_menuFaction, count);
}

// ---------------------------------------------------------------------------------------------
// Server-side call-site hooks (only reached through TrVaryResourceReq)

static volatile DWORD g_activeTid;
static volatile int64_t g_activeSilver;

static int *__fastcall HkVary(void *comp, int *rv, uint32_t faction, uint32_t item, uint64_t count, uint8_t flag) {
    if (flag != MARKER) return G.vary(comp, rv, faction, item, count, flag);
    int64_t rate = Rate();
    if (count == 0 || count % rate) { *rv = -1; Log("server: bad withdraw amount %llu", (unsigned long long)count); return rv; }
    g_activeSilver = (int64_t)(count / rate);
    g_activeTid = GetCurrentThreadId();
    int *r = G.vary(comp, rv, faction, item, count, 0);
    g_activeTid = 0;
    Log("server: withdraw %llu funds -> %lld silver, result %d", (unsigned long long)count,
        (long long)g_activeSilver, r ? *r : -1);
    return r;
}

static void *__fastcall HkItemValue(void *elem, const uint16_t *idx, int64_t count) {
    if (g_activeTid && g_activeTid == GetCurrentThreadId()) {
        uint16_t silver[8];
        memcpy(silver, idx, sizeof silver);
        silver[0] = SilverIdx();
        Log("server: add %lld silver instead of %lld funds", (long long)g_activeSilver, (long long)count);
        return G.itemValue(elem, silver, g_activeSilver);
    }
    return G.itemValue(elem, idx, count);
}

static void __fastcall HkRecord(void *p, uint32_t key, int64_t count) {
    if (g_activeTid && g_activeTid == GetCurrentThreadId()) return;  // not a donation
    G.record(p, key, count);
}

// ---------------------------------------------------------------------------------------------
// Patching

static uint8_t *g_tramp;
static size_t g_trampUsed;

static bool AllocTrampolines() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t gran = si.dwAllocationGranularity;
    uintptr_t base = (uintptr_t)g_base;
    for (uintptr_t a = (base - 0x10000) & ~(gran - 1); a > base - 0x7ff00000ull; a -= gran) {
        g_tramp = (uint8_t *)VirtualAlloc((void *)a, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (g_tramp) return true;
    }
    return false;
}

static uint8_t *Trampoline(void *target) {  // jmp [rip+0]; dq target
    uint8_t *t = g_tramp + g_trampUsed;
    g_trampUsed += 16;
    t[0] = 0xFF; t[1] = 0x25;
    *(uint32_t *)(t + 2) = 0;
    *(void **)(t + 6) = target;
    return t;
}

// Redirects one `call rel32` (E8) or tail `jmp rel32` (E9) to `hook`; the original target is unchanged.
static bool PatchCall(uint8_t *site, void *expectTarget, void *hook) {
    if ((site[0] != 0xE8 && site[0] != 0xE9) || Rel(site + 1) != expectTarget) return false;
    uint8_t *t = Trampoline(hook);
    int64_t rel = t - (site + 5);
    if (rel != (int32_t)rel) return false;
    DWORD old;
    VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
    *(int32_t *)(site + 1) = (int32_t)rel;
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return true;
}

static void PatchSlot(void **vt, int slot, void *hook, void **orig) {
    *orig = vt[slot];
    DWORD old;
    VirtualProtect(&vt[slot], 8, PAGE_READWRITE, &old);
    vt[slot] = hook;
    VirtualProtect(&vt[slot], 8, old, &old);
}

static DWORD WINAPI InitThread(void *) {
    DWORD t0 = GetTickCount();
    if (!FindSections()) { Log("sections not found, disabled"); return 0; }
    if (!Resolve()) { Log("disabled: game version not supported (no patches applied)"); return 0; }
    if (!AllocTrampolines()) { Log("disabled: no memory for trampolines"); return 0; }
    if (!PatchCall(G.siteVary, (void *)G.vary, (void *)HkVary) ||
        !PatchCall(G.siteItemValue, (void *)G.itemValue, (void *)HkItemValue) ||
        !PatchCall(G.siteRecord, (void *)G.record, (void *)HkRecord)) {
        Log("disabled: server call-site patch failed");
        return 0;
    }
    g_serverReady = true;
    PatchSlot(G.vtWarehouse, WH_SLOT_REFRESH, (void *)HkWhRefresh, &G.origWhRefresh);
    PatchSlot(G.vtMenu, MENU_SLOT_OPEN, (void *)HkMenuOpen, &G.origMenuOpen);
    PatchSlot(G.vtMenu, MENU_SLOT_CONFIRM, (void *)HkMenuConfirm, &G.origMenuConfirm);
    if (!PatchCall(G.siteAddButton, (void *)G.addButton, (void *)HkAddButton) ||
        !PatchCall(G.siteActivateKey, (void *)G.activate, (void *)HkActivate) ||
        !PatchCall(G.siteActivateMouse, (void *)G.activate, (void *)HkActivate)) {
        Log("menu call-site patch failed: the menu entry will not appear");
        return 0;
    }
    Log("ready in %lu ms", GetTickCount() - t0);
    return 0;
}

#ifndef CFW_TEST_HARNESS
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    GetModuleFileNameA(inst, g_dir, MAX_PATH);
    if (char *slash = strrchr(g_dir, '\\')) *slash = 0;
    InitializeCriticalSection(&g_logLock);
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\CampFundsWithdraw.log", g_dir);
    g_log = fopen(path, "w");
    LoadConfig();
    Log("CampFundsWithdraw %s loaded (enabled %d)", MOD_VERSION, g_cfg.enabled);
    if (!g_cfg.enabled) return TRUE;
    if (HANDLE th = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr)) CloseHandle(th);
    return TRUE;
}
#else
// Offline check: map CrimsonDesert.exe's sections like the loader would and run the resolver.
int main(int argc, char **argv) {
    g_log = stdout;
    InitializeCriticalSection(&g_logLock);
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *file = (uint8_t *)malloc(n);
    fread(file, 1, n, f);
    auto nt = (IMAGE_NT_HEADERS64 *)(file + ((IMAGE_DOS_HEADER *)file)->e_lfanew);
    // Map at the preferred base so absolute pointers (RTTI COL refs) match without relocations.
    g_base = (uint8_t *)VirtualAlloc((void *)nt->OptionalHeader.ImageBase, nt->OptionalHeader.SizeOfImage,
                                     MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_base) { printf("cannot map at preferred base\n"); return 1; }
    memcpy(g_base, file, nt->OptionalHeader.SizeOfHeaders);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
        memcpy(g_base + sec->VirtualAddress, file + sec->PointerToRawData,
               sec->SizeOfRawData < sec->Misc.VirtualSize ? sec->SizeOfRawData : sec->Misc.VirtualSize);
    DWORD t0 = GetTickCount();
    bool ok = FindSections() && Resolve();
    printf("resolve %s in %lu ms\n", ok ? "OK" : "FAILED", GetTickCount() - t0);
    if (ok) {
        auto rva = [](const void *p) { return (unsigned long long)((const uint8_t *)p - g_base); };
        printf("rva activate %llx sites %llx/%llx addButton %llx site %llx extra %llx locHash %llx locId %llx locText %llx typeInfo %llx\n",
               rva((void *)G.activate), rva(G.siteActivateKey), rva(G.siteActivateMouse), rva((void *)G.addButton),
               rva(G.siteAddButton), rva((void *)G.addExtraButton), rva((void *)G.locHash), rva((void *)G.locId),
               rva((void *)G.locText), rva((void *)G.typeInfo));
        printf("rva createModal %llx send %llx vary %llx itemValue %llx record %llx rate %llx findView %llx\n",
               rva((void *)G.createModal), rva((void *)G.send), rva((void *)G.vary), rva((void *)G.itemValue),
               rva((void *)G.record), rva(G.rateFlag), rva((void *)G.findView));
    }
    return ok ? 0 : 2;
}
#endif
