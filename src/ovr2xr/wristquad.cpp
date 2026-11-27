// WRIST HUD STAGE 2 - see wristquad.h. Trace docs/traces/20261112_1810_wrist_stage2_quads.md.
#include "wristquad.h"

#include <cmath>
#include <cstring>
#include <vector>

#include <d3d11_1.h>   // ID3D11DeviceContext1::ClearView - the 3b outline box

#include "../common/log.h"
// Added by AngelWolf 2026-11-26
#include "deskmirror.h"    // DrawQuadLayer - mirror Stage 2 wrist quads + 3b placement box/beams
#include "aimhook.h"       // AimMirrorYaw - the arm anchor's room mapping (as looting.cpp HandWorld)
#include "boneblend.h"     // RigArmAnchorRead - the drawn forearm / hand (arm anchor)
#include "camhook.h"       // CamHookGetOpenXrToGameYaw / CamHookGetRenderedViewMatrix - the arm anchor's room mapping
#include "controllers.h"   // ControllerGripSpace / GetControllerGripPose / ControllerGripFlags
#include "headpose.h"      // GetHeadPose / NeckPivotReportedPos - the head when the declared eye poses are not valid this submit
#include "reload.h"        // ReloadWristFamily - the held gun's reload family (stage B)
#include <cstdio>
#include "settings.h"      // DlvrFilePath
#include "uipanel_logic.h" // MetresPerUiPx / FitStep - the UI headset sweep (trace 20261113_2125)
#include "wristprobe.h"    // UiScreenH - the running headset's UI-screen height

#define LOG(...) ::dlvr::LogTo("dlvr_ovr2xr.log", __VA_ARGS__)

