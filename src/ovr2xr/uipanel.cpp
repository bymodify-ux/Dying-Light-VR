// MENU PANEL, stage 1 - see uipanel.h (what, why, cost) and docs/SPEC-UI-MENU-PANEL-2026-10-11.md. Maths: uipanel_logic.h.
#include "uipanel.h"

#include <atomic>
#include <cmath>
#include <vector>

#include "../common/log.h"
#include "boneblend.h"     // PatchGameBytes - the verified, thread-suspended small patch
#include "deskmirror.h"    // DrawPanel - the menu panel in the desktop window (trace 20261114_1125)
#include "holster.h"       // HolsterWheelShowing - a selection wheel is open: show only the wheel, further out (D13)
#include "physcrouch.h"    // PhysCrouchEdges - a physical crouch / stand re-places the HUD panel (ui_hud_follow)
#include "uipanel_logic.h"
#include "uipin.h"         // MenuState - the game's level array says whether a menu shows

#define LOG(...) ::dlvr::LogTo("dlvr_ovr2xr.log", __VA_ARGS__)
#include "periodic_log.h"   // LOG_WINDOW - the panels' 5 s lines (2026-11-08 log trim)

namespace dlvr {

// [DEP: ui_panel_menus] required-for=the menu panel (UiPanelMenus; SPEC-UI-MENU-PANEL-2026-10-11): while an in-game menu shows, the bracketed sprite pass goes onto a room-fixed OpenXR quad instead of onto the eyes, the tab menus take the game's flat branch (gamedll+7AF08D), and uipin's main-menu rule is armed. Needs the HUD layer's brackets in this launch's pak (afw_enable=1 at launch, or dlvr\hud_layer.txt) and the bootstrap's DlvrMenuPanel* exports - refused by name otherwise and the menus stay on the eyes. TIER A since 2026-10-11, INV-535 - Jay after run 4: "Inventory is now good aswell, no rotation whilst head turning, works as intended ... Pause menu is still fine, no issues. No cuts/doubles at all." Compiled ON = his disk.  tree=INERT
bool UiPanelMenus = true;
// [DEP: ui_panel_distance_m] required-for=how far ahead of the head the menu panel is latched, metres (UiPanelDistanceM). Needs ui_panel_menus. Tuning (Tier D); 1.8 = the value Jay set in his tuning run, 2026-10-11 (read from his disk; his first value was 2.0).  tree=INERT
float UiPanelDistanceM = 1.8f;
// [DEP: ui_panel_height_m] required-for=the menu panel's centre relative to head height at the latch, metres, negative = below (UiPanelHeightM). Needs ui_panel_menus. Tuning (Tier D); -0.25 = "slightly below eye level", Jay 2026-10-11, value mine.  tree=INERT
float UiPanelHeightM = -0.25f;
// [DEP: ui_panel_scale] required-for=the menu panel's size against the size the menu has on the eyes today, 1.0 = the same angle (UiPanelScale). Needs ui_panel_menus. Tuning (Tier D).  tree=INERT
float UiPanelScale = 1.0f;
// [DEP: ui_panel_tabs_up_m] required-for=how much higher than the pause menu the OTHER in-game menus (inventory, map, quests, skills, shop) sit on the panel, metres (UiPanelTabsUpM). Jay after run 4: "It could stand to move up a little bit in terms of placement". Needs ui_panel_menus. Tuning (Tier D); 0.30 = the value Jay set in his tuning run, 2026-10-11 (read from his disk).  tree=INERT
float UiPanelTabsUpM = 0.30f;
// [DEP: ui_panel_tabs_x_m] required-for=the same menus' sideways position against the pause menu, metres along the panel, negative = to the left (UiPanelTabsXM). Jay 2026-10-11: "add an X axis slider for the inventory menu ... I'd like to be able to bring it over to the left slightly". Needs ui_panel_menus. Tuning (Tier D); -0.20 = the value Jay set, 2026-10-11 (read from his disk).  tree=INERT
float UiPanelTabsXM = -0.20f;
// [DEP: ui_panel_all_menus] required-for=which in-game menus go on the menu panel (UiPanelAllMenus): 1 = every menu of the game's menu level (pause, inventory, map, quests, skills, shop - Jay's first answer, "Sure"), 0 = only while the pause menu's own screen MenuInGame is visible. Added after run 1 (inventory / vendor read "skewed" on the panel) so the two can be compared live. The main menu is never on the panel. Needs ui_panel_menus. Was unverified either way (Tier C); now Tier A (INV-535).  tree=INERT
bool UiPanelAllMenus = true;
// (ui_panel_main_menu was REMOVED 2026-10-11 on Jay's word - it had had no effect since v4: the main menu is never on the panel.)

// ---- HUD PANEL, stage 1 (docs/SPEC-UI-HUD-PANEL-2026-10-11.md; Jay 2026-10-11: "I'd like to have it sit static opposed to
// follow the head at first. That way we can confirm it's working as required" ... "Go for it"). Own keys, not the menu
// panel's (his answer 1: "Yes").
// [DEP: ui_hud_panel] required-for=the in-game HUD on its own room-fixed OpenXR quad pair instead of blended onto the eyes (UiHudPanel; SPEC-UI-HUD-PANEL-2026-10-11): in play the bracketed sprite pass is handed to the runtime as a panel latched in front of the head after a load. Needs the HUD layer's brackets in this launch's pak (afw_enable=1 at launch, or dlvr\hud_layer.txt - Jay: "AFW only for the time being"), the bootstrap's DlvrMenuPanelInfo / DlvrMenuPanelEnd / DlvrHudPanelCopyEye exports, and uipin's level walk (HudShowing) - refused by name otherwise and the HUD stays on the eyes. Independent of ui_panel_menus. KNOWN (INV-538): markers, prompts, enemy health and the melee dot ride on the panel. Tier A, compiled ON (2026-10-16, Jay: "Ship disk UI_HUD_PANEL" - his disk 1).  tree=INERT
bool UiHudPanel = true;
// HELD ON BY THE WRIST HUD (Jay 2026-11-14, HARD: "WRIST HUD HARD REQUIREMENT - HUD ON A FIXED PANEL ON - NEEDS TO TURN ON AUTOMATICALLY
// OTHERWISE CAN GLITCH FOR USERS"; trace docs/traces/20261114_1200_wrist_holds_hud_panel.md): the wrist HUD's HUD redirect rides this
// panel's PanelInfo. ui_wrist_hud=1 at launch turns it on (ovr2xr.cpp LoadLayerConfig, file rewritten); while the wrist HUD is ticked or
// running, SaveSettingsIfChanged keeps it on and overlay.cpp DrawOneSetting greys its row (wristquad::HudPanelNeeded).
// [DEP: ui_hud_distance_m] required-for=how far ahead of the head the HUD panel is latched, metres (UiHudDistanceM). Needs ui_hud_panel. Tuning (Tier D); 0.5 = Jay's disk (2026-10-16; was 1.8, the menu panel's value). A change re-latches at once.  tree=INERT
float UiHudDistanceM = 0.5f;
// [DEP: ui_wheel_distance_m] required-for=how far ahead of the head the game's item wheel is placed while a selection wheel is open (UiWheelDistanceM; plan 2026-10-27-vr-selection-wheels Task 6, D6 "a bit further out than where the HUD UI sits currently", D13: the rest of the HUD is hidden meanwhile). With ui_hud_panel on or OFF (2026-10-28, Jay: "Out in the room, same as the other panel" - off, the wheel alone takes the pass while open). Same angle as on the HUD panel. Tuning (Tier D), my default.  tree=INERT
float UiWheelDistanceM = 0.8f;
// [DEP: ui_hud_height_m] required-for=the HUD panel's centre relative to head height at the latch, metres, negative = below (UiHudHeightM). Needs ui_hud_panel. Tuning (Tier D); -0.05 = Jay's disk (2026-10-16; was -0.25, the menu panel's value). A change re-latches at once.  tree=INERT
float UiHudHeightM = -0.05f;
// [DEP: ui_hud_x_m] required-for=the HUD panel's sideways offset at the latch, metres to the head's right, negative = left (UiHudXM). Needs ui_hud_panel. Tuning (Tier D). A change re-latches at once.  tree=INERT
float UiHudXM = 0.0f;
// [DEP: ui_hud_scale] required-for=the HUD panel's size against the size the HUD has on the eyes today, 1.0 = the same angle at the latch distance (UiHudScale). Needs ui_hud_panel. Tuning (Tier D); 1.5 = Jay's disk (2026-10-16).  tree=INERT
float UiHudScale = 1.5f;
// [DEP: ui_hud_hide] required-for=no HUD at all while ui_hud_panel is on (UiHudHide): the pass is still handed over, so it is not on the eyes, and no quad is submitted (Jay's 2026-10-03 "hidden with certain recording methods"; 2026-10-11: "Hide checkbox is enough for now"). Needs ui_hud_panel - does nothing without it. Tier D, OFF.  tree=INERT
bool UiHudHide = false;
// ---- HUD PANEL LAZY FOLLOW (docs/superpowers/specs/2026-10-20-hud-panel-lazy-follow-design.md; Jay 2026-10-20: "New key as a
// switch, your defaults, ship in next BETA").
// [DEP: ui_hud_follow] required-for=the HUD panel coming back in front of the player (UiHudFollow): when the view has been turned more than ui_hud_follow_deg from the panel, or the head has moved ui_hud_follow_drift_m along the floor from where it was placed, for ui_hud_follow_delay_ms (or physical crouch has crouched / stood - PhysCrouchEdges, Jay 2026-10-27 option A; the wait still applies), the panel glides to where a fresh latch would put it over ui_hud_follow_ms (uipanel_logic.h HudFollowStep). Needs ui_hud_panel - does nothing without it. 0 = the room-fixed panel exactly as before. Tier A, compiled ON (Jay 2026-10-20 "New key as a switch, your defaults"; HEADSET 2026-10-20 13:36-13:40, layer 20c45e06: "Yes it did, values were okay", logs backups/20261020_hud_follow_run1).  tree=INERT
bool UiHudFollow = true;
// [DEP: ui_hud_follow_deg] required-for=how far the view must be turned from the HUD panel before it follows, degrees (UiHudFollowDeg). Needs ui_hud_follow. Tuning (Tier D); 30 = Jay's disk (2026-10-27: "30 & 250, set the glide time to 220 aswell"; was the spec's 45).  tree=INERT
float UiHudFollowDeg = 30.0f;
// [DEP: ui_hud_follow_delay_ms] required-for=how long the view must stay turned away (or the head moved away) before the HUD panel follows, ms (UiHudFollowDelayMs). Needs ui_hud_follow. Tuning (Tier D); 250 = Jay's disk (2026-10-27; was the spec's 500).  tree=INERT
int UiHudFollowDelayMs = 250;
// [DEP: ui_hud_follow_ms] required-for=how long the HUD panel's glide back in front takes, ms (UiHudFollowMs). Needs ui_hud_follow. Tuning (Tier D); 220 = Jay's disk (2026-10-27; was the spec's 300).  tree=INERT
int UiHudFollowMs = 220;
// [DEP: ui_hud_follow_drift_m] required-for=how far the head may move along the floor from where the HUD panel was placed before it follows, metres (UiHudFollowDriftM). Needs ui_hud_follow. Tuning (Tier D); 0.5 = the spec's default.  tree=INERT
float UiHudFollowDriftM = 0.5f;

namespace uipanel {

namespace {

typedef int  (*PFN_Info)(int*, int*);
typedef int  (*PFN_Copy)(void*, int, int, int, int, int, int);   // dst, eye, sx, sy, w, h, dx
typedef void (*PFN_End)();

ID3D11Device* g_device = nullptr;
XrSession     g_session = XR_NULL_HANDLE;
XrSwapchain   g_sc = XR_NULL_HANDLE;
std::vector<XrSwapchainImageD3D11KHR> g_images;
int  g_scW = 0, g_scH = 0;
bool g_scFailed = false;

PFN_Info g_info = nullptr;
PFN_Copy g_copy = nullptr;
PFN_End  g_end = nullptr;
bool g_looked = false;

bool g_active = false;          // a menu shows and the panel owns the pass (the handshake is ours)
bool g_latched = false;
int  g_state = 0;
float g_pos[3] = {}, g_quat[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
long g_latches = 0, g_shown = 0, g_noSize = 0, g_noPass = 0, g_noViews = 0, g_xrFail = 0, g_copyRc[8] = {}, g_eye1Fail = 0;
long g_noRedecl = 0;            // panels submitted at the latched pose as it is (no usable eye poses that frame)
float g_declDeg = 0.0f;         // the angle between the rendered and the declared eye orientation, last frame
ULONGLONG g_next = 0;
bool g_saidRefuse = false;

// ---- HUD panel (ui_hud_panel)
PFN_Copy g_hudCopy = nullptr;       // DlvrHudPanelCopyEye - the plain copy
bool g_hudActive = false;           // the HUD panel owns the pass (in play; the handshake is ours)
bool g_hudLatched = false, g_hudRecentre = false, g_hudSaidRefuse = false, g_hudSaidFirst = false;
int  g_hudSig = -2;                 // uipin::HudShowing() last submit (-2 = not asked yet)
// The same reading published for the wrist router (HudScreenSig; trace docs/traces/20261112_2235_wrist_standdown_hudsig.md):
// written on the submit thread, read on the bootstrap's render thread.
volatile LONG   g_pubHudSig = -2;
volatile LONG64 g_pubHudSigMs = 0;
float g_hudPos[3] = {}, g_hudQuat[4] = { 0.0f, 0.0f, 0.0f, 1.0f }, g_hudPlaced[3] = {};
// The selection wheels' frame (WheelFrame): written by RenderHud on the submit thread, read by the holster poll.
SRWLOCK   g_wfLock = SRWLOCK_INIT;
float     g_wfC[3] = {}, g_wfR[3] = {}, g_wfU[3] = {}, g_wfN[3] = {};
float     g_wfE[4] = {};   // the drawn quad's edges from the wheel centre, metres on the panel: x lo, x hi, y lo, y hi (2026-11-16)
ULONGLONG g_wfAt = 0;
// Task 6 (D13): the wheel's own latch while a selection wheel shows.
bool      g_wheelLatched = false;
float     g_wheelPos[3] = {}, g_wheelQuat[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
long      g_hWheelShows = 0, g_hWheelLines = 0;
ULONGLONG g_wheelLastShowMs = 0;   // the last submit a wheel was showing (the fade hold counts from here)
ULONGLONG g_hudLostMs = 0;          // when the HUD went (0 = it is showing, or it went for a menu)
ULONGLONG g_hudNoPassMs = 0;        // when the bootstrap first had no finished pass (0 = it has one)
// MENU-OPEN PAUSE HOLD (RenderHud; trace docs/traces/20261113_1100_menuopen_pause_hold.md): the submit a hold began (0 = none), the
// holds begun this session and the submits the current one held.
ULONGLONG g_hudPauseHoldMs = 0;
long      g_hPauseHolds = 0, g_hPauseHeld = 0;
constexpr ULONGLONG kPauseHoldMaxMs = 5000;   // the longest measured HUD-gone -> menu gap is 3.4 s (771 archived logs, 2026-11-13)
ULONGLONG g_hudNext = 0, g_hudSliderLog = 0;
FollowState g_hudFollow{};          // ui_hud_follow (uipanel_logic.h HudFollowStep); reset at every real latch
long  g_hFollowLines = 0;           // FOLLOW lines printed (capped - INV-9: the count keeps going, the lines stop)
float g_hFollowFrom = 0.0f, g_hFollowDrift = 0.0f;   // the glide in progress: panel facing and head drift when it began
unsigned long long g_hFollowWaited = 0, g_hFollowAt = 0;
long g_hCrouchEdges = 0;            // PhysCrouchEdges() last seen by the follow; a change = a crouch / stand to follow
long g_hCrouchFollows = 0;          // follows started by one (for the log)
bool g_hFollowCrouch = false;       // the glide in progress was started by one
long g_hLatches = 0, g_hShown = 0, g_hHidden = 0, g_hNoSize = 0, g_hNoPass = 0, g_hNoViews = 0, g_hXrFail = 0, g_hNoRedecl = 0;
long g_hNotPlay = 0, g_hSliderLatches = 0, g_hCopyRc[8] = {};
float g_hDeclDeg = 0.0f, g_hDeclMaxDeg = 0.0f;
bool g_endOwed = false;             // a panel stopped: the bootstrap's handshake is dropped at the end of this Render
// A runtime recentre (HudRecentreRuntime): the display time the re-latch waits for (0 = none). Written by PollEvents, read
// by Render - atomic, they need not be on one thread. g_hudRtReason names it in the LATCHED line.
std::atomic<long long> g_hudRtDue{ 0 };
bool g_hudRtReason = false;
long g_hRtEvents = 0;
                                    // unless the other panel took the pass over (no pass may land on the eyes between)

const char* StateName(int s)
{
    return s == 1 ? "MAIN-MENU" : s == 2 ? "IN-GAME-MENU" : s == 0 ? "none" : s == 4 ? "MAIN-MENU (left on the eyes)"
         : s == 3 ? "IN-GAME-MENU other than pause (left on the eyes)" : "not readable";
}

bool Resolve()
{
    if (g_looked) { return g_info && g_copy && g_end; }
    const HMODULE b = GetModuleHandleA("xinput1_3.dll");
    if (!b) { return false; }
    g_looked = true;
    g_info = reinterpret_cast<PFN_Info>(GetProcAddress(b, "DlvrMenuPanelInfo"));
    g_copy = reinterpret_cast<PFN_Copy>(GetProcAddress(b, "DlvrMenuPanelCopyEye"));
    g_end  = reinterpret_cast<PFN_End>(GetProcAddress(b, "DlvrMenuPanelEnd"));
    g_hudCopy = reinterpret_cast<PFN_Copy>(GetProcAddress(b, "DlvrHudPanelCopyEye"));   // ui_hud_panel; reported by RenderHud
    const bool ok = g_info && g_copy && g_end;
    // [PROBE: UIPANEL] gate=ui_panel_menus  site=HEADSET  log-only=no
    LOG("[UIPANEL] bootstrap exports DlvrMenuPanelInfo / CopyEye / End: %s", ok ? "found"
        : "*** MISSING - an older xinput1_3.dll is deployed; the menus stay on the eyes this session ***");
    return ok;
}

bool EnsureSwapchain(int w, int h)
{
    if (g_sc != XR_NULL_HANDLE && g_scW >= w && g_scH >= h) { return true; }
    if (g_scFailed) { return false; }
    if (g_sc != XR_NULL_HANDLE) { xrDestroySwapchain_(g_sc); g_sc = XR_NULL_HANDLE; g_images.clear(); }
    XrSwapchainCreateInfo ci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    ci.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
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
    // [PROBE: UIPANEL] gate=ui_panel_menus  site=HEADSET  log-only=no
    LOG("[UIPANEL] panel swapchain %s: %dx%d R8G8B8A8_UNORM_SRGB, %u image(s) | XrResult %d", ok ? "CREATED"
        : "*** REFUSED - the menus stay on the eyes this session ***", w, h, n, static_cast<int>(r));
    if (!ok) {
        if (g_sc != XR_NULL_HANDLE) { xrDestroySwapchain_(g_sc); g_sc = XR_NULL_HANDLE; }
        g_images.clear();
        g_scFailed = true;
        return false;
    }
    g_scW = w; g_scH = h;
    return true;
}

// ---- v4 (1): THE TAB MENUS DRAWN LIKE THE PAUSE MENU (INV-534 (1); Jay: the pause menu's placement is the reference).
// gamedll +7AF080 is UI3dRotatingGroup's apply: `cmp byte [rcx+34h],0 / mov rbx,rcx / jz <tilted branch>`; the fall-through
// is the game's own FLAT branch (the group's own position, identity rotation) - what the pause menu, which has no such
// group, looks like. While ui_panel_menus is on the `jz` (74 13) is two NOPs; switched off, the bytes go back. Seven bytes
// of context are verified with it; PatchGameBytes suspends the other threads for the write (only at a toggle).
constexpr uintptr_t kTabsRva = 0x7AF086;
const unsigned char kTabsStock[9] = { 0x80, 0x79, 0x34, 0x00, 0x48, 0x8B, 0xD9, 0x74, 0x13 };
const unsigned char kTabsFlat[9]  = { 0x80, 0x79, 0x34, 0x00, 0x48, 0x8B, 0xD9, 0x90, 0x90 };
int g_tabsFlat = 0;             // 0 stock, 1 flat, -1 refused (not tried again this session)

void FlatTabs(bool want)
{
    if (g_tabsFlat < 0 || (g_tabsFlat == 1) == want) { return; }
    const uintptr_t gd = reinterpret_cast<uintptr_t>(GetModuleHandleA("gamedll_x64_rwdi.dll"));
    if (!gd) { return; }
    int attempts = 0;
    const bool ok = PatchGameBytes(gd + kTabsRva, want ? kTabsStock : kTabsFlat, want ? kTabsFlat : kTabsStock, sizeof(kTabsStock),
                                   &attempts);
    // [PROBE: UIPANEL] gate=ui_panel_menus  site=HEADSET  log-only=no
    LOG("[UIPANEL] tab menus %s: gamedll+7AF08D %s (attempts %d)%s", want ? "FLAT like the pause menu" : "back to the game's tilt",
        ok ? "PATCHED" : "*** NOT PATCHED - the bytes are not gamedll 97046bba's, or the write was refused ***", attempts,
        ok ? "" : " - the tab menus keep the game's tilt and sway this session");
    g_tabsFlat = ok ? (want ? 1 : 0) : -1;
}

void Stop(const char* why)
{
    if (!g_active) { return; }
    g_active = false;
    g_latched = false;
    // ui_hud_panel: the HUD panel may take the pass over in this same Render - the handshake is then never dropped, so no
    // HUD pass lands on the eyes between the two (SPEC-UI-HUD-PANEL §10). Render() settles g_endOwed.
    if (UiHudPanel) { g_endOwed = true; } else if (g_end) { g_end(); }
    // [PROBE: UIPANEL] gate=ui_panel_menus  site=HEADSET  log-only=no
    LOG("[UIPANEL] panel OFF (%s) - %s", why, UiHudPanel ? "the HUD panel takes the pass if the HUD is showing, else it draws "
        "onto the eyes again" : "the pass draws onto the eyes again");
}

void Periodic()
{
    const ULONGLONG now = GetTickCount64();
    if (now < g_next) { return; }
    g_next = now + 5000;
    // [PROBE: UIPANEL] gate=ui_panel_menus  site=HEADSET  log-only=no
    LOG_PERIODIC("[UIPANEL] 5 s (layer): menu state %s | panel submitted %ld | NOT submitted: pass size not ready %ld, no finished pass "
        "%ld, no usable eye poses to latch from %ld, OpenXR image call failed %ld | eye 0's copy results: drawn %ld, nothing to "
        "show %ld, off %ld, bad rectangle %ld, D3D refused %ld, bootstrap off %ld, other %ld | latches %ld | panel at (%.3f %.3f "
        "%.3f) m, distance %.2f, height %.2f, scale %.2f | re-declared against the game image: rendered-to-declared angle "
        "%.2f deg (the declared pitch; 0 = nothing to correct), submitted WITHOUT it %ld | tab menus %s",
        StateName(g_state), g_shown, g_noSize, g_noPass, g_noViews, g_xrFail, g_copyRc[0], g_copyRc[1], g_copyRc[2], g_copyRc[3],
        g_copyRc[5], g_copyRc[6], g_copyRc[4] + g_copyRc[7], g_latches, g_pos[0], g_pos[1], g_pos[2],
        static_cast<double>(UiPanelDistanceM), static_cast<double>(UiPanelHeightM), static_cast<double>(UiPanelScale),
        static_cast<double>(g_declDeg), g_noRedecl, g_tabsFlat == 1 ? "FLAT (patched)" : g_tabsFlat == 0 ? "stock" : "PATCH REFUSED");
    g_shown = g_noSize = g_noPass = g_noViews = g_xrFail = g_noRedecl = 0;
    for (long& c : g_copyRc) { c = 0; }
}

}  // namespace

void Init(ID3D11Device* device, XrSession session)
{
    g_device = device;
    g_session = session;
    g_scFailed = false;
}

void Shutdown()
{
    if (g_sc != XR_NULL_HANDLE && xrDestroySwapchain_) { xrDestroySwapchain_(g_sc); }
    g_sc = XR_NULL_HANDLE;
    g_images.clear();
    g_scW = g_scH = 0;
    g_active = false;
    g_latched = false;
    g_hudActive = false;
    g_hudLatched = false;
    g_endOwed = false;
}

bool Active() { return g_active || g_hudActive; }

int HudScreenSig()
{
    const LONG sig = g_pubHudSig;
    const ULONGLONG at = static_cast<ULONGLONG>(g_pubHudSigMs);
    if (at == 0 || GetTickCount64() - at > 100) { return -2; }   // not read lately (ui_hud_panel off, a menu panel owns the pass)
    return static_cast<int>(sig);
}

void HudRecentre() { g_hudRecentre = true; }

void HudRecentreRuntime(XrTime changeTime, int spaceType)
{
    // 100 ms past changeTime: the eye poses the latch reads are the game's declared ones, located up to a frame or two
    // before the display time (AFW's held eye the older) - this keeps them from being old-space poses. My margin, not measured.
    const long long due = static_cast<long long>(changeTime) + 100000000LL;
    if (due > g_hudRtDue.load()) { g_hudRtDue.store(due); }
    ++g_hRtEvents;
    // [PROBE: HUDPANEL] gate=ui_hud_panel  site=HEADSET  log-only=no
    LOG("[HUDPANEL] runtime recentre event #%ld (reference space type %d, changeTime %lld) - %s", g_hRtEvents, spaceType,
        static_cast<long long>(changeTime),
        UiHudPanel ? "the HUD panel latches again at the first submit past changeTime + 100 ms"
                   : "ui_hud_panel is off: noted, it latches again when the panel is next shown");
}

// The menu panel (stage 1, verified - INV-535). Unchanged by the HUD panel except for Stop()'s deferred hand-back.
static int RenderMenu(XrSpace appSpace, bool viewsValid, const XrPosef* eyes, const XrQuaternionf& rendered0, const XrFovf& fov0,
                      const XrFovf& fov1, XrCompositionLayerQuad* out)
{
    // v5: not while the panel is REFUSED this session (no brackets - AFW off at launch): flat tab menus with no panel would
    // only change the stock look.
    FlatTabs(UiPanelMenus && !g_saidRefuse);
    if (!UiPanelMenus) { Stop("ui_panel_menus switched off"); return 0; }
    if (g_session == XR_NULL_HANDLE || !Resolve()) { return false; }
    // v2 (Jay after run 1, 2026-10-11: "Main menu looked off because of the depth difference ... either fix that or disable
    // it for main menu specifically"; inventory / vendor "skewed"): the MAIN MENU is never put on the panel - its screens are
    // placed in the 3D scene with real depth per eye, and one eye's image on a flat quad loses that. It is reported as state
    // 4 so the log still shows it. ui_panel_all_menus=0 keeps the panel to the pause menu's own screen (state 3 = another
    // in-game menu left on the eyes).
    bool pause = false;
    int state = uipin::MenuState(&pause);
    // v4: the main menu is NEVER on the panel (Jay after run 3: "Main menu still isn't correct with the switch"). Its
    // screens stay in the 3D scene and uipin's main-menu rule gives their camera the head. ui_panel_main_menu is read
    // nowhere else and no longer has an effect; the key stays registered (a shipped key is never retired, D-156).
    if (state == 1) { state = 4; }
    else if (state == 2 && !pause && !UiPanelAllMenus) { state = 3; }
    if (state != g_state) {
        // [PROBE: UIPANEL] gate=ui_panel_menus  site=HEADSET  log-only=no
        LOG("[UIPANEL] menu state %s -> %s", StateName(g_state), StateName(state));
        if ((state == 1 || state == 2) && (g_state == 1 || g_state == 2)) { g_latched = false; }   // main menu <-> in game: a new opening
        g_state = state;
    }
    if (state != 1 && state != 2) {
        // -1 (not readable this frame): no panel this frame, the latch is kept
        if (state == 0) { Stop("no menu"); }
        else if (state == 4) { Stop("the main menu is left on the eyes"); }
        else if (state == 3) { Stop("ui_panel_all_menus=0 and this is not the pause menu"); }
        Periodic();
        return false;
    }
    // The handshake: from here the bootstrap redirects the bracketed pass, and the caller must not lay it on the eyes.
    int W = 0, H = 0;
    const int info = g_info(&W, &H);
    if (info == 2 || info == 6) {
        if (!g_saidRefuse) {
            g_saidRefuse = true;
            // [PROBE: UIPANEL] gate=ui_panel_menus  site=HEADSET  log-only=no
            LOG("[UIPANEL] *** REFUSED: the bootstrap's HUD layer is %s - the bracketed pass does not exist this launch (it needs "
                "afw_enable=1 at launch or the marker dlvr\\hud_layer.txt, and dlss_flat.txt). The menus stay on the eyes. ***",
                info == 6 ? "not running (dlss_flat off)" : "off");
        }
        Stop("the bootstrap's HUD layer is off");
        return false;
    }
    g_active = true;
    if (info != 0 || W < 64 || H < 64) { ++g_noSize; Periodic(); return false; }
    if (!g_latched) {
        if (!viewsValid) { ++g_noViews; Periodic(); return false; }
        const float head[3] = { (eyes[0].position.x + eyes[1].position.x) * 0.5f, (eyes[0].position.y + eyes[1].position.y) * 0.5f,
                                (eyes[0].position.z + eyes[1].position.z) * 0.5f };
        // latched in the GAME's frame: from the orientation the image is rendered at, not the pitched one it is declared at
        const float hq[4] = { rendered0.x, rendered0.y, rendered0.z, rendered0.w };
        if (!Latch(head, hq, UiPanelDistanceM, UiPanelHeightM, g_pos, g_quat)) { ++g_noViews; Periodic(); return false; }
        g_latched = true;
        ++g_latches;
        // [PROBE: UIPANEL] gate=ui_panel_menus  site=HEADSET  log-only=no
        LOG("[UIPANEL] LATCHED (#%ld, %s): head (%.3f %.3f %.3f) m, panel centre (%.3f %.3f %.3f) m, turned %.1f deg about the "
            "vertical | distance %.2f m, height %.2f m, scale %.2f", g_latches, StateName(state), head[0], head[1], head[2],
            g_pos[0], g_pos[1], g_pos[2], 2.0f * std::atan2(g_quat[1], g_quat[3]) * 57.29578f,
            static_cast<double>(UiPanelDistanceM), static_cast<double>(UiPanelHeightM), static_cast<double>(UiPanelScale));
    }
    // v3 - THE STEREO PANEL (INV-533): the WHOLE eye image of EACH eye, side by side in one swapchain image, shown to its
    // own eye on its own quad at the same room pose. The inventory / map / main-menu screens are drawn with real per-eye
    // parallax (run 2's capture: the map screen sits ~135 px right of centre in eye 0 and its bottom was cut by the
    // centred rectangle) - one eye's image on a flat quad loses that depth and the cut loses the cursor.
    const Rect r = Crop(W, H);   // the whole image (was Crop(W, H, true))
    if (!EnsureSwapchain(2 * W, H)) { Stop("no panel swapchain"); return 0; }

    XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    uint32_t index = 0;
    if (XR_FAILED(xrAcquireSwapchainImage_(g_sc, &ai, &index)) || index >= g_images.size()) { ++g_xrFail; Periodic(); return 0; }
    XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wi.timeout = XR_INFINITE_DURATION;
    const bool waited = XR_SUCCEEDED(xrWaitSwapchainImage_(g_sc, &wi));
    int rc = 7, rc1 = 7;
    if (waited) {
        rc = g_copy(g_images[index].texture, 0, r.x, r.y, r.w, r.h, 0);
        rc1 = g_copy(g_images[index].texture, 1, r.x, r.y, r.w, r.h, r.w);
        // The desktop window shows the menu too (deskmirror.h DrawPanel; Jay 2026-11-14, trace 20261114_1125): eye 0's half, read here
        // while the image is still acquired. Only when the window picture was redrawn this submit (deskmirror::SetFresh).
        // Removed by AngelWolf 2026-11-26: rendered in 3D perspective via DrawQuadLayer below
        // if (rc == 0 && rc1 == 0) { ::dlvr::deskmirror::DrawPanel(g_device, g_images[index].texture, r.w, r.h); }
    }
    XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    xrReleaseSwapchainImage_(g_sc, &ri);
    if (!waited) { ++g_xrFail; Periodic(); return 0; }
    ++g_copyRc[(rc >= 0 && rc < 7) ? rc : 7];
    if (rc == 0 && rc1 != 0) { rc = rc1; ++g_eye1Fail; }
    if (rc != 0) { ++g_noPass; Periodic(); return 0; }

    // Per eye (trace docs/traces/20261124_1345_panel_eye_offcentre.md): each eye's quad is sized from ITS OWN declared frustum and
    // centred on ITS OWN crop centre's direction (EyeQuadOffset) - 0 / 0 and identical sizes for a symmetric frustum (as before).
    const XrFovf* fovE[2] = { &fov0, &fov1 };
    float widthE[2] = {}, heightE[2] = {}, dxE[2] = {}, dyE[2] = {};
    for (int e = 0; e < 2; ++e) {
        const float tl = std::tan(fovE[e]->angleLeft), tr = std::tan(fovE[e]->angleRight);
        const float td = std::tan(fovE[e]->angleDown), tu = std::tan(fovE[e]->angleUp);
        SizeM(r, W, H, tl, tr, td, tu, UiPanelDistanceM, UiPanelScale, &widthE[e], &heightE[e]);
        EyeQuadOffset(r, W, H, tl, tr, td, tu, UiPanelDistanceM, UiPanelScale, &dxE[e], &dyE[e]);
    }
    const float width = widthE[0], height = heightE[0];
    static bool s_saidFirst = false;
    if (!s_saidFirst) {
        s_saidFirst = true;
        // [PROBE: UIPANEL] gate=ui_panel_menus  site=HEADSET  log-only=no
        LOG("[UIPANEL] FIRST panel submitted (logged unconditionally): %s | STEREO: each eye's whole pass %dx%d on its own quad "
            "(eye 0 -> LEFT, eye 1 -> RIGHT), one room pose | eye 0 declared field of view tangents L %.4f R %.4f D %.4f U %.4f | "
            "panel %.3f x %.3f m | per-eye quad shift (right, up) eye 0 %+.4f %+.4f m, eye 1 %+.4f %+.4f m (0 = a symmetric "
            "frustum); eye 1 tangents L %.4f R %.4f D %.4f U %.4f, panel %.3f x %.3f m", StateName(state), W, H,
            std::tan(fov0.angleLeft), std::tan(fov0.angleRight), std::tan(fov0.angleDown), std::tan(fov0.angleUp), width, height,
            dxE[0], dyE[0], dxE[1], dyE[1], std::tan(fov1.angleLeft), std::tan(fov1.angleRight), std::tan(fov1.angleDown),
            std::tan(fov1.angleUp), widthE[1], heightE[1]);
    }
    // v4 (3) THE ROLL (INV-534 (3)): the game image is rendered at `rendered0` and declared at eyes[0].orientation (the
    // declared pitch). The runtime holds a room-space quad rigid, so the latched pose is turned by the same difference
    // about the head every frame - otherwise the game image tilts against the panel as the head turns.
    // v5 (Jay after run 4, the inventory: "It could stand to move up a little bit in terms of placement"): every in-game
    // menu OTHER than the pause menu is raised by ui_panel_tabs_up_m - the pause menu, his reference, stays where it is.
    // v6 (Jay after his tuning run: "add an X axis slider for the inventory menu ... I'd like to be able to bring it over
    // to the left slightly"): the same menus are also moved ui_panel_tabs_x_m along the panel's own right axis (negative =
    // to the viewer's left - the panel faces the viewer, so its +x is the viewer's right).
    const bool tabs = state == 2 && !pause;
    float right[3] = { 1.0f, 0.0f, 0.0f };
    {
        const float xAxis[3] = { 1.0f, 0.0f, 0.0f };
        Rotate(g_quat, xAxis, right);
    }
    const float tx = tabs ? UiPanelTabsXM : 0.0f;
    const float lPos[3] = { g_pos[0] + right[0] * tx, g_pos[1] + (tabs ? UiPanelTabsUpM : 0.0f) + right[1] * tx,
                            g_pos[2] + right[2] * tx };
    float sPos[3] = { lPos[0], lPos[1], lPos[2] }, sQuat[4] = { g_quat[0], g_quat[1], g_quat[2], g_quat[3] };
    if (viewsValid) {
        const float pivot[3] = { (eyes[0].position.x + eyes[1].position.x) * 0.5f, (eyes[0].position.y + eyes[1].position.y) * 0.5f,
                                 (eyes[0].position.z + eyes[1].position.z) * 0.5f };
        const float qR[4] = { rendered0.x, rendered0.y, rendered0.z, rendered0.w };
        const float qD[4] = { eyes[0].orientation.x, eyes[0].orientation.y, eyes[0].orientation.z, eyes[0].orientation.w };
        Redeclare(lPos, g_quat, pivot, qR, qD, sPos, sQuat);
        const float n = std::sqrt(sQuat[0] * sQuat[0] + sQuat[1] * sQuat[1] + sQuat[2] * sQuat[2] + sQuat[3] * sQuat[3]);
        if (n > 0.5f) { for (float& c : sQuat) { c /= n; } }
        const float d = std::fabs(qR[0] * qD[0] + qR[1] * qD[1] + qR[2] * qD[2] + qR[3] * qD[3]);
        g_declDeg = 2.0f * std::acos(d > 1.0f ? 1.0f : d) * 57.29578f;
    } else {
        ++g_noRedecl;
    }
    float mAx[3], mAy[3];
    {
        const float xAxis[3] = { 1.0f, 0.0f, 0.0f }, yAxis[3] = { 0.0f, 1.0f, 0.0f };
        Rotate(sQuat, xAxis, mAx);   // the panel's own right / up as submitted (after Redeclare)
        Rotate(sQuat, yAxis, mAy);
    }
    for (int e = 0; e < 2; ++e) {
        XrCompositionLayerQuad& q = out[e];
        q = XrCompositionLayerQuad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
        q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;   // the pass is premultiplied (hudlayer's own lay-on blend)
        q.space = appSpace;                                                   // fixed in the room
        q.eyeVisibility = e == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;   // eye 0 = the left eye (INV-386)
        q.subImage.swapchain = g_sc;
        q.subImage.imageArrayIndex = 0;
        q.subImage.imageRect.offset = { e * r.w, 0 };
        q.subImage.imageRect.extent = { r.w, r.h };
        q.pose.orientation = { sQuat[0], sQuat[1], sQuat[2], sQuat[3] };
        q.pose.position = { sPos[0] + mAx[0] * dxE[e] + mAy[0] * dyE[e], sPos[1] + mAx[1] * dxE[e] + mAy[1] * dyE[e],
                            sPos[2] + mAx[2] * dxE[e] + mAy[2] * dyE[e] };
        q.size = { widthE[e], heightE[e] };
    }

    // Added by AngelWolf 2026-11-26: mirror the right-eye in-game menu panel quad onto the desktop window
    if (g_device && index < g_images.size()) {
        ::dlvr::deskmirror::DrawQuadLayer(g_device, g_images[index].texture, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            out[1], eyes[1], fov1, 1);
    }
    ++g_shown;
    Periodic();
    return 2;
}

// ===== HUD PANEL, stage 1 (ui_hud_panel; docs/SPEC-UI-HUD-PANEL-2026-10-11.md). =====
// In play (uipin::HudShowing) the SAME bracketed pass the menu panel takes is copied plain into the same swapchain and
// shown on a quad pair latched in the room: ui_hud_distance_m ahead of the head, ui_hud_height_m above it, ui_hud_x_m to
// its right. Latched at the first in-play pass, after the HUD was gone for over a second (a load), on the re-centre
// button and when a placement slider moves; KEPT across a menu. Never runs in the same submit as the menu panel.
// With ui_hud_follow the latch also glides back in front when the player has turned or walked away from it for a moment
// (uipanel_logic.h HudFollowStep; docs/superpowers/specs/2026-10-20-hud-panel-lazy-follow-design.md).
// COST: off = one bool test per submit. On, in play: the level walk (a few dozen pointer reads), three OpenXR swapchain
// calls, two export calls (a region copy + a full-screen draw each in the bootstrap), two quads for the runtime.
namespace {

void HudStop(const char* why)
{
    if (!g_hudActive) { return; }
    g_hudActive = false;
    g_endOwed = true;
    // [PROBE: HUDPANEL] gate=ui_hud_panel  site=HEADSET  log-only=no
    LOG("[HUDPANEL] panel OFF (%s) - latch %s", why, g_hudLatched ? "kept" : "none");
}

void HudPeriodic()
{
    const ULONGLONG now = GetTickCount64();
    if (now < g_hudNext) { return; }
    g_hudNext = now + 5000;
    // [PROBE: HUDPANEL] gate=ui_hud_panel  site=HEADSET  log-only=no
    LOG_PERIODIC("[HUDPANEL] 5 s (layer): HUD signal %d (1 showing, 0 not, -1 unreadable) | panel submitted %ld, hidden by ui_hud_hide "
        "%ld | NOT submitted: HUD not showing %ld, pass size not ready %ld, no finished pass %ld, no usable eye poses to latch "
        "from %ld, OpenXR image call failed %ld | eye 0's copy results: drawn %ld, nothing to show %ld, off %ld, bad rectangle "
        "%ld, D3D refused %ld, bootstrap off %ld, other %ld | latches %ld (of them by a moved slider %ld) | panel at (%.3f "
        "%.3f %.3f) m, distance %.2f, height %.2f, sideways %.2f, scale %.2f | rendered-to-declared angle now %.2f deg, "
        "largest %.2f deg (the declared pitch; a larger value = more than the pitch is in it), submitted WITHOUT it %ld | "
        "follow %s: follows so far %ld (of them a physical crouch / stand %ld), glances cancelled %ld, largest view-to-panel "
        "angle this window %.1f deg, now %s",
        g_hudSig, g_hShown, g_hHidden, g_hNotPlay, g_hNoSize, g_hNoPass, g_hNoViews, g_hXrFail, g_hCopyRc[0], g_hCopyRc[1],
        g_hCopyRc[2], g_hCopyRc[3], g_hCopyRc[5], g_hCopyRc[6], g_hCopyRc[4] + g_hCopyRc[7], g_hLatches, g_hSliderLatches,
        g_hudPos[0], g_hudPos[1], g_hudPos[2], static_cast<double>(UiHudDistanceM), static_cast<double>(UiHudHeightM),
        static_cast<double>(UiHudXM), static_cast<double>(UiHudScale), static_cast<double>(g_hDeclDeg),
        static_cast<double>(g_hDeclMaxDeg), g_hNoRedecl, UiHudFollow ? "ON" : "OFF", g_hudFollow.follows,
        g_hCrouchFollows, g_hudFollow.cancels, static_cast<double>(g_hudFollow.maxOffDeg),
        g_hudFollow.phase == 2 ? "gliding" : (g_hudFollow.phase == 1 ? "armed" : "idle"));
    g_hShown = g_hHidden = g_hNotPlay = g_hNoSize = g_hNoPass = g_hNoViews = g_hXrFail = g_hNoRedecl = 0;
    g_hDeclMaxDeg = 0.0f;
    g_hudFollow.maxOffDeg = 0.0f;
    for (long& c : g_hCopyRc) { c = 0; }
}

// QUEST-BRIEF EXEMPTION from ui_hud_hide (trace docs/traces/20261126_1215_challengebrief_component.md, v2; Jay 2026-11-26: "Yes, that's
// got to be fixed"). The log side: the first hidden submit of the session (unconditional - names the reading and the component id, so a
// component never found SAYS so), then every SHOWN / GONE edge, capped (INV-9). Submit thread only.
int       g_hBriefState = -2;          // ChallengeBriefActive's last reading while hidden (-2 = never asked)
long      g_hBriefShows = 0, g_hBriefLines = 0;
ULONGLONG g_hBriefSinceMs = 0;
constexpr long kBriefMaxLines = 200;

void HudBriefEdge(int brief, int id)
{
    static int s_idSaid = -1;   // the component id last named by a FOUND line (a re-registration under a new id names it again)
    if (id >= 0 && id != s_idSaid && g_hBriefLines < kBriefMaxLines) {
        s_idSaid = id;
        ++g_hBriefLines;
        // [PROBE: HUDPANEL] gate=ui_hud_panel,ui_hud_hide  site=HEADSET  log-only=yes
        LOG("[HUDPANEL] QUEST BRIEF FOUND - HUD component HudChallengeBrief registered at id %d, reading %d (1 = active)", id, brief);
    }
    const int was = g_hBriefState;
    g_hBriefState = brief;
    const bool shownNow = brief == 1, shownWas = was == 1;
    if (was != -2 && shownNow == shownWas) { return; }
    if (g_hBriefLines >= kBriefMaxLines) { return; }
    ++g_hBriefLines;
    const ULONGLONG now = GetTickCount64();
    if (was == -2) {
        // [PROBE: HUDPANEL] gate=ui_hud_panel,ui_hud_hide  site=HEADSET  log-only=no
        LOG("[HUDPANEL] QUEST BRIEF WATCH (logged unconditionally, the first submit ui_hud_hide hides the HUD) - HUD component "
            "HudChallengeBrief id %d (-1 = not found in the HUD manager yet), reading %d (1 = active = the card shows, 0 = registered "
            "and not active, -1 = not found / not readable)", id, brief);
    }
    if (shownNow && !shownWas) {
        ++g_hBriefShows;
        g_hBriefSinceMs = now;
        // [PROBE: HUDPANEL] gate=ui_hud_panel,ui_hud_hide  site=HEADSET  log-only=no
        LOG("[HUDPANEL] QUEST BRIEF SHOWN #%ld - HUD component HudChallengeBrief id %d ACTIVE: the HUD panel draws while it is (ui_hud_hide "
            "waived, the whole HUD pass with it)", g_hBriefShows, id);
    } else if (!shownNow && shownWas) {
        // [PROBE: HUDPANEL] gate=ui_hud_panel,ui_hud_hide  site=HEADSET  log-only=no
        LOG("[HUDPANEL] QUEST BRIEF GONE #%ld after %llu ms (reading %d) - ui_hud_hide hides the HUD again%s", g_hBriefShows,
            now - g_hBriefSinceMs, brief, g_hBriefLines >= kBriefMaxLines ? " | LAST QUEST BRIEF line" : "");
    }
}

int RenderHud(XrSpace appSpace, bool viewsValid, const XrPosef* eyes, const XrQuaternionf& rendered0, const XrFovf& fov0,
              const XrFovf& fov1, XrCompositionLayerQuad* out)
{
    // Selection wheels (Task 6, D13): while a wheel shows, the quad pair carries ONLY the wheel's rectangle, at its own latch
    // ui_wheel_distance_m ahead (same angle), and ui_hud_hide does not hide it (spec C5). The HUD's own latch is kept for after.
    // With ui_hud_panel OFF (Jay 2026-10-28, after Bono's log: "We'll make it work without the panel" ... "Out in the room, same as
    // the other panel") the wheel still gets its own quad: the pass is taken over only from the wheel's open to the fade hold's
    // end, the HUD is never latched or followed, and the pass goes back onto the eyes after.
    // COST: one volatile read per submit; a latch (LatchSide) once per opening.
    const ULONGLONG now = GetTickCount64();
    if (HolsterWheelShowing()) { g_wheelLastShowMs = now; }
    const bool wheelShow = g_wheelLastShowMs != 0 && now - g_wheelLastShowMs <= kWheelFadeHoldMs;   // + the game's fade-out
    const bool wheelOnly = !UiHudPanel;
    if (wheelOnly && !wheelShow) {
        HudStop(g_wheelLatched ? "the selection wheel closed - ui_hud_panel is off, the HUD is back on the eyes"
                               : "ui_hud_panel switched off");
        g_hudLatched = false;       // switched on again = a fresh latch in front of the head
        g_wheelLatched = false;
        g_hudSig = -2;
        return 0;
    }
    if (g_session == XR_NULL_HANDLE) { return 0; }
    Resolve();
    if (!g_looked) { return 0; }    // the bootstrap module is not there yet: asked again next submit
    if (!g_info || !g_end || !g_hudCopy) {
        if (!g_hudSaidRefuse) {
            g_hudSaidRefuse = true;
            // [PROBE: HUDPANEL] gate=ui_hud_panel  site=HEADSET  log-only=no
            LOG("[HUDPANEL] *** REFUSED: the bootstrap exports are not all there (DlvrMenuPanelInfo %s, DlvrMenuPanelEnd %s, "
                "DlvrHudPanelCopyEye %s) - an older xinput1_3.dll is deployed. The HUD stays on the eyes this session. ***",
                g_info ? "found" : "MISSING", g_end ? "found" : "MISSING", g_hudCopy ? "found" : "MISSING");
        }
        return 0;
    }
    const int sig = uipin::HudShowing();
    InterlockedExchange64(&g_pubHudSigMs, static_cast<LONG64>(now));
    InterlockedExchange(&g_pubHudSig, sig);
    if (sig != g_hudSig) {
        char levels[512];
        uipin::DescribeLevels(levels, sizeof(levels));
        // [PROBE: HUDPANEL] gate=ui_hud_panel  site=HEADSET  log-only=no
        LOG("[HUDPANEL] HUD signal %d -> %d (1 = a LevelDI level shows its HUD screen, 0 = not, -1 = not readable; -2 = first "
            "reading, logged unconditionally) | the game's levels and their visible screens: %s", g_hudSig, sig, levels);
        g_hudSig = sig;
    }
    // MENU-OPEN PAUSE HOLD (trace docs/traces/20261113_1100_menuopen_pause_hold.md). RUN WS2n: the HUD screen leaves the visible list
    // 20-25 ms before the menu state reads open; stopping here dropped the bootstrap's handshake and the next HUD pass was refused
    // straight onto the eye image (2 refused passes per opening, 22 of 22). While this panel holds the pass AND the game's own
    // PauseGame hide reason is set (uipin::HudHiddenForPause - set only when the game pauses for a menu / dialog / end screen), the
    // handshake is kept and NOTHING is shown until the menu panel takes the pass (Render's menu branch); at most kPauseHoldMaxMs, then
    // the old path. Without the reason (a load, a movie, a portal) the old path, unchanged.
    // COST: only on a submit reading sig 0 while this panel is active - <= 19 guarded reads. Every other submit: one compare.
    if (sig != 0) {
        if (g_hudPauseHoldMs) {
            // [PROBE: HUDPANEL] gate=ui_hud_panel  site=HEADSET  log-only=no
            LOG("[HUDPANEL] MENU-OPEN HOLD #%ld ended: the HUD came back (signal %d) after %llu ms, %ld submits held, no menu took the "
                "pass", g_hPauseHolds, sig, now - g_hudPauseHoldMs, g_hPauseHeld);
            g_hudPauseHoldMs = 0;
        }
    } else if (g_hudActive && !wheelOnly) {
        const int paused = uipin::HudHiddenForPause();
        if (paused == 1 && (!g_hudPauseHoldMs || now - g_hudPauseHoldMs <= kPauseHoldMaxMs)) {
            if (!g_hudPauseHoldMs) {
                g_hudPauseHoldMs = now;
                g_hPauseHeld = 0;
                ++g_hPauseHolds;
                // [PROBE: HUDPANEL] gate=ui_hud_panel  site=HEADSET  log-only=no
                LOG("[HUDPANEL] MENU-OPEN HOLD #%ld: the HUD screen left with the game's PauseGame hide reason set - the pass is "
                    "held (handshake kept, nothing shown, nothing laid on the eyes) until the menu panel takes it (cap %llu ms)",
                    g_hPauseHolds, kPauseHoldMaxMs);
            }
            int hw = 0, hh = 0;
            g_info(&hw, &hh);   // the handshake: the bracketed pass stays redirected into the layer
            ++g_hPauseHeld;
            HudPeriodic();
            return 0;
        }
        if (g_hudPauseHoldMs) {
            // [PROBE: HUDPANEL] gate=ui_hud_panel  site=HEADSET  log-only=no
            LOG("[HUDPANEL] MENU-OPEN HOLD #%ld ended WITHOUT a menu after %llu ms, %ld submits held (%s) - the pass is handed back "
                "as before", g_hPauseHolds, now - g_hudPauseHoldMs, g_hPauseHeld,
                paused == 1 ? "the cap was reached" : paused == 0 ? "the PauseGame reason went" : "the hide list became unreadable");
            g_hudPauseHoldMs = 0;
        } else {
            // [PROBE: HUDPANEL] gate=ui_hud_panel  site=HEADSET  log-only=no
            LOG("[HUDPANEL] HUD screen gone WITHOUT the PauseGame hide reason (reading %d: 0 = another reason or none, -1 = not "
                "readable) - no hold, the pass is handed back as before", paused);
        }
    }
    if (sig != 1) {
        ++g_hNotPlay;
        if (sig == 0) {             // -1 (not readable this frame): no panel this frame, everything kept
            const bool menu = uipin::MenuState() == 2;
            if (menu) { g_hudLostMs = 0; }                    // gone for an in-game menu: never a reason to latch again
            else if (!g_hudLostMs) { g_hudLostMs = now; }
            HudStop(menu ? "an in-game menu is open and ui_panel_menus is off - the menu draws on the eyes"
                         : "the HUD is not showing (a load, the main menu, a start-up screen)");
        }
        HudPeriodic();
        return 0;
    }
    // The handshake: from here the bootstrap redirects the bracketed pass, and the caller must not lay it on the eyes.
    int W = 0, H = 0;
    const int info = g_info(&W, &H);
    if (info == 2 || info == 6) {
        if (!g_hudSaidRefuse) {
            g_hudSaidRefuse = true;
            // [PROBE: HUDPANEL] gate=ui_hud_panel  site=HEADSET  log-only=no
            LOG("[HUDPANEL] *** REFUSED: the bootstrap's HUD layer is %s - the bracketed pass does not exist this launch (it "
                "needs afw_enable=1 at launch or the marker dlvr\\hud_layer.txt, and dlss_flat.txt). The HUD stays on the eyes. ***",
                info == 6 ? "not running (dlss_flat off)" : "off");
        }
        HudStop("the bootstrap's HUD layer is off");
        return 0;
    }
    if (!g_hudActive) {
        g_hudActive = true;
        if (wheelOnly) {
            // [PROBE: HUDPANEL] gate=holster_enable  site=HEADSET  log-only=no
            LOG("[HUDPANEL] panel ON for the selection wheel only (ui_hud_panel is off) - the pass goes to the wheel's quad until "
                "it closes, then back onto the eyes");
        } else {
            // [PROBE: HUDPANEL] gate=ui_hud_panel  site=HEADSET  log-only=no
            LOG("[HUDPANEL] panel ON - the HUD pass goes to the panel, nothing is laid on the eyes (latch %s)",
                g_hudLatched ? "kept" : "to be taken");
        }
    }
    if (info != 0 || W < 64 || H < 64) { ++g_hNoSize; HudPeriodic(); return 0; }
    Rect r = Crop(W, H);
    if (wheelShow) {   // the wheel box from eye 0's own frustum (INV-735 - any headset; trace 20261113_2115)
        EyeTan t{};
        if (!GameEyeTan(0, &t) || !WheelRect(W, H, t, &r)) {
            static bool s_saidNoTan = false;
            if (!s_saidNoTan) {
                s_saidNoTan = true;
                // [PROBE: HUDPANEL] gate=ui_hud_panel,holster_enable  site=HEADSET  log-only=no
                LOG("[HUDPANEL] wheel box NOT MADE - eye 0's game frustum %s (image %dx%d); this submit shows no wheel quad (logged once)",
                    GameEyeTan(0, &t) ? "known but the box was refused" : "not known yet", W, H);
            }
            ++g_hNoSize; HudPeriodic(); return 0;
        }
    }
    // Eye 1's box (trace docs/traces/20261124_1612_wheel_taa_fsr_offcentre.md): from eye 1's OWN frustum at eye 0's size (SameSizeAt) -
    // on an off-centre frustum eye 1's wheel is at its own projection axis, and eye 0's box cut it. Eye 1 not known: eye 0's, as before.
    // Symmetric frustum: identical to r.
    Rect r1 = r;
    if (wheelShow) {
        EyeTan t1{};
        Rect own{};
        if (GameEyeTan(1, &t1) && WheelRect(W, H, t1, &own)) { r1 = SameSizeAt(r, own, W, H); }
    }
    if (!EnsureSwapchain(2 * W, H)) { HudStop("no panel swapchain"); return 0; }
    // ui_hud_hide: handed over (not on the eyes), nothing shown - EXCEPT while the HUD component HudChallengeBrief is active (trace
    // docs/traces/20261126_1215_challengebrief_component.md): the quarantine zone's start card is that component, drawn in this pass, and
    // the game holds the player until it is accepted - hidden, a tester stood stuck in 3 of 3 zones (RUN DZ2: Jay "That's exactly the
    // issue"). COST: only while ui_hud_hide hides and no wheel shows - ~5 guarded reads per submit (uipin::ChallengeBriefActive).
    if (UiHudHide && !wheelShow) {
        int briefId = -1;
        const int brief = uipin::ChallengeBriefActive(&briefId);
        HudBriefEdge(brief, briefId);
        if (brief != 1) { ++g_hHidden; HudPeriodic(); return 0; }
    }

    XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    uint32_t index = 0;
    if (XR_FAILED(xrAcquireSwapchainImage_(g_sc, &ai, &index)) || index >= g_images.size()) { ++g_hXrFail; HudPeriodic(); return 0; }
    XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wi.timeout = XR_INFINITE_DURATION;
    const bool waited = XR_SUCCEEDED(xrWaitSwapchainImage_(g_sc, &wi));
    int rc = 7, rc1 = 7;
    if (waited) {
        rc = g_hudCopy(g_images[index].texture, 0, r.x, r.y, r.w, r.h, 0);
        rc1 = g_hudCopy(g_images[index].texture, 1, r1.x, r1.y, r1.w, r1.h, r.w);
    }
    XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    xrReleaseSwapchainImage_(g_sc, &ri);
    if (!waited) { ++g_hXrFail; HudPeriodic(); return 0; }
    ++g_hCopyRc[(rc >= 0 && rc < 7) ? rc : 7];
    if (rc == 0 && rc1 != 0) { rc = rc1; }
    if (rc != 0) {
        if (!g_hudNoPassMs) { g_hudNoPassMs = now; }
        ++g_hNoPass;
        HudPeriodic();
        return 0;
    }

    // The latch (uipanel_logic.h HudRelatch): gone = the longer of "HUD screen not showing" and "no finished pass".
    unsigned long long gone = g_hudLostMs ? now - g_hudLostMs : 0;
    if (g_hudNoPassMs && now - g_hudNoPassMs > gone) { gone = now - g_hudNoPassMs; }
    const float place[3] = { UiHudDistanceM, UiHudHeightM, UiHudXM };
    if (!wheelOnly && HudRelatch(g_hudLatched, g_hudRecentre, gone, false, g_hudPlaced, place)) {   // panel off: no HUD placement
        if (!viewsValid) { ++g_hNoViews; HudPeriodic(); return 0; }
        const float head[3] = { (eyes[0].position.x + eyes[1].position.x) * 0.5f, (eyes[0].position.y + eyes[1].position.y) * 0.5f,
                                (eyes[0].position.z + eyes[1].position.z) * 0.5f };
        // latched in the GAME's frame: from the orientation the image is rendered at, not the pitched one it is declared at
        const float hq[4] = { rendered0.x, rendered0.y, rendered0.z, rendered0.w };
        float nPos[3], nQuat[4];
        if (!LatchSide(head, hq, UiHudDistanceM, UiHudHeightM, UiHudXM, nPos, nQuat)) { ++g_hNoViews; HudPeriodic(); return 0; }
        const bool slider = g_hudLatched && !g_hudRecentre && gone <= 1000ull;
        const char* reason = !g_hudLatched ? (g_hLatches == 0 ? "the first HUD pass of the session, or ui_hud_panel just ticked"
                                                             : "ui_hud_panel ticked again")
                           : g_hudRecentre ? (g_hudRtReason ? "a runtime recentre (Meta hold / XR recentre event)"
                                                            : "the re-centre button")
                           : gone > 1000ull ? "the HUD was gone for over a second (a load)" : "a placement slider moved";
        for (int i = 0; i < 3; ++i) { g_hudPos[i] = nPos[i]; g_hudPlaced[i] = place[i]; }
        for (int i = 0; i < 4; ++i) { g_hudQuat[i] = nQuat[i]; }
        HudFollowReset(g_hudFollow, head);   // a real latch: a glide in progress is dropped, drift is measured from here
        g_hudLatched = true;
        g_hudRecentre = false;
        g_hudRtReason = false;
        ++g_hLatches;
        if (slider) { ++g_hSliderLatches; }
        if (!slider || now >= g_hudSliderLog) {       // a dragged slider latches every frame: one line a second for those
            g_hudSliderLog = now + 1000;
            // [PROBE: HUDPANEL] gate=ui_hud_panel  site=HEADSET  log-only=no
            LOG("[HUDPANEL] LATCHED (#%ld, %s; HUD gone %llu ms before): head (%.3f %.3f %.3f) m, panel centre (%.3f %.3f %.3f) "
                "m, turned %.1f deg about the vertical | distance %.2f m, height %.2f m, sideways %.2f m, scale %.2f",
                g_hLatches, reason, gone, head[0], head[1], head[2], g_hudPos[0], g_hudPos[1], g_hudPos[2],
                2.0f * std::atan2(g_hudQuat[1], g_hudQuat[3]) * 57.29578f, static_cast<double>(UiHudDistanceM),
                static_cast<double>(UiHudHeightM), static_cast<double>(UiHudXM), static_cast<double>(UiHudScale));
        }
    }
    g_hudLostMs = 0;
    g_hudNoPassMs = 0;

    // LAZY FOLLOW (ui_hud_follow): from the same head and orientation the latch is taken from. It moves the latch itself
    // (g_hudPos / g_hudQuat), so the roll fix below and the quads are built exactly as for any latch.
    // A physical crouch / stand (PhysCrouchEdges changed) is followed like a turn: the head's height alone is never drift, so
    // without it the panel stayed at standing height through a crouch (Jay 2026-10-27, option A).
    // COST: one FlatForward + atan2 + sqrt + one interlocked read per submit; while gliding one LatchSide more. No engine call, no lock.
    if (UiHudFollow && g_hudLatched && viewsValid) {
        const float head[3] = { (eyes[0].position.x + eyes[1].position.x) * 0.5f, (eyes[0].position.y + eyes[1].position.y) * 0.5f,
                                (eyes[0].position.z + eyes[1].position.z) * 0.5f };
        const long edges = PhysCrouchEdges();
        if (edges != g_hCrouchEdges) {
            g_hCrouchEdges = edges;
            g_hudFollow.pending = true;
        }
        const bool crouchPending = g_hudFollow.pending;
        const float hq[4] = { rendered0.x, rendered0.y, rendered0.z, rendered0.w };
        const FollowCfg cfg{ UiHudFollowDeg, static_cast<unsigned long long>(UiHudFollowDelayMs > 0 ? UiHudFollowDelayMs : 0),
                             static_cast<unsigned long long>(UiHudFollowMs > 0 ? UiHudFollowMs : 0), UiHudFollowDriftM,
                             UiHudDistanceM, UiHudHeightM, UiHudXM };
        const int prevPhase = g_hudFollow.phase;
        const unsigned long long armedAt = g_hudFollow.armedAt;
        const float yawBefore = 2.0f * std::atan2(g_hudQuat[1], g_hudQuat[3]) * 57.29578f;
        const float ddx = head[0] - g_hudFollow.latchHead[0], ddz = head[2] - g_hudFollow.latchHead[2];
        const FollowResult fr = HudFollowStep(g_hudFollow, head, hq, now, cfg, g_hudPos, g_hudQuat);
        if (prevPhase != 2 && (fr == kFollowGliding || fr == kFollowSettled)) {   // a glide began this submit
            g_hFollowFrom = yawBefore;
            g_hFollowDrift = std::sqrt(ddx * ddx + ddz * ddz);
            g_hFollowWaited = now - armedAt;
            g_hFollowAt = now;
            g_hFollowCrouch = crouchPending;
            if (crouchPending) { ++g_hCrouchFollows; }
        }
        if (fr == kFollowSettled && g_hFollowLines < 200) {
            ++g_hFollowLines;
            // [PROBE: HUDPANEL] gate=ui_hud_panel  site=HEADSET  log-only=no
            LOG("[HUDPANEL] FOLLOW #%ld (%s): panel facing %.1f -> %.1f deg over %llu ms, after %llu ms turned / moved away "
                "(head %.2f m along the floor from the last placement), panel centre now %.3f m up | ui_hud_follow_deg %.0f, "
                "delay %d ms, glide %d ms, drift %.2f m%s",
                g_hudFollow.follows, g_hFollowCrouch ? "a physical crouch / stand" : "a turn or a walk",
                static_cast<double>(g_hFollowFrom),
                static_cast<double>(2.0f * std::atan2(g_hudQuat[1], g_hudQuat[3]) * 57.29578f), now - g_hFollowAt,
                g_hFollowWaited, static_cast<double>(g_hFollowDrift), static_cast<double>(g_hudPos[1]),
                static_cast<double>(UiHudFollowDeg),
                UiHudFollowDelayMs, UiHudFollowMs, static_cast<double>(UiHudFollowDriftM),
                g_hFollowLines == 200 ? "  (line cap reached - further follows are counted in the 5 s line only)" : "");
        }
    }

    // The wheel's own latch: taken at the first submit of each opening, in front of the head as the HUD's is (same height rule).
    float* basePos = g_hudPos;
    float* baseQuat = g_hudQuat;
    float dist = UiHudDistanceM;
    if (wheelShow) {
        if (!g_wheelLatched && viewsValid) {
            const float head[3] = { (eyes[0].position.x + eyes[1].position.x) * 0.5f, (eyes[0].position.y + eyes[1].position.y) * 0.5f,
                                    (eyes[0].position.z + eyes[1].position.z) * 0.5f };
            const float hq[4] = { rendered0.x, rendered0.y, rendered0.z, rendered0.w };
            g_wheelLatched = LatchSide(head, hq, UiWheelDistanceM, UiHudHeightM, 0.0f, g_wheelPos, g_wheelQuat);
            ++g_hWheelShows;
            if (g_hWheelLines < 200) {
                ++g_hWheelLines;
                // [PROBE: HUDPANEL] gate=ui_hud_panel  site=HEADSET  log-only=no
                LOG("[HUDPANEL] WHEEL #%ld: only the wheel shows (the HUD hidden), %s at %.2f m | wheel rect %d,%d %dx%d of %dx%d "
                    "(eye 1: %d,%d) | %s",
                    g_hWheelShows, g_wheelLatched ? "latched"
                        : wheelOnly ? "*** NOT LATCHED (no heading) - not shown this submit, tried again ***"
                                    : "*** NOT LATCHED (no heading) - on the HUD's placement ***",
                    static_cast<double>(UiWheelDistanceM), r.x, r.y, r.w, r.h, W, H, r1.x, r1.y,
                    wheelOnly ? "ui_hud_panel OFF: the wheel alone, the HUD on the eyes after" : "ui_hud_panel on");
            }
        }
        if (wheelOnly && !g_wheelLatched) { ++g_hNoViews; return 0; }   // panel off: there is no HUD placement to fall back on
        if (g_wheelLatched) {
            basePos = g_wheelPos;
            baseQuat = g_wheelQuat;
            dist = UiWheelDistanceM;
        }
    } else if (g_wheelLatched) {
        g_wheelLatched = false;   // closed: the HUD is back on its own latch
    }
    // Per eye (trace docs/traces/20261124_1345_panel_eye_offcentre.md): as the menu panel - each eye's own size and crop-centre
    // direction (EyeQuadOffset); 0 / 0 and identical sizes for a symmetric frustum (as before). `r` is the whole image, or the
    // wheel's crop while a wheel shows; eye 1 uses its own box r1 (same size; trace 20261124_1612).
    const XrFovf* fovE[2] = { &fov0, &fov1 };
    float widthE[2] = {}, heightE[2] = {}, dxE[2] = {}, dyE[2] = {};
    for (int e = 0; e < 2; ++e) {
        const float tl = std::tan(fovE[e]->angleLeft), tr = std::tan(fovE[e]->angleRight);
        const float td = std::tan(fovE[e]->angleDown), tu = std::tan(fovE[e]->angleUp);
        const Rect& re = e == 0 ? r : r1;
        SizeM(re, W, H, tl, tr, td, tu, dist, UiHudScale, &widthE[e], &heightE[e]);
        EyeQuadOffset(re, W, H, tl, tr, td, tu, dist, UiHudScale, &dxE[e], &dyE[e]);
    }
    const float width = widthE[0], height = heightE[0];
    if (!g_hudSaidFirst) {
        g_hudSaidFirst = true;
        // [PROBE: HUDPANEL] gate=ui_hud_panel  site=HEADSET  log-only=no
        LOG("[HUDPANEL] FIRST HUD panel submitted (logged unconditionally): each eye's whole pass %dx%d on its own quad (eye 0 "
            "-> LEFT, eye 1 -> RIGHT), one room pose | eye 0 declared field of view tangents L %.4f R %.4f D %.4f U %.4f | "
            "panel %.3f x %.3f m | per-eye quad shift (right, up) eye 0 %+.4f %+.4f m, eye 1 %+.4f %+.4f m (0 = a symmetric "
            "frustum); eye 1 panel %.3f x %.3f m", W, H, std::tan(fov0.angleLeft), std::tan(fov0.angleRight),
            std::tan(fov0.angleDown), std::tan(fov0.angleUp), width, height, dxE[0], dyE[0], dxE[1], dyE[1], widthE[1], heightE[1]);
    }
    // The declared pitch, as for the menu panel (INV-534 (3)): the latched pose is turned about the head by the difference
    // between the rendered and the declared eye orientation every submit. In play that difference is still the pitch alone
    // (ovr2xr.cpp: menuRendered0 is taken directly before PitchPose - SPEC-UI-HUD-PANEL §10 U3).
    float sPos[3] = { basePos[0], basePos[1], basePos[2] }, sQuat[4] = { baseQuat[0], baseQuat[1], baseQuat[2], baseQuat[3] };
    if (viewsValid) {
        const float pivot[3] = { (eyes[0].position.x + eyes[1].position.x) * 0.5f, (eyes[0].position.y + eyes[1].position.y) * 0.5f,
                                 (eyes[0].position.z + eyes[1].position.z) * 0.5f };
        const float qR[4] = { rendered0.x, rendered0.y, rendered0.z, rendered0.w };
        const float qD[4] = { eyes[0].orientation.x, eyes[0].orientation.y, eyes[0].orientation.z, eyes[0].orientation.w };
        Redeclare(basePos, baseQuat, pivot, qR, qD, sPos, sQuat);
        const float n = std::sqrt(sQuat[0] * sQuat[0] + sQuat[1] * sQuat[1] + sQuat[2] * sQuat[2] + sQuat[3] * sQuat[3]);
        if (n > 0.5f) { for (float& c : sQuat) { c /= n; } }
        const float d = std::fabs(qR[0] * qD[0] + qR[1] * qD[1] + qR[2] * qD[2] + qR[3] * qD[3]);
        g_hDeclDeg = 2.0f * std::acos(d > 1.0f ? 1.0f : d) * 57.29578f;
        if (g_hDeclDeg > g_hDeclMaxDeg) { g_hDeclMaxDeg = g_hDeclDeg; }
    } else {
        ++g_hNoRedecl;
    }
    float hAx[3], hAy[3];
    {
        const float xAxis[3] = { 1.0f, 0.0f, 0.0f }, yAxis[3] = { 0.0f, 1.0f, 0.0f };
        Rotate(sQuat, xAxis, hAx);   // the panel's own right / up as submitted (after Redeclare)
        Rotate(sQuat, yAxis, hAy);
    }
    for (int e = 0; e < 2; ++e) {
        XrCompositionLayerQuad& q = out[e];
        q = XrCompositionLayerQuad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
        q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;   // the pass is premultiplied (hudlayer's own lay-on blend)
        q.space = appSpace;                                                   // fixed in the room
        q.eyeVisibility = e == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;   // eye 0 = the left eye (INV-386)
        q.subImage.swapchain = g_sc;
        q.subImage.imageArrayIndex = 0;
        q.subImage.imageRect.offset = { e * r.w, 0 };
        q.subImage.imageRect.extent = { r.w, r.h };
        q.pose.orientation = { sQuat[0], sQuat[1], sQuat[2], sQuat[3] };
        q.pose.position = { sPos[0] + hAx[0] * dxE[e] + hAy[0] * dyE[e], sPos[1] + hAx[1] * dxE[e] + hAy[1] * dyE[e],
                            sPos[2] + hAx[2] * dxE[e] + hAy[2] * dyE[e] };
        q.size = { widthE[e], heightE[e] };
    }
    // Selection wheels (spec D12): publish the wheel's centre and the panel's axes. The quad shows the crop r of the eye image;
    // the wheel sits at the UI canvas centre (CanvasCentre: the projection axis of eye 0's game frustum - (W / 2, H / 2) only for a
    // symmetric frustum; UI headset sweep, trace 20261113_2125). Frustum not known = the frame is not republished this submit (the
    // 250 ms freshness of WheelFrame then answers false), never a guessed centre. Quad axes = sQuat applied to x right, y up, z toward
    // the viewer. COST per submit: three quaternion rotations, a few flops, one exclusive SRW lock.
    EyeTan wt{};
    float wcx = 0.0f, wcy = 0.0f;
    if (GameEyeTan(0, &wt) && CanvasCentre(W, H, wt, &wcx, &wcy)) {
        auto rot = [&](float vx, float vy, float vz, float o[3]) {
            const float x = sQuat[0], y = sQuat[1], z = sQuat[2], w = sQuat[3];
            const float tx = 2.0f * (y * vz - z * vy), ty = 2.0f * (z * vx - x * vz), tz = 2.0f * (x * vy - y * vx);
            o[0] = vx + w * tx + (y * tz - z * ty);
            o[1] = vy + w * ty + (z * tx - x * tz);
            o[2] = vz + w * tz + (x * ty - y * tx);
        };
        float ax[3], ay[3], az[3];
        rot(1.0f, 0.0f, 0.0f, ax);
        rot(0.0f, 1.0f, 0.0f, ay);
        rot(0.0f, 0.0f, 1.0f, az);
        const float u = r.w > 0 ? ((wcx - r.x) / r.w - 0.5f) * width : 0.0f;    // image x grows to the right
        const float v = r.h > 0 ? (0.5f - (wcy - r.y) / r.h) * height : 0.0f;   // image y grows downward
        AcquireSRWLockExclusive(&g_wfLock);
        for (int i = 0; i < 3; ++i) {
            g_wfC[i] = sPos[i] + dxE[0] * ax[i] + dyE[0] * ay[i] + u * ax[i] + v * ay[i];   // + eye 0's quad shift (0 when symmetric)
            g_wfR[i] = ax[i];
            g_wfU[i] = ay[i];
            g_wfN[i] = az[i];
        }
        // The quad's edges from the wheel centre (while a wheel shows, the quad IS the wheel's crop r): a hand pointing outside
        // them is off the wheel as drawn (Jay 2026-11-16, trace 20261116_1328_wheel_offwheel).
        g_wfE[0] = -0.5f * width - u;
        g_wfE[1] = 0.5f * width - u;
        g_wfE[2] = -0.5f * height - v;
        g_wfE[3] = 0.5f * height - v;
        g_wfAt = now;
        ReleaseSRWLockExclusive(&g_wfLock);
    }

    // Added by AngelWolf 2026-11-26: mirror the right-eye floating HUD / weapon wheel quad onto the desktop window
    if (g_device && index < g_images.size()) {
        ::dlvr::deskmirror::DrawQuadLayer(g_device, g_images[index].texture, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            out[1], eyes[1], fov1, 1);
    }
    ++g_hShown;
    HudPeriodic();
    return 2;
}

}  // namespace

int Render(XrSpace appSpace, bool viewsValid, const XrPosef* eyes, const XrQuaternionf& rendered0, const XrFovf& fov0,
           const XrFovf& fov1, XrCompositionLayerQuad* out, XrTime displayTime)
{
    // A runtime recentre is due: hand it to the button's path (kept through a menu or the panel being off, like the button).
    const long long due = g_hudRtDue.load();
    if (due != 0 && static_cast<long long>(displayTime) >= due) {
        g_hudRtDue.store(0);
        g_hudRecentre = true;
        g_hudRtReason = true;
    }
    int quads = RenderMenu(appSpace, viewsValid, eyes, rendered0, fov0, fov1, out);
    if (g_active) {
        // A menu owns the pass (also on a submit it could not show): the HUD panel stands down and KEEPS its latch.
        if (g_hudActive) {
            g_hudActive = false;
            // [PROBE: HUDPANEL] gate=ui_hud_panel  site=HEADSET  log-only=no
            LOG("[HUDPANEL] panel PAUSED - a menu owns the pass (the menu panel); latch kept");
        }
        if (g_hudPauseHoldMs) {
            // [PROBE: HUDPANEL] gate=ui_hud_panel  site=HEADSET  log-only=no
            LOG("[HUDPANEL] MENU-OPEN HOLD #%ld ended: the menu panel took the pass after %llu ms, %ld submits held",
                g_hPauseHolds, GetTickCount64() - g_hudPauseHoldMs, g_hPauseHeld);
            g_hudPauseHoldMs = 0;
        }
        g_hudLostMs = 0;
        g_hudNoPassMs = 0;
    } else {
        quads = RenderHud(appSpace, viewsValid, eyes, rendered0, fov0, fov1, out);
    }
    if (g_endOwed) {
        // A panel stopped this submit. Drop the bootstrap's handshake only if neither panel has the pass now.
        if (!g_active && !g_hudActive && g_end) { g_end(); }
        g_endOwed = false;
    }
    return quads;
}

bool WheelFrame(float centre[3], float right[3], float up[3], float normal[3], float extent[4])
{
    AcquireSRWLockShared(&g_wfLock);
    const ULONGLONG at = g_wfAt;
    for (int i = 0; i < 3; ++i) {
        centre[i] = g_wfC[i];
        right[i] = g_wfR[i];
        up[i] = g_wfU[i];
        normal[i] = g_wfN[i];
    }
    for (int i = 0; i < 4; ++i) { extent[i] = g_wfE[i]; }
    ReleaseSRWLockShared(&g_wfLock);
    return at != 0 && GetTickCount64() - at <= 250;
}

}  // namespace uipanel
}  // namespace dlvr
