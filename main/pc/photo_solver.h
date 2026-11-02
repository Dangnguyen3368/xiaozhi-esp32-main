#pragma once

#include <string>

// Explains a photo the user takes with their phone, without a computer: the robot serves a small
// web page (http://<robot>:8080/chup) on the home Wi-Fi; the phone takes the picture and uploads
// it to the robot, which sends it to the image explain service the xiaozhi server announces at
// session start.
namespace photo_solver {

// Adds the self.pc.explain_photo MCP tool.
void RegisterTool();

// Starts the robot's photo page if needed and returns its address for the phone
// ("http://<robot ip>:8080/chup"); empty when the robot has no Wi-Fi address.
std::string PhoneUrl();

}  // namespace photo_solver
