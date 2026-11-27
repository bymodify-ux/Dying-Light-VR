// DESKTOP MIRROR, ONE EYE - see deskmirror.h for why and what; trace docs/traces/20261114_1030_desktop_mirror_single_eye.md.
#include "deskmirror.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "../common/log.h"
#include "deskmirror_logic.h"
#include "overlay.h"
#include "uipanel.h"

#define LOG(...) ::dlvr::LogTo("dlvr_ovr2xr.log", __VA_ARGS__)

namespace dlvr {
namespace deskmirror {

namespace {

// Jay 2026-11-14: "60FPS cap for the window ONLY is fine, but if it affects the refresh rate in HEADSET, then we'll have it set to the
// same AS the headset". The cap only spaces OUR draws; the engine's window copy + Present(0, 0) run every submit either way.
constexpr int    kLivePerSec     = 60;
constexpr double kReportEverySec = 30.0;
constexpr double kWindowEverySec = 2.0;   // how often the window handle is looked up again
constexpr int    kSrvCache       = 8;

int64_t g_freq = 0;
RateGate g_gate;

// The desktop window: the largest visible, unowned top-level window of this process (the engine's swap chain window).
HWND    g_hwnd = nullptr;
int64_t g_hwndAt = 0;
bool    g_hwndEverFound = false;

struct SrvEntry { ID3D11Texture2D* tex; ID3D11ShaderResourceView* srv; DXGI_FORMAT fmt; };
SrvEntry g_srv[kSrvCache] = {};
int      g_nSrv = 0;
ID3D11Texture2D*        g_rtvTex = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
float g_mirW = 0.0f, g_mirH = 0.0f;

// 30 s tallies (render thread only).
int64_t g_reportAt = 0;
long g_live = 0, g_backdrop = 0, g_noSource = 0, g_noWindow = 0, g_minimised = 0, g_noView = 0, g_noFrame = 0, g_notInit = 0;
long g_dueCalls = 0;
bool g_firstDraw = false;
// Each refusal named ONCE (then counted in the 30 s line).
bool g_saidNoSource = false, g_saidNoWindow = false, g_saidMinimised = false, g_saidNoFrame = false, g_saidNotInit = false;

// The menu panel (trace 20261114_1125): the last Draw's crop as fractions of the view-0 rect, and this submit's fresh mirror.
bool             g_haveCrop = false;
double           g_cropF[4] = { 0.0, 0.0, 1.0, 1.0 };   // x, y, w, h as fractions of the eye rect
ID3D11Texture2D* g_fresh = nullptr;
long g_panelDraws = 0, g_panelFail = 0;
bool g_firstPanel = false, g_saidPanelFail = false;

int64_t Now()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    if (!g_freq) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        g_freq = f.QuadPart > 0 ? f.QuadPart : 1;
    }
    return t.QuadPart;
}

struct FindCtx { DWORD pid; HWND best; long long area; };
BOOL CALLBACK FindProc(HWND h, LPARAM lp)
{
    FindCtx* c = reinterpret_cast<FindCtx*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != c->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER) != nullptr) { return TRUE; }
    RECT r{};
    if (!GetClientRect(h, &r)) { return TRUE; }
    const long long a = static_cast<long long>(r.right - r.left) * static_cast<long long>(r.bottom - r.top);
    if (a >= c->area) { c->area = a; c->best = h; }
    return TRUE;
}

