# Perceptual Ramp

A native **After Effects** effect plug-in that generates color gradients and
ramps with **perceptually-uniform interpolation** — so a two-color gradient
travels through color space *cleanly*, without the muddy gray dip you get from
naïve sRGB blending.

> Effect menu: **Effect → KPX → Perceptual Ramp** · Windows · AE 2025 / 2026

![menu category KPX](docs/screenshot.png)

## Why

Most gradient tools lerp in gamma-encoded sRGB, which darkens and desaturates
the midpoints (blue→yellow turns gray in the middle). Perceptual Ramp lets you
interpolate in **OKLab / OKLCh** (and others), with proper **gamut mapping**
(chroma reduction, not clipping), so transitions stay even and vivid.

The same two-color midpoint, measured by OKLab lightness:
`sRGB ≈ 0.58` vs `OKLab ≈ 0.75` — the perceptual mix keeps its brightness.

## Features

- **Interpolation spaces:** sRGB, Linear RGB, **OKLab**, **OKLCh**, CIELAB,
  CIE LCh, HSL.
- **Hue paths** for cylindrical spaces (CSS Color 4): Shorter / Longer /
  Increasing / Decreasing — control how the hue wheel is traversed.
- **Gamut mapping** via OKLCh chroma reduction + MINDE (CSS Color 4), instead of
  hard clipping.
- **Multi-stop gradient editor** in the Effect Controls: live preview bar with
  draggable markers — click the bar to add a stop, drag to move, double-click for
  the color picker, Alt-click to delete.
- **Shapes:** Linear, Radial, Angular, Reflected, Diamond — driven by two
  on-canvas points (Start / End). Plus Repeat (None/Repeat/Mirror), Offset,
  Reverse.
- **From Map** mode: drive the gradient position from the **luminance of any
  layer** (with Input Black / White remap). Defaults to the layer itself.
- **Smart FX**, 8 / 16 / **32-bit float**, multi-threaded.

The color engine (`src/color_math.h`) and shape geometry (`src/RampShapes.h`)
are dependency-free C++ — unit-tested standalone and ready to port to a GPU
kernel later.

## Install (prebuilt)

1. Download `PerceptualRamp-*-win-x64.aex` from the
   [Releases](../../releases) page.
2. Copy it into your AE plug-ins folder, e.g.
   `C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\`.
3. Restart After Effects → **Effect → KPX → Perceptual Ramp**.

## Build from source

Requires **Visual Studio 2022+** (C++ desktop workload) and the
**Adobe After Effects SDK** (free, requires an Adobe ID):
<https://developer.adobe.com/after-effects/>.

```powershell
# Point the build at your unzipped SDK (the folder containing Examples\Headers):
$env:AE_SDK_PATH = "C:\path\to\AfterEffectsSDK\...\ae<ver>.AfterEffectsSDK"

.\build.ps1            # builds build\plugins\Ramp.aex
.\build.ps1 -Test      # also builds & runs the color_math unit tests
.\install.ps1          # copies the .aex into AE 2026 Plug-ins (self-elevates)
```

The PiPL resource is compiled by the SDK's `PiPLtool` as a project build step.
The unit test (`test/color_math_test.cpp`) needs no SDK.

## Project layout

```
src/
  color_math.h       # color spaces + interpolation + gamut mapping (no SDK)
  RampShapes.h       # pixel -> gradient parameter t (no SDK)
  Ramp.h / Ramp.cpp  # entry, params, classic + Smart render
  GradientData.cpp   # arbitrary-data (multi-stop) handlers
  GradientUI.cpp     # custom Drawbot UI (bar + markers + picker)
  RampPiPL.r         # PiPL resource
  win/Ramp.vcxproj   # self-contained VS project (references $(AE_SDK_PATH))
test/color_math_test.cpp
build.ps1  install.ps1
```

## License

[MIT](LICENSE). The Adobe After Effects SDK is **not** included and is governed
by Adobe's own license.
