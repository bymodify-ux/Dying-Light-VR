// LOADING SCREEN VOID - see loadscreen.h (what, why, cost) and docs/superpowers/specs/2026-10-20-loading-screen-void-design.md.
// Decisions: loadscreen_logic.h (offline test tests/loadscreen_logic_test.cpp).
#include "loadscreen.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "../common/log.h"
#if __has_include("../common/buildrva.h")
#include "../common/buildrva.h"   // the BETA tree: the IGame slot is translated on the Epic build
#define DLVR_LS_GAME_SLOT(rva) ::dlvr::buildrva::Eng(rva)
#else
#define DLVR_LS_GAME_SLOT(rva) (rva)   // the DEV tree: Steam rvas only, as the rest of this tree
#endif
#include "loadscreen_logic.h"
// Added by AngelWolf 2026-11-26
#include "deskmirror.h"
#include "uipanel.h"   // uipanel::GameEyeTan - each eye's own frustum for the HUD box (INV-735)
#include "uipin.h"   // the level walk: the loading module's screens, the HUD, a menu (amendment 1)

#define LOG(...) ::dlvr::LogTo("dlvr_ovr2xr.log", __VA_ARGS__)

namespace dlvr {
namespace loadscreen {
namespace {

// IGame* (engine e9fbfc2c; uicam.cpp / camhook.cpp read the same slot). The export reads [[game+8]+0x9C0], null-safe:
// `48 8b 41 08 48 85 c0 74 08 0f b6 80 c0 09 00 00 c3`, read 2026-10-20 from the deployed engine's export table.
constexpr uintptr_t kGameSlot = 0xA3F3B8;
using IsBgFn = bool (*)(void* game);

ID3D11Device* g_device = nullptr;
XrSession     g_session = XR_NULL_HANDLE;
State         g_st;

IsBgFn    g_isBg = nullptr;
uintptr_t g_eng = 0;
bool      g_resolved = false;
bool      g_saidGame = false;

XrSwapchain g_eyeChain[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
int64_t     g_eyeFmt[2] = { 0, 0 };

// One private copy per eye chain (both eyes may share one chain - the game packs both into one texture).
struct Cap {
    XrSwapchain      chain = XR_NULL_HANDLE;
    ID3D11Texture2D* tex = nullptr;
    bool             fresh = false;   // copied since this screen started
};
Cap g_cap[2];

// Each eye's latest picture (the HUD box), kept so a held eye shows its previous one on a newly acquired panel image.
ID3D11Texture2D* g_pic[2] = { nullptr, nullptr };
int              g_picW = 0, g_picH = 0;

XrSwapchain                           g_sc = XR_NULL_HANDLE;
std::vector<XrSwapchainImageD3D11KHR> g_images;
int                                   g_scW = 0, g_scH = 0;

float g_pos[3] = {}, g_quat[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

// This screen's tallies, printed by the OFF line.
ULONGLONG g_startMs = 0;
long g_submits = 0, g_skips = 0, g_captures = 0, g_copied[2] = {}, g_noCap[2] = {}, g_shown = 0;

// Amendment 1: the loading module's level classes (both in our gamedll's RTTI), and the LEVELS watch - from the flag's first
// ON until the HUD or a menu shows, a line at every change of the level list (capped per session).
const char* const kModuleClass[2] = { "BackgroundModuleDI", "BackgroundLoadingModule" };
constexpr int kLevelLinesMax = 80;
bool g_watch = false;
int  g_levelLines = 0;
char g_lastLevels[512] = "";
int  g_lastMod = 0, g_lastHud = 0, g_lastMenu = 0;

// Screens the loading module shows: the larger count of the two classes; -1 only when both reads failed.
int ModuleScreens()
{
    const int a = uipin::LevelScreensShowing(kModuleClass[0]);
    const int b = uipin::LevelScreensShowing(kModuleClass[1]);
    if (a < 0 && b < 0) { return -1; }
    return a > b ? a : b;
}

Cap* FindCap(XrSwapchain chain, bool create)
{
    for (Cap& c : g_cap) { if (c.chain == chain) { return &c; } }
    if (!create) { return nullptr; }
    for (Cap& c : g_cap) {
        if (c.chain == XR_NULL_HANDLE) { c.chain = chain; return &c; }
    }
    return nullptr;
}

void ReleaseTex(ID3D11Texture2D*& t)
{
    if (t) { t->Release(); t = nullptr; }
}

// A refusal: the game image comes back for the rest of the session, and the reason is named once.
void Refuse(const char* why)
{
    if (g_st.fault) { return; }
    g_st.fault = true;
    // [PROBE: LOADSCREEN] gate=none (always on)  site=HEADSET  log-only=no
    LOG("[LOADSCREEN] REFUSED - %s. The game image is submitted as before on every loading screen for the rest of this "
        "session.", why);
}

// The engine's flag. __try lives in a function with no C++ objects. 1 = set, 0 = clear, -1 = no game object, -2 = fault.
int ReadFlag(uintptr_t slot)
{
    __try {
        const uintptr_t game = *reinterpret_cast<const uintptr_t*>(slot);
        if (!game) { return -1; }
        return g_isBg(reinterpret_cast<void*>(game)) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
}

bool EnsureTex(ID3D11Texture2D*& t, const D3D11_TEXTURE2D_DESC& want)
{
    if (t) {
        D3D11_TEXTURE2D_DESC have{};
        t->GetDesc(&have);
        if (have.Width == want.Width && have.Height == want.Height && have.Format == want.Format &&
            have.ArraySize == want.ArraySize && have.MipLevels == want.MipLevels &&
            have.SampleDesc.Count == want.SampleDesc.Count) {
            return true;
        }
        ReleaseTex(t);
    }
    return g_device && SUCCEEDED(g_device->CreateTexture2D(&want, nullptr, &t)) && t;
}

bool EnsureSwapchain(int w, int h, int64_t fmt)
{
    if (g_sc != XR_NULL_HANDLE && g_scW == w && g_scH == h) { return true; }
    if (g_sc != XR_NULL_HANDLE) { xrDestroySwapchain_(g_sc); g_sc = XR_NULL_HANDLE; g_images.clear(); }
    XrSwapchainCreateInfo ci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    ci.format = fmt;
    ci.sampleCount = 1;
    ci.width = static_cast<uint32_t>(w);
    ci.height = static_cast<uint32_t>(h);
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    XrResult r = xrCreateSwapchain_(g_session, &ci, &g_sc);
    uint32_t n = 0;
    if (XR_SUCCEEDED(r)) { r = xrEnumerateSwapchainImages_(g_sc, 0, &n, nullptr); }
    if (XR_SUCCEEDED(r)) {
        g_images.resize(n);
        for (auto& i : g_images) { i = XrSwapchainImageD3D11KHR{ XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR }; }
        r = xrEnumerateSwapchainImages_(g_sc, n, &n, reinterpret_cast<XrSwapchainImageBaseHeader*>(g_images.data()));
    }
    const bool ok = XR_SUCCEEDED(r) && n > 0;
    // [PROBE: LOADSCREEN] gate=none (always on)  site=HEADSET  log-only=no
    LOG("[LOADSCREEN] panel swapchain %s: %dx%d, DXGI format %lld, %u image(s) | XrResult %d", ok ? "CREATED" : "REFUSED", w, h,
        static_cast<long long>(fmt), n, static_cast<int>(r));
    if (!ok) {
        if (g_sc != XR_NULL_HANDLE) { xrDestroySwapchain_(g_sc); g_sc = XR_NULL_HANDLE; }
        g_images.clear();
        return false;
    }
    g_scW = w;
    g_scH = h;
    return true;
}

}  // namespace

void Init(ID3D11Device* device, XrSession session)
{
    g_device = device;
    g_session = session;
}

void Shutdown()
{
    if (g_sc != XR_NULL_HANDLE && xrDestroySwapchain_) { xrDestroySwapchain_(g_sc); }
    g_sc = XR_NULL_HANDLE;
    g_images.clear();
    g_scW = g_scH = 0;
    for (Cap& c : g_cap) { ReleaseTex(c.tex); c = Cap{}; }
    ReleaseTex(g_pic[0]);
    ReleaseTex(g_pic[1]);
    g_picW = g_picH = 0;
    g_eyeChain[0] = g_eyeChain[1] = XR_NULL_HANDLE;
    const bool fault = g_st.fault;
    g_st = State{};
    g_st.fault = fault;   // a refusal holds for the rest of the process
}

void Tick()
{
    if (!g_resolved) {
        g_resolved = true;
        const HMODULE eng = GetModuleHandleA("engine_x64_rwdi.dll");
        g_eng = reinterpret_cast<uintptr_t>(eng);
        g_isBg = eng ? reinterpret_cast<IsBgFn>(GetProcAddress(eng, "?IsBackgroundRender@IGame@@QEAA_NXZ")) : nullptr;
        // [PROBE: LOADSCREEN] gate=none (always on)  site=HEADSET  log-only=no
        LOG("[LOADSCREEN] FIRST (logged unconditionally): engine %s, IsBackgroundRender export %s | loading screens %s",
            eng ? "found" : "NOT FOUND", g_isBg ? "resolved" : "NOT RESOLVED",
            (eng && g_isBg) ? "go in a black void with the picture on a room-fixed panel"
                            : "are submitted as before (the flag cannot be read)");
    }
    if (!g_isBg || !g_eng) { return; }
    // Final review I2: after a faulted read the flag is not read again (no exception per frame); 0 lets the OFF edge log.
    const int f = ReadAllowed(g_st) ? ReadFlag(g_eng + DLVR_LS_GAME_SLOT(kGameSlot)) : 0;
    if (f == -2) { Refuse("reading the engine's background-render flag faulted"); }
    if (f >= 0 && !g_saidGame) {
        g_saidGame = true;
        // [PROBE: LOADSCREEN] gate=none (always on)  site=HEADSET  log-only=no
        LOG("[LOADSCREEN] game object read - the background-render flag reads %d", f);
    }
    // Amendment 1: the void SEQUENCE - the flag starts it; after the flag drops, the loading module's screens hold it until the
    // HUD or a menu shows. The level reads run only while a sequence or the LEVELS watch is on (not in play).
    const bool flag = f == 1;
    if (flag) { g_watch = true; }
    int mod = 0, hud = 0, menu = 0;
    if (g_watch || g_st.seq) {
        hud = uipin::HudShowing();
        menu = uipin::MenuState();
        mod = ModuleScreens();   // read with the flag on too, so the LEVELS line shows whether the module is there during a load
    }
    static bool s_prevFlag = false;
    const bool wasSeqOnly = g_st.loading && !s_prevFlag;   // the void was held by the module alone up to this submit
    s_prevFlag = flag;
    const Edge e = Step(g_st, SequenceActive(g_st, flag, mod, hud, menu));
    if (e == Edge::Start) {
        for (Cap& c : g_cap) { c.fresh = false; }
        g_startMs = GetTickCount64();
        g_submits = g_skips = g_captures = g_shown = 0;
        for (int i = 0; i < 2; ++i) { g_copied[i] = g_noCap[i] = 0; }
        // [PROBE: LOADSCREEN] gate=none (always on)  site=HEADSET  log-only=no
        LOG("[LOADSCREEN] ON - the engine draws its background path%s", g_st.fault ? " (refused earlier: the game image stays)" : "");
    } else if (e == Edge::End) {
        // [PROBE: LOADSCREEN] gate=none (always on)  site=HEADSET  log-only=no
        LOG("[LOADSCREEN] OFF after %llu ms | ended by %s (module screens %d, HUD %d, menu %d) | submits %ld, game image skipped "
            "%ld, panel shown %ld | image copies %ld | pictures copied L %ld R %ld, no copy yet L %ld R %ld | gave up %s",
            static_cast<unsigned long long>(GetTickCount64() - g_startMs),
            !wasSeqOnly ? "the engine flag (no loading-module screen held it)"
                        : (hud == 1 ? "the HUD" : (menu > 0 ? "a menu" : (mod < 0 ? "an unreadable level read" : "the loading module gone"))),
            mod, hud, menu, g_submits, g_skips, g_shown, g_captures, g_copied[0], g_copied[1], g_noCap[0], g_noCap[1],
            g_st.gaveUp ? "yes" : "no");
    }
    if (g_watch) {
        char levels[512];
        uipin::DescribeLevels(levels, sizeof(levels));
        if ((strcmp(levels, g_lastLevels) != 0 || mod != g_lastMod || hud != g_lastHud || menu != g_lastMenu) &&
            g_levelLines < kLevelLinesMax) {
            ++g_levelLines;
            // [PROBE: LOADSCREEN] gate=none (always on)  site=HEADSET  log-only=yes
            LOG("[LOADSCREEN] LEVELS (%d of %d) flag %d, void %s | loading-module screens %d, HUD %d, menu %d | %s", g_levelLines,
                kLevelLinesMax, flag ? 1 : 0, g_st.loading ? "UP" : "down", mod, hud, menu, levels);
        }
        strcpy_s(g_lastLevels, sizeof(g_lastLevels), levels);
        g_lastMod = mod;
        g_lastHud = hud;
        g_lastMenu = menu;
        if (!flag && (hud == 1 || menu > 0)) {
            g_watch = false;
            // [PROBE: LOADSCREEN] gate=none (always on)  site=HEADSET  log-only=yes
            LOG("[LOADSCREEN] LEVELS watch ended - %s shows", hud == 1 ? "the HUD" : "a menu");
        }
    }
    if (g_st.loading) { ++g_submits; }
    // Final review I3: a void that has shown no picture for kWatchdogMs gives the game image back for this screen.
    const unsigned long long since = g_st.loading ? GetTickCount64() - g_startMs : 0;
    if (WatchdogTrips(g_st, since)) {
        g_st.gaveUp = true;
        // [PROBE: LOADSCREEN] gate=none (always on)  site=HEADSET  log-only=no
        LOG("[LOADSCREEN] GAVE UP - no picture reached the panel %llu ms after ON (latched %s, image copies %ld, pictures "
            "copied L %ld R %ld, no copy yet L %ld R %ld). The game image is submitted for the rest of this screen; the next "
            "screen tries again.", since, g_st.latched ? "yes" : "no", g_captures, g_copied[0], g_copied[1], g_noCap[0],
            g_noCap[1]);
    }
}

void NoteEyeChain(int eye, XrSwapchain chain, int64_t dxgiFormat)
{
    if (eye < 0 || eye > 1) { return; }
    g_eyeChain[eye] = chain;
    g_eyeFmt[eye] = dxgiFormat;
}

void CaptureBeforeRelease(XrSwapchain chain, ID3D11Texture2D* image)
{
    if (!g_st.loading || g_st.fault || !image || chain == XR_NULL_HANDLE) { return; }
    if (chain != g_eyeChain[0] && chain != g_eyeChain[1]) { return; }
    Cap* c = FindCap(chain, true);
    if (!c) { Refuse("more than two eye chains"); return; }
    D3D11_TEXTURE2D_DESC d{};
    image->GetDesc(&d);
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = 0;
    d.CPUAccessFlags = 0;
    d.MiscFlags = 0;
    if (!EnsureTex(c->tex, d)) { Refuse("the private copy of the eye image could not be created"); return; }
    ID3D11DeviceContext* ctx = nullptr;
    g_device->GetImmediateContext(&ctx);
    if (!ctx) { Refuse("no device context"); return; }
    ctx->CopyResource(c->tex, image);
    ctx->Release();
    c->fresh = true;
    ++g_captures;
}

bool SkipProjection(bool calibVoid)
{
    const bool skip = Decide(g_st, calibVoid).skipProjection;
    if (skip) { ++g_skips; }
    return skip;
}

int Render(XrSpace appSpace, bool viewsValid, bool posesValid, const XrPosef eyes[2], const XrFovf& fov0, const XrRect2Di vp[2],
           bool calibVoid, XrCompositionLayerQuad out[2])
{
    if (!Decide(g_st, calibVoid).skipProjection) { return 0; }
    if (!g_st.latched && LatchAllowed(viewsValid, posesValid)) {   // final review I1: never from the game's echo pose
        const float head[3] = { (eyes[0].position.x + eyes[1].position.x) * 0.5f, (eyes[0].position.y + eyes[1].position.y) * 0.5f,
                                (eyes[0].position.z + eyes[1].position.z) * 0.5f };
        const float hq[4] = { eyes[0].orientation.x, eyes[0].orientation.y, eyes[0].orientation.z, eyes[0].orientation.w };
        if (uipanel::Latch(head, hq, kDistanceM, kHeightM, g_pos, g_quat)) {
            g_st.latched = true;
            // [PROBE: LOADSCREEN] gate=none (always on)  site=HEADSET  log-only=no
            LOG("[LOADSCREEN] LATCHED %llu ms after ON: head (%.3f %.3f %.3f) m, panel centre (%.3f %.3f %.3f) m, turned %.1f deg "
                "about the vertical | distance %.2f m, height %.2f m",
                static_cast<unsigned long long>(GetTickCount64() - g_startMs), head[0], head[1], head[2], g_pos[0], g_pos[1],
                g_pos[2], 2.0f * std::atan2(g_quat[1], g_quat[3]) * 57.29578f, static_cast<double>(kDistanceM),
                static_cast<double>(kHeightM));
        }
    }
    uipanel::Rect box[2];
    bool boxOk[2];
    int bw = 0, bh = 0;
    for (int e = 0; e < 2; ++e) {
        // the HUD box from this eye's own frustum (INV-735, trace 20261113_2115) - unknown frustum = no box this submit
        uipanel::EyeTan t{};
        box[e] = uipanel::Rect{};
        boxOk[e] = uipanel::GameEyeTan(e, &t) &&
                   EyeBox(vp[e].offset.x, vp[e].offset.y, vp[e].extent.width, vp[e].extent.height, t, &box[e]);
        if (boxOk[e]) {
            if (box[e].w > bw) { bw = box[e].w; }
            if (box[e].h > bh) { bh = box[e].h; }
        }
    }
    if (bw <= 0 || bh <= 0) { return 0; }

    // Stage 1: each eye's box from its chain's private copy into that eye's picture. Amendment 1: EVERY eye, every submit -
    // AFW's held eye is not consulted (its value freezes on the background path, which draws both eyes).
    ID3D11DeviceContext* ctx = nullptr;
    g_device->GetImmediateContext(&ctx);
    if (!ctx) { Refuse("no device context"); return 0; }
    for (int e = 0; e < 2; ++e) {
        if (!boxOk[e]) { continue; }
        Cap* c = FindCap(g_eyeChain[e], false);
        if (!c || !c->tex || !c->fresh) { ++g_noCap[e]; continue; }
        D3D11_TEXTURE2D_DESC d{};
        c->tex->GetDesc(&d);
        d.Width = static_cast<UINT>(bw);
        d.Height = static_cast<UINT>(bh);
        d.ArraySize = 1;
        d.MipLevels = 1;
        if (!EnsureTex(g_pic[e], d)) { ctx->Release(); Refuse("the panel picture texture could not be created"); return 0; }
        const D3D11_BOX src{ static_cast<UINT>(box[e].x), static_cast<UINT>(box[e].y), 0,
                             static_cast<UINT>(box[e].x + box[e].w), static_cast<UINT>(box[e].y + box[e].h), 1 };
        ctx->CopySubresourceRegion(g_pic[e], 0, 0, 0, 0, c->tex, 0, &src);
        g_st.havePic[e] = true;
        ++g_copied[e];
    }

    // Stage 2: both pictures into this submit's panel image, side by side.
    const Plan p = Decide(g_st, calibVoid);
    if (!p.quad[0] && !p.quad[1]) { ctx->Release(); return 0; }
    if (!EnsureSwapchain(2 * bw, bh, g_eyeFmt[0])) { ctx->Release(); Refuse("the panel swapchain was refused"); return 0; }
    XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    uint32_t index = 0;
    if (XR_FAILED(xrAcquireSwapchainImage_(g_sc, &ai, &index)) || index >= g_images.size()) {
        ctx->Release();
        Refuse("acquiring the panel image failed");
        return 0;
    }
    XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wi.timeout = XR_INFINITE_DURATION;
    const bool waited = XR_SUCCEEDED(xrWaitSwapchainImage_(g_sc, &wi));
    if (waited) {
        for (int e = 0; e < 2; ++e) {
            if (!p.quad[e] || !g_pic[e]) { continue; }
            const D3D11_BOX src{ 0, 0, 0, static_cast<UINT>(box[e].w), static_cast<UINT>(box[e].h), 1 };
            ctx->CopySubresourceRegion(g_images[index].texture, 0, static_cast<UINT>(e * bw), 0, 0, g_pic[e], 0, &src);
        }
    }
    ctx->Release();
    XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    xrReleaseSwapchainImage_(g_sc, &ri);
    if (!waited) { Refuse("waiting on the panel image failed"); return 0; }

    int n = 0;
    for (int e = 0; e < 2; ++e) {
        if (!p.quad[e]) { continue; }
        float width = 0.0f, height = 0.0f;
        uipanel::SizeM(box[e], vp[e].extent.width, vp[e].extent.height, std::tan(fov0.angleLeft), std::tan(fov0.angleRight),
                       std::tan(fov0.angleDown), std::tan(fov0.angleUp), kDistanceM, kScale, &width, &height);
        XrCompositionLayerQuad& q = out[n++];
        q = XrCompositionLayerQuad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
        q.layerFlags = 0;                                                             // the picture is opaque
        q.space = appSpace;                                                           // fixed in the room
        q.eyeVisibility = e == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;   // OpenXR view 0 = the left eye
        q.subImage.swapchain = g_sc;
        q.subImage.imageArrayIndex = 0;
        q.subImage.imageRect.offset = { e * bw, 0 };
        q.subImage.imageRect.extent = { box[e].w, box[e].h };
        q.pose.orientation = { g_quat[0], g_quat[1], g_quat[2], g_quat[3] };
        q.pose.position = { g_pos[0], g_pos[1], g_pos[2] };
        q.size = { width, height };
    }
    if (n > 0) {
        ++g_shown;
        g_st.shown = true;
    }

	// Added by AngelWolf 2026-11-26
    // Mirror the loading screen onto the desktop window: clear to the void's black backdrop, then project the right-eye panel quad.
    if (n > 0 && g_device && index < g_images.size()) {
        for (int qi = 0; qi < n; ++qi) {
            if (out[qi].eyeVisibility == XR_EYE_VISIBILITY_RIGHT) {
                ::dlvr::deskmirror::DrawQuadLayer(g_device, g_images[index].texture, static_cast<DXGI_FORMAT>(g_eyeFmt[0]),
                    out[qi], eyes[1], fov0, 0);
                break;
            }
        }
    }

    return n;
}

}  // namespace loadscreen
}  // namespace dlvr