// The window, looked up again at most every kWindowEverySec (or at once when the cached handle died). nullptr = none found.
HWND Window(int64_t now)
{
    if (g_hwnd && IsWindow(g_hwnd) && now - g_hwndAt < static_cast<int64_t>(kWindowEverySec * g_freq)) { return g_hwnd; }
    g_hwndAt = now;
    FindCtx c{ GetCurrentProcessId(), nullptr, -1 };
    EnumWindows(FindProc, reinterpret_cast<LPARAM>(&c));
    if (c.best != g_hwnd && c.best) {
        char cls[64] = {};
        GetClassNameA(c.best, cls, static_cast<int>(sizeof(cls)));
        RECT r{};
        GetClientRect(c.best, &r);
        // [PROBE: DESKMIRROR] gate=mirror (or the mod menu's backdrop)  site=HEADSET  log-only=yes
        LOG("[DESKMIRROR] desktop window %s: class '%s', client %ldx%ld", g_hwndEverFound ? "CHANGED" : "found", cls,
            r.right - r.left, r.bottom - r.top);
        g_hwndEverFound = true;
    }
    g_hwnd = c.best;
    return g_hwnd;
}

ID3D11ShaderResourceView* SrvFor(ID3D11Device* dev, ID3D11Texture2D* tex, DXGI_FORMAT fmt)
{
    for (int i = 0; i < g_nSrv; ++i) {
        if (g_srv[i].tex == tex && g_srv[i].fmt == fmt) { return g_srv[i].srv; }
    }
    if (g_nSrv >= kSrvCache) {   // a new set of swapchain images (a recreated chain): start over
        for (int i = 0; i < g_nSrv; ++i) { if (g_srv[i].srv) { g_srv[i].srv->Release(); } }
        g_nSrv = 0;
    }
    D3D11_TEXTURE2D_DESC td{};
    tex->GetDesc(&td);
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = fmt;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MostDetailedMip = 0;
    sd.Texture2D.MipLevels = 1;
    ID3D11ShaderResourceView* srv = nullptr;
    const HRESULT hr = dev->CreateShaderResourceView(tex, &sd, &srv);
    if (FAILED(hr) || !srv) {
        LOG("[DESKMIRROR] *** the eye image's view could not be made (texture %ux%u format %d, view format %d, hr 0x%08lX) - the "
            "window is NOT refreshed from it ***", td.Width, td.Height, static_cast<int>(td.Format), static_cast<int>(fmt),
            static_cast<unsigned long>(hr));
        return nullptr;
    }
    g_srv[g_nSrv++] = SrvEntry{ tex, srv, fmt };
    return srv;
}

ID3D11RenderTargetView* RtvFor(ID3D11Device* dev, ID3D11Texture2D* mirror)
{
    if (g_rtvTex == mirror && g_rtv) { return g_rtv; }
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    g_rtvTex = nullptr;
    D3D11_TEXTURE2D_DESC td{};
    mirror->GetDesc(&td);
    D3D11_RENDER_TARGET_VIEW_DESC rd{};
    rd.Format = td.Format;
    rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    const HRESULT hr = dev->CreateRenderTargetView(mirror, &rd, &g_rtv);
    if (FAILED(hr) || !g_rtv) {
        LOG("[DESKMIRROR] *** the mirror texture's target view could not be made (%ux%u format %d, hr 0x%08lX) - the window is NOT "
            "refreshed ***", td.Width, td.Height, static_cast<int>(td.Format), static_cast<unsigned long>(hr));
        g_rtv = nullptr;
        return nullptr;
    }
    g_rtvTex = mirror;
    g_mirW = static_cast<float>(td.Width);
    g_mirH = static_cast<float>(td.Height);
    return g_rtv;
}

//Added by AngelWolf 2026-11-26
XrVector3f QuatRotate(const XrQuaternionf& q, const XrVector3f& v)
{
    const float tx = 2.0f * (q.y * v.z - q.z * v.y);
    const float ty = 2.0f * (q.z * v.x - q.x * v.z);
    const float tz = 2.0f * (q.x * v.y - q.y * v.x);
    return XrVector3f{
        v.x + q.w * tx + (q.y * tz - q.z * ty),
        v.y + q.w * ty + (q.z * tx - q.x * tz),
        v.z + q.w * tz + (q.x * ty - q.y * tx)
    };
}