namespace dlvr {

// ===== STAGE 3a SETTINGS (trace docs/traces/20261113_1040_wrist_stage3a_settings.md; Jay 2026-11-13 "Yes, build it"). Registered in
// ovr2xr.cpp kSettings, drawn on the menu's HUD tab (overlay.cpp DrawWristHud). Defaults = PRESET A below (stage 2b's values). =====
// [DEP: ui_wrist_hud] required-for=the wrist HUD (UiWristHud): the five HUD pieces leave the eyes and go on quads held on the hands. READ ONCE AT LAUNCH by both DLLs (layer: wristquad::OnThisLaunch from wristprobe::Start; bootstrap: the ui_wrist_hud line of ovr2xr.txt as text in wristroute::Init) - replaces the markers dlvr\wrist_route.txt + dlvr\wrist_hud.txt (Jay 2026-11-13 "Replace markers", "Next launch"). Needs the HUD layer (afw_enable=1) + dlvr\dlss_cbsnap.txt. Tier C, ships 0.  tree=INERT
bool  UiWristHud = false;
// [DEP: ui_wrist_preset] required-for=which placement the menu shows as chosen (UiWristPreset): 0 A, 1 B (the split - spec §6's "C", renamed by Jay 11-13), 2 Custom. Choosing A / B copies that preset's 40 values (wristquad::ApplyPreset); any slider / hand edit turns it to Custom. Render never reads it. Tier D.  tree=INERT
int   UiWristPreset = 0;
// [DEP: ui_wrist_fade_start_deg] required-for=the facing fade's start (UiWristFadeStartDeg): a piece is fully shown while its front is within this angle of the direction to the head. Needs ui_wrist_hud. Tier D (spec §7 default 50).  tree=INERT
float UiWristFadeStartDeg = 50.0f;
// [DEP: ui_wrist_fade_end_deg] required-for=the facing fade's end (UiWristFadeEndDeg): gone from this angle; smooth between start and end (an end at or below the start = start + 1). Needs ui_wrist_hud. Tier D (spec §7 default 75).  tree=INERT
float UiWristFadeEndDeg = 75.0f;
// [DEP: ui_wrist_size] required-for=every piece's size (UiWristSize): x 0.000115 m per UI px (stage 2's size - the minimap ~9 cm at 1.0). Needs ui_wrist_hud. Tier D.  tree=INERT
float UiWristSize = 1.0f;
// [DEP: UiWristHand] required-for=ui_wrist_minimap_hand / ui_wrist_health_hand / ui_wrist_quests_hand / ui_wrist_weapon_hand / ui_wrist_quick_hand: which hand holds the piece, 0 left / 1 right / 2 off (hidden - the router still takes it off the eyes, no quad; spec Q4 "Hidden"). Needs ui_wrist_hud. Tier D.  tree=INERT
// THE BUILT-IN STARTING VALUES = PRESET A = JAY's TUNING (2026-11-15, Jay: "My current tuning values for the wrist HUD ... are to be
// SET as the A preset"; his dlvr\ovr2xr.txt 07efa60c; trace docs/traces/20261115_2020_wrist_preset_a_jay_tuning.md). Keep these
// arrays and kPresets[0] equal - a new player starts on A.
// EXCEPT THE QUEST LIST (2026-11-23, Jay: "Yes please" + "Ship it ON (Left)"; trace docs/traces/20261123_1020_wrist_quests_default_tester.md):
// a tester's left-forearm placement from a 1.065 report (values as the menu sliders print them), ON - Jay's own disk keeps his.
int   UiWristHand[5] = { 0, 0, 0, 1, 0 };
// [DEP: UiWristX] required-for=ui_wrist_<piece>_x: the piece's centre along the anchor's X, metres (grip: +X = back of the RIGHT hand / palm of the LEFT, RUN WS2). Needs ui_wrist_hud. Tier D.  tree=INERT
float UiWristX[5] = { 0.0671082f, 0.156311f, 0.14f, 0.260864f, 0.234619f };
// [DEP: UiWristY] required-for=ui_wrist_<piece>_y: the piece's centre along the anchor's +Y (grip: up), metres. Needs ui_wrist_hud. Tier D.  tree=INERT
float UiWristY[5] = { 0.00325012f, -0.0472336f, 0.03f, 0.0342255f, -0.0438843f };
// [DEP: UiWristZ] required-for=ui_wrist_<piece>_z: the piece's centre along the anchor's +Z (grip: back toward the forearm), metres. Needs ui_wrist_hud. Tier D.  tree=INERT
float UiWristZ[5] = { 0.0245361f, 0.0157776f, 0.05f, -0.00646973f, -0.0161743f };
// [DEP: UiWristPitch] required-for=ui_wrist_<piece>_pitch: the quad's tilt about its own X, degrees (after roll, before yaw). Needs ui_wrist_hud. Tier D.  tree=INERT
float UiWristPitch[5] = { -5.39321f, 71.1794f, -70.97f, -77.6069f, 70.6539f };
// [DEP: UiWristYaw] required-for=ui_wrist_<piece>_yaw: the quad's turn about the anchor's +Y, degrees (+90 = its front faces +X). Needs ui_wrist_hud. Tier D.  tree=INERT
float UiWristYaw[5] = { -2.37228f, 4.925f, 13.86f, 164.573f, -136.234f };
// [DEP: UiWristRoll] required-for=ui_wrist_<piece>_roll: the picture's spin in its own plane, degrees (fixes "upside down", RUN WS2). Needs ui_wrist_hud. Tier D.  tree=INERT
// ROLL 180 (2026-11-13, RUN S3A1 - Jay: "they're inverted by default"; trace docs/traces/20261113_1052_wrist_roll180.md) was the old
// grip-frame A's value; Jay's tuning (2026-11-15) has its own roll per piece. Preset B still carries 180.
float UiWristRoll[5] = { -12.6019f, 0.267904f, -16.93f, 40.1018f, 132.933f };
// [DEP: UiWristScale] required-for=ui_wrist_<piece>_scale: this piece's size x ui_wrist_size. Needs ui_wrist_hud. Tier D.  tree=INERT
float UiWristScale[5] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
// [DEP: UiWristAnchor] required-for=ui_wrist_<piece>_anchor: what the piece's x / y / z / pitch / yaw / roll are relative to - 0 the controller's grip, 1 the DRAWN forearm, 2 the DRAWN hand (the arm solve's own values, boneblend RigArmAnchorRead; trace docs/traces/20261113_1145_wrist_arm_anchor.md). Set by a place (the nearest bone, Jay 2026-11-13 "Nearest bone"); a preset / Reset sets the preset's own anchor (A = Jay's: minimap hand, health / weapon / quick forearm; quests forearm = the tester's, 11-23; B = grip). Needs ui_wrist_hud. Tier D.  tree=INERT
int   UiWristAnchor[5] = { 2, 1, 1, 1, 1 };

namespace wristquad {

namespace {

typedef int (*PFN_Info)(int, int*, float*);                       // DlvrWristPieceInfo(piece, box4, ui2)
typedef int (*PFN_Copy)(void*, int, int, int, int, int, float, int);   // DlvrWristPieceCopy(dst, piece, dx, dy, w, h, fade, step)
// STAGE A (trace docs/traces/20261113_1300_wrist_ingame_stageA.md): DlvrWristDrawPieces(eyeTex, vpA, vpB, flags, sep, cam3, pieces, n)
// and DlvrWristArmBones(rows96, mask) - the arm pieces drawn in the game picture, and the drawn arm read back for the placement.
typedef int (*PFN_Draw)(void*, const int*, const int*, int, float, const float*, const void*, int);
typedef int (*PFN_Bones)(float*, unsigned*);
typedef int (*PFN_Cam)(float*, int*);   // FIX ROUND 2: DlvrWristCamera(vp16, eye) - the newest image's game camera
// = the bootstrap's wristarm::DrawPiece (dlssflat.cpp), byte for byte
struct DrawPiece { int piece; int bone; float M[12]; float halfW, halfH, fadeStart, fadeEnd; int box[4]; };
static_assert(sizeof(DrawPiece) == 88, "DrawPiece must match the bootstrap's record (dlssflat.cpp wristarm)");

constexpr int kP = 5;                       // wristleaf.h kPieces: 0 minimap, 1 health, 2 quests, 3 weapon, 4 quick item
const char* const kTag[kP] = { "minimap", "health", "quests", "weapon", "quick item" };
constexpr int kSlotW = 1024, kSlotH = 512;  // one slot per piece, stacked, + the 3b frame slot: the swapchain is 1024 x 3072
constexpr int kFrameSlot = kP;              // STAGE 3b: slot 5 = the placement box (its top border = the beams' white strip)
constexpr int kSlots = kP + 1;
// Size: metres per UI pixel at ui_wrist_size 1.0 = uipanel::MetresPerUiPx(the running headset's UI-screen height) - the minimap's
// 780 of 2880 UI px (RUN WS0, Jay's Quest 3) at ~9 cm, the same share at the same metres on any headset (Jay HARD RULE 2026-11-13,
// trace 20261113_2125). Was a Quest 3 constant 0.000115: on a tester's 1824-high UI screen every piece came out 0.63x.
float MetresPerUiPxNow() { return static_cast<float>(uipanel::MetresPerUiPx(static_cast<double>(wristprobe::UiScreenH()))); }

// THE PRESETS (spec §6). In the hand's GRIP frame (OpenXR: -Z forward along the grip, +Y up). +X points OUT OF THE PALM FACE on the
// left hand and from the palm toward the BACK on the right - MEASURED in RUN WS2 (stage 2's first signs put health on the left palm
// and the minimap on the right palm; Jay's screenshots, backups/20261112_wrist_ws2_run1). So the back of the LEFT hand is -X and the
// back of the RIGHT hand is +X. yaw = the quad's turn about grip +Y: +90 = its front (+Z) faces grip +X, -90 faces -X.
// A - after Mamfro's layout: STARTING VALUES picked 2026-11-12 (Jay: "I pick starting values"), signs swapped in stage 2b (INV-727).
// C - split (Jay 2026-11-12: "A/C are fine"): health + minimap on the left wrist, both weapon widgets on the right wrist, the quest list
//     OFF (Jay 2026-11-13: "Off (hidden)"; its position keeps A's so turning it back on puts it somewhere sensible). STARTING VALUES
//     picked 2026-11-13 (mine; Jay tunes them with the sliders): each wrist's pair 0.10 m apart along the forearm, as A's minimap /
//     quests.
// A (2026-11-15) = JAY's TUNING (Jay: "My current tuning values for the wrist HUD ... are to be SET as the A preset"), each piece in
//     its own anchor (the place he left it at: 2 the drawn hand, 1 the drawn forearm, 0 the grip). = the UiWrist* starting values.
//     Was: the grip-frame starting values after Mamfro's layout (minimap / quests on the back of the right hand, health on the left).
//     Quests row (2026-11-23): a tester's left-forearm placement, ON (Jay: "Ship it ON (Left)"; trace 20261123_1020) - was OFF at the grip.
struct Place { int hand; float pos[3]; float pitch, yaw, roll, scale; int anchor; };
const Place kPresets[2][kP] = {
    {   // A - Jay's tuning
        { 0, { 0.0671082f, 0.00325012f, 0.0245361f }, -5.39321f, -2.37228f, -12.6019f, 1.0f, 2 },   // minimap - left hand (drawn hand)
        { 0, { 0.156311f, -0.0472336f, 0.0157776f }, 71.1794f, 4.925f, 0.267904f, 1.0f, 1 },      // health - left forearm
        { 0, { 0.14f, 0.03f, 0.05f }, -70.97f, 13.86f, -16.93f, 1.0f, 1 },                         // quests - left forearm (tester)
        { 1, { 0.260864f, 0.0342255f, -0.00646973f }, -77.6069f, 164.573f, 40.1018f, 1.0f, 1 },    // weapon (primary) - right forearm
        { 0, { 0.234619f, -0.0438843f, -0.0161743f }, 70.6539f, -136.234f, 132.933f, 1.0f, 1 },    // quick item - left forearm
    },
    {   // B (the split; spec §6's "C" - Jay 11-13: "There's no point skipping a letter") - grip frame
        { 0, { -0.04f, 0.02f, 0.15f }, 0.0f, -90.0f, 180.0f, 1.0f, 0 },   // minimap - back of the left wrist, above health
        { 0, { -0.04f, 0.02f, 0.05f }, 0.0f, -90.0f, 180.0f, 1.0f, 0 },   // health - back of the left wrist
        { 2, {  0.04f, 0.02f, 0.13f }, 0.0f,  90.0f, 180.0f, 1.0f, 0 },   // quests - OFF
        { 1, {  0.04f, 0.02f, 0.05f }, 0.0f,  90.0f, 180.0f, 1.0f, 0 },   // weapon (primary) - back of the right wrist
        { 1, {  0.04f, 0.02f, 0.15f }, 0.0f,  90.0f, 180.0f, 1.0f, 0 },   // quick item (secondary) - back of the right wrist, above it
    },
};

ID3D11Device* g_device = nullptr;
XrSession g_session = XR_NULL_HANDLE;
bool g_on = false, g_saidOn = false;
XrSwapchain g_sc = XR_NULL_HANDLE;
std::vector<XrSwapchainImageD3D11KHR> g_images;
bool g_scFailed = false;
bool g_looked = false, g_saidNoExports = false;
PFN_Info g_info = nullptr;
PFN_Copy g_copy = nullptr;
PFN_Draw g_draw = nullptr;     // STAGE A
PFN_Bones g_bones = nullptr;   // STAGE A
PFN_Cam g_cam = nullptr;       // FIX ROUND 2 (trace docs/traces/20261113_1330_wrist_ingame_A2.md)
// The placement's camera pair, refreshed every submit: the game camera of the image just rendered (g_camVp, jitter stripped) and the
// pose + FOV that image was DECLARED with (what the compositor shows it with). g_camEye = which eye that is.
float g_camVp[16] = {};
bool g_camOk = false;
int g_camEye = -1;
XrPosef g_declPose[2] = {};
XrFovf g_declFov[2] = {};
bool g_declOk[2] = {};
bool g_saidFirst = false;
// 5 s window
ULONGLONG g_nextLog = 0;
long w_submits = 0, w_shown[kP] = {}, w_faded[kP] = {}, w_notPub[kP] = {}, w_untracked[kP] = {}, w_clipped[kP] = {}, w_off[kP] = {};
long w_copyFail[kP] = {}, w_noRoom = 0, w_xrFail = 0, w_noSpace = 0, w_rc[8] = {};
float w_angMin[kP], w_angMax[kP];
bool w_angSet[kP] = {};
long w_carried[kP] = {};   // STAGE 3b: submits the piece was being placed (floating or carried)
long w_onBone[kP] = {}, w_noBone[kP] = {};   // ARM ANCHOR: drawn on its bone / its bone (or the room map) not available

// ===== STAGE 3b - PLACE BY HAND, POINT-AND-GRAB (traces docs/traces/20261113_1108_wrist_3b_place.md, then
// 20261113_1122_wrist_3b_pointgrab.md - Jay after RUN P3B1: "hard to actually see the piece"). Submit thread only. =====
int g_placePiece = -1;               // the piece being placed, -1 none
ULONGLONG g_placeStartMs = 0;
constexpr float kPressOff = 0.4f, kPressOn = 0.6f;          // holster.cpp's place thresholds, for the trigger and the grip
constexpr float kMaxAxisM = 0.3f;                           // the x / y / z sliders' range
constexpr float kAppearM = 0.5f;                            // the piece appears this far ahead of the head
constexpr float kBeamM = 2.0f, kBeamW = 0.004f;             // a beam's length when it hits nothing, and its width
constexpr float kHitMarginM = 0.02f;                        // a beam within this of the piece's edge counts as on it
constexpr float kFrameMarginM = 0.01f;                      // the box is this much bigger than the piece on every side
int g_carrier = -1;                  // -1 floating in the room, 0 left / 1 right = carried by that controller's grip
XrPosef g_floatPose{};               // the piece's pose in appSpace while floating
XrPosef g_carryOff{};                // the piece's pose in the carrier's grip while carried
bool g_appearPending = false;        // Begin asked for the appear pose; set from the head at the first tick
bool g_gripArmed[2] = {}, g_trigArmed[2] = {};   // the button read < kPressOff since the last press (no press carried in)
float g_placeW = 0.06f, g_placeH = 0.03f;        // the placed piece's quad size, metres (Render; the hit test reads it)
bool g_beamOk[2] = {};                // this tick: the hand's aim is tracked
XrPosef g_aim[2] = {};                // this tick: each hand's aim pose (appSpace)
float g_beamLen[2] = { kBeamM, kBeamM };
bool g_hit[2] = {};                   // this tick: the hand's beam is on the piece
long g_refusals = 0;                  // places refused this placement (logged each)
bool g_placeGrabbed = false;          // grabbed at least once this placement - a floating piece's trigger confirm needs it (trace 1205)
int g_lastBox[kP][4] = {};           // the last published box / UI size per piece - the outline's size
float g_lastUi[kP][2] = {};
bool g_haveLast[kP] = {};
long g_placeLines = 0;               // [WRISTPLACE] lines so far (first one says it is unconditional)
ID3D11DeviceContext1* g_ctx1 = nullptr;              // ClearView for the outline (the device's immediate context)
std::vector<ID3D11RenderTargetView*> g_rtv;          // one per swapchain image, made on first use
bool g_outlineFailed = false;

void Rot(const XrQuaternionf& q, const float v[3], float o[3])
{
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    const float tx = 2.0f * (y * v[2] - z * v[1]), ty = 2.0f * (z * v[0] - x * v[2]), tz = 2.0f * (x * v[1] - y * v[0]);
    o[0] = v[0] + w * tx + (y * tz - z * ty);
    o[1] = v[1] + w * ty + (z * tx - x * tz);
    o[2] = v[2] + w * tz + (x * ty - y * tx);
}

XrQuaternionf Mul(const XrQuaternionf& a, const XrQuaternionf& b)
{
    return XrQuaternionf{ a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                          a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                          a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}

XrQuaternionf Axis(int axis, float deg)
{
    const float h = deg * 0.5f * 0.01745329f;
    const float s = std::sin(h);
    return XrQuaternionf{ axis == 0 ? s : 0.0f, axis == 1 ? s : 0.0f, axis == 2 ? s : 0.0f, std::cos(h) };
}

// The quad's orientation in the grip frame (STAGE 3a): yaw about grip +Y x pitch about X x roll about the quad's own +Z (its normal),
// so roll spins the picture in its plane and pitch tilts it. Pitch 0 + roll 0 = stage 2's Yaw(yaw) exactly.
XrQuaternionf PieceOrientation(int p)
{
    return Mul(Mul(Axis(1, UiWristYaw[p]), Axis(0, UiWristPitch[p])), Axis(2, UiWristRoll[p]));
}

bool Resolve()
{
    if (g_looked) { return g_info && g_copy; }
    const HMODULE b = GetModuleHandleA("xinput1_3.dll");
    if (!b) { return false; }
    g_looked = true;
    g_info = reinterpret_cast<PFN_Info>(GetProcAddress(b, "DlvrWristPieceInfo"));
    g_copy = reinterpret_cast<PFN_Copy>(GetProcAddress(b, "DlvrWristPieceCopy"));
    g_draw = reinterpret_cast<PFN_Draw>(GetProcAddress(b, "DlvrWristDrawPieces"));
    g_bones = reinterpret_cast<PFN_Bones>(GetProcAddress(b, "DlvrWristArmBones"));
    g_cam = reinterpret_cast<PFN_Cam>(GetProcAddress(b, "DlvrWristCamera"));
    // [PROBE: WRISTQUAD] gate=ui_wrist_hud (read at launch)  site=HEADSET  log-only=no
    LOG("[WRISTQUAD] stage A exports (logged unconditionally): DlvrWristDrawPieces %s, DlvrWristArmBones %s, DlvrWristCamera %s%s",
        g_draw ? "found" : "MISSING", g_bones ? "found" : "MISSING", g_cam ? "found" : "MISSING",
        (g_draw && g_bones) ? "" : " - pieces on the forearm / hand are NOT drawn (an older xinput1_3.dll)");
    if (!(g_info && g_copy) && !g_saidNoExports) {
        g_saidNoExports = true;
        // [PROBE: WRISTQUAD] gate=ui_wrist_hud (read at launch)  site=HEADSET  log-only=no
        LOG("[WRISTQUAD] *** REFUSED: the bootstrap exports DlvrWristPieceInfo %s / DlvrWristPieceCopy %s - an older xinput1_3.dll is "
            "deployed. Nothing goes on the hands this session. ***", g_info ? "found" : "MISSING", g_copy ? "found" : "MISSING");
    }
    return g_info && g_copy;
}

bool EnsureSwapchain()
{
    if (g_sc != XR_NULL_HANDLE) { return true; }
    if (g_scFailed || g_session == XR_NULL_HANDLE) { return false; }
    XrSwapchainCreateInfo ci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    ci.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;   // as the menu / HUD panels (uipanel.cpp EnsureSwapchain)
    ci.sampleCount = 1;
    ci.width = static_cast<uint32_t>(kSlotW);
    ci.height = static_cast<uint32_t>(kSlotH * kSlots);
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
    // [PROBE: WRISTQUAD] gate=ui_wrist_hud (read at launch)  site=HEADSET  log-only=no
    LOG("[WRISTQUAD] wrist swapchain %s: %dx%d R8G8B8A8_UNORM_SRGB (6 slots of %dx%d: 5 pieces + the placement box), %u image(s) | XrResult %d",
        ok ? "CREATED" : "*** REFUSED - nothing goes on the hands this session ***", kSlotW, kSlotH * kSlots, kSlotW, kSlotH, n,
        static_cast<int>(r));
    if (!ok) {
        if (g_sc != XR_NULL_HANDLE) { xrDestroySwapchain_(g_sc); g_sc = XR_NULL_HANDLE; }
        g_images.clear();
        g_scFailed = true;
        return false;
    }
    return true;
}

void Window()
{
    const ULONGLONG now = GetTickCount64();
    if (!g_nextLog) { g_nextLog = now + 5000; return; }
    if (now < g_nextLog) { return; }
    g_nextLog = now + 5000;
    char per[kP][320] = {};
    for (int p = 0; p < kP; ++p) {
        _snprintf_s(per[p], sizeof(per[p]), _TRUNCATE, "%s: shown %ld, faded out %ld, not published %ld, hand untracked %ld, hand OFF %ld, "
                    "carried %ld, in the game picture %ld, not drawn there %ld (anchor %d), stepped down %ld, copy refused %ld, angle %.0f..%.0f deg", kTag[p],
                    w_shown[p], w_faded[p], w_notPub[p], w_untracked[p], w_off[p], w_carried[p], w_onBone[p], w_noBone[p],
                    UiWristAnchor[p], w_clipped[p], w_copyFail[p],
                    static_cast<double>(w_angSet[p] ? w_angMin[p] : 0.0f), static_cast<double>(w_angSet[p] ? w_angMax[p] : 0.0f));
    }
    // [PROBE: WRISTQUAD] gate=ui_wrist_hud (read at launch)  site=HEADSET  log-only=yes
    LOG("[WRISTQUAD] 5 s: submits %ld | %s | %s | %s | %s | %s | layer list full %ld, grip space missing %ld, swapchain acquire / wait "
        "failed %ld | copy return codes 0:%ld 1:%ld 2:%ld 3:%ld 4:%ld 5:%ld 6:%ld other:%ld", w_submits, per[0], per[1], per[2], per[3],
        per[4], w_noRoom, w_noSpace, w_xrFail, w_rc[0], w_rc[1], w_rc[2], w_rc[3], w_rc[4], w_rc[5], w_rc[6], w_rc[7]);
    w_submits = w_noRoom = w_xrFail = w_noSpace = 0;
    for (int p = 0; p < kP; ++p) {
        w_shown[p] = w_faded[p] = w_notPub[p] = w_untracked[p] = w_clipped[p] = w_copyFail[p] = w_off[p] = w_carried[p] = 0;
        w_onBone[p] = w_noBone[p] = 0;
        w_angSet[p] = false;
    }
    for (long& c : w_rc) { c = 0; }
}

// STAGE 3b: the outline box for a carried piece the game is not drawing - its slot cleared to transparent, then a 4 px white frame
// (premultiplied alpha 1). ClearView through a cached RTV per swapchain image. False = it could not draw (named once).
bool DrawOutline(uint32_t index, int slotY, int w, int h)
{
    if (g_outlineFailed || !g_device || index >= g_images.size() || w < 8 || h < 8) { return false; }
    const char* why = nullptr;
    if (!g_ctx1) {
        ID3D11DeviceContext* c = nullptr;
        g_device->GetImmediateContext(&c);
        if (c) {
            if (FAILED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&g_ctx1)))) { g_ctx1 = nullptr; }
            c->Release();
        }
        if (!g_ctx1) { why = "no ID3D11DeviceContext1"; }
    }
    if (!why) {
        if (g_rtv.size() != g_images.size()) { g_rtv.assign(g_images.size(), nullptr); }
        if (!g_rtv[index]) {
            D3D11_RENDER_TARGET_VIEW_DESC d{};
            d.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;   // the swapchain's format (EnsureSwapchain)
            d.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            const HRESULT hr = g_device->CreateRenderTargetView(g_images[index].texture, &d, &g_rtv[index]);
            if (FAILED(hr)) { g_rtv[index] = nullptr; why = "CreateRenderTargetView failed"; }
        }
    }
    if (why) {
        g_outlineFailed = true;
        // [PROBE: WRISTPLACE] gate=ui_wrist_hud (read at launch) + a placement running  site=HEADSET  log-only=no
        LOG("[WRISTPLACE] *** OUTLINE REFUSED: %s - a carried piece the game is not drawing shows nothing this session ***", why);
        return false;
    }
    const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const D3D11_RECT r{ 0, slotY, w, slotY + h };
    g_ctx1->ClearView(g_rtv[index], clear, &r, 1);
    const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    constexpr int t = 4;
    const D3D11_RECT b[4] = { { 0, slotY, w, slotY + t }, { 0, slotY + h - t, w, slotY + h }, { 0, slotY, t, slotY + h },
                              { w - t, slotY, w, slotY + h } };
    g_ctx1->ClearView(g_rtv[index], white, b, 4);
    return true;
}

void PlaceEnd()
{
    g_placePiece = -1;
    g_carrier = -1;
    g_appearPending = false;
    g_beamOk[0] = g_beamOk[1] = g_hit[0] = g_hit[1] = false;
}

// ---- 3b point-and-grab maths (all poses in appSpace unless named) ----
XrQuaternionf Conj(const XrQuaternionf& q) { return XrQuaternionf{ -q.x, -q.y, -q.z, q.w }; }

XrPosef PoseMul(const XrPosef& a, const XrPosef& b)   // b given in a's frame -> b in a's parent frame
{
    XrPosef o{};
    o.orientation = Mul(a.orientation, b.orientation);
    const float v[3] = { b.position.x, b.position.y, b.position.z };
    float r[3];
    Rot(a.orientation, v, r);
    o.position = { a.position.x + r[0], a.position.y + r[1], a.position.z + r[2] };
    return o;
}

XrPosef PoseInv(const XrPosef& a)
{
    XrPosef o{};
    o.orientation = Conj(a.orientation);
    const float v[3] = { -a.position.x, -a.position.y, -a.position.z };
    float r[3];
    Rot(o.orientation, v, r);
    o.position = { r[0], r[1], r[2] };
    return o;
}

bool GripTracked(Hand h, XrPosef& out)
{
    const unsigned long long need = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    const long f = ControllerGripFlags(h);
    return f >= 0 && (static_cast<unsigned long long>(f) & need) == need && GetControllerGripPose(h, out);
}

// The quaternion of a rotation whose columns are X, Y, Z (orthonormal).
XrQuaternionf QuatFromBasis(const float X[3], const float Y[3], const float Z[3])
{
    const float m00 = X[0], m11 = Y[1], m22 = Z[2], tr = m00 + m11 + m22;
    XrQuaternionf q{};
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        q.w = 0.25f * s; q.x = (Y[2] - Z[1]) / s; q.y = (Z[0] - X[2]) / s; q.z = (X[1] - Y[0]) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (Y[2] - Z[1]) / s; q.x = 0.25f * s; q.y = (Y[0] + X[1]) / s; q.z = (Z[0] + X[2]) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (Z[0] - X[2]) / s; q.x = (Y[0] + X[1]) / s; q.y = 0.25f * s; q.z = (Z[1] + Y[2]) / s;
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (X[1] - Y[0]) / s; q.x = (Z[0] + X[2]) / s; q.y = (Z[1] + Y[2]) / s; q.z = 0.25f * s;
    }
    return q;
}

