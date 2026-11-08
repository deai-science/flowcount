#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace pc {
// All coordinates are fractions of the full image, in [0,1].
struct Box { float x1, y1, x2, y2; };
struct Detection { Box box; float score=1.f; };
bool full_body_visible(const Box& box);
bool covers_image(const Box& box);
struct Config {
    bool vertical = false;   // false: horizontal gate, movement along Y
    bool positive_is_entry = true;
    float line = .50f;
    float half_band = .06f;
    float roi_min = .05f, roi_max = .95f; // transverse extent of the gate
    float max_distance = .20f;
    int confirm_frames = 2;
    uint64_t expire_ms = 1200;
    uint64_t max_crossing_gap_ms = 400;
    uint64_t max_occlusion_gap_ms = 900;
    bool full_body_only = true;
    float start_score = .55f;
    float continuation_score = .30f;
};
struct Track {
    uint32_t id = 0;
    Box box{};
    float x = 0, y = 0, vx = 0, vy = 0;
    uint64_t last_ms = 0;
    int hits = 0, side = 0, pending_side = 0, pending_hits = 0;
    bool visible = false;
    float score = 0;
    uint64_t strong_ms = 0;
    bool recovered = false;
};
struct Event { uint32_t id; bool entry; };
class Counter {
public:
    explicit Counter(Config config = {}) : config_(config) {}
    std::vector<Event> update(const std::vector<Box>& boxes, uint64_t now_ms);
    std::vector<Event> update_scored(const std::vector<Detection>& detections, uint64_t now_ms);
    void configure(Config config); // discards trajectories; preserves totals
    void set_totals(uint64_t entries, uint64_t exits);
    void reset();
    const std::vector<Track>& tracks() const { return tracks_; }
    const Config& config() const { return config_; }
    uint64_t entries() const { return entries_; }
    uint64_t exits() const { return exits_; }
    int64_t balance() const { return int64_t(entries_) - int64_t(exits_); }
    static constexpr size_t max_tracks = 24;
private:
    void observe(Track& track, bool continuous, std::vector<Event>& events);
    Config config_;
    std::vector<Track> tracks_;
    uint32_t next_id_ = 1;
    uint64_t entries_ = 0, exits_ = 0;
};
} // namespace pc
