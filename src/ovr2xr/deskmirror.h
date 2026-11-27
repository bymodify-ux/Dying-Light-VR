#pragma once
// DESKTOP MIRROR, ONE EYE (Jay 2026-11-14: "The desktop window should show what the headset sees"; "1. Single eye. 2. Start without,
// include later. 3. 60FPS cap for the window ONLY is fine, but if it affects the refresh rate in HEADSET, then we'll have it set to the
// same AS the headset"). Trace docs/traces/20261114_1030_desktop_mirror_single_eye.md.
//
// The engine copies the mirror texture into its desktop back buffer after every VR submit and presents it (rd3d11 opcode 0x30, INV-379),
// so the mirror texture IS the window's picture, stretched to the window by DXGI. This fills it with the LEFT view's finished image (the
// image and rectangle the headset is given for view 0), cropped to the window's shape, through the overlay's ImGui DX11 backend.
// The headset's own add-ons (wrist HUD, HUD layer, grapple, calibration quads) are compositor layers and are NOT in it (Jay: later).
//
// Setting `mirror` (Timing tab): on = live, at most kLivePerSec per second. Off = only the overlay's backdrop (every 8th submit while the
// mod menu is open, the rule ovr2xr.cpp has always had). Render thread only (ovr_SubmitFrame).
#include <d3d11.h>
// Added by AngelWolf 2026-11-26
#include <openxr/openxr.h>

namespace dlvr {
namespace deskmirror {

// Which headset view the window shows: 1 = the RIGHT eye (OpenXR PRIMARY_STEREO view 1; Jay 2026-11-16: "change the desktop mirror to
// the right eye instead"; trace docs/traces/20261116_1925_desktop_mirror_right_eye.md). The submit loop still draws in view 0's pass
// (before any chain is released) but takes this view's chain, rectangle and game eye; DrawPanel takes this view's half of the panel.
constexpr int kMirrorView = 1;

// Why a submit draws: 0 = not this submit, 1 = live (mirror on, the 60 per second clock is due), 2 = the overlay's backdrop. Also keeps
// the 30 s [DESKMIRROR] line. COST: off and the menu shut = not called; else one QueryPerformanceCounter.
int Due(bool live, bool backdropDue);

// Draws `shown` (the texture the view-0 image is in; nullptr = none this submit - counted, nothing drawn) region `rect` (x, y, w, h) into
// `mirror`, cropped to the desktop window's client shape and centred on game eye `eye`'s forward point. `eyeFmt` = the format the
// compositor reads the image with (the swapchain's declared format). `why` = Due()'s answer. True = the mirror now holds this frame.
bool Draw(ID3D11Device* device, ID3D11Texture2D* mirror, ID3D11Texture2D* shown, DXGI_FORMAT eyeFmt, const int rect[4], int eye, int why);

// THE MENU PANEL IN THE WINDOW (Jay 2026-11-14: "just seeing it is enough for now"; trace 20261114_1125). ovr2xr.cpp calls SetFresh
// once per submit, right before uipanel::Render: the mirror texture when Draw refreshed it THIS submit, else nullptr. uipanel's
// RenderMenu then calls DrawPanel while its panel image is acquired (after the copy, before the release): the image's eye-0 half
// (0, 0, w x h - the whole eye pass, premultiplied) is laid over the window with the same crop as the eye. Nothing is drawn when the
// mirror was not refreshed this submit (a translucent menu over a stale picture would darken it frame after frame). Render thread.
void SetFresh(ID3D11Texture2D* mirror);
void DrawPanel(ID3D11Device* device, ID3D11Texture2D* panelImage, int w, int h);

// Added by AngelWolf 2026-11-26
void DrawQuadLayer(ID3D11Device* device, ID3D11Texture2D* tex, DXGI_FORMAT fmt,
    const XrCompositionLayerQuad& quad, const XrPosef& eyePose, const XrFovf& eyeFov, int blendMode = 2);

// Session teardown, before the mirror textures are released: drops every cached view.
void Shutdown();

}  // namespace deskmirror
}  // namespace dlvr