// Decompose as PieceOrientation composes: R = Ry(yaw) Rx(pitch) Rz(roll) -> r12 = -sin(pitch), yaw = atan2(r02, r22),
// roll = atan2(r10, r11). Degrees.
void YawPitchRoll(const XrQuaternionf& q, float& pitch, float& yaw, float& roll)
{
    const float r02 = 2.0f * (q.x * q.z + q.w * q.y), r22 = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    const float r10 = 2.0f * (q.x * q.y + q.w * q.z), r11 = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
    float r12 = 2.0f * (q.y * q.z - q.w * q.x);
    r12 = r12 > 1.0f ? 1.0f : r12 < -1.0f ? -1.0f : r12;
    pitch = std::asin(-r12) * 57.29578f;
    yaw = std::atan2(r02, r22) * 57.29578f;
    roll = std::atan2(r10, r11) * 57.29578f;
}

// The placed piece's pose now (appSpace): carried = the carrier's grip x the grab offset; floating = where it was left.
bool PiecePoseNow(XrPosef& out)
{
    if (g_appearPending) { return false; }
    if (g_carrier == 0 || g_carrier == 1) {
        XrPosef grip{};
        if (!GripTracked(g_carrier == 0 ? kLeftHand : kRightHand, grip)) { return false; }
        out = PoseMul(grip, g_carryOff);
        return true;
    }
    out = g_floatPose;
    return true;
}

// A ray (appSpace origin o, direction d, unit) against the piece's rectangle (its local XY, size g_placeW x g_placeH + the margin).
// Returns the distance along the ray, or -1.
float RayHit(const XrPosef& piece, const float o[3], const float d[3])
{
    const XrQuaternionf ci = Conj(piece.orientation);
    const float rel[3] = { o[0] - piece.position.x, o[1] - piece.position.y, o[2] - piece.position.z };
    float lo[3], ld[3];
    Rot(ci, rel, lo);
    Rot(ci, d, ld);
    if (std::fabs(ld[2]) < 1e-4f) { return -1.0f; }
    const float t = -lo[2] / ld[2];
    if (t <= 0.0f || t > kBeamM) { return -1.0f; }
    const float x = lo[0] + t * ld[0], y = lo[1] + t * ld[1];
    if (std::fabs(x) > g_placeW * 0.5f + kHitMarginM || std::fabs(y) > g_placeH * 0.5f + kHitMarginM) { return -1.0f; }
    return t;
}

// ---- ARM ANCHOR (trace docs/traces/20261113_1145_wrist_arm_anchor.md) ----
// Game world -> room: the inverse of looting.cpp HandWorld (camera + Ry(rel) (grip - reported head); rel negated under AimMirrorYaw).
struct RoomMap { bool ok; float cam[3]; float hr[3]; float c, s; XrQuaternionf inv; };

RoomMap MakeRoomMap()
{
    RoomMap m{};
    float rel = 0.0f, v[12] = {};
    unsigned age = 0;
    XrPosef head{};
    if (!CamHookGetOpenXrToGameYaw(rel) || !CamHookGetRenderedViewMatrix(v, &age) || !GetHeadPose(head)) { return m; }
    if (AimMirrorYaw) { rel = -rel; }
    const XrVector3f hr = NeckPivotReportedPos(head);
    m.cam[0] = v[3]; m.cam[1] = v[7]; m.cam[2] = v[11];
    m.hr[0] = hr.x; m.hr[1] = hr.y; m.hr[2] = hr.z;
    m.c = std::cos(rel);
    m.s = std::sin(rel);
    m.inv = Conj(Axis(1, rel * 57.29578f));
    m.ok = true;
    return m;
}

void WorldToRoom(const RoomMap& m, const float w[3], float o[3])
{
    const float dx = w[0] - m.cam[0], dy = w[1] - m.cam[1], dz = w[2] - m.cam[2];
    o[0] = m.hr[0] + m.c * dx - m.s * dz;   // inverse of x' = c x + s z, z' = -s x + c z
    o[1] = m.hr[1] + dy;
    o[2] = m.hr[2] + m.s * dx + m.c * dz;
}

// A row-major 3x3 with the basis in its COLUMNS (game world) -> a room quaternion (columns re-orthonormalised: Gram-Schmidt X, Y).
XrQuaternionf BasisToRoom(const RoomMap& m, const float b[9])
{
    float X[3] = { b[0], b[3], b[6] }, Y[3] = { b[1], b[4], b[7] };
    const float xl = std::sqrt(X[0] * X[0] + X[1] * X[1] + X[2] * X[2]);
    if (xl > 1e-6f) { X[0] /= xl; X[1] /= xl; X[2] /= xl; }
    const float d = X[0] * Y[0] + X[1] * Y[1] + X[2] * Y[2];
    Y[0] -= d * X[0]; Y[1] -= d * X[1]; Y[2] -= d * X[2];
    const float yl = std::sqrt(Y[0] * Y[0] + Y[1] * Y[1] + Y[2] * Y[2]);
    if (yl > 1e-6f) { Y[0] /= yl; Y[1] /= yl; Y[2] /= yl; }
    const float Z[3] = { X[1] * Y[2] - X[2] * Y[1], X[2] * Y[0] - X[0] * Y[2], X[0] * Y[1] - X[1] * Y[0] };
    return Mul(m.inv, QuatFromBasis(X, Y, Z));
}

// ---- STAGE A: THE DRAWN ARM (trace docs/traces/20261113_1300_wrist_ingame_stageA.md) ----
// Bones 0..7 = L forearm, foretwist1, foretwist, hand, R the same (boneblend wristarm). P = the drawn palette rows (world x inverse
// bind, read back from the bootstrap 1-2 frames late), Bind = the rest frame. A piece's offset is in its REFERENCE bone's rest frame
// (forearm or hand); it is drawn at P_b x Bind_ref x offset, b = the forearm bone under it (TwistBone) or the hand.
// STAGE B: bones 8 / 9 = the right / left held item's body bone (bone_body, else bone_root) - the gun (trace 20261113_1420).
constexpr int kArmBones = 10, kGunBoneR = 8;
struct Arm { float P[kArmBones][12]; unsigned mask; float bind[kArmBones][12]; unsigned bindOk; int age; };
constexpr int kArmMaxAgeMs = 500;   // older readback rows = not tracking the drawn arm (counted, not used)

bool ReadBinds(Arm& a)
{
    long gen = 0;
    WristArmTable(nullptr, nullptr, 0, &a.bind[0][0], &a.bindOk, &gen);
    return a.bindOk != 0;
}

bool ReadArm(Arm& a)
{
    a = Arm{};
    if (!g_bones) { return false; }
    a.age = g_bones(&a.P[0][0], &a.mask);
    if (a.age <= 0 || a.age > kArmMaxAgeMs) { return false; }
    return ReadBinds(a);
}

void Mul34(const float a[12], const float b[12], float o[12])   // o = a x b (3x4 affine, rows)
{
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            o[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] + a[r * 4 + 2] * b[2 * 4 + c] + (c == 3 ? a[r * 4 + 3] : 0.0f);
        }
    }
}

// Which forearm bone a forearm piece rides, by its offset along the forearm (x = the bone axis, ARCHIVE "col0 IS the bone axis"):
// elbow third = forearm, middle = foretwist1, wrist third = foretwist (INV-355's weights 49 -> 51 -> 50).
int TwistBone(const Arm& a, int hand, float x)
{
    const int base = hand * 4;
    const float* f = a.bind[base + 0];
    const float* h = a.bind[base + 3];
    const float d[3] = { h[3] - f[3], h[7] - f[7], h[11] - f[11] };
    const float L = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    const float t = L > 1e-4f ? std::fabs(x) / L : 0.0f;
    return t < (1.0f / 3.0f) ? base + 0 : t < (2.0f / 3.0f) ? base + 1 : base + 2;
}

// ===== STAGE B (trace docs/traces/20261113_1420_wrist_stageB_gun.md): the weapon box on the held gun, per reload family, kept in
// dlvr\wrist_gun_places.txt ("<Family> x y z pitch yaw roll", one line each - Jay: "Own file"); and the weapon box hidden while either
// hand holds a non-weapon item (Jay: "Any item in either hand"). Submit thread. =====
// KEY (trace docs/traces/20261113_1540_wrist_gun_key_family_type.md - RUN B4: the Dawnlight pistol and the double-barrel shotgun shared
// "DBarrel"): "<reload family>/<item type>", family None -> "Other", type 18 Pistol / 21 Rifle / 24 Shotgun / 37 Crossbow / else Type<n>.
struct GunPlace { char fam[32]; float pos[3]; float pitch, yaw, roll; };
GunPlace g_gunPlace[24] = {};
int g_nGunPlace = 0;
bool g_gunLoaded = false;
bool g_itemInHand = false;       // this submit: either hand holds a non-weapon item (the weapon piece is hidden)
bool g_rightIsGun = false;       // this submit: the right hand holds FireWeaponVis / BowVis / CrossbowVis
bool g_weaponOnGun = false;      // this submit: DrawInGame drew the weapon piece on the gun (Render submits no quad for it)
const char* g_heldFam = "None";  // this submit: the right hand gun's place KEY "<family>/<type>" (g_heldKey), "None" = no key
char g_heldKey[32] = {};
char g_handMesh[2][128] = {};    // this submit: right / left held mesh (for the log)
constexpr int kWeaponPiece = 3;  // HudPrimaryWeaponIndicator

bool GunFilePath(char* out, size_t n) { return DlvrFilePath("wrist_gun_places.txt", out, n); }
void SaveGunPlaces();   // below (LoadGunPlaces rewrites a migrated file)

void LoadGunPlaces()
{
    g_gunLoaded = true;
    g_nGunPlace = 0;
    char path[MAX_PATH] = {};
    if (!GunFilePath(path, sizeof(path))) { return; }
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || !f) {
        // [PROBE: WRISTGUN] gate=ui_wrist_hud (read at launch)  site=HEADSET  log-only=no
        LOG("[WRISTGUN] gun places: no file yet (%s) - every gun shows the weapon box at its arm place until one is placed", path);
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f) && g_nGunPlace < 24) {
        GunPlace g = {};
        if (sscanf_s(line, "%31s %f %f %f %f %f %f", g.fam, static_cast<unsigned>(sizeof(g.fam)), &g.pos[0], &g.pos[1], &g.pos[2], &g.pitch,
                     &g.yaw, &g.roll) == 7) {
            g_gunPlace[g_nGunPlace++] = g;
        }
    }
    fclose(f);
    // MIGRATION (trace 1540): a one-word line (before the "<family>/<type>" key) -> the type it was placed with in RUNS B1-B4 (the
    // classify_all table: Pump / DBarrel = shotguns, Smg / Gate / Break6 / Pistol = type 18, Rifle / Fuel = 21, Crossbow = 37)
    static const char* const kOld[][2] = { { "Pump", "Pump/Shotgun" }, { "DBarrel", "DBarrel/Shotgun" }, { "Smg", "Smg/Pistol" },
        { "Gate", "Gate/Pistol" }, { "Break6", "Break6/Pistol" }, { "Pistol", "Pistol/Pistol" }, { "Rifle", "Rifle/Rifle" },
        { "Fuel", "Fuel/Rifle" }, { "Crossbow", "Crossbow/Crossbow" } };
    int migrated = 0;
    for (int i = 0; i < g_nGunPlace; ++i) {
        if (strchr(g_gunPlace[i].fam, '/')) { continue; }
        for (const auto& m : kOld) {
            if (strcmp(g_gunPlace[i].fam, m[0]) == 0) { strncpy_s(g_gunPlace[i].fam, m[1], _TRUNCATE); ++migrated; break; }
        }
    }
    if (migrated) {
        SaveGunPlaces();
        // [PROBE: WRISTGUN] gate=ui_wrist_hud (read at launch)  site=HEADSET  log-only=no
        LOG("[WRISTGUN] gun places: %d one-word line(s) migrated to \"<family>/<type>\" and the file rewritten", migrated);
    }
    // [PROBE: WRISTGUN] gate=ui_wrist_hud (read at launch)  site=HEADSET  log-only=no
    LOG("[WRISTGUN] gun places loaded from %s: %d famil%s", path, g_nGunPlace, g_nGunPlace == 1 ? "y" : "ies");
}

void SaveGunPlaces()
{
    char path[MAX_PATH] = {};
    if (!GunFilePath(path, sizeof(path))) { return; }
    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") != 0 || !f) {
        // [PROBE: WRISTGUN] gate=ui_wrist_hud (read at launch)  site=HEADSET  log-only=no
        LOG("[WRISTGUN] gun places NOT written - %s could not be opened", path);
        return;
    }
    fprintf(f, "# DyingLightVR wrist HUD - the weapon box's place on the held gun, per reload family: Family x y z pitch yaw roll\n");
    for (int i = 0; i < g_nGunPlace; ++i) {
        const GunPlace& g = g_gunPlace[i];
        fprintf(f, "%s %.5f %.5f %.5f %.3f %.3f %.3f\n", g.fam, g.pos[0], g.pos[1], g.pos[2], g.pitch, g.yaw, g.roll);
    }
    fclose(f);
}

GunPlace* FindGunPlace(const char* fam)
{
    for (int i = 0; i < g_nGunPlace; ++i) { if (strcmp(g_gunPlace[i].fam, fam) == 0) { return &g_gunPlace[i]; } }
    return nullptr;
}

bool StartsWith(const char* s, const char* p) { return strncmp(s, p, strlen(p)) == 0; }

