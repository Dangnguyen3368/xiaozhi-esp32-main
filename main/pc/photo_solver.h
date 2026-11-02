#pragma once

#include <string>

// Explains a photo the user took with their phone: the phone opens <computer>/chup (served by
// tools/music_server/robot_may_tinh.py) and uploads the picture; the robot downloads it and sends
// it to the image explain service that the xiaozhi server announces at session start.
namespace photo_solver {

// Adds the self.pc.explain_photo MCP tool.
void RegisterTool();

// The address to open on the phone ("http://<computer>:8765/chup"), or empty when the computer
// has not been found yet.
std::string PhoneUrl();

}  // namespace photo_solver
