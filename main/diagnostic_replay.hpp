#pragma once
#include <string>
class PedestrianDetect;
// Physical UART only. Runs the actual P4 detector on a supplied JPEG without
// modifying saved counts/settings. Never exposed through the HTTP command API.
bool diagnostic_replay(const std::string& line, PedestrianDetect& model);