// FIX 2 (trace docs/traces/20261113_1500_wrist_swap_delay_throwables.md - RUN B2: wn_bomb_b in hand, the box not hidden): THROWABLES
// NAMED LIKE WEAPONS - every wn_* mesh our 55 paks' inventory scripts class Throwable / ThrowableLiquid / ThrowableFlare (scratchpad
// inv_family.py over DW + DW_DLC*); wn_shield_c (Melee + Throwable) and wn_shotgun_a_tpp (a 3rd-person model) left out. Compared with
// ".msh" and "_hq" stripped.
const char* const kWnThrowables[] = { "wn_bomb_b", "wn_dynamite_a", "wn_grenade_a", "wn_grenade_b", "wn_petard_a", "wn_shuriken_a",
    "wn_shuriken_a_bundle", "wn_shuriken_b", "wn_snowball_a", "wn_throwing_axe_a_bundle", "wn_throwing_axe_b", "wn_throwing_axe_b_bundle",
    "wn_throwing_axe_c", "wn_throwing_axe_c_bundle", "wn_throwing_knife_a", "wn_throwing_knife_b", "wn_throwing_knives_a_bundle" };

bool IsWnThrowable(const char* mesh)
{
    char b[128];
    size_t n = 0;
    for (; mesh[n] && n + 1 < sizeof(b); ++n) { b[n] = mesh[n]; }
    b[n] = 0;
    if (n >= 4 && strcmp(b + n - 4, ".msh") == 0) { n -= 4; b[n] = 0; }
    if (n >= 3 && strcmp(b + n - 3, "_hq") == 0) { n -= 3; b[n] = 0; }
    for (const char* t : kWnThrowables) { if (strcmp(b, t) == 0) { return true; } }
    return false;
}

// Once per submit: what each hand holds - FIX 2: straight from boneblend's [ITEM] capture (HeldItemNow; the mesh name read once per model
// change), not the AFW mask's 500 ms owner list - and the right gun's family.
uintptr_t g_handModel[2] = {};
char g_handCls[2][64] = {};
void HandsTick()
{
    if (!g_gunLoaded) { LoadGunPlaces(); }
    g_itemInHand = false;
    for (int h = 0; h < 2; ++h) {
        char cls[64] = {};
        const uintptr_t m = HeldItemNow(h, cls, sizeof(cls));
        if (m != g_handModel[h]) {
            g_handModel[h] = m;
            g_handMesh[h][0] = 0;
            if (m && !ItemMeshName(m, g_handMesh[h], sizeof(g_handMesh[h]))) { strcpy_s(g_handMesh[h], "(mesh not read)"); }
            if (!m) { strcpy_s(g_handMesh[h], "(empty hand)"); }
        }
        memcpy(g_handCls[h], cls, sizeof(cls));
        const char* mesh = g_handMesh[h];
        if (!m || mesh[0] == '(') { continue; }   // empty hand, or a mesh not read: not an item
        if ((!StartsWith(mesh, "wn_") && !StartsWith(mesh, "dlc_wn_")) || IsWnThrowable(mesh)) { g_itemInHand = true; }
    }
    const char* rc = g_handCls[0];
    g_rightIsGun = g_handModel[0] && (strcmp(rc, "FireWeaponVis") == 0 || strcmp(rc, "BowVis") == 0 || strcmp(rc, "CrossbowVis") == 0);
    uintptr_t fm = 0;
    // the place KEY "<family>/<type>" (trace 1540): family None -> "Other"; no type read -> no key (not placeable)
    int type = 0;
    const char* fam = g_rightIsGun ? ReloadWristFamily(&fm, &type) : nullptr;
    g_heldFam = "None";
    if (g_rightIsGun && type > 0) {
        const char* tn = type == 18 ? "Pistol" : type == 21 ? "Rifle" : type == 24 ? "Shotgun" : type == 37 ? "Crossbow" : nullptr;
        const char* fn = (fam && strcmp(fam, "None") != 0) ? fam : "Other";
        if (tn) { _snprintf_s(g_heldKey, sizeof(g_heldKey), _TRUNCATE, "%s/%s", fn, tn); }
        else { _snprintf_s(g_heldKey, sizeof(g_heldKey), _TRUNCATE, "%s/Type%d", fn, type); }
        g_heldFam = g_heldKey;
    }
}

// The drawn frame of bone `bi` carrying the reference bone's rest frame (anchor 1 forearm / 2 hand): F = P_bi x Bind_ref.
bool DrawnFrame(const Arm& a, int hand, int anchor, int bi, float F[12])
{
    const int ref = hand * 4 + (anchor == 1 ? 0 : 3);
    if (!(a.mask & (1u << bi)) || !(a.bindOk & (1u << ref))) { return false; }
    Mul34(a.P[bi], a.bind[ref], F);
    return true;
}

bool Inv34w(const float m[12], float o[12])   // 3x4 affine inverse (rows, translation in column 3)
{
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const float det = a * A + b * B + c * C;
    if (!(std::fabs(det) > 1e-12f)) { return false; }
    const float k = 1.0f / det;
    o[0] = A * k; o[1] = -(b * i - c * h) * k; o[2] = (b * f - c * e) * k;
    o[4] = B * k; o[5] = (a * i - c * g) * k;  o[6] = -(a * f - c * d) * k;
    o[8] = C * k; o[9] = -(a * h - b * g) * k; o[10] = (a * e - b * d) * k;
    o[3] = -(o[0] * m[3] + o[1] * m[7] + o[2] * m[11]);
    o[7] = -(o[4] * m[3] + o[5] * m[7] + o[6] * m[11]);
    o[11] = -(o[8] * m[3] + o[9] * m[7] + o[10] * m[11]);
    return true;
}

// FIX ROUND 2 (trace docs/traces/20261113_1330_wrist_ingame_A2.md): a room point -> the game world through the EYE THE HEADSET SHOWS:
// its declared pose + FOV (ovr2xr.cpp projViews - "declared MUST equal rendered") give the point's tangents and view depth; the game
// ray through the same tangents at the same view depth is solved from that image's camera (rows 0, 1, 3 of VP; clip w = view depth).
bool RoomToWorld(const float P[3], float W[3])
{
    if (!g_camOk || g_camEye < 0 || g_camEye > 1 || !g_declOk[g_camEye]) { return false; }
    const XrPosef& D = g_declPose[g_camEye];
    const XrFovf& f = g_declFov[g_camEye];
    const float d0[3] = { P[0] - D.position.x, P[1] - D.position.y, P[2] - D.position.z };
    float p[3];
    Rot(Conj(D.orientation), d0, p);
    const float depth = -p[2];
    if (!(depth > 0.01f)) { return false; }
    const float tx = p[0] / depth, ty = p[1] / depth;
    const float tL = std::tan(f.angleLeft), tR = std::tan(f.angleRight), tU = std::tan(f.angleUp), tD = std::tan(f.angleDown);
    if (!(tR - tL > 1e-4f) || !(tU - tD > 1e-4f)) { return false; }
    const double nx = (2.0 * tx - (tR + tL)) / (tR - tL), ny = (2.0 * ty - (tU + tD)) / (tU - tD);
    const float* v = g_camVp;
    const double A[3][3] = { { v[0] - nx * v[12], v[1] - nx * v[13], v[2] - nx * v[14] },
                             { v[4] - ny * v[12], v[5] - ny * v[13], v[6] - ny * v[14] },
                             { v[12], v[13], v[14] } };
    const double b[3] = { nx * v[15] - v[3], ny * v[15] - v[7], depth - v[15] };
    const double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) +
                       A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    if (!(std::fabs(det) > 1e-12)) { return false; }
    for (int k = 0; k < 3; ++k) {
        double M[3][3];
        for (int r = 0; r < 3; ++r) { for (int c = 0; c < 3; ++c) { M[r][c] = c == k ? b[r] : A[r][c]; } }
        const double dk = M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                          M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
        W[k] = static_cast<float>(dk / det);
    }
    return true;
}

// The piece's world pose (3x4) from its room pose: centre + the ends of its X and Y axes mapped through RoomToWorld; X normalised,
// Y made orthogonal, Z = X x Y (world and room have the same handedness - MakeRoomMap is a yaw + a shift).
bool PieceWorld(const XrPosef& pose, float Pw[12])
{
    const float c[3] = { pose.position.x, pose.position.y, pose.position.z };
    const float ux[3] = { 0.05f, 0.0f, 0.0f }, uy[3] = { 0.0f, 0.05f, 0.0f };
    float ax[3], ay[3];
    Rot(pose.orientation, ux, ax);
    Rot(pose.orientation, uy, ay);
    const float px[3] = { c[0] + ax[0], c[1] + ax[1], c[2] + ax[2] }, py[3] = { c[0] + ay[0], c[1] + ay[1], c[2] + ay[2] };
    float w0[3], wx[3], wy[3];
    if (!RoomToWorld(c, w0) || !RoomToWorld(px, wx) || !RoomToWorld(py, wy)) { return false; }
    float X[3] = { wx[0] - w0[0], wx[1] - w0[1], wx[2] - w0[2] }, Y[3] = { wy[0] - w0[0], wy[1] - w0[1], wy[2] - w0[2] };
    const float xl = std::sqrt(X[0] * X[0] + X[1] * X[1] + X[2] * X[2]);
    if (!(xl > 1e-5f)) { return false; }
    for (float& q : X) { q /= xl; }
    const float dd = X[0] * Y[0] + X[1] * Y[1] + X[2] * Y[2];
    for (int k = 0; k < 3; ++k) { Y[k] -= dd * X[k]; }
    const float yl = std::sqrt(Y[0] * Y[0] + Y[1] * Y[1] + Y[2] * Y[2]);
    if (!(yl > 1e-5f)) { return false; }
    for (float& q : Y) { q /= yl; }
    const float Z[3] = { X[1] * Y[2] - X[2] * Y[1], X[2] * Y[0] - X[0] * Y[2], X[0] * Y[1] - X[1] * Y[0] };
    const float out[12] = { X[0], Y[0], Z[0], w0[0], X[1], Y[1], Z[1], w0[1], X[2], Y[2], Z[2], w0[2] };
    memcpy(Pw, out, sizeof(out));
    return true;
}

XrPosef FrameRoom(const RoomMap& m, const float F[12])
{
    const float o[3] = { F[3], F[7], F[11] };
    const float b[9] = { F[0], F[1], F[2], F[4], F[5], F[6], F[8], F[9], F[10] };
    float r[3];
    WorldToRoom(m, o, r);
    XrPosef p{};
    p.position = { r[0], r[1], r[2] };
    p.orientation = BasisToRoom(m, b);
    return p;
}

// The drawn bone's room pose for the nearest-bone test and the place: bone 1 forearm (origin = elbow, the forearm bone's own frame) /
// 2 hand (origin = wrist). segA / segB = its segment in the room: forearm elbow -> wrist; hand wrist -> wrist + 0.08 m along the
// forearm line (the hand's own long axis is not used - which local axis it is was not read).
bool BoneRoomPose(const RoomMap& m, const Arm& a, int hand, int bone, XrPosef& out, float segA[3] = nullptr, float segB[3] = nullptr)
{
    if (!m.ok || (hand != 0 && hand != 1) || (bone != 1 && bone != 2)) { return false; }
    float Ff[12], Fh[12];
    if (!DrawnFrame(a, hand, 1, hand * 4 + 0, Ff) || !DrawnFrame(a, hand, 2, hand * 4 + 3, Fh)) { return false; }
    const XrPosef pf = FrameRoom(m, Ff), ph = FrameRoom(m, Fh);
    out = bone == 1 ? pf : ph;
    if (segA && segB) {
        const float e[3] = { pf.position.x, pf.position.y, pf.position.z }, w[3] = { ph.position.x, ph.position.y, ph.position.z };
        if (bone == 1) {
            for (int k = 0; k < 3; ++k) { segA[k] = e[k]; segB[k] = w[k]; }
        } else {
            float d[3] = { w[0] - e[0], w[1] - e[1], w[2] - e[2] };
            const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            for (int k = 0; k < 3; ++k) { segA[k] = w[k]; segB[k] = w[k] + (l > 1e-4f ? d[k] / l * 0.08f : 0.0f); }
        }
    }
    return true;
}

float SegDist(const float p[3], const float a[3], const float b[3])
{
    const float ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, ap[3] = { p[0] - a[0], p[1] - a[1], p[2] - a[2] };
    const float l2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
    float t = l2 > 1e-8f ? (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / l2 : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    const float q[3] = { a[0] + ab[0] * t - p[0], a[1] + ab[1] * t - p[1], a[2] + ab[2] * t - p[2] };
    return std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
}

}  // namespace

// STAGE 3a (Jay 2026-11-13: "Next launch"): the first caller is wristprobe::Start, run from ovr_Initialize AFTER LoadLayerConfig
// (ovr2xr.cpp), so the latch holds the file's value; every later call (wristquad::Init) gets the same answer however the menu flips it.
bool OnThisLaunch()
{
    static const bool on = UiWristHud;
    return on;
}

// Jay 2026-11-14 (HARD; trace docs/traces/20261114_1200_wrist_holds_hud_panel.md): ticked now, or running this launch (an untick waits
// for the restart, and the running wrist HUD still needs the panel until then).
bool HudPanelNeeded()
{
    return UiWristHud || OnThisLaunch();
}

void ApplyPreset(int preset)
{
    if (preset != 0 && preset != 1) { return; }
    for (int p = 0; p < kP; ++p) {
        const Place& s = kPresets[preset][p];
        UiWristHand[p] = s.hand;
        UiWristX[p] = s.pos[0]; UiWristY[p] = s.pos[1]; UiWristZ[p] = s.pos[2];
        UiWristPitch[p] = s.pitch; UiWristYaw[p] = s.yaw; UiWristRoll[p] = s.roll;
        UiWristScale[p] = s.scale;
        UiWristAnchor[p] = s.anchor;   // the preset's own anchor (A: Jay's arm anchors; B: the grip)
    }
}

// A tolerance, not ==: the values come back from ovr2xr.txt as printed text (memory compare-game-floats-by-file-bits).
bool MatchesPreset(int preset)
{
    if (preset != 0 && preset != 1) { return false; }
    const auto same = [](float a, float b, float tol) { return std::fabs(a - b) <= tol; };   // not `near` - a windows.h macro
    for (int p = 0; p < kP; ++p) {
        const Place& s = kPresets[preset][p];
        if (UiWristHand[p] != s.hand || UiWristAnchor[p] != s.anchor) { return false; }
        if (!same(UiWristX[p], s.pos[0], 5e-4f) || !same(UiWristY[p], s.pos[1], 5e-4f) || !same(UiWristZ[p], s.pos[2], 5e-4f)) {
            return false;
        }
        if (!same(UiWristPitch[p], s.pitch, 0.05f) || !same(UiWristYaw[p], s.yaw, 0.05f) || !same(UiWristRoll[p], s.roll, 0.05f)) {
            return false;
        }
        if (!same(UiWristScale[p], s.scale, 5e-3f)) { return false; }
    }
    return true;
}