// Added by AngelWolf 2026-11-26
XrVector3f QuatUnrotate(const XrQuaternionf& q, const XrVector3f& v)
{
    const XrQuaternionf inv{ -q.x, -q.y, -q.z, q.w };
    return QuatRotate(inv, v);
}

}  // namespace

int Due(bool live, bool backdropDue)
{
    const int64_t now = Now();
    ++g_dueCalls;
    if (!g_reportAt) { g_reportAt = now; }
    const double since = static_cast<double>(now - g_reportAt) / static_cast<double>(g_freq);
    if (since >= kReportEverySec) {
        LOG("[DESKMIRROR] %.0f s: live draws %ld (%.1f / s, cap %d), menu backdrop draws %ld, of %ld submits | not drawn: no image held "
            "%ld, no window %ld, minimised %ld, no view %ld, no ImGui frame yet %ld, overlay not up %ld | game menu panel draws %ld, "
            "failed %ld",
            since, g_live, static_cast<double>(g_live) / since, kLivePerSec, g_backdrop, g_dueCalls, g_noSource, g_noWindow,
            g_minimised, g_noView, g_noFrame, g_notInit, g_panelDraws, g_panelFail);
        g_reportAt = now;
        g_live = g_backdrop = g_noSource = g_noWindow = g_minimised = g_noView = g_noFrame = g_notInit = 0;
        g_panelDraws = g_panelFail = 0;
        g_dueCalls = 0;
    }
    if (live) {
        return g_gate.Due(now, g_freq / kLivePerSec) ? 1 : 0;
    }
    g_gate.Reset();   // switched off: the next switch-on starts due
    return backdropDue ? 2 : 0;
}

