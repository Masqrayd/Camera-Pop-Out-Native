#pragma once
#include <algorithm>
#include <cmath>
namespace camera {
struct Crop { double left, top, width, height; };
inline Crop crop(double fw, double fh, double vw, double vh, double zoom, double cx, double cy)
{
    if (fw <= 0 || fh <= 0 || vw <= 0 || vh <= 0) return {0,0,0,0};
    zoom = std::clamp(zoom, 1.0, 8.0);
    const double scale = std::max(vw/fw, vh/fh) * zoom;
    const double w = std::min(fw, vw/scale), h = std::min(fh, vh/scale);
    const double x = std::clamp(cx*fw, w/2, fw-w/2);
    const double y = std::clamp(cy*fh, h/2, fh-h/2);
    return {x-w/2, y-h/2, w, h};
}
}