// ===== STAGE 3b - PLACE BY HAND (trace docs/traces/20261113_1108_wrist_3b_place.md; spec §8, holster.cpp's pattern). =====
bool Placing() { return g_placePiece >= 0; }
int PlacingPiece() { return g_placePiece; }

int PlaceBegin(int piece)
{
    if (piece < 0 || piece >= kP || g_placePiece >= 0) { return kWpCancelled; }
    const int res = !OnThisLaunch() ? kWpNotOn : (UiWristHand[piece] != 0 && UiWristHand[piece] != 1) ? kWpHandOff : -1;
    const long k = ++g_placeLines;
    // [PROBE: WRISTPLACE] gate=ui_wrist_hud (read at launch) + the HUD tab's Place button  site=HEADSET  log-only=no
    LOG("[WRISTPLACE] BEGIN %s %s%s | appears %.2f m ahead, fixed in the room, boxed; a beam from each controller; grip on it carries "
        "(release to leave it), trigger places it on the NEAREST hand; the menu button cancels | now on the %s hand (%.3f %.3f %.3f) m, "
        "pitch %.0f yaw %.0f roll %.0f%s", kTag[piece], res < 0 ? "STARTED" : "REFUSED", res == kWpNotOn ?
        " - the wrist HUD is not on this launch" : res == kWpHandOff ? " - its hand is Off" : "", static_cast<double>(kAppearM),
        UiWristHand[piece] == 0 ? "LEFT" : "RIGHT", static_cast<double>(UiWristX[piece]), static_cast<double>(UiWristY[piece]),
        static_cast<double>(UiWristZ[piece]), static_cast<double>(UiWristPitch[piece]), static_cast<double>(UiWristYaw[piece]),
        static_cast<double>(UiWristRoll[piece]), k == 1 ? " | first line, logged unconditionally" : "");
    if (res >= 0) { return res; }
    g_placePiece = piece;
    g_placeStartMs = GetTickCount64();
    g_carrier = -1;
    g_appearPending = true;
    g_gripArmed[0] = g_gripArmed[1] = g_trigArmed[0] = g_trigArmed[1] = false;   // a button already held does nothing until let go
    g_refusals = 0;
    g_placeGrabbed = false;
    return -1;
}

namespace {
// The trigger placed it: on the NEAREST tracked grip (Jay: "Nearest hand"). Returns kWpPlaced, or -1 (refused - stays placing).
int TryPlace(int p, const XrPosef& pose, int byHand)
{
    // ARM ANCHOR (Jay: "Nearest bone"): the nearest of the four drawn bones - L / R forearm (elbow -> wrist), L / R hand - when the
    // arm solve publishes them and the room map is readable; else the nearest controller grip as before (anchor 0).
    {
        // STAGE A: the DRAWN arm (the bootstrap's read-back palette rows x the rest frames), not the rig's own values
        const RoomMap m = MakeRoomMap();
        Arm arm{};
        const bool armOk = ReadArm(arm);
        const float pp[3] = { pose.position.x, pose.position.y, pose.position.z };
        int bh = -1, bb = 0;
        float bd = 1e9f;
        XrPosef bp{};
        for (int h = 0; armOk && h < 2; ++h) {
            for (int b = 1; b <= 2; ++b) {
                XrPosef bone{};
                float a[3], c[3];
                if (!BoneRoomPose(m, arm, h, b, bone, a, c)) { continue; }
                const float d = SegDist(pp, a, c);
                if (d < bd) { bd = d; bh = h; bb = b; bp = bone; }
            }
        }
        // STAGE B (fix, trace docs/traces/20261113_1440_wrist_gun_place_always.md - RUN B1: the SMG's drops lost a gun / arm distance
        // contest and rewrote the arm place): while the right hand holds a gun of a KNOWN family, the weapon box goes to THAT family's
        // place ONLY - no distance contest; anything missing = PLACE REFUSED (still placing), never the arm place. The offset through the
        // eye camera (as round 2). The arm place (settings) changes only while no known-family gun is held.
        if (p == kWeaponPiece && g_rightIsGun && strcmp(g_heldFam, "None") != 0) {
            const bool rows = armOk && (arm.mask & (1u << kGunBoneR)) != 0;
            const bool rest = armOk && (arm.bindOk & (1u << kGunBoneR)) != 0;
            float Fg[12] = {}, Pw[12] = {}, iF[12], off[12];
            if (rows && rest) { Mul34(arm.P[kGunBoneR], arm.bind[kGunBoneR], Fg); }
            const bool mapped = rows && rest && PieceWorld(pose, Pw);
            const bool inv = mapped && Inv34w(Fg, iF);
            const float dg = mapped ? std::sqrt((Pw[3] - Fg[3]) * (Pw[3] - Fg[3]) + (Pw[7] - Fg[7]) * (Pw[7] - Fg[7]) +
                                                (Pw[11] - Fg[11]) * (Pw[11] - Fg[11])) : -1.0f;
            // [PROBE: WRISTGUN] gate=ui_wrist_hud (read at launch) + a weapon-box placement  site=HEADSET  log-only=no
            LOG("[WRISTGUN] weapon-box drop with the %s gun in the right hand (%s): readback rows %s (age %d ms, bones 0x%03X), its rest frame "
                "%s, eye mapping %s, %.3f m from its body bone (nearest arm bone %.3f m - not used)", g_heldFam, g_handMesh[0],
                rows ? "READ" : "NOT READ", arm.age, arm.mask, rest ? "read" : "NOT READ", mapped ? "ok" : "NOT OK", static_cast<double>(dg),
                static_cast<double>(bh >= 0 ? bd : -1.0f));
            if (!inv) {
                ++g_refusals;
                // [PROBE: WRISTGUN] gate=ui_wrist_hud (read at launch) + a weapon-box placement  site=HEADSET  log-only=no
                LOG("[WRISTGUN] PLACE REFUSED weapon box (trigger %s) - the %s gun's %s; the arm place is NOT changed; still placing",
                    byHand == 0 ? "LEFT" : "RIGHT", g_heldFam, !rows ? "drawn rows are not read back (not drawn in the copied pass?)"
                    : !rest ? "rest frame is not read" : !mapped ? "drop could not be mapped through the eye camera"
                    : "frame is not invertible");
                return -1;
            }
            {
                {
                    Mul34(iF, Pw, off);
                    float X[3] = { off[0], off[4], off[8] }, Y[3] = { off[1], off[5], off[9] };
                    const float xl = std::sqrt(X[0] * X[0] + X[1] * X[1] + X[2] * X[2]);
                    for (float& q : X) { q /= (xl > 1e-6f ? xl : 1.0f); }
                    const float dd = X[0] * Y[0] + X[1] * Y[1] + X[2] * Y[2];
                    for (int k = 0; k < 3; ++k) { Y[k] -= dd * X[k]; }
                    const float yl = std::sqrt(Y[0] * Y[0] + Y[1] * Y[1] + Y[2] * Y[2]);
                    for (float& q : Y) { q /= (yl > 1e-6f ? yl : 1.0f); }
                    const float Z[3] = { X[1] * Y[2] - X[2] * Y[1], X[2] * Y[0] - X[0] * Y[2], X[0] * Y[1] - X[1] * Y[0] };
                    float pitch = 0.0f, yaw = 0.0f, roll = 0.0f;
                    YawPitchRoll(QuatFromBasis(X, Y, Z), pitch, yaw, roll);
                    GunPlace* gp = FindGunPlace(g_heldFam);
                    if (!gp && g_nGunPlace < 24) { gp = &g_gunPlace[g_nGunPlace++]; strncpy_s(gp->fam, g_heldFam, _TRUNCATE); }
                    if (gp) {
                        gp->pos[0] = off[3]; gp->pos[1] = off[7]; gp->pos[2] = off[11];
                        gp->pitch = pitch; gp->yaw = yaw; gp->roll = roll;
                        SaveGunPlaces();
                    }
                    // [PROBE: WRISTGUN] gate=ui_wrist_hud (read at launch) + a placement running  site=HEADSET  log-only=no
                    LOG("[WRISTGUN] PLACED the weapon box on the %s gun (%s, %.3f m from its body bone, nearest arm bone %.3f m; trigger %s): "
                        "offset (%.3f %.3f %.3f) m, pitch / yaw / roll %.1f / %.1f / %.1f deg%s", g_heldFam, g_handMesh[0],
                        static_cast<double>(dg), static_cast<double>(bh >= 0 ? bd : -1.0f), byHand == 0 ? "LEFT" : "RIGHT",
                        static_cast<double>(off[3]), static_cast<double>(off[7]), static_cast<double>(off[11]), static_cast<double>(pitch),
                        static_cast<double>(yaw), static_cast<double>(roll), gp ? "" : " - NOT KEPT (24 families already)");
                    return kWpPlaced;
                }
            }
        }
        if (bh >= 0 && bb == 1) {
            // the forearm bone under the drop point carries the piece (its twist): re-express in THAT bone's drawn frame
            const XrPosef relFa = PoseMul(PoseInv(bp), pose);
            float F[12];
            if (DrawnFrame(arm, bh, 1, TwistBone(arm, bh, relFa.position.x), F)) { bp = FrameRoom(m, F); }
        }
        if (bh >= 0) {
            // FIX ROUND 2: the offset through the EYE the headset shows (the piece's world pose from the declared pose + FOV and that
            // image's game camera) against the drawn bone frame in WORLD; the room map above only chose the bone. Room-map offset =
            // the fallback, named in the line.
            XrPosef rel = PoseMul(PoseInv(bp), pose);
            const char* via = "the room map (FALLBACK - no eye camera pair yet)";
            float Pw[12];
            if (PieceWorld(pose, Pw)) {
                int bi = bb == 2 ? bh * 4 + 3 : bh * 4 + 0;
                float F[12], iF[12], off[12];
                bool okF = DrawnFrame(arm, bh, bb, bi, F);
                if (okF && bb == 1 && Inv34w(F, iF)) {
                    Mul34(iF, Pw, off);
                    bi = TwistBone(arm, bh, off[3]);
                    okF = DrawnFrame(arm, bh, 1, bi, F);
                }
                if (okF && Inv34w(F, iF)) {
                    Mul34(iF, Pw, off);
                    // the drawn frame may carry a little scale / skew (palette x bind): re-orthonormalise before the quaternion
                    float X[3] = { off[0], off[4], off[8] }, Y[3] = { off[1], off[5], off[9] };
                    const float xl = std::sqrt(X[0] * X[0] + X[1] * X[1] + X[2] * X[2]);
                    for (float& q : X) { q /= (xl > 1e-6f ? xl : 1.0f); }
                    const float dd = X[0] * Y[0] + X[1] * Y[1] + X[2] * Y[2];
                    for (int k = 0; k < 3; ++k) { Y[k] -= dd * X[k]; }
                    const float yl = std::sqrt(Y[0] * Y[0] + Y[1] * Y[1] + Y[2] * Y[2]);
                    for (float& q : Y) { q /= (yl > 1e-6f ? yl : 1.0f); }
                    const float Z[3] = { X[1] * Y[2] - X[2] * Y[1], X[2] * Y[0] - X[0] * Y[2], X[0] * Y[1] - X[1] * Y[0] };
                    rel.position = { off[3], off[7], off[11] };
                    rel.orientation = QuatFromBasis(X, Y, Z);
                    via = "the eye camera";
                } else {
                    via = "the room map (FALLBACK - the drawn bone frame not invertible / not read)";
                }
            } else if (g_camOk) {
                via = "the room map (FALLBACK - the piece is behind / outside the eye's view)";
            }
            float pitch = 0.0f, yaw = 0.0f, roll = 0.0f;
            YawPitchRoll(rel.orientation, pitch, yaw, roll);
            const char* hn = bh == 0 ? "LEFT" : "RIGHT";
            const char* bn = bb == 1 ? "forearm" : "hand";
            // [PROBE: WRISTPLACE] gate=ui_wrist_hud (read at launch) + a placement running  site=HEADSET  log-only=no
            LOG("[WRISTPLACE] %s: offset taken through %s (eye %d, camera pair %s)", kTag[p], via, g_camEye, g_camOk ? "ok" : "none");
            if (std::fabs(rel.position.x) > kMaxAxisM || std::fabs(rel.position.y) > kMaxAxisM || std::fabs(rel.position.z) > kMaxAxisM) {
                ++g_refusals;
                // [PROBE: WRISTPLACE] gate=ui_wrist_hud (read at launch) + a placement running  site=HEADSET  log-only=no
                LOG("[WRISTPLACE] PLACE REFUSED %s (trigger %s) - nearest bone %s %s (%.3f m), but (%.3f %.3f %.3f) m from its origin is past "
                    "the %.1f m reach on an axis; still placing", kTag[p], byHand == 0 ? "LEFT" : "RIGHT", hn, bn, static_cast<double>(bd),
                    static_cast<double>(rel.position.x), static_cast<double>(rel.position.y), static_cast<double>(rel.position.z),
                    static_cast<double>(kMaxAxisM));
                return -1;
            }
            // [PROBE: WRISTPLACE] gate=ui_wrist_hud (read at launch) + a placement running  site=HEADSET  log-only=no
            LOG("[WRISTPLACE] PLACED %s on the %s %s (nearest bone, %.3f m from its segment; trigger %s) after %llu ms, %ld refused before: "
                "hand %d -> %d, anchor %d -> %d, offset (%.3f %.3f %.3f) m, pitch / yaw / roll %.1f / %.1f / %.1f deg; layout -> Custom",
                kTag[p], hn, bn, static_cast<double>(bd), byHand == 0 ? "LEFT" : "RIGHT",
                static_cast<unsigned long long>(GetTickCount64() - g_placeStartMs), g_refusals, UiWristHand[p], bh, UiWristAnchor[p], bb,
                static_cast<double>(rel.position.x), static_cast<double>(rel.position.y), static_cast<double>(rel.position.z),
                static_cast<double>(pitch), static_cast<double>(yaw), static_cast<double>(roll));
            UiWristHand[p] = bh;
            UiWristAnchor[p] = bb;
            UiWristX[p] = rel.position.x; UiWristY[p] = rel.position.y; UiWristZ[p] = rel.position.z;
            UiWristPitch[p] = pitch; UiWristYaw[p] = yaw; UiWristRoll[p] = roll;
            UiWristPreset = 2;
            return kWpPlaced;
        }
        // [PROBE: WRISTPLACE] gate=ui_wrist_hud (read at launch) + a placement running  site=HEADSET  log-only=no
        LOG("[WRISTPLACE] %s: no drawn arm bone readable (room map %s, drawn rows %s - age %d ms, bones 0x%02X, rest frames 0x%02X) - "
            "placing on the nearest controller instead", kTag[p], m.ok ? "ok" : "NOT READ (camera / yaw / head)",
            armOk ? "ok" : "NOT READ", arm.age, arm.mask, arm.bindOk);
    }
    XrPosef g[2]{};
    const bool ok[2] = { GripTracked(kLeftHand, g[0]), GripTracked(kRightHand, g[1]) };
    if (!ok[0] && !ok[1]) {
        ++g_refusals;
        // [PROBE: WRISTPLACE] gate=ui_wrist_hud (read at launch) + a placement running  site=HEADSET  log-only=no
        LOG("[WRISTPLACE] PLACE REFUSED %s (trigger %s) - neither grip tracked; still placing", kTag[p], byHand == 0 ? "LEFT" : "RIGHT");
        return -1;
    }
    float d2[2] = { 1e9f, 1e9f };
    for (int h = 0; h < 2; ++h) {
        if (!ok[h]) { continue; }
        const float dx = pose.position.x - g[h].position.x, dy = pose.position.y - g[h].position.y, dz = pose.position.z - g[h].position.z;
        d2[h] = dx * dx + dy * dy + dz * dz;
    }
    const int h = d2[0] <= d2[1] ? 0 : 1;
    const XrPosef rel = PoseMul(PoseInv(g[h]), pose);
    float pitch = 0.0f, yaw = 0.0f, roll = 0.0f;
    YawPitchRoll(rel.orientation, pitch, yaw, roll);
    const char* hn = h == 0 ? "LEFT" : "RIGHT";
    if (std::fabs(rel.position.x) > kMaxAxisM || std::fabs(rel.position.y) > kMaxAxisM || std::fabs(rel.position.z) > kMaxAxisM) {
        ++g_refusals;
        // [PROBE: WRISTPLACE] gate=ui_wrist_hud (read at launch) + a placement running  site=HEADSET  log-only=no
        LOG("[WRISTPLACE] PLACE REFUSED %s (trigger %s) - nearest hand %s (%.3f m), but (%.3f %.3f %.3f) m from its grip is past the %.1f m "
            "reach on an axis; still placing", kTag[p], byHand == 0 ? "LEFT" : "RIGHT", hn, static_cast<double>(std::sqrt(d2[h])),
            static_cast<double>(rel.position.x), static_cast<double>(rel.position.y), static_cast<double>(rel.position.z),
            static_cast<double>(kMaxAxisM));
        return -1;
    }
    // [PROBE: WRISTPLACE] gate=ui_wrist_hud (read at launch) + a placement running  site=HEADSET  log-only=no
    LOG("[WRISTPLACE] PLACED %s on the %s hand (nearest, %.3f m; trigger %s, %s) after %llu ms, %ld refused before: hand %d -> %d, "
        "(%.3f %.3f %.3f) m -> (%.3f %.3f %.3f) m, pitch / yaw / roll %.0f / %.0f / %.0f -> %.1f / %.1f / %.1f deg; layout -> Custom",
        kTag[p], hn, static_cast<double>(std::sqrt(d2[h])), byHand == 0 ? "LEFT" : "RIGHT", g_carrier >= 0 ? "carried" : "floating",
        static_cast<unsigned long long>(GetTickCount64() - g_placeStartMs), g_refusals, UiWristHand[p], h,
        static_cast<double>(UiWristX[p]), static_cast<double>(UiWristY[p]), static_cast<double>(UiWristZ[p]),
        static_cast<double>(rel.position.x), static_cast<double>(rel.position.y), static_cast<double>(rel.position.z),
        static_cast<double>(UiWristPitch[p]), static_cast<double>(UiWristYaw[p]), static_cast<double>(UiWristRoll[p]),
        static_cast<double>(pitch), static_cast<double>(yaw), static_cast<double>(roll));
    UiWristHand[p] = h;
    UiWristAnchor[p] = 0;   // the controller-grip fallback (no drawn bone readable)
    UiWristX[p] = rel.position.x; UiWristY[p] = rel.position.y; UiWristZ[p] = rel.position.z;
    UiWristPitch[p] = pitch; UiWristYaw[p] = yaw; UiWristRoll[p] = roll;
    UiWristPreset = 2;
    return kWpPlaced;
}
}  // namespace

