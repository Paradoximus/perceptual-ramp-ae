// color_math_test.cpp
// Standalone unit test for color_math.h. No AE SDK required.
//   build:  cl /EHsc /O2 /I..\src color_math_test.cpp
//   run:    color_math_test.exe   (exit code 0 = all pass)
#include "../src/color_math.h"
#include <cstdio>
#include <cmath>

using namespace cm;

static int g_fail = 0, g_total = 0;

static void check(bool cond, const char* msg) {
    ++g_total;
    if (!cond) { ++g_fail; std::printf("  [FAIL] %s\n", msg); }
}
static bool approx(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
static void checkApprox(float a, float b, float tol, const char* msg) {
    ++g_total;
    if (!approx(a, b, tol)) {
        ++g_fail;
        std::printf("  [FAIL] %s : got %.6f, want %.6f (tol %.4f)\n", msg, a, b, tol);
    }
}

// ---------------------------------------------------------------------------
static void test_srgb_transfer() {
    std::printf("sRGB transfer round-trip\n");
    for (float c = 0.0f; c <= 1.0f; c += 0.05f)
        checkApprox(linear_to_srgb(srgb_to_linear(c)), c, 1e-4f, "srgb<->linear");
    checkApprox(srgb_to_linear(0.0f), 0.0f, 1e-6f, "linear(0)");
    checkApprox(srgb_to_linear(1.0f), 1.0f, 1e-5f, "linear(1)");
}

static void test_oklab_roundtrip() {
    std::printf("OKLab round-trip\n");
    RGB samples[] = { {0,0,0},{1,1,1},{1,0,0},{0,1,0},{0,0,1},
                      {0.5f,0.25f,0.75f},{0.2f,0.9f,0.4f} };
    for (RGB s : samples) {
        RGB r = oklab_to_srgb(srgb_to_oklab(s));
        checkApprox(r.r, s.r, 1e-3f, "oklab rt r");
        checkApprox(r.g, s.g, 1e-3f, "oklab rt g");
        checkApprox(r.b, s.b, 1e-3f, "oklab rt b");
    }
}

static void test_oklab_reference() {
    std::printf("OKLab reference values (Ottosson)\n");
    OKLab w = srgb_to_oklab({1,1,1});
    checkApprox(w.L, 1.0f, 2e-3f, "white L"); checkApprox(w.a, 0.0f, 2e-3f, "white a");
    checkApprox(w.b, 0.0f, 2e-3f, "white b");

    OKLab red = srgb_to_oklab({1,0,0});
    checkApprox(red.L, 0.627955f, 3e-3f, "red L");
    checkApprox(red.a, 0.224863f, 3e-3f, "red a");
    checkApprox(red.b, 0.125846f, 3e-3f, "red b");

    OKLab grn = srgb_to_oklab({0,1,0});
    checkApprox(grn.L, 0.866440f, 3e-3f, "green L");
    checkApprox(grn.a, -0.233888f, 3e-3f, "green a");
    checkApprox(grn.b, 0.179498f, 3e-3f, "green b");

    OKLab blu = srgb_to_oklab({0,0,1});
    checkApprox(blu.L, 0.452014f, 3e-3f, "blue L");
    checkApprox(blu.a, -0.032457f, 3e-3f, "blue a");
    checkApprox(blu.b, -0.311528f, 3e-3f, "blue b");
}

static void test_oklch_roundtrip() {
    std::printf("OKLCh round-trip\n");
    RGB samples[] = { {1,0,0},{0,1,0},{0,0,1},{0.3f,0.6f,0.2f},{0.8f,0.8f,0.1f} };
    for (RGB s : samples) {
        OKLab lab = srgb_to_oklab(s);
        OKLab back = oklch_to_oklab(oklab_to_oklch(lab));
        checkApprox(back.L, lab.L, 1e-4f, "oklch rt L");
        checkApprox(back.a, lab.a, 1e-4f, "oklch rt a");
        checkApprox(back.b, lab.b, 1e-4f, "oklch rt b");
    }
}

static void test_lab_roundtrip() {
    std::printf("CIELAB round-trip\n");
    RGB samples[] = { {0,0,0},{1,1,1},{1,0,0},{0,1,0},{0,0,1},{0.4f,0.6f,0.7f} };
    for (RGB s : samples) {
        RGBlin lin = to_linear(s);
        Lab lab = xyz_to_lab(linear_srgb_to_xyz(lin));
        RGBlin back = xyz_to_linear_srgb(lab_to_xyz(lab));
        checkApprox(back.r, lin.r, 1e-3f, "lab rt r");
        checkApprox(back.g, lin.g, 1e-3f, "lab rt g");
        checkApprox(back.b, lin.b, 1e-3f, "lab rt b");
    }
    // White -> L≈100, a≈0, b≈0
    Lab w = xyz_to_lab(linear_srgb_to_xyz(to_linear({1,1,1})));
    checkApprox(w.L, 100.0f, 0.5f, "lab white L");
    checkApprox(w.a, 0.0f, 0.5f, "lab white a");
    checkApprox(w.b, 0.0f, 0.5f, "lab white b");
}

static void test_hsl_roundtrip() {
    std::printf("HSL round-trip\n");
    RGB samples[] = { {1,0,0},{0,1,0},{0,0,1},{0.5f,0.5f,0.5f},{0.2f,0.7f,0.9f} };
    for (RGB s : samples) {
        RGB r = hsl_to_rgb(rgb_to_hsl(s));
        checkApprox(r.r, s.r, 1e-3f, "hsl rt r");
        checkApprox(r.g, s.g, 1e-3f, "hsl rt g");
        checkApprox(r.b, s.b, 1e-3f, "hsl rt b");
    }
}

static void test_hue_paths() {
    std::printf("Hue interpolation paths\n");
    // 350 -> 10 : shorter arc passes through 0 (=360), midpoint = 0
    checkApprox(lerp_hue(350.0f, 10.0f, 0.5f, HuePath::Shorter), 0.0f, 1e-3f, "shorter mid");
    // longer arc midpoint = 180
    checkApprox(lerp_hue(350.0f, 10.0f, 0.5f, HuePath::Longer), 180.0f, 1e-3f, "longer mid");
    // increasing from 350 must go up through 360 -> 10 (=370), mid=0
    checkApprox(lerp_hue(350.0f, 10.0f, 0.5f, HuePath::Increasing), 0.0f, 1e-3f, "increasing mid");
    // decreasing from 10 -> 350 forced downward
    checkApprox(lerp_hue(10.0f, 350.0f, 0.5f, HuePath::Decreasing), 0.0f, 1e-3f, "decreasing mid");
}

static void test_gamut_map() {
    std::printf("Gamut mapping invariants\n");
    // High-chroma OKLCh that is far out of sRGB gamut must map back inside.
    OKLCh wild[] = { {0.7f, 0.5f, 30.0f}, {0.5f, 0.4f, 250.0f}, {0.9f, 0.3f, 140.0f} };
    for (OKLCh c : wild) {
        RGB r = gamut_map_oklch(c);
        check(r.r >= -1e-3f && r.r <= 1.0f + 1e-3f, "gamut r in range");
        check(r.g >= -1e-3f && r.g <= 1.0f + 1e-3f, "gamut g in range");
        check(r.b >= -1e-3f && r.b <= 1.0f + 1e-3f, "gamut b in range");
    }
    // An already in-gamut color should be (nearly) unchanged.
    RGB src = {0.6f, 0.3f, 0.2f};
    RGB mapped = gamut_map_oklch(oklab_to_oklch(srgb_to_oklab(src)));
    checkApprox(mapped.r, src.r, 5e-3f, "gamut identity r");
    checkApprox(mapped.g, src.g, 5e-3f, "gamut identity g");
    checkApprox(mapped.b, src.b, 5e-3f, "gamut identity b");
}

static void test_no_muddy_dip() {
    std::printf("OKLab avoids the muddy gray dip (red->green)\n");
    RGB r = {1,0,0}, g = {0,1,0};
    RGB mid_srgb  = mix(r, g, 0.5f, Space::sRGB,  HuePath::Shorter);
    RGB mid_oklab = mix(r, g, 0.5f, Space::OKLab, HuePath::Shorter);
    float L_srgb  = srgb_to_oklab(mid_srgb).L;
    float L_oklab = srgb_to_oklab(mid_oklab).L;
    // The perceptual mix should keep the midpoint at least as light as the
    // naive sRGB mix (in practice noticeably lighter / less muddy).
    check(L_oklab >= L_srgb - 1e-3f, "oklab midpoint not darker than srgb");
    std::printf("    midpoint OKLab L: sRGB=%.3f  OKLab=%.3f\n", L_srgb, L_oklab);
}

static void test_evaluate_gradient() {
    std::printf("evaluate_gradient end-to-end\n");
    Stop stops[3] = {
        { 0.0f, {1,0,0,1}, Easing::Linear, 0.5f },
        { 0.5f, {0,1,0,1}, Easing::Linear, 0.5f },
        { 1.0f, {0,0,1,1}, Easing::Linear, 0.5f },
    };
    RGBA a = evaluate_gradient(stops, 3, 0.0f, Space::OKLab, HuePath::Shorter);
    checkApprox(a.r, 1.0f, 2e-2f, "grad t0 r"); checkApprox(a.g, 0.0f, 2e-2f, "grad t0 g");
    RGBA c = evaluate_gradient(stops, 3, 1.0f, Space::OKLab, HuePath::Shorter);
    checkApprox(c.b, 1.0f, 2e-2f, "grad t1 b");
    RGBA mid = evaluate_gradient(stops, 3, 0.5f, Space::OKLab, HuePath::Shorter);
    checkApprox(mid.g, 1.0f, 5e-2f, "grad mid is green");
    // clamp below first / above last stop
    RGBA below = evaluate_gradient(stops, 3, -0.5f, Space::OKLab, HuePath::Shorter);
    checkApprox(below.r, 1.0f, 1e-4f, "grad clamp low");
    // alpha lerp
    Stop alphaStops[2] = {
        { 0.0f, {1,1,1,0}, Easing::Linear, 0.5f },
        { 1.0f, {1,1,1,1}, Easing::Linear, 0.5f },
    };
    RGBA am = evaluate_gradient(alphaStops, 2, 0.5f, Space::LinearRGB, HuePath::Shorter);
    checkApprox(am.a, 0.5f, 1e-3f, "grad alpha mid");
}

static void test_blend() {
    std::printf("Blend modes\n");
    RGB b = {0.4f, 0.6f, 0.8f};
    checkApprox(blend_channel(Blend::Normal, b.r, 0.5f), 0.5f, 1e-6f, "normal=src");
    checkApprox(blend_channel(Blend::Multiply, 0.7f, 0.0f), 0.0f, 1e-6f, "mult*black");
    checkApprox(blend_channel(Blend::Multiply, 0.7f, 1.0f), 0.7f, 1e-6f, "mult*white");
    checkApprox(blend_channel(Blend::Screen, 0.3f, 1.0f), 1.0f, 1e-6f, "screen white");
    checkApprox(blend_channel(Blend::Darken, 0.3f, 0.7f), 0.3f, 1e-6f, "darken");
    checkApprox(blend_channel(Blend::Lighten, 0.3f, 0.7f), 0.7f, 1e-6f, "lighten");
    checkApprox(blend_channel(Blend::Difference, 0.3f, 0.7f), 0.4f, 1e-6f, "difference");
    // composite coverage 0 -> base, 1 -> source (Normal)
    RGBA c0 = composite(Blend::Normal, b, 1.0f, {0.1f, 0.2f, 0.3f, 1.0f}, 0.0f);
    checkApprox(c0.r, b.r, 1e-6f, "composite cov0");
    RGBA c1 = composite(Blend::Normal, b, 1.0f, {0.1f, 0.2f, 0.3f, 1.0f}, 1.0f);
    checkApprox(c1.r, 0.1f, 1e-6f, "composite cov1");
    RGBA ch = composite(Blend::Normal, {0, 0, 0}, 1.0f, {1, 1, 1, 1}, 0.5f);
    checkApprox(ch.r, 0.5f, 1e-6f, "composite half opacity");
}

int main() {
    std::printf("=== color_math unit tests ===\n");
    test_srgb_transfer();
    test_oklab_roundtrip();
    test_oklab_reference();
    test_oklch_roundtrip();
    test_lab_roundtrip();
    test_hsl_roundtrip();
    test_hue_paths();
    test_gamut_map();
    test_no_muddy_dip();
    test_evaluate_gradient();
    test_blend();

    std::printf("\n%d/%d checks passed.\n", g_total - g_fail, g_total);
    if (g_fail) { std::printf("RESULT: FAIL (%d)\n", g_fail); return 1; }
    std::printf("RESULT: PASS\n");
    return 0;
}
