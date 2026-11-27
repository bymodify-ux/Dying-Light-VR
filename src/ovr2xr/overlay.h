// In-headset settings overlay: Dear ImGui on an OpenXR quad layer.
//
// Drawn into its own swapchain and submitted as a head-locked quad on top of
// the game's projection layer, so it needs nothing from the engine and cannot
// disturb what the engine renders.
//
// Input is gamepad and keyboard only. Motion controllers are not wired up yet,
// so there is no pointer to aim with; ImGui's gamepad/keyboard navigation gives
// a fully usable menu without one, and drops out cleanly if controllers arrive
// later.
#pragma once

#include "xrloader.h"

namespace dlvr {

// Creates the ImGui context, its D3D11 objects and the quad swapchain. Safe to
// call every frame; does the work once. Returns false if the overlay cannot be
// brought up, in which case the caller carries on without it.
bool OverlayInit(ID3D11Device* device, XrSession session);

// Reads the bindings and flips visibility on a fresh press. Call once a frame.
void OverlayPoll();

bool OverlayVisible();

// The device OverlayInit was given (no reference added - do not Release it), or nullptr before OverlayInit / after
// OverlayShutdown. Render thread (calibassets.cpp's texture creation, build D).
ID3D11Device* OverlayDevice();

// True when the ImGui quad render targets are _SRGB views (the runtime's images were not TYPELESS - the fallback), false when they
// are plain UNORM. Set by OverlayInit's swapchain creation; true before it. calibassets.cpp picks its art textures' format from it.
bool OverlayQuadRtvIsSrgb();

// True while the overlay is up and the left trigger is held: the right stick is
// let through to the game so gamepad look can be checked with the panel open.
bool OverlayPassLook();

// The aim reticle's on/off, exposed because the settings registry in ovr2xr.cpp
// binds `show_reticle` directly to it. Drawn only while the panel is CLOSED,
// and it shares the panel's swapchain - so leaving it on means the overlay's
// render path still runs every frame with the menu shut.
extern bool g_showReticle;

// Builds and renders one frame of UI, then fills `out` with a quad layer to
// append to the frame's layer list. Returns false if there is nothing to
// submit. The panel is held in the room (`appSpace`, placed at open in front of
// the head's level facing - trace 20261124_1855); `viewSpace` only until that
// anchor latches. `appSpace` is also used, world-locked, for the aim reticle
// (see its own comment) when the panel itself is closed.
bool OverlayRender(XrSpace viewSpace, XrSpace appSpace, XrTime displayTime,
                   XrCompositionLayerQuad* out);

// fire_reticle_3d: the two eye poses THIS frame's projection layer is declared with (appSpace, after the auto-pitch),
// set by ovr2xr.cpp just before OverlayRender - the gun dot is placed relative to them so it moves with the game image.
// `valid` false = no projection layer this frame (nothing to anchor to, no dot). Render thread only.
void OverlaySetDeclaredViews(bool valid, const XrPosef& left, const XrPosef& right, const XrFovf& rightFov);

// grapple_reticle_3d: the grapple's icon on its own quad (world-locked at the grapple target). False = nothing to submit.
// Call after OverlayRender, same frame. Not drawn while the settings panel is open.
bool OverlayRenderGrapple(XrSpace appSpace, XrCompositionLayerQuad* out);

// Calibration screen (build A): renders `draw` into a THIRD quad swapchain (created on first use, kPanelW x kPanelH) and
// fills `out` as a head-locked quad in `viewSpace` at `pose`, `sizeM` wide. ImGui is the 2D rasteriser only - no window.
// False = nothing to submit (not initialised, swapchain refused - logged once). Render thread, same frame as OverlayRender.
using OverlayDrawFn = void (*)(void* drawList, float w, float h, const void* user);
bool OverlayRenderCalibQuad(XrSpace viewSpace, const XrPosef& pose, float sizeM, OverlayDrawFn draw, const void* user,
                            XrCompositionLayerQuad* out);
// Build B: the floor under the calibration void - a FOURTH swapchain, a square `sizeM` x `sizeM` quad in `floorSpace` at `pose`.
bool OverlayRenderCalibFloor(XrSpace floorSpace, const XrPosef& pose, float sizeM, OverlayDrawFn draw, const void* user,
                             XrCompositionLayerQuad* out);
// Build D: the floor from Jay's art - a FIFTH swapchain at `src`'s size (R8G8B8A8_UNORM_SRGB, 1 mip), filled per submit by
// CopyResource (one 16 MB GPU copy at 2048 x 2048). False = nothing to submit this frame. A refusal is logged once and
// OverlayCalibFloorImageRefused() then stays true: the caller falls back to OverlayRenderCalibFloor for the session.
bool OverlayRenderCalibFloorImage(XrSpace floorSpace, const XrPosef& pose, float sizeM, ID3D11Texture2D* src,
                                  XrCompositionLayerQuad* out);
bool OverlayCalibFloorImageRefused();
// The void's walls (ceiling false: a SIXTH swapchain) or ceiling (true: a SEVENTH) from Jay's videos (2026-10-16): the floor
// image's pattern - one CopyResource per call, then `n` square `sizeM` quads in `floorSpace`, one per pose, all showing that
// one image. False = nothing to submit; a refusal is logged once and that surface is not drawn for the session.
bool OverlayRenderCalibVoidImage(bool ceiling, XrSpace floorSpace, const XrPosef* poses, int n, float sizeM,
                                 ID3D11Texture2D* src, XrCompositionLayerQuad* out);

// Holster place-by-hand (plan 2026-10-30 Task 4): the zone the Holsters page has selected (0 shoulder, 1 right hip, 2 left hip,
// 3 head; -1 none). Defined in overlay.cpp (ruling R1), set by the page (Task 5). Render thread only.
extern int g_holsterShowZone;
// That zone's sphere: a square quad 2 x radius wide in `appSpace` at HolsterZoneWorld's point, facing the head, on its own
// swapchain (an EIGHTH, created on first use; a refusal is logged once by name). Drawn while g_holsterShowZone >= 0 and the
// panel is open - including while placing, when OverlayRender skips the panel quad itself. Submitted BEFORE the panel quad
// (ruling R11: the panel draws on top); it starts no ImGui frame, so its order against OverlayRender / OverlayRenderToMirror
// does not matter to them. Same frame. False = nothing to submit. COST when no zone is selected: one int test.
bool OverlayRenderHolsterSphere(XrSpace appSpace, XrCompositionLayerQuad* out);
// The visible holster rings (plan 2026-11-20-holster-pivot-posture-rings Task 5): up to maxOut (<= 3) quads in `appSpace` - each
// holster its hand comes near, option B, fading in over 0.30 m (holster.cpp HolsterRingTargets). None while holster_rings is off,
// the panel is open / a placement sphere shows, or in a menu / pause / calibration / vehicle / cutscene. STARTS AN IMGUI FRAME per
// ring (never with the panel open). Returns the quads written. Render / submit thread.
int OverlayRenderHolsterRings(XrSpace appSpace, XrCompositionLayerQuad* out, int maxOut);

// The mod menu's title (dlvr\menu\title.png) on its own quad ABOVE the panel's top-left corner, at the panel's pixel
// scale (Jay 2026-11-11). Only while the panel is drawn and title.png loaded. No ImGui frame (own ImDrawList, as the sphere), so
// its order against OverlayRender does not matter. False = nothing to submit. COST menu closed: two bool tests.
// Held in the room with the panel (appSpace, the menu anchor - trace 20261124_1855); viewSpace until the anchor latches.
bool OverlayRenderTitle(XrSpace viewSpace, XrSpace appSpace, XrTime time, XrCompositionLayerQuad* out);

// Draws the same frame again into the desktop mirror texture, so the panel is
// visible to a screenshot. The quad layer never reaches the flat window.
void OverlayRenderToMirror(ID3D11Texture2D* dst);

// Desktop mirror, one eye (deskmirror.cpp): draws `srv`'s rectangle uv = (u0, v0, u1, v1) over the whole of `rtv` (w x h px), opaque,
// through the overlay's ImGui DX11 backend on its own draw list - no ImGui frame (as the holster sphere / title). The backend restores
// the context state it sets; the target is left bound, as every overlay pass leaves its own. Render thread only.
// 0 = drawn; 1 = not drawn, the overlay is not initialised (or a null / empty argument). (2 = "no ImGui frame yet" is no longer
// returned since 2026-11-14: before the first frame the backend's objects are created here instead.) 3 = not drawn, the premultiplied
// blend state could not be made. `premultiplied` (2026-11-14, the menu panel in the window): ONE / INV_SRC_ALPHA over the target
// instead of the opaque write.
int OverlayDrawImageToTarget(ID3D11RenderTargetView* rtv, float w, float h, ID3D11ShaderResourceView* srv, const float uv[4],
                             bool premultiplied = false);

// Added by AngelWolf 2026-11-26
// Draws `quadCount` projected 4-corner quads onto `rtv` (target size w x h) in one ImGui draw call.
int OverlayDrawQuadToTarget(ID3D11RenderTargetView* rtv, float w, float h, ID3D11ShaderResourceView* srv,
    const float* pts, const float* uvs, int blendMode = 2, int quadCount = 1);

void OverlayShutdown();

}  // namespace dlvr