// COST while not placing: one int test. Placing: per frame 2 aim + up to 3 grip pose reads, 2 squeeze + 2 trigger reads, 2 ray-rect
// tests; a grab / let-go / place a few dozen flops, once.
int PlaceTick(bool menuEdge)
{
    const int p = g_placePiece;
    if (p < 0) { return -1; }
    if (menuEdge) {
        // [PROBE: WRISTPLACE] gate=ui_wrist_hud (read at launch) + a placement running  site=HEADSET  log-only=no
        LOG("[WRISTPLACE] CANCELLED %s after %llu ms (the menu button / the panel closing), %ld place(s) refused - its place is unchanged",
            kTag[p], static_cast<unsigned long long>(GetTickCount64() - g_placeStartMs), g_refusals);
        PlaceEnd();
        return kWpCancelled;
    }
    // Appear: kAppearM along the view, upright (turned about world up only), its front facing back toward the head.
    if (g_appearPending) {
        XrPosef hp{};
        if (!GetHeadPose(hp)) { return -1; }
        const float fz[3] = { 0.0f, 0.0f, -1.0f };
        float f[3];
        Rot(hp.orientation, fz, f);
        const float hl = std::sqrt(f[0] * f[0] + f[2] * f[2]);
        const float yawRad = hl > 1e-3f ? std::atan2(-f[0], -f[2]) : 0.0f;   // Ry(yaw) * +Z = the horizontal back-to-the-head direction
        g_floatPose.orientation = Axis(1, yawRad * 57.29578f);
        g_floatPose.position = { hp.position.x + f[0] * kAppearM, hp.position.y + f[1] * kAppearM, hp.position.z + f[2] * kAppearM };
        g_appearPending = false;
        // [PROBE: WRISTPLACE] gate=ui_wrist_hud (read at launch) + a placement running  site=HEADSET  log-only=no
        LOG("[WRISTPLACE] %s APPEARS at (%.3f %.3f %.3f) m, facing yaw %.0f deg (head at %.3f %.3f %.3f)", kTag[p],
            static_cast<double>(g_floatPose.position.x), static_cast<double>(g_floatPose.position.y),
            static_cast<double>(g_floatPose.position.z), static_cast<double>(yawRad * 57.29578f), static_cast<double>(hp.position.x),
            static_cast<double>(hp.position.y), static_cast<double>(hp.position.z));
    }
    XrPosef pose{};
    if (!PiecePoseNow(pose)) {   // the carrier lost tracking: leave it floating where it was last seen
        if (g_carrier >= 0) { g_carrier = -1; }
        return -1;
    }
    // The beams: each hand's AIM ray against the piece.
    for (int h = 0; h < 2; ++h) {
        const Hand hand = h == 0 ? kLeftHand : kRightHand;
        g_beamOk[h] = GetControllerPose(hand, g_aim[h]);
        g_hit[h] = false;
        g_beamLen[h] = kBeamM;
        if (!g_beamOk[h]) { continue; }
        const float o[3] = { g_aim[h].position.x, g_aim[h].position.y, g_aim[h].position.z };
        const float fz[3] = { 0.0f, 0.0f, -1.0f };
        float d[3];
        Rot(g_aim[h].orientation, fz, d);
        const float t = RayHit(pose, o, d);
        if (t > 0.0f) { g_hit[h] = true; g_beamLen[h] = t; }
    }
    for (int h = 0; h < 2; ++h) {
        const Hand hand = h == 0 ? kLeftHand : kRightHand;
        float sq = 0.0f, tr = 0.0f;
        const bool sqOk = GetControllerSqueeze(hand, sq), trOk = GetControllerTrigger(hand, tr);
        // GRIP: on the piece = carry it (the offset kept); let go = it stays where it is.
        if (sqOk) {
            if (g_carrier == h && sq < kPressOff) {
                g_floatPose = pose;
                g_carrier = -1;
                // [PROBE: WRISTPLACE] gate=ui_wrist_hud (read at launch) + a placement running  site=HEADSET  log-only=no
                LOG("[WRISTPLACE] %s LET GO by the %s grip - floating at (%.3f %.3f %.3f) m", kTag[p], h == 0 ? "LEFT" : "RIGHT",
                    static_cast<double>(pose.position.x), static_cast<double>(pose.position.y), static_cast<double>(pose.position.z));
            }
            if (sq < kPressOff) { g_gripArmed[h] = true; }
            else if (sq >= kPressOn && g_gripArmed[h]) {
                g_gripArmed[h] = false;
                XrPosef grip{};
                if (g_carrier < 0 && g_hit[h] && GripTracked(hand, grip)) {
                    g_carryOff = PoseMul(PoseInv(grip), pose);
                    g_carrier = h;
                    g_placeGrabbed = true;
                    // [PROBE: WRISTPLACE] gate=ui_wrist_hud (read at launch) + a placement running  site=HEADSET  log-only=no
                    LOG("[WRISTPLACE] %s GRABBED by the %s grip (beam %.3f m) - offset (%.3f %.3f %.3f) m", kTag[p], h == 0 ? "LEFT" : "RIGHT",
                        static_cast<double>(g_beamLen[h]), static_cast<double>(g_carryOff.position.x),
                        static_cast<double>(g_carryOff.position.y), static_cast<double>(g_carryOff.position.z));
                }
            }
        }
        // TRIGGER = CONFIRM, never a grab: the carrier's trigger, or a pointing controller's while the piece floats - but only once
        // the piece has been grabbed this placement (Jay after RUN P3B3: "We just didn't want trigger to be capable of GRABBING the
        // element. It was able to grab and also confirmed at the moment of grab prior"; P3B2's two places 1.2 / 1.7 s after Place
        // came before any grab). Trace docs/traces/20261113_1205_wrist_trigger_confirm.md.
        if (trOk) {
            if (tr < kPressOff) { g_trigArmed[h] = true; }
            else if (tr >= kPressOn && g_trigArmed[h]) {
                g_trigArmed[h] = false;
                if (g_carrier == h || (g_carrier < 0 && g_hit[h] && g_placeGrabbed)) {
                    if (TryPlace(p, pose, h) == kWpPlaced) {
                        PlaceEnd();
                        return kWpPlaced;
                    }
                }
            }
        }
    }
    return -1;
}

void ResetPiece(int piece)
{
    if (piece < 0 || piece >= kP) { return; }
    const int preset = (UiWristPreset == 0 || UiWristPreset == 1) ? UiWristPreset : 0;
    const Place& s = kPresets[preset][piece];
    UiWristHand[piece] = s.hand;
    UiWristX[piece] = s.pos[0]; UiWristY[piece] = s.pos[1]; UiWristZ[piece] = s.pos[2];
    UiWristPitch[piece] = s.pitch; UiWristYaw[piece] = s.yaw; UiWristRoll[piece] = s.roll;
    UiWristScale[piece] = s.scale;
    UiWristAnchor[piece] = s.anchor;
    // STAGE B: Reset on the weapon box also removes the held gun family's place (the box goes back to its arm place on that gun)
    bool gunCleared = false;
    if (piece == kWeaponPiece && g_rightIsGun) {
        for (int i = 0; i < g_nGunPlace; ++i) {
            if (strcmp(g_gunPlace[i].fam, g_heldFam) != 0) { continue; }
            g_gunPlace[i] = g_gunPlace[--g_nGunPlace];
            gunCleared = true;
            SaveGunPlaces();
            break;
        }
    }
    // [PROBE: WRISTPLACE] gate=ui_wrist_hud (read at launch) + the HUD tab's Reset button  site=HEADSET  log-only=no
    LOG("[WRISTPLACE] RESET %s to layout %s's place%s%s", kTag[piece], preset == 0 ? "A" : "B",
        gunCleared ? " - and the gun place of family " : "", gunCleared ? g_heldFam : "");
}

void Init(ID3D11Device* device, XrSession session)
{
    g_device = device;
    g_session = session;
    g_scFailed = false;
    g_on = OnThisLaunch();
    if (!g_saidOn) {
        g_saidOn = true;
        // [PROBE: WRISTQUAD] gate=ui_wrist_hud (read at launch)  site=HEADSET  log-only=no
        LOG("[WRISTQUAD] %s (logged unconditionally) - ui_wrist_hud at launch %d (now %d). %s | preset %d, size %.2f, fade %.0f..%.0f deg "
            "| hands (0 L / 1 R / 2 off) %d %d %d %d %d", g_on ? "ON - the five HUD pieces on quads on the hands (STAGE 3a: placement "
            "from the ui_wrist_* settings)" : "off", g_on ? 1 : 0, UiWristHud ? 1 : 0,
            g_on ? "NEEDS the bootstrap's router (same ui_wrist_hud line, its own log line) and its needs." : "",
            UiWristPreset, static_cast<double>(UiWristSize), static_cast<double>(UiWristFadeStartDeg),
            static_cast<double>(UiWristFadeEndDeg), UiWristHand[0], UiWristHand[1], UiWristHand[2], UiWristHand[3], UiWristHand[4]);
    }
}

void Shutdown()
{
    PlaceEnd();   // STAGE 3b: no placement outlives the session
    for (auto*& v : g_rtv) { if (v) { v->Release(); v = nullptr; } }   // the outline's views (before their swapchain goes)
    g_rtv.clear();
    if (g_ctx1) { g_ctx1->Release(); g_ctx1 = nullptr; }
    if (g_sc != XR_NULL_HANDLE && xrDestroySwapchain_) { xrDestroySwapchain_(g_sc); }
    g_sc = XR_NULL_HANDLE;
    g_images.clear();
}

