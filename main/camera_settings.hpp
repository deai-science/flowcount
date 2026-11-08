#pragma once
#include <cstdint>

// Kept separate from the counter settings so existing NVS totals survive updates.
struct CameraSettings {
    uint32_t version=1;
    int32_t fps=8, quality=60;
    int32_t automatic=1, exposure_us=8000, max_exposure_us=20000, gain=9;
    int32_t brightness=0, contrast=100, saturation=100, mirror=0, flip=0;
};
inline bool valid_camera_settings(const CameraSettings& s) {
    return s.version==1 && s.fps>=1 && s.fps<=12 && s.quality>=30 && s.quality<=85 &&
        (s.automatic==0 || s.automatic==1) && s.exposure_us>=1 && s.exposure_us<=100000 &&
        s.max_exposure_us>=1 && s.max_exposure_us<=100000 && s.gain>=0 && s.gain<=255 &&
        s.brightness>=-64 && s.brightness<=64 && s.contrast>=50 && s.contrast<=150 &&
        s.saturation>=0 && s.saturation<=150 && (s.mirror==0 || s.mirror==1) &&
        (s.flip==0 || s.flip==1);
}