bool Draw(ID3D11Device* device, ID3D11Texture2D* mirror, ID3D11Texture2D* shown, DXGI_FORMAT eyeFmt, const int rect[4], int eye, int why)
{
    if (!device || !mirror || why == 0) { return false; }
    if (!shown) {
        ++g_noSource;
        if (!g_saidNoSource) {
            g_saidNoSource = true;
            LOG("[DESKMIRROR] no view-%d image is held at this submit - the window keeps its last picture (counted in the 30 s line)",
                kMirrorView);
        }
        return false;
    }
    const int64_t now = Now();
    HWND h = Window(now);
    double aspect = 0.0;   // 0 = the mirror's own shape (CropToAspect falls back to the rect's; corrected below)
    if (!h) {
        ++g_noWindow;
        if (!g_saidNoWindow) {
            g_saidNoWindow = true;
            LOG("[DESKMIRROR] no visible desktop window of this process was found - the eye is cropped to the mirror texture's own "
                "shape instead (counted in the 30 s line)");
        }
    } else {
        RECT r{};
        if (IsIconic(h) || !GetClientRect(h, &r) || r.right - r.left <= 0 || r.bottom - r.top <= 0) {
            ++g_minimised;
            if (!g_saidMinimised) {
                g_saidMinimised = true;
                LOG("[DESKMIRROR] the desktop window is minimised (no client area) - not drawn while it is (counted in the 30 s line)");
            }
            return false;
        }
        aspect = static_cast<double>(r.right - r.left) / static_cast<double>(r.bottom - r.top);
    }
    ID3D11RenderTargetView* rtv = RtvFor(device, mirror);
    ID3D11ShaderResourceView* srv = rtv ? SrvFor(device, shown, eyeFmt) : nullptr;
    if (!rtv || !srv) { ++g_noView; return false; }
    if (!(aspect > 0.0)) { aspect = g_mirH > 0.0f ? static_cast<double>(g_mirW) / static_cast<double>(g_mirH) : 0.0; }

    uipanel::EyeTan t{};
    const bool haveTan = uipanel::GameEyeTan(eye, &t);
    const double u = haveTan ? CentreFrac(t.l, t.r) : 0.5;
    const double v = haveTan ? CentreFrac(t.u, t.d) : 0.5;
    const RectI eyeRect{ rect[0], rect[1], rect[2], rect[3] };
    const RectF c = CropToAspect(eyeRect, aspect, u, v);

    D3D11_TEXTURE2D_DESC sd{};
    shown->GetDesc(&sd);
    if (sd.Width == 0 || sd.Height == 0) { ++g_noView; return false; }
    const float uv[4] = { static_cast<float>(c.x / sd.Width), static_cast<float>(c.y / sd.Height),
                          static_cast<float>((c.x + c.w) / sd.Width), static_cast<float>((c.y + c.h) / sd.Height) };
    const int rc = OverlayDrawImageToTarget(rtv, g_mirW, g_mirH, srv, uv);
    if (rc == 1) {
        ++g_notInit;
        if (!g_saidNotInit) { g_saidNotInit = true; LOG("[DESKMIRROR] the overlay is not initialised - not drawn (counted in the 30 s line)"); }
        return false;
    }
    if (rc == 2) {
        ++g_noFrame;
        if (!g_saidNoFrame) {
            g_saidNoFrame = true;
            LOG("[DESKMIRROR] no ImGui frame has run yet (the overlay's draw lists are set up by the first one: reticle or menu) - not "
                "drawn until then (counted in the 30 s line)");
        }
        return false;
    }
    if (why == 1) { ++g_live; } else { ++g_backdrop; }
    if (rect[2] > 0 && rect[3] > 0) {
        g_cropF[0] = (c.x - rect[0]) / rect[2];
        g_cropF[1] = (c.y - rect[1]) / rect[3];
        g_cropF[2] = c.w / rect[2];
        g_cropF[3] = c.h / rect[3];
        g_haveCrop = true;
    }
    if (!g_firstDraw) {
        g_firstDraw = true;
        D3D11_TEXTURE2D_DESC md{};
        mirror->GetDesc(&md);
        LOG("[DESKMIRROR] FIRST DRAW (%s): game eye %d, image %ux%u format %d viewed as %d, view-%d rect (%d,%d %dx%d) -> crop (%.1f,%.1f "
            "%.1fx%.1f), window shape %.4f, centre %.4f %.4f (%s) -> mirror %ux%u format %d; live at most %d / s",
            why == 1 ? "live" : "menu backdrop", eye, sd.Width, sd.Height, static_cast<int>(sd.Format), static_cast<int>(eyeFmt),
            kMirrorView, rect[0], rect[1], rect[2], rect[3], c.x, c.y, c.w, c.h, aspect, u, v,
            haveTan ? "the game frustum's forward point" : "rect centre - no frustum described yet", md.Width, md.Height,
            static_cast<int>(md.Format), kLivePerSec);
    }
    return true;
}

void SetFresh(ID3D11Texture2D* mirror)
{
    g_fresh = mirror;
}

