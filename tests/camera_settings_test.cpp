#include "camera_settings.hpp"
#include <cassert>
#include <iostream>

int main() {
    CameraSettings defaults;
    assert(valid_camera_settings(defaults));
    // The stream and exposure ranges are independent: requesting more preview
    // frames must never silently change exposure or the detector input.
    auto fast=defaults; fast.fps=12;
    assert(valid_camera_settings(fast));
    assert(fast.exposure_us==defaults.exposure_us && fast.contrast==defaults.contrast);
    auto invalid=defaults; invalid.fps=0; assert(!valid_camera_settings(invalid));
    invalid=defaults; invalid.fps=13; assert(!valid_camera_settings(invalid));
    invalid=defaults; invalid.quality=100; assert(!valid_camera_settings(invalid));
    invalid=defaults; invalid.exposure_us=-1; assert(!valid_camera_settings(invalid));
    invalid=defaults; invalid.gain=-1; assert(!valid_camera_settings(invalid));
    invalid=defaults; invalid.mirror=2; assert(!valid_camera_settings(invalid));
    invalid=defaults; invalid.version=2; assert(!valid_camera_settings(invalid));
    auto dark=defaults; dark.brightness=-64; dark.saturation=0; dark.contrast=150;
    assert(valid_camera_settings(dark));
    std::cout<<"Camera settings boundaries passed\n";
}
