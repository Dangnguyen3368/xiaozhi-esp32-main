// Speed and traffic cameras in Vietnam from OpenStreetMap (highway=speed_camera and
// enforcement=*), fetched 2026-11-04 through the Overpass API. Data (c) OpenStreetMap
// contributors, ODbL 1.0 (https://www.openstreetmap.org/copyright). The map has few cameras in
// Vietnam; the user adds more by voice or on the robot's web page.
#include "nav_cameras.h"

namespace nav_cameras {

const Camera kBuiltIn[] = {
    {10.236916f, 106.378918f, 20},  // maxspeed
    {10.95609f, 106.812939f, 60},  // speed_camera
    {10.956093f, 106.812815f, 60},  // maxspeed
    {10.956218f, 106.812704f, 60},  // speed_camera
    {10.956219f, 106.812661f, 60},  // maxspeed
    {11.701422f, 109.005388f, 90},  // speed_camera
    {11.701541f, 109.005376f, 90},  // speed_camera
    {11.704362f, 109.019763f, 90},  // speed_camera
    {11.704481f, 109.019706f, 90},  // speed_camera
    {11.716612f, 109.032747f, 90},  // speed_camera
    {11.716679f, 109.032619f, 90},  // speed_camera
    {11.732144f, 109.04197f, 90},  // speed_camera
    {11.732157f, 109.041979f, 90},  // speed_camera
    {11.732214f, 109.041851f, 90},  // speed_camera
    {11.732225f, 109.041858f, 90},  // speed_camera
    {11.758753f, 109.057406f, 90},  // speed_camera
    {11.758825f, 109.057265f, 90},  // speed_camera
    {11.763463f, 109.060103f, 90},  // speed_camera
    {11.763527f, 109.059965f, 90},  // speed_camera
    {11.771389f, 109.064358f, 90},  // speed_camera
    {11.771456f, 109.06424f, 90},  // speed_camera
    {11.788681f, 109.069275f, 90},  // speed_camera
    {11.788707f, 109.069152f, 90},  // speed_camera
    {11.806376f, 109.072593f, 90},  // speed_camera
    {11.8064f, 109.072441f, 90},  // speed_camera
    {11.823099f, 109.079111f, 90},  // speed_camera
    {11.823177f, 109.078968f, 90},  // speed_camera
    {11.839727f, 109.086214f, 90},  // speed_camera
    {11.839775f, 109.086062f, 90},  // speed_camera
    {11.857534f, 109.087105f, 90},  // speed_camera
    {11.857568f, 109.087278f, 90},  // speed_camera
    {11.866259f, 109.084957f, 90},  // speed_camera
    {11.866267f, 109.08511f, 90},  // speed_camera
    {12.65969f, 108.030492f, 50},  // speed_camera
    {12.673664f, 108.045269f, 50},  // speed_camera
    {12.71206f, 108.069619f, 50},  // speed_camera
    {17.435474f, 106.634692f, 0},  // speed_camera
    {17.445567f, 106.632351f, 0},  // speed_camera
    {17.48566f, 106.572282f, 0},  // speed_camera
    {20.915508f, 105.950829f, 0},  // speed_camera
    {20.916043f, 105.950831f, 0},  // speed_camera
    {20.943412f, 106.000789f, 0},  // speed_camera
    {20.943416f, 106.000787f, 0},  // speed_camera
    {20.944514f, 105.991439f, 0},  // speed_camera
    {20.944515f, 105.991439f, 0},  // speed_camera
    {20.944518f, 105.99144f, 0},  // speed_camera
    {21.152051f, 106.230784f, 0},  // speed_camera
    {21.152052f, 106.230783f, 0},  // speed_camera
    {21.152052f, 106.230785f, 0},  // speed_camera
};
const size_t kBuiltInCount = sizeof(kBuiltIn) / sizeof(kBuiltIn[0]);

}  // namespace nav_cameras