int Render(XrSpace appSpace, bool viewsValid, const XrPosef eyes[2], int room, XrCompositionLayerQuad out[kMaxQuads])
{
    // The pieces live in grip space; the fade reads the grip pose already located in appSpace. STAGE 3b: a floating piece, its box and
    // the beams are submitted in appSpace.
    if (!g_on) { return 0; }
    ++w_submits;
    Window();
    if (!Resolve()) { return 0; }

    // the head: the mid-point of the declared eyes, else the located head pose
    float head[3] = {};
    bool haveHead = false;
    if (viewsValid) {
        head[0] = (eyes[0].position.x + eyes[1].position.x) * 0.5f;
        head[1] = (eyes[0].position.y + eyes[1].position.y) * 0.5f;
        head[2] = (eyes[0].position.z + eyes[1].position.z) * 0.5f;
        haveHead = true;
    } else {
        XrPosef hp{};
        if (GetHeadPose(hp)) { head[0] = hp.position.x; head[1] = hp.position.y; head[2] = hp.position.z; haveHead = true; }
    }

    // STAGE 3a: the fade angles from the settings (an end at or below the start = start + 1, so the smooth step never divides by 0)
    const float fadeStart = UiWristFadeStartDeg;
    const float fadeEnd = UiWristFadeEndDeg > fadeStart + 1.0f ? UiWristFadeEndDeg : fadeStart + 1.0f;
    // STAGE 3b: hand = the grip the quad is held in; local = the quad's pose in that grip; outline = draw the frame, not a copy
    // inApp = the quad is submitted in appSpace (3b: the piece floating in the room), else in `hand`'s grip space.
    struct Want { int p; int box[4]; float ui[2]; float fade; Hand hand; XrPosef local; bool outline; bool inApp; };
    Want want[kP];
    int nWant = 0;
    const float mpp = MetresPerUiPxNow();   // 0 = the UI-screen height not published yet: no piece is placed (never a guessed size)
    for (int p = 0; p < kP; ++p) {
        Want w{};
        w.p = p;
        if (!(mpp > 0.0f)) { ++w_notPub[p]; continue; }
        const bool pub = g_info(p, w.box, w.ui) == 0;
        if (pub && w.box[2] > 0 && w.box[3] > 0) {   // the outline's size when the piece is carried later and not drawn then
            for (int k = 0; k < 4; ++k) { g_lastBox[p][k] = w.box[k]; }
            g_lastUi[p][0] = w.ui[0]; g_lastUi[p][1] = w.ui[1];
            g_haveLast[p] = true;
        }
        // STAGE 3b - THE PIECE BEING PLACED (point-and-grab): floating in the room (appSpace) or carried in its carrier's grip space
        // at the grab offset; fade 1; the real picture when the game draws it, else the outline box. Not yet appeared = not drawn.
        if (p == g_placePiece) {
            if (g_appearPending) { continue; }
            if (g_carrier == 0 || g_carrier == 1) {
                w.hand = g_carrier == 0 ? kLeftHand : kRightHand;
                w.local = g_carryOff;
                w.inApp = false;
            } else {
                w.hand = kLeftHand;   // unused while inApp
                w.local = g_floatPose;
                w.inApp = true;
            }
            w.fade = 1.0f;
            if (!pub) {
                w.outline = true;
                if (g_haveLast[p]) {
                    for (int k = 0; k < 4; ++k) { w.box[k] = g_lastBox[p][k]; }
                    w.ui[0] = g_lastUi[p][0]; w.ui[1] = g_lastUi[p][1];
                } else {   // never seen this session: 256 x 128 slot px at ~6 cm wide
                    w.box[0] = w.box[1] = 0; w.box[2] = 256; w.box[3] = 128;
                    w.ui[0] = 0.06f / (mpp * (UiWristSize * UiWristScale[p] > 0.01f ? UiWristSize * UiWristScale[p] : 1.0f));
                    w.ui[1] = w.ui[0] * 0.5f;
                }
            }
            ++w_carried[p];
            want[nWant++] = w;
            continue;
        }
        if (!pub) { ++w_notPub[p]; continue; }
        // STAGE B: the weapon box - hidden while an item is in either hand; drawn on the gun by DrawInGame (this submit) = no quad
        if (p == kWeaponPiece && (g_itemInHand || g_weaponOnGun)) { ++w_onBone[p]; continue; }
        // hand OFF (2) = hidden: the router has already taken the piece off the eyes; no quad (spec Q4 "Hidden")
        if (UiWristHand[p] != 0 && UiWristHand[p] != 1) { ++w_off[p]; continue; }
        const Hand hand = UiWristHand[p] == 0 ? kLeftHand : kRightHand;
        w.hand = hand;
        w.local.orientation = PieceOrientation(p);
        w.local.position = { UiWristX[p], UiWristY[p], UiWristZ[p] };
        // The anchor in appSpace: the controller's grip (anchor 0), or the DRAWN forearm / hand (1 / 2, arm anchor - then the quad
        // is submitted in appSpace at anchor x offset). No anchor this submit = not drawn (counted).
        XrPosef anchor{};
        if (UiWristAnchor[p] == 1 || UiWristAnchor[p] == 2) {
            // STAGE A: drawn IN THE GAME PICTURE by DrawInGame (D-358: no compositor quad for an arm-anchored piece)
            ++w_onBone[p];
            continue;
        } else {
            const long fl = ControllerGripFlags(hand);
            const unsigned long long need = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
            if (fl < 0 || (static_cast<unsigned long long>(fl) & need) != need || !GetControllerGripPose(hand, anchor) || !haveHead) {
                ++w_untracked[p];
                continue;
            }
        }
        // the quad's centre and front (+Z) in appSpace
        const XrPosef pieceApp = w.inApp ? w.local : PoseMul(anchor, w.local);
        const float c[3] = { pieceApp.position.x, pieceApp.position.y, pieceApp.position.z };
        const float z[3] = { 0.0f, 0.0f, 1.0f };
        float n[3];
        Rot(pieceApp.orientation, z, n);
        float d[3] = { head[0] - c[0], head[1] - c[1], head[2] - c[2] };
        const float dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (dl < 1e-4f) { ++w_untracked[p]; continue; }
        float cosA = (n[0] * d[0] + n[1] * d[1] + n[2] * d[2]) / dl;
        cosA = cosA > 1.0f ? 1.0f : cosA < -1.0f ? -1.0f : cosA;
        const float ang = std::acos(cosA) * 57.29578f;
        if (!w_angSet[p] || ang < w_angMin[p]) { w_angMin[p] = ang; }
        if (!w_angSet[p] || ang > w_angMax[p]) { w_angMax[p] = ang; }
        w_angSet[p] = true;
        float f = 1.0f;
        if (ang >= fadeEnd) { f = 0.0f; }
        else if (ang > fadeStart) {
            const float t = (fadeEnd - ang) / (fadeEnd - fadeStart);
            f = t * t * (3.0f - 2.0f * t);
        }
        if (f < 0.01f) { ++w_faded[p]; continue; }
        w.fade = f;
        want[nWant++] = w;
    }
    // STAGE 3b: while a piece is placed (and has appeared), its box + a beam per tracked controller follow the pieces in the list.
    const bool placing = g_placePiece >= 0 && !g_appearPending;
    int extras = placing ? 1 + (g_beamOk[0] ? 1 : 0) + (g_beamOk[1] ? 1 : 0) : 0;
    if (nWant == 0 && extras == 0) { return 0; }
    if (room < nWant) { w_noRoom += nWant - (room > 0 ? room : 0); nWant = room > 0 ? room : 0; }
    if (room - nWant < extras) { w_noRoom += extras - (room - nWant > 0 ? room - nWant : 0); extras = room - nWant > 0 ? room - nWant : 0; }
    if ((nWant == 0 && extras == 0) || !EnsureSwapchain()) { return 0; }

    XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    uint32_t index = 0;
    if (XR_FAILED(xrAcquireSwapchainImage_(g_sc, &ai, &index)) || index >= g_images.size()) { ++w_xrFail; return 0; }
    XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wi.timeout = XR_INFINITE_DURATION;
    const bool waited = XR_SUCCEEDED(xrWaitSwapchainImage_(g_sc, &wi));
    int done[kP] = {};
    int cw[kP] = {}, ch[kP] = {}, cs[kP] = {};
    int nDone = 0;
    if (waited) {
        for (int i = 0; i < nWant; ++i) {
            const Want& w = want[i];
            // the whole box into the 1024 x 512 slot by an integer step (a high-resolution headset's piece is stepped, never cut);
            // w_clipped counts pieces that NEEDED a step > 1
            if (!uipanel::FitStep(w.box[2], w.box[3], kSlotW, kSlotH, &cw[i], &ch[i], &cs[i])) { ++w_copyFail[w.p]; continue; }
            if (cs[i] > 1) { ++w_clipped[w.p]; }
            if (w.outline) {   // STAGE 3b: the carried piece the game is not drawing - a frame in its own slot
                if (DrawOutline(index, w.p * kSlotH, cw[i], ch[i])) { done[nDone++] = i; } else { ++w_copyFail[w.p]; }
                continue;
            }
            const int rc = g_copy(g_images[index].texture, w.p, 0, w.p * kSlotH, cw[i], ch[i], w.fade, cs[i]);
            ++w_rc[(rc >= 0 && rc < 7) ? rc : 7];
            if (rc != 0) { ++w_copyFail[w.p]; continue; }
            done[nDone++] = i;
        }
    }
    // STAGE 3b: the placed piece's size (the hit test and the box read it), then the box drawn into slot 5 at the piece's aspect.
    const auto sizeOf = [&](int i, float& wM, float& hM) {
        const Want& w = want[i];
        const float size = UiWristSize * UiWristScale[w.p];
        wM = w.ui[0] * mpp * size;   // the whole box is in the slot (FitStep), so the quad is the whole piece's width
        hM = wM * static_cast<float>(ch[i]) / static_cast<float>(cw[i] > 0 ? cw[i] : 1);
    };
    int fw = 0, fh = 0;
    bool frameOk = false;
    if (waited && placing && extras > 0) {
        for (int i = 0; i < nWant; ++i) {
            if (want[i].p == g_placePiece && cw[i] > 0 && ch[i] > 0) { sizeOf(i, g_placeW, g_placeH); }
        }
        const float bw = g_placeW + 2.0f * kFrameMarginM, bh = g_placeH + 2.0f * kFrameMarginM;
        fw = kSlotW;
        fh = static_cast<int>(static_cast<float>(kSlotW) * bh / (bw > 1e-4f ? bw : 1e-4f));
        if (fh > kSlotH) { fw = static_cast<int>(static_cast<float>(kSlotH) * bw / (bh > 1e-4f ? bh : 1e-4f)); fh = kSlotH; }
        fw = fw < 16 ? 16 : fw;
        fh = fh < 16 ? 16 : fh;
        frameOk = DrawOutline(index, kFrameSlot * kSlotH, fw, fh);
    }
    XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    xrReleaseSwapchainImage_(g_sc, &ri);
    if (!waited) { ++w_xrFail; return 0; }

    int nOut = 0;
    for (int k = 0; k < nDone; ++k) {
        const int i = done[k];
        const Want& w = want[i];
        const Hand hand = w.hand;   // the wearing hand, or the carrying controller for the placed piece (STAGE 3b)
        const XrSpace sp = w.inApp ? appSpace : ControllerGripSpace(hand);
        if (sp == XR_NULL_HANDLE) { ++w_noSpace; continue; }
        float widthM = 0.0f, heightM = 0.0f;
        sizeOf(i, widthM, heightM);
        XrCompositionLayerQuad& q = out[nOut++];
        q = XrCompositionLayerQuad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
        q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;   // the pass is premultiplied (as the HUD panel)
        q.space = sp;                                                         // held on the controller by the runtime (or the room, 3b)
        q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;                             // one quad for both eyes (spec §5, U-68)
        q.subImage.swapchain = g_sc;
        q.subImage.imageArrayIndex = 0;
        q.subImage.imageRect.offset = { 0, w.p * kSlotH };
        q.subImage.imageRect.extent = { cw[i], ch[i] };
        q.pose = w.local;
        // The controller grip offset (trace 20261116_1750_grip_offset_sliders): the getters return the shifted grip, so a quad held
        // IN the raw grip space takes the same grip-local offset - a translation in that frame - to ride the shifted hand.
        if (!w.inApp) {
            float ho[3];
            if (RigHandOffsetLocalM(hand, ho)) {
                q.pose.position.x += ho[0];
                q.pose.position.y += ho[1];
                q.pose.position.z += ho[2];
            }
        }
        q.size = { widthM, heightM };
        ++w_shown[w.p];
        if (!g_saidFirst) {
            g_saidFirst = true;
            // [PROBE: WRISTQUAD] gate=ui_wrist_hud (read at launch)  site=HEADSET  log-only=no
            LOG("[WRISTQUAD] FIRST quad submitted (logged unconditionally): %s on the %s hand's grip space at (%.3f %.3f %.3f) m, pitch "
                "%.0f yaw %.0f roll %.0f deg, %.3f x %.3f m (size %.2f x scale %.2f; box %dx%d layer px, %.0f x %.0f UI px, slot rect "
                "0,%d %dx%d), fade %.2f", kTag[w.p], hand == kLeftHand ? "LEFT" : "RIGHT", static_cast<double>(UiWristX[w.p]),
                static_cast<double>(UiWristY[w.p]), static_cast<double>(UiWristZ[w.p]), static_cast<double>(UiWristPitch[w.p]),
                static_cast<double>(UiWristYaw[w.p]), static_cast<double>(UiWristRoll[w.p]), static_cast<double>(widthM),
                static_cast<double>(heightM), static_cast<double>(UiWristSize), static_cast<double>(UiWristScale[w.p]), w.box[2],
                w.box[3], static_cast<double>(w.ui[0]), static_cast<double>(w.ui[1]), w.p * kSlotH, cw[i], ch[i],
                static_cast<double>(w.fade));
        }
    }

    // STAGE 3b: the box (same pose and space as the placed piece, kFrameMarginM bigger each side) and the beams (appSpace: a kBeamW
    // strip from each tracked controller's aim along its forward, to the piece when on it else kBeamM, turned to face the head; its
    // texels = the box's top border strip, white).
    if (placing && frameOk && extras > 0) {
        XrPosef piece{};
        const XrSpace pieceSp = (g_carrier == 0 || g_carrier == 1) ? ControllerGripSpace(g_carrier == 0 ? kLeftHand : kRightHand) : appSpace;
        piece = (g_carrier == 0 || g_carrier == 1) ? g_carryOff : g_floatPose;
        if (g_carrier == 0 || g_carrier == 1) {   // held in the raw grip space: the grip offset, as the pieces above
            float ho[3];
            if (RigHandOffsetLocalM(g_carrier == 0 ? kLeftHand : kRightHand, ho)) {
                piece.position.x += ho[0];
                piece.position.y += ho[1];
                piece.position.z += ho[2];
            }
        }
        if (pieceSp != XR_NULL_HANDLE && extras > 0 && nOut < kMaxQuads) {
            XrCompositionLayerQuad& q = out[nOut++];
            q = XrCompositionLayerQuad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
            q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            q.space = pieceSp;
            q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            q.subImage.swapchain = g_sc;
            q.subImage.imageRect.offset = { 0, kFrameSlot * kSlotH };
            q.subImage.imageRect.extent = { fw, fh };
            q.pose = piece;
            q.size = { g_placeW + 2.0f * kFrameMarginM, g_placeH + 2.0f * kFrameMarginM };
            --extras;
        }
        for (int h = 0; h < 2 && extras > 0 && nOut < kMaxQuads; ++h) {
            if (!g_beamOk[h] || !haveHead) { continue; }
            const float fz[3] = { 0.0f, 0.0f, -1.0f };
            float Y[3];
            Rot(g_aim[h].orientation, fz, Y);   // the strip's long axis = the aim's forward
            const float o[3] = { g_aim[h].position.x, g_aim[h].position.y, g_aim[h].position.z };
            const float L = g_beamLen[h];
            const float c[3] = { o[0] + Y[0] * L * 0.5f, o[1] + Y[1] * L * 0.5f, o[2] + Y[2] * L * 0.5f };
            const float th[3] = { head[0] - c[0], head[1] - c[1], head[2] - c[2] };
            float X[3] = { Y[1] * th[2] - Y[2] * th[1], Y[2] * th[0] - Y[0] * th[2], Y[0] * th[1] - Y[1] * th[0] };   // Y x toHead
            const float xl = std::sqrt(X[0] * X[0] + X[1] * X[1] + X[2] * X[2]);
            if (xl < 1e-5f) { continue; }   // looking straight down the beam: nothing to draw
            X[0] /= xl; X[1] /= xl; X[2] /= xl;
            const float Z[3] = { X[1] * Y[2] - X[2] * Y[1], X[2] * Y[0] - X[0] * Y[2], X[0] * Y[1] - X[1] * Y[0] };   // X x Y, toward the head
            XrCompositionLayerQuad& q = out[nOut++];
            q = XrCompositionLayerQuad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
            q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            q.space = appSpace;
            q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            q.subImage.swapchain = g_sc;
            q.subImage.imageRect.offset = { 0, kFrameSlot * kSlotH };
            q.subImage.imageRect.extent = { fw, 4 };   // the box's top border: solid white
            q.pose.orientation = QuatFromBasis(X, Y, Z);
            q.pose.position = { c[0], c[1], c[2] };
            q.size = { kBeamW, L };
            --extras;
        }
    }

    // Added by AngelWolf: 2026-11-26
    // Mirror the Stage 2 wrist quads + Stage 3b box/beams onto the desktop window (right eye, kMirrorView == 1)
    if (nOut > 0 && g_device && index < g_images.size() && viewsValid && g_declOk[1]) {
        for (int qi = 0; qi < nOut; ++qi) {
            XrCompositionLayerQuad mq = out[qi];
            if (mq.space != appSpace) {
                const Hand h = (mq.space == ControllerGripSpace(kLeftHand)) ? kLeftHand : kRightHand;
                XrPosef grip{};
                if (!GripTracked(h, grip)) { continue; }
                mq.pose = PoseMul(grip, mq.pose);
                mq.space = appSpace;
            }
            ::dlvr::deskmirror::DrawQuadLayer(g_device, g_images[index].texture, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                mq, eyes[1], g_declFov[1], 1);
        }
    }

    return nOut;
}

