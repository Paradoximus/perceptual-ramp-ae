// RampShapes.h
// ---------------------------------------------------------------------------
// Geometry of a gradient: maps a pixel coordinate to a gradient parameter
// t in [0,1]. Pure C++, no AE SDK deps (testable + GPU-portable, like
// color_math.h). The Map-driven shape is intentionally NOT handled here -- it
// reads luminance from a checked-out input layer, which only the render stage
// (with the SDK) can do.
// ---------------------------------------------------------------------------
#ifndef RAMP_SHAPES_H
#define RAMP_SHAPES_H

#include <cmath>

namespace shapes {

enum class Shape  : int { Linear = 0, Radial, Angular, Reflected, Diamond, Map };
enum class Repeat : int { None = 0, Repeat, Mirror };

struct ShapeParams {
    Shape  type;
    float  startX, startY;   // linear / reflected: start point (px)
    float  endX,   endY;     // linear / reflected: end point   (px)
    float  centerX, centerY; // radial / angular / diamond: center (px)
    float  radius;           // radial / diamond: radius (px), >0
    float  angle;            // angular: rotation offset (degrees)
    Repeat repeat;
    float  offset;           // phase added to t before repeat
    bool   reverse;          // flip t -> 1-t
};

inline float frac(float x) { return x - std::floor(x); }

// Triangle wave with period 2 over [0,1] -> mirror tiling.
inline float mirror_wave(float x) {
    float m = frac(x * 0.5f) * 2.0f;   // [0,2)
    return (m > 1.0f) ? (2.0f - m) : m;
}

// Raw, unbounded parameter for each geometric shape (before repeat/offset/
// reverse). Map shape returns 0 -- the render stage substitutes layer luma.
inline float raw_t(const ShapeParams& p, float x, float y) {
    switch (p.type) {
        case Shape::Linear:
        case Shape::Reflected: {
            float dx = p.endX - p.startX, dy = p.endY - p.startY;
            float len2 = dx * dx + dy * dy;
            if (len2 < 1e-7f) return 0.0f;
            float t = ((x - p.startX) * dx + (y - p.startY) * dy) / len2;
            if (p.type == Shape::Reflected) return std::fabs(2.0f * t - 1.0f);
            return t;
        }
        case Shape::Radial: {
            float dx = x - p.centerX, dy = y - p.centerY;
            float r = (p.radius > 1e-4f) ? p.radius : 1.0f;
            return std::sqrt(dx * dx + dy * dy) / r;
        }
        case Shape::Angular: {
            float dx = x - p.centerX, dy = y - p.centerY;
            float a = std::atan2(dy, dx) * 57.2957795131f - p.angle; // degrees
            a = std::fmod(a, 360.0f); if (a < 0.0f) a += 360.0f;
            return a / 360.0f;
        }
        case Shape::Diamond: {
            float dx = std::fabs(x - p.centerX), dy = std::fabs(y - p.centerY);
            float r = (p.radius > 1e-4f) ? p.radius : 1.0f;
            return std::fmax(dx, dy) / r;            // L-infinity distance
        }
        case Shape::Map:
        default:
            return 0.0f;
    }
}

// Apply offset, repeat mode, reverse, and clamp to [0,1].
inline float finalize_t(const ShapeParams& p, float t) {
    t += p.offset;
    switch (p.repeat) {
        case Repeat::Repeat: t = frac(t); break;
        case Repeat::Mirror: t = mirror_wave(t); break;
        case Repeat::None:
        default:             t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); break;
    }
    if (p.reverse) t = 1.0f - t;
    return t;
}

// Full pixel -> t for geometric shapes.
inline float shape_t(const ShapeParams& p, float x, float y) {
    return finalize_t(p, raw_t(p, x, y));
}

} // namespace shapes

#endif // RAMP_SHAPES_H