void DrawPanel(ID3D11Device* device, ID3D11Texture2D* panelImage, int w, int h)
{
    // Not redrawn this submit / no crop yet / no panel image: nothing (the normal skip on submits the 60 / s clock did not take).
    if (!g_fresh || !g_haveCrop || !device || !panelImage || w <= 0 || h <= 0) { return; }
    ID3D11Texture2D* const mirror = g_fresh;
    // Removed by AngelWolf 2026-11-26
    // g_fresh = nullptr;   // at most one panel draw per refreshed picture
    ID3D11RenderTargetView* rtv = RtvFor(device, mirror);
    D3D11_TEXTURE2D_DESC pd{};
    panelImage->GetDesc(&pd);
    // The panel chain is created R8G8B8A8_UNORM_SRGB (uipanel.cpp EnsureSwapchain); its images may come back TYPELESS - view as sRGB.
    ID3D11ShaderResourceView* srv = rtv ? SrvFor(device, panelImage, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) : nullptr;
    if (!rtv || !srv || pd.Width == 0 || pd.Height == 0) {
        ++g_panelFail;
        return;
    }
    // The panel image holds eye 0's pass at x 0 and eye 1's at x w (uipanel.cpp: the XR_EYE_VISIBILITY_RIGHT quad reads offset w).
    const double x = kMirrorView * static_cast<double>(w) + g_cropF[0] * w, y = g_cropF[1] * h, cw = g_cropF[2] * w, ch = g_cropF[3] * h;
    const float uv[4] = { static_cast<float>(x / pd.Width), static_cast<float>(y / pd.Height),
                          static_cast<float>((x + cw) / pd.Width), static_cast<float>((y + ch) / pd.Height) };
    const int rc = OverlayDrawImageToTarget(rtv, g_mirW, g_mirH, srv, uv, true);
    if (rc != 0) {
        ++g_panelFail;
        if (!g_saidPanelFail) {
            g_saidPanelFail = true;
            LOG("[DESKMIRROR] the menu panel could NOT be drawn into the window (overlay draw returned %d: 1 = overlay not up, 3 = no "
                "blend state) - the window shows the game without the menu (counted in the 30 s line)", rc);
        }
        return;
    }
    ++g_panelDraws;
    if (!g_firstPanel) {
        g_firstPanel = true;
        LOG("[DESKMIRROR] FIRST MENU PANEL DRAW: panel image %ux%u format %d viewed as %d, eye-%d pass %dx%d, crop (%.1f,%.1f %.1fx%.1f) "
            "-> over the window, premultiplied", pd.Width, pd.Height, static_cast<int>(pd.Format),
            static_cast<int>(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB), kMirrorView, w, h, x, y, cw, ch);
    }
}