// ===== STAGE A - THE ARM PIECES DRAWN IN THE GAME PICTURE (trace docs/traces/20261113_1300_wrist_ingame_stageA.md) =====
// From the layer's submit, the eye image still acquired, after the AFW rebuild and the HUD lay-on (ovr2xr.cpp). Every piece whose
// anchor is 1 (forearm) / 2 (hand) and whose hand is on: one record (bone, M = Bind_ref x offset, size, fade, its pieces-target box);
// the bootstrap draws it into both eyes with the drawn palette rows of that image and the eye's camera. Submit thread.
// COST: per submit <= 5 x (one PieceInfo + ~60 flops) + one export call; per 5 s one readback read + 2 rig reads (the self-check).
namespace {
long d_calls = 0, d_pieces = 0, d_drawn = 0, d_refused = 0, d_noBind = 0, d_noPub = 0;
long d_hidden = 0, d_onGun = 0, d_gunNoBone = 0;   // STAGE B: weapon box hidden (item in hand) / on the gun / placed but no gun rows
ULONGLONG d_next = 0;
bool d_saidFirst = false, d_saidNoExport = false;

void QuatToRows(const XrQuaternionf& q, float r[9])
{
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    r[0] = 1 - 2 * (y * y + z * z); r[1] = 2 * (x * y - z * w);     r[2] = 2 * (x * z + y * w);
    r[3] = 2 * (x * y + z * w);     r[4] = 1 - 2 * (x * x + z * z); r[5] = 2 * (y * z - x * w);
    r[6] = 2 * (x * z - y * w);     r[7] = 2 * (y * z + x * w);     r[8] = 1 - 2 * (x * x + y * y);
}

// SELF-CHECK (independent source - the rig's own published elbow / wrist, RigArmAnchorRead): the drawn forearm / hand origins.
// Pre-registered (trace): < 0.010 m when the arm is still (the readback is 1-2 frames older than the rig's latest).
void SelfCheck()
{
    Arm a{};
    const bool ok = ReadArm(a);
    char side[2][160] = {};
    for (int h = 0; h < 2; ++h) {
        float Ff[12], Fh[12], E[3], F[9], W[3], H[9];
        const bool drawn = ok && DrawnFrame(a, h, 1, h * 4 + 0, Ff) && DrawnFrame(a, h, 2, h * 4 + 3, Fh);
        const bool rig = RigArmAnchorRead(h == 0 ? 1 : 0, E, F, W, H, nullptr);
        if (drawn && rig) {
            const float de = std::sqrt((Ff[3] - E[0]) * (Ff[3] - E[0]) + (Ff[7] - E[1]) * (Ff[7] - E[1]) + (Ff[11] - E[2]) * (Ff[11] - E[2]));
            const float dw = std::sqrt((Fh[3] - W[0]) * (Fh[3] - W[0]) + (Fh[7] - W[1]) * (Fh[7] - W[1]) + (Fh[11] - W[2]) * (Fh[11] - W[2]));
            _snprintf_s(side[h], sizeof(side[h]), _TRUNCATE, "elbow %.4f m, wrist %.4f m", static_cast<double>(de), static_cast<double>(dw));
        } else {
            _snprintf_s(side[h], sizeof(side[h]), _TRUNCATE, "NOT COMPARED (drawn %s, rig %s)", drawn ? "ok" : "none", rig ? "ok" : "none");
        }
    }
    // [PROBE: WRISTARM] gate=ui_wrist_hud (read at launch)  site=HEADSET  log-only=no
    LOG("[WRISTARM] SELF-CHECK drawn vs the rig's own (pre-registered < 0.010 m still): LEFT %s | RIGHT %s | rows age %d ms, bones 0x%02X, "
        "rest frames 0x%02X", side[0], side[1], a.age, a.mask, a.bindOk);
}
}  // namespace

// FIX ROUND 2: each eye's DECLARED pose + FOV at this submit (ovr2xr.cpp, right after projViews[e].fov) - the placement's eye.
void NoteDeclared(int eye, const XrPosef& pose, const XrFovf& fov)
{
    if (!g_on || eye < 0 || eye > 1) { return; }
    g_declPose[eye] = pose;
    g_declFov[eye] = fov;
    g_declOk[eye] = true;
}

void DrawInGame(void* eyeTex, const int vpA[4], const int vpB[4], bool afw, bool rebuilt, int held, bool prevIsHeld, float sep)
{
    if (!g_on || !eyeTex || !vpA || !vpB || !Resolve()) { return; }
    const ULONGLONG now = GetTickCount64();
    // FIX ROUND 2: the placement's camera pair - the game camera of the image this submit carries for its rendered eye (AFW: the fresh
    // eye = not the held one; off: the newest latch's own label), paired below with that eye's declared pose + FOV (NoteDeclared).
    {
        int lbl = -1;
        g_camOk = g_cam && g_cam(g_camVp, &lbl) == 1;
        g_camEye = afw ? (held == 1 ? 0 : 1) : lbl;
    }
    // STAGE B: what the hands hold (the weapon box's hide + its gun), before the draw export test so the hide works on the quad path too
    HandsTick();
    g_weaponOnGun = false;
    if (!g_draw) {
        if (!d_saidNoExport) {
            d_saidNoExport = true;
            // [PROBE: WRISTARM] gate=ui_wrist_hud (read at launch)  site=HEADSET  log-only=no
            LOG("[WRISTARM] layer: DlvrWristDrawPieces MISSING - no piece is drawn in the game picture (an older xinput1_3.dll)");
        }
        return;
    }
    ++d_calls;
    DrawPiece pc[kP];
    int n = 0;
    const float mpp = MetresPerUiPxNow();   // 0 = the UI-screen height not published yet: the piece counts as not published
    Arm binds{};
    const bool haveBinds = ReadBinds(binds);
    for (int p = 0; p < kP; ++p) {
        if (p == g_placePiece) { continue; }                                  // carried / floating: Render's quad
        if (UiWristHand[p] != 0 && UiWristHand[p] != 1) { continue; }
        // STAGE B: the weapon box - hidden while an item is in either hand; on the gun when its family has a place
        if (p == kWeaponPiece && g_itemInHand) { ++d_hidden; continue; }
        if (p == kWeaponPiece && g_rightIsGun) {
            const GunPlace* gp = FindGunPlace(g_heldFam);
            if (gp && haveBinds && (binds.bindOk & (1u << kGunBoneR))) {
                int box[4] = {};
                float ui[2] = {};
                if (g_info(p, box, ui) != 0 || box[2] <= 0 || box[3] <= 0 || !(mpp > 0.0f)) { ++d_noPub; continue; }
                DrawPiece& d = pc[n];
                d = DrawPiece{};
                d.piece = p;
                d.bone = kGunBoneR;
                float R[9];
                QuatToRows(Mul(Mul(Axis(1, gp->yaw), Axis(0, gp->pitch)), Axis(2, gp->roll)), R);
                const float off[12] = { R[0], R[1], R[2], gp->pos[0], R[3], R[4], R[5], gp->pos[1], R[6], R[7], R[8], gp->pos[2] };
                Mul34(binds.bind[kGunBoneR], off, d.M);
                const float size = UiWristSize * UiWristScale[p];
                d.halfW = 0.5f * ui[0] * mpp * size;
                d.halfH = d.halfW * static_cast<float>(box[3]) / static_cast<float>(box[2]);
                d.fadeStart = UiWristFadeStartDeg;
                d.fadeEnd = UiWristFadeEndDeg;
                for (int k = 0; k < 4; ++k) { d.box[k] = box[k]; }
                ++n;
                g_weaponOnGun = true;
                ++d_onGun;
                continue;
            }
            if (gp) { ++d_gunNoBone; }
        }
        if (UiWristAnchor[p] != 1 && UiWristAnchor[p] != 2) { continue; }
        int box[4] = {};
        float ui[2] = {};
        if (g_info(p, box, ui) != 0 || box[2] <= 0 || box[3] <= 0 || !(mpp > 0.0f)) { ++d_noPub; continue; }
        const int h = UiWristHand[p];
        const int ref = h * 4 + (UiWristAnchor[p] == 1 ? 0 : 3);
        if (!haveBinds || !(binds.bindOk & (1u << ref))) { ++d_noBind; ++w_noBone[p]; continue; }
        DrawPiece& d = pc[n];
        d = DrawPiece{};
        d.piece = p;
        d.bone = UiWristAnchor[p] == 2 ? h * 4 + 3 : TwistBone(binds, h, UiWristX[p]);
        float R[9];
        QuatToRows(PieceOrientation(p), R);
        const float off[12] = { R[0], R[1], R[2], UiWristX[p], R[3], R[4], R[5], UiWristY[p], R[6], R[7], R[8], UiWristZ[p] };
        Mul34(binds.bind[ref], off, d.M);
        const float size = UiWristSize * UiWristScale[p];
        d.halfW = 0.5f * ui[0] * mpp * size;
        d.halfH = d.halfW * static_cast<float>(box[3]) / static_cast<float>(box[2]);
        d.fadeStart = UiWristFadeStartDeg;
        d.fadeEnd = UiWristFadeEndDeg;
        for (int k = 0; k < 4; ++k) { d.box[k] = box[k]; }
        ++n;
    }
    d_pieces += n;
    if (n > 0) {
        float v[12] = {};
        unsigned age = 0;
        float cam[3] = {};
        if (CamHookGetRenderedViewMatrix(v, &age)) { cam[0] = v[3]; cam[1] = v[7]; cam[2] = v[11]; }
        const int flags = (afw ? 1 : 0) | (rebuilt ? 2 : 0) | (held == 1 ? 4 : 0) | (prevIsHeld ? 8 : 0);   // bit 3: trace 20261114_1315
        const int rc = g_draw(eyeTex, vpA, vpB, flags, sep, cam, pc, n);
        if (rc >= 0) { d_drawn += rc; } else { ++d_refused; }
        if (!d_saidFirst) {
            d_saidFirst = true;
            // [PROBE: WRISTARM] gate=ui_wrist_hud (read at launch)  site=HEADSET  log-only=no
            LOG("[WRISTARM] layer: FIRST in-picture draw call (logged unconditionally): %d piece(s), first %s on bone %d, %.3f x %.3f m | "
                "AFW %d rebuilt %d held %d sep %.4f | bootstrap returned %d", n, kTag[pc[0].piece], pc[0].bone,
                static_cast<double>(2.0f * pc[0].halfW), static_cast<double>(2.0f * pc[0].halfH), afw ? 1 : 0, rebuilt ? 1 : 0, held,
                static_cast<double>(sep), rc);
        }
    }
    if (!d_next) { d_next = now + 5000; }
    if (now >= d_next) {
        d_next = now + 5000;
        // [PROBE: WRISTARM] gate=ui_wrist_hud (read at launch)  site=HEADSET  log-only=no
        LOG("[WRISTARM] layer 5 s: calls %ld, pieces sent %ld, drawn (both eyes) %ld, refused calls %ld | not sent: no rest frame %ld, "
            "not published %ld | WEAPON BOX: hidden (an item in hand) %ld, on the gun %ld, gun placed but its rows not read %ld | now: "
            "right '%s' left '%s', item in hand %d, right is a gun %d, place key %s, gun places %d", d_calls, d_pieces, d_drawn, d_refused,
            d_noBind, d_noPub, d_hidden, d_onGun, d_gunNoBone, g_handMesh[0], g_handMesh[1], g_itemInHand ? 1 : 0, g_rightIsGun ? 1 : 0,
            g_heldFam, g_nGunPlace);
        d_calls = d_pieces = d_drawn = d_refused = d_noBind = d_noPub = 0;
        d_hidden = d_onGun = d_gunNoBone = 0;
        SelfCheck();
    }
}

}  // namespace wristquad
}  // namespace dlvr
