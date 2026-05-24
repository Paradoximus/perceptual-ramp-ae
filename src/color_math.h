// color_math.h
// ---------------------------------------------------------------------------
// Self-contained color engine for the AE Ramp/Gradient plugin.
//
// Design goals:
//   * Header-only, pure C++ (C++11), float math, NO After Effects SDK deps.
//     -> can be unit-tested with a plain console exe, and later copied almost
//        verbatim into a CUDA/OpenCL/HLSL kernel for the GPU phase.
//   * "Travel through color space nicely": interpolate in a perceptual space
//     (OKLab / OKLCh by default) instead of naive gamma-sRGB lerp, which gives
//     the muddy gray dip in the middle of a two-color gradient.
//   * Out-of-gamut results are gamut-MAPPED (chroma reduction in OKLCh + MINDE),
//     not naively clipped, following the CSS Color 4 algorithm.
//
// References:
//   Bjorn Ottosson, "A perceptual color space for image processing" (OKLab).
//   CSS Color Module Level 4 - interpolation & gamut mapping.
// ---------------------------------------------------------------------------
#ifndef RAMP_COLOR_MATH_H
#define RAMP_COLOR_MATH_H

#include <cmath>
#include <cstdint>

namespace cm {

// --------------------------------------------------------------------------
// Small helpers
// --------------------------------------------------------------------------
inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// cbrt that is safe for negative inputs (OKLab nonlinearity uses signed roots
// in some derivations; OKLab proper uses cbrt of non-negative LMS, but we keep
// it sign-safe to avoid NaNs on numerical noise).
inline float cbrtf_safe(float x) {
    return (x < 0.0f) ? -std::pow(-x, 1.0f / 3.0f) : std::pow(x, 1.0f / 3.0f);
}

// --------------------------------------------------------------------------
// Color value types. Distinct structs (not a single float3) so the compiler
// stops us from mixing up color spaces.
// --------------------------------------------------------------------------
struct RGB    { float r, g, b; };   // sRGB, gamma-encoded, nominal [0,1]
struct RGBlin { float r, g, b; };   // sRGB primaries, LINEAR light
struct RGBA   { float r, g, b, a; };// gamma sRGB + straight alpha
struct OKLab  { float L, a, b; };
struct OKLCh  { float L, C, h; };   // h in DEGREES [0,360)
struct XYZ    { float x, y, z; };
struct Lab    { float L, a, b; };   // CIELAB, D65
struct LCh    { float L, C, h; };   // CIELAB polar, h in DEGREES
struct HSL    { float h, s, l; };   // h in DEGREES, s/l in [0,1]

// Interpolation spaces exposed to the UI dropdown.
enum class Space : int {
    sRGB    = 0,  // gamma sRGB lerp (the "wrong" baseline, kept for comparison)
    LinearRGB,    // gamma-correct but not perceptual
    OKLab,        // perceptual, rectangular  <- default
    OKLCh,        // perceptual, cylindrical  (best for hue ramps)
    Lab,          // CIELAB rectangular
    LCh,          // CIELAB cylindrical
    HSL           // cheap rainbow, for expectation compatibility
};

// Hue interpolation strategy for cylindrical spaces (CSS Color 4).
enum class HuePath : int { Shorter = 0, Longer, Increasing, Decreasing };

// Per-segment easing of the local parameter.
enum class Easing : int { Linear = 0, Smooth, Ease };

// Blend modes for compositing the gradient over the input layer.
enum class Blend : int {
    Normal = 0, Multiply, Screen, Overlay, Darken, Lighten,
    Add, Subtract, Difference, HardLight, SoftLight
};

// --------------------------------------------------------------------------
// sRGB transfer function
// --------------------------------------------------------------------------
inline float srgb_to_linear(float c) {
    return (c <= 0.04045f) ? (c / 12.92f)
                           : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
inline float linear_to_srgb(float c) {
    return (c <= 0.0031308f) ? (c * 12.92f)
                             : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
}
inline RGBlin to_linear(RGB c) {
    return { srgb_to_linear(c.r), srgb_to_linear(c.g), srgb_to_linear(c.b) };
}
inline RGB to_gamma(RGBlin c) {
    return { linear_to_srgb(c.r), linear_to_srgb(c.g), linear_to_srgb(c.b) };
}

// --------------------------------------------------------------------------
// Linear sRGB <-> OKLab  (Ottosson)
// --------------------------------------------------------------------------
inline OKLab linear_srgb_to_oklab(RGBlin c) {
    float l = 0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b;
    float m = 0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b;
    float s = 0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b;

    float l_ = cbrtf_safe(l), m_ = cbrtf_safe(m), s_ = cbrtf_safe(s);

    return {
        0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
        1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
        0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_
    };
}
inline RGBlin oklab_to_linear_srgb(OKLab c) {
    float l_ = c.L + 0.3963377774f * c.a + 0.2158037573f * c.b;
    float m_ = c.L - 0.1055613458f * c.a - 0.0638541728f * c.b;
    float s_ = c.L - 0.0894841775f * c.a - 1.2914855480f * c.b;

    float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;

    return {
        +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
        -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
        -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s
    };
}

// --------------------------------------------------------------------------
// Rectangular <-> cylindrical (shared by OK and CIE). h in DEGREES.
// --------------------------------------------------------------------------
inline float wrap360(float h) {
    h = std::fmod(h, 360.0f);
    return h < 0.0f ? h + 360.0f : h;
}
inline OKLCh oklab_to_oklch(OKLab c) {
    float C = std::sqrt(c.a * c.a + c.b * c.b);
    float h = (C < 1e-7f) ? 0.0f : wrap360(std::atan2(c.b, c.a) * 57.2957795131f);
    return { c.L, C, h };
}
inline OKLab oklch_to_oklab(OKLCh c) {
    float r = c.h * 0.01745329252f;
    return { c.L, c.C * std::cos(r), c.C * std::sin(r) };
}

// --------------------------------------------------------------------------
// Linear sRGB <-> CIE XYZ (D65) <-> CIELAB
// --------------------------------------------------------------------------
inline XYZ linear_srgb_to_xyz(RGBlin c) {
    return {
        0.4123907993f * c.r + 0.3575843394f * c.g + 0.1804807884f * c.b,
        0.2126390059f * c.r + 0.7151686788f * c.g + 0.0721923154f * c.b,
        0.0193308187f * c.r + 0.1191947798f * c.g + 0.9505321522f * c.b
    };
}
inline RGBlin xyz_to_linear_srgb(XYZ c) {
    return {
        +3.2409699419f * c.x - 1.5373831776f * c.y - 0.4986107603f * c.z,
        -0.9692436363f * c.x + 1.8759675015f * c.y + 0.0415550574f * c.z,
        +0.0556300797f * c.x - 0.2039769589f * c.y + 1.0569715142f * c.z
    };
}
// D65 reference white (2 deg observer).
static const float kXn = 0.95047f, kYn = 1.0f, kZn = 1.08883f;

inline float lab_f(float t) {
    const float d = 6.0f / 29.0f;
    return (t > d * d * d) ? cbrtf_safe(t) : (t / (3.0f * d * d) + 4.0f / 29.0f);
}
inline float lab_f_inv(float t) {
    const float d = 6.0f / 29.0f;
    return (t > d) ? (t * t * t) : (3.0f * d * d * (t - 4.0f / 29.0f));
}
inline Lab xyz_to_lab(XYZ c) {
    float fx = lab_f(c.x / kXn), fy = lab_f(c.y / kYn), fz = lab_f(c.z / kZn);
    return { 116.0f * fy - 16.0f, 500.0f * (fx - fy), 200.0f * (fy - fz) };
}
inline XYZ lab_to_xyz(Lab c) {
    float fy = (c.L + 16.0f) / 116.0f;
    float fx = fy + c.a / 500.0f;
    float fz = fy - c.b / 200.0f;
    return { kXn * lab_f_inv(fx), kYn * lab_f_inv(fy), kZn * lab_f_inv(fz) };
}
inline LCh lab_to_lch(Lab c) {
    float C = std::sqrt(c.a * c.a + c.b * c.b);
    float h = (C < 1e-7f) ? 0.0f : wrap360(std::atan2(c.b, c.a) * 57.2957795131f);
    return { c.L, C, h };
}
inline Lab lch_to_lab(LCh c) {
    float r = c.h * 0.01745329252f;
    return { c.L, c.C * std::cos(r), c.C * std::sin(r) };
}

// --------------------------------------------------------------------------
// gamma sRGB <-> HSL  (HSL conventionally operates on gamma-encoded sRGB)
// --------------------------------------------------------------------------
inline HSL rgb_to_hsl(RGB c) {
    float mx = std::fmax(c.r, std::fmax(c.g, c.b));
    float mn = std::fmin(c.r, std::fmin(c.g, c.b));
    float l = 0.5f * (mx + mn);
    float d = mx - mn;
    if (d < 1e-7f) return { 0.0f, 0.0f, l };
    float s = d / (1.0f - std::fabs(2.0f * l - 1.0f));
    float h;
    if (mx == c.r)      h = std::fmod((c.g - c.b) / d, 6.0f);
    else if (mx == c.g) h = (c.b - c.r) / d + 2.0f;
    else                h = (c.r - c.g) / d + 4.0f;
    return { wrap360(h * 60.0f), s, l };
}
inline RGB hsl_to_rgb(HSL c) {
    float C = (1.0f - std::fabs(2.0f * c.l - 1.0f)) * c.s;
    float hp = c.h / 60.0f;
    float x = C * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
    float r = 0, g = 0, b = 0;
    if      (hp < 1) { r = C; g = x; }
    else if (hp < 2) { r = x; g = C; }
    else if (hp < 3) { g = C; b = x; }
    else if (hp < 4) { g = x; b = C; }
    else if (hp < 5) { r = x; b = C; }
    else             { r = C; b = x; }
    float m = c.l - 0.5f * C;
    return { r + m, g + m, b + m };
}

// Convenience full-chain helpers (gamma sRGB <-> perceptual).
inline OKLab srgb_to_oklab(RGB c)  { return linear_srgb_to_oklab(to_linear(c)); }
inline RGB   oklab_to_srgb(OKLab c){ return to_gamma(oklab_to_linear_srgb(c)); }

// --------------------------------------------------------------------------
// Gamut: is a linear-sRGB color inside the display gamut?
// --------------------------------------------------------------------------
inline bool in_gamut(RGBlin c, float eps = 1e-4f) {
    return c.r >= -eps && c.r <= 1.0f + eps &&
           c.g >= -eps && c.g <= 1.0f + eps &&
           c.b >= -eps && c.b <= 1.0f + eps;
}
inline RGBlin clip_gamut(RGBlin c) {
    return { clamp01(c.r), clamp01(c.g), clamp01(c.b) };
}
inline float delta_e_ok(OKLab a, OKLab b) {
    float dL = a.L - b.L, da = a.a - b.a, db = a.b - b.b;
    return std::sqrt(dL * dL + da * da + db * db);
}

// CSS Color 4 gamut mapping: reduce OKLCh chroma (keep L and h) with a binary
// search, accepting a clipped result once it is within a just-noticeable
// difference (MINDE). Returns an in-gamut gamma-sRGB color.
inline RGB gamut_map_oklch(OKLCh col) {
    const float JND = 0.02f;
    const float EPS = 1e-4f;

    if (col.L >= 1.0f) return { 1, 1, 1 };
    if (col.L <= 0.0f) return { 0, 0, 0 };

    RGBlin direct = oklab_to_linear_srgb(oklch_to_oklab(col));
    if (in_gamut(direct)) return to_gamma(clip_gamut(direct));

    float lo = 0.0f, hi = col.C;
    RGB  result = to_gamma(clip_gamut(direct));
    while (hi - lo > EPS) {
        float C = 0.5f * (lo + hi);
        OKLCh cand = { col.L, C, col.h };
        OKLab candLab = oklch_to_oklab(cand);
        RGBlin lin = oklab_to_linear_srgb(candLab);
        if (in_gamut(lin)) {
            lo = C;
        } else {
            RGBlin clipped = clip_gamut(lin);
            OKLab clippedLab = linear_srgb_to_oklab(clipped);
            float e = delta_e_ok(clippedLab, candLab);
            if (e < JND) {
                result = to_gamma(clipped);
                if (JND - e < EPS) break;
                lo = C;
            } else {
                hi = C;
            }
        }
    }
    // Final: use the largest in-gamut chroma found.
    OKLCh finalCol = { col.L, lo, col.h };
    RGBlin finalLin = oklab_to_linear_srgb(oklch_to_oklab(finalCol));
    if (in_gamut(finalLin)) return to_gamma(clip_gamut(finalLin));
    return result;
}

// --------------------------------------------------------------------------
// Hue interpolation (CSS Color 4 "adjust hue"). a,b in degrees -> returns the
// pair adjusted so a plain lerp travels the requested arc.
// --------------------------------------------------------------------------
inline void adjust_hue(float& a, float& b, HuePath path) {
    a = wrap360(a); b = wrap360(b);
    float d = b - a;
    switch (path) {
        case HuePath::Shorter:
            if (d >  180.0f) a += 360.0f;
            else if (d < -180.0f) b += 360.0f;
            break;
        case HuePath::Longer:
            if (d > 0.0f && d < 180.0f) a += 360.0f;
            else if (d <= 0.0f && d > -180.0f) b += 360.0f;
            break;
        case HuePath::Increasing:
            if (d < 0.0f) b += 360.0f;
            break;
        case HuePath::Decreasing:
            if (d > 0.0f) a += 360.0f;
            break;
    }
}
inline float lerp_hue(float a, float b, float t, HuePath path) {
    adjust_hue(a, b, path);
    return wrap360(lerpf(a, b, t));
}

// --------------------------------------------------------------------------
// Easing of the local segment parameter
// --------------------------------------------------------------------------
inline float apply_easing(float t, Easing e) {
    switch (e) {
        case Easing::Smooth: return t * t * (3.0f - 2.0f * t);          // smoothstep
        case Easing::Ease:   return t * t * t * (t * (6.0f * t - 15.0f) + 10.0f); // smootherstep
        case Easing::Linear:
        default:             return t;
    }
}

// --------------------------------------------------------------------------
// Two-color interpolation in the chosen space, returning gamut-mapped gamma
// sRGB. Inputs c0,c1 are gamma sRGB. (Alpha handled by caller.)
// --------------------------------------------------------------------------
inline RGB mix(RGB c0, RGB c1, float t, Space space, HuePath hue) {
    switch (space) {
        case Space::sRGB:
            return { lerpf(c0.r, c1.r, t), lerpf(c0.g, c1.g, t), lerpf(c0.b, c1.b, t) };

        case Space::LinearRGB: {
            RGBlin a = to_linear(c0), b = to_linear(c1);
            return to_gamma({ lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t) });
        }
        case Space::OKLab: {
            OKLab a = srgb_to_oklab(c0), b = srgb_to_oklab(c1);
            OKLab m = { lerpf(a.L, b.L, t), lerpf(a.a, b.a, t), lerpf(a.b, b.b, t) };
            return gamut_map_oklch(oklab_to_oklch(m));
        }
        case Space::OKLCh: {
            OKLCh a = oklab_to_oklch(srgb_to_oklab(c0));
            OKLCh b = oklab_to_oklch(srgb_to_oklab(c1));
            // If one endpoint is achromatic, carry the other's hue (avoid hue
            // snapping through 0 deg / gray).
            if (a.C < 1e-4f) a.h = b.h;
            if (b.C < 1e-4f) b.h = a.h;
            OKLCh m = { lerpf(a.L, b.L, t), lerpf(a.C, b.C, t),
                        lerp_hue(a.h, b.h, t, hue) };
            return gamut_map_oklch(m);
        }
        case Space::Lab: {
            Lab a = xyz_to_lab(linear_srgb_to_xyz(to_linear(c0)));
            Lab b = xyz_to_lab(linear_srgb_to_xyz(to_linear(c1)));
            Lab m = { lerpf(a.L, b.L, t), lerpf(a.a, b.a, t), lerpf(a.b, b.b, t) };
            RGBlin lin = xyz_to_linear_srgb(lab_to_xyz(m));
            // Reuse OK gamut mapping by routing through OKLCh.
            return gamut_map_oklch(oklab_to_oklch(linear_srgb_to_oklab(lin)));
        }
        case Space::LCh: {
            LCh a = lab_to_lch(xyz_to_lab(linear_srgb_to_xyz(to_linear(c0))));
            LCh b = lab_to_lch(xyz_to_lab(linear_srgb_to_xyz(to_linear(c1))));
            if (a.C < 1e-4f) a.h = b.h;
            if (b.C < 1e-4f) b.h = a.h;
            LCh m = { lerpf(a.L, b.L, t), lerpf(a.C, b.C, t),
                      lerp_hue(a.h, b.h, t, hue) };
            RGBlin lin = xyz_to_linear_srgb(lab_to_xyz(lch_to_lab(m)));
            return gamut_map_oklch(oklab_to_oklch(linear_srgb_to_oklab(lin)));
        }
        case Space::HSL: {
            HSL a = rgb_to_hsl(c0), b = rgb_to_hsl(c1);
            if (a.s < 1e-4f) a.h = b.h;
            if (b.s < 1e-4f) b.h = a.h;
            HSL m = { lerp_hue(a.h, b.h, t, hue), lerpf(a.s, b.s, t), lerpf(a.l, b.l, t) };
            RGB out = hsl_to_rgb(m);
            return { clamp01(out.r), clamp01(out.g), clamp01(out.b) };
        }
    }
    return c0;
}

// --------------------------------------------------------------------------
// Multi-stop gradient. Stops must be sorted by position ascending.
//   position : [0,1]
//   color    : gamma sRGB + straight alpha
//   easing   : applied to the local parameter within the segment that STARTS
//              at this stop
//   midpoint : [0,1] location of the 50% blend point within that segment
//              (0.5 = neutral). Like Photoshop gradient midpoints.
// --------------------------------------------------------------------------
struct Stop {
    float position;
    RGBA  color;
    Easing easing;
    float midpoint;
};

// Remap local t so that lt == midpoint maps to 0.5 (gamma-style midpoint).
inline float apply_midpoint(float lt, float mid) {
    mid = clampf(mid, 0.001f, 0.999f);
    if (lt <= mid) return 0.5f * (lt / mid);
    return 0.5f + 0.5f * ((lt - mid) / (1.0f - mid));
}

inline RGBA evaluate_gradient(const Stop* stops, int n, float t,
                              Space space, HuePath hue) {
    if (n <= 0) return { 0, 0, 0, 0 };
    if (n == 1) return stops[0].color;

    t = clamp01(t);
    if (t <= stops[0].position)      return stops[0].color;
    if (t >= stops[n - 1].position)  return stops[n - 1].color;

    // Find bracketing segment [i, i+1].
    int i = 0;
    while (i < n - 1 && t > stops[i + 1].position) ++i;

    const Stop& s0 = stops[i];
    const Stop& s1 = stops[i + 1];
    float span = s1.position - s0.position;
    float lt = (span > 1e-7f) ? (t - s0.position) / span : 0.0f;

    lt = apply_midpoint(lt, s0.midpoint);
    lt = apply_easing(lt, s0.easing);

    RGB c0 = { s0.color.r, s0.color.g, s0.color.b };
    RGB c1 = { s1.color.r, s1.color.g, s1.color.b };
    RGB rgb = mix(c0, c1, lt, space, hue);
    float a = lerpf(s0.color.a, s1.color.a, lt);
    return { rgb.r, rgb.g, rgb.b, a };
}

// --------------------------------------------------------------------------
// Blend modes. b = base (input/backdrop), s = source (gradient). Channels in
// nominal [0,1]; out-of-range results (Add/Subtract/Difference) are left for
// the caller to clamp at write time.
// --------------------------------------------------------------------------
inline float blend_channel(Blend m, float b, float s) {
    switch (m) {
        case Blend::Normal:     return s;
        case Blend::Multiply:   return b * s;
        case Blend::Screen:     return 1.0f - (1.0f - b) * (1.0f - s);
        case Blend::Overlay:    return b < 0.5f ? 2.0f * b * s : 1.0f - 2.0f * (1.0f - b) * (1.0f - s);
        case Blend::Darken:     return b < s ? b : s;
        case Blend::Lighten:    return b > s ? b : s;
        case Blend::Add:        return b + s;
        case Blend::Subtract:   return b - s;
        case Blend::Difference: return std::fabs(b - s);
        case Blend::HardLight:  return s < 0.5f ? 2.0f * b * s : 1.0f - 2.0f * (1.0f - b) * (1.0f - s);
        case Blend::SoftLight:  return (1.0f - 2.0f * s) * b * b + 2.0f * s * b;   // Pegtop
    }
    return s;
}
inline RGB blend_rgb(Blend m, RGB b, RGB s) {
    return { blend_channel(m, b.r, s.r), blend_channel(m, b.g, s.g), blend_channel(m, b.b, s.b) };
}

// Composite gradient color `g` over backdrop `base` with a blend mode and a
// source coverage `cov` (= gradient alpha * effect opacity). Returns straight
// RGBA. RGB may exceed [0,1] for additive modes; caller clamps for 8/16-bit.
inline RGBA composite(Blend m, RGB base, float baseA, RGBA g, float cov) {
    cov = clamp01(cov * g.a);
    RGB bl = blend_rgb(m, base, { g.r, g.g, g.b });
    return {
        lerpf(base.r, bl.r, cov),
        lerpf(base.g, bl.g, cov),
        lerpf(base.b, bl.b, cov),
        baseA + cov * (1.0f - baseA)
    };
}

// --------------------------------------------------------------------------
// Ordered dithering (4x4 Bayer) to break banding when writing to 8/16-bit.
// Returns a +/- offset in [0,1] units of one LSB at the given bit depth.
// --------------------------------------------------------------------------
inline float bayer4x4_dither(int x, int y, int bits) {
    static const int B[16] = {
         0,  8,  2, 10,
        12,  4, 14,  6,
         3, 11,  1,  9,
        15,  7, 13,  5 };
    float threshold = (B[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.0f - 0.5f;
    float lsb = 1.0f / ((1 << bits) - 1);
    return threshold * lsb;
}

} // namespace cm

#endif // RAMP_COLOR_MATH_H
