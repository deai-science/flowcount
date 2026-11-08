#pragma once
#include <cmath>
#include "people_counter.hpp"

namespace pc {
constexpr float diagnostic_score_floor=.30f;
enum class DetectionDecision { Invalid, LowScore, Closeup, Trackable };
inline DetectionDecision detection_decision(const Box& b,float score,float threshold,bool filter_closeups) {
    if(!std::isfinite(score) || score<0 || score>1 || !std::isfinite(b.x1) || !std::isfinite(b.y1) ||
       !std::isfinite(b.x2) || !std::isfinite(b.y2) || b.x1>=b.x2 || b.y1>=b.y2)
        return DetectionDecision::Invalid;
    if(covers_image(b) || (filter_closeups && !full_body_visible(b))) return DetectionDecision::Closeup;
    if(score<threshold) return DetectionDecision::LowScore;
    return DetectionDecision::Trackable;
}
inline const char* detection_reason(DetectionDecision d) {
    switch(d) {
        case DetectionDecision::LowScore:return "low_score";
        case DetectionDecision::Closeup:return "closeup";
        case DetectionDecision::Trackable:return "tracked";
        default:return "invalid";
    }
}
}