// Added by AngelWolf 2026-11-26
// Projects a 3D OpenXR quad layer (`quad`, whose space matches `eyePose` — e.g. appSpace) into kMirrorView's declared pose/FOV,
// remaps through this submit's desktop window crop (g_cropF), and composites `tex` onto the fresh mirror texture.
// Subdivides into a 4x4 grid so ImDrawList's 2D affine UV interpolation does not bend/fold angled panels.
// blendMode: 0 = opaque, 1 = premultiplied alpha, 2 = straight alpha.
void DrawQuadLayer(ID3D11Device* device, ID3D11Texture2D* tex, DXGI_FORMAT fmt,
    const XrCompositionLayerQuad& quad, const XrPosef& eyePose, const XrFovf& eyeFov, int blendMode)
{
    if (!g_fresh || !g_haveCrop || !device || !tex || !(g_mirW > 0.0f) || !(g_mirH > 0.0f)) { return; }
    if (quad.eyeVisibility == XR_EYE_VISIBILITY_LEFT) { return; }

    D3D11_TEXTURE2D_DESC td{};
    tex->GetDesc(&td);
    if (td.Width == 0 || td.Height == 0) { return; }

    ID3D11RenderTargetView* rtv = RtvFor(device, g_fresh);
    ID3D11ShaderResourceView* srv = rtv ? SrvFor(device, tex, fmt) : nullptr;
    if (!rtv || !srv) { return; }

    const float tanL = tanf(eyeFov.angleLeft);
    const float tanR = tanf(eyeFov.angleRight);
    const float tanU = tanf(eyeFov.angleUp);
    const float tanD = tanf(eyeFov.angleDown);
    const float spanX = tanR - tanL;
    const float spanY = tanU - tanD;
    if (!(spanX > 0.0f) || !(spanY > 0.0f) || !(g_cropF[2] > 0.0) || !(g_cropF[3] > 0.0)) { return; }

    const float u0 = static_cast<float>(quad.subImage.imageRect.offset.x) / static_cast<float>(td.Width);
    const float v0 = static_cast<float>(quad.subImage.imageRect.offset.y) / static_cast<float>(td.Height);
    const float u1 = static_cast<float>(quad.subImage.imageRect.offset.x + quad.subImage.imageRect.extent.width) / static_cast<float>(td.Width);
    const float v1 = static_cast<float>(quad.subImage.imageRect.offset.y + quad.subImage.imageRect.extent.height) / static_cast<float>(td.Height);

    constexpr int kGrid = 16;
    float gridPt[kGrid + 1][kGrid + 1][2] = {};
    float gridUv[kGrid + 1][kGrid + 1][2] = {};

    for (int gy = 0; gy <= kGrid; ++gy) {
        const float tyFrac = static_cast<float>(gy) / static_cast<float>(kGrid);
        const float localY = (0.5f - tyFrac) * quad.size.height;
        const float uvV = v0 + (v1 - v0) * tyFrac;

        for (int gx = 0; gx <= kGrid; ++gx) {
            const float txFrac = static_cast<float>(gx) / static_cast<float>(kGrid);
            const float localX = (txFrac - 0.5f) * quad.size.width;
            const float uvU = u0 + (u1 - u0) * txFrac;

            const XrVector3f rot = QuatRotate(quad.pose.orientation, XrVector3f{ localX, localY, 0.0f });
            const XrVector3f world{ quad.pose.position.x + rot.x,
                                    quad.pose.position.y + rot.y,
                                    quad.pose.position.z + rot.z };
            const XrVector3f rel{ world.x - eyePose.position.x,
                                  world.y - eyePose.position.y,
                                  world.z - eyePose.position.z };
            const XrVector3f eye = QuatUnrotate(eyePose.orientation, rel);
            if (eye.z >= -0.01f) { return; }

            const float invDepth = 1.0f / -eye.z;
            const float tx = eye.x * invDepth;
            const float ty = eye.y * invDepth;

            const float eyeU = (tx - tanL) / spanX;
            const float eyeV = (tanU - ty) / spanY;

            const float mirU = static_cast<float>((eyeU - g_cropF[0]) / g_cropF[2]);
            const float mirV = static_cast<float>((eyeV - g_cropF[1]) / g_cropF[3]);

            gridPt[gy][gx][0] = mirU * g_mirW;
            gridPt[gy][gx][1] = mirV * g_mirH;
            gridUv[gy][gx][0] = uvU;
            gridUv[gy][gx][1] = uvV;
        }
    }

    float pts[kGrid * kGrid * 8];
    float uvs[kGrid * kGrid * 8];
    int q = 0;
    for (int gy = 0; gy < kGrid; ++gy) {
        for (int gx = 0; gx < kGrid; ++gx) {
            float* p = pts + q * 8;
            float* u = uvs + q * 8;
            p[0] = gridPt[gy][gx][0];         p[1] = gridPt[gy][gx][1];
            p[2] = gridPt[gy][gx + 1][0];     p[3] = gridPt[gy][gx + 1][1];
            p[4] = gridPt[gy + 1][gx + 1][0]; p[5] = gridPt[gy + 1][gx + 1][1];
            p[6] = gridPt[gy + 1][gx][0];     p[7] = gridPt[gy + 1][gx][1];

            u[0] = gridUv[gy][gx][0];         u[1] = gridUv[gy][gx][1];
            u[2] = gridUv[gy][gx + 1][0];     u[3] = gridUv[gy][gx + 1][1];
            u[4] = gridUv[gy + 1][gx + 1][0]; u[5] = gridUv[gy + 1][gx + 1][1];
            u[6] = gridUv[gy + 1][gx][0];     u[7] = gridUv[gy + 1][gx][1];
            ++q;
        }
    }
    OverlayDrawQuadToTarget(rtv, g_mirW, g_mirH, srv, pts, uvs, blendMode, q);    
}

void Shutdown()
{
    for (int i = 0; i < g_nSrv; ++i) { if (g_srv[i].srv) { g_srv[i].srv->Release(); } }
    g_nSrv = 0;
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    g_rtvTex = nullptr;
    g_hwnd = nullptr;
    g_gate.Reset();
    g_fresh = nullptr;
    g_haveCrop = false;
}

}  // namespace deskmirror
}  // namespace dlvr
