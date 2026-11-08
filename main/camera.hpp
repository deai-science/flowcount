#pragma once
#include <cstdint>
#include <cstddef>
#include "esp_err.h"
#include <string>
#include "camera_settings.hpp"

class Camera {
public:
    esp_err_t start();
    // Capture to an owned 320x240 RGB565 buffer; V4L2 buffers are returned immediately.
    esp_err_t capture();
    esp_err_t configure(const CameraSettings& settings);
    const CameraSettings& settings() const { return settings_; }
    std::string status(bool dirty);
    uint8_t* pixels() const { return pixels_; }
    static constexpr int width = 320, height = 240;
private:
    int fd_ = -1;
    uint8_t* buffers_[2]{};
    size_t lengths_[2]{};
    uint32_t sensor_width_ = 0, sensor_height_ = 0, stride_ = 0;
    uint8_t* pixels_ = nullptr;
    uint32_t row_offsets_[height]{};
    uint16_t column_offsets_[width]{};
    CameraSettings settings_;
    uint32_t line_ns_=0;
    int sensor_fps_=0;
    int exposure_min_=1, exposure_max_=1, gain_max_=0;
    bool exposure_supported_=false, gain_supported_=false;
    int auto_exposure_lines_=-1, auto_gain_=-1;
    float mean_luma_=0, luma_stddev_=0;
    int dark_percent_=0, bright_percent_=0;
    int read_control(uint32_t id) const;
    bool write_control(uint32_t id, int value) const;
};
