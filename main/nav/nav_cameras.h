#pragma once

#include <cstddef>

namespace nav_cameras {

struct Camera {
    float lat;
    float lon;
    int limit_kmh;  // 0 when unknown
};

// Cameras from OpenStreetMap (nav_cameras_osm.cc).
extern const Camera kBuiltIn[];
extern const size_t kBuiltInCount;

}  // namespace nav_cameras
