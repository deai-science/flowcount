#include "people_counter.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace pc {
namespace {
float cx(const Box& b) { return (b.x1 + b.x2) * .5f; }
float cy(const Box& b) { return (b.y1 + b.y2) * .5f; }
float area(const Box& b) { return (b.x2-b.x1)*(b.y2-b.y1); }
float iou(const Box& a, const Box& b) {
    float intersection = std::max(0.f, std::min(a.x2,b.x2)-std::max(a.x1,b.x1)) *
                         std::max(0.f, std::min(a.y2,b.y2)-std::max(a.y1,b.y1));
    float union_area = area(a)+area(b)-intersection;
    return union_area > 0 ? intersection / union_area : 0;
}
bool similar_area(const Box& previous, const Box& current) {
    return area(current)>area(previous)*.35f && area(current)<area(previous)*2.8f;
}
bool compatible_after_occlusion(const Box& previous, const Box& current) {
    if (similar_area(previous,current)) return true;
    const bool shared_edge=(previous.x1<=.015f && current.x1<=.015f) ||
        (previous.x2>=.985f && current.x2>=.985f) ||
        (previous.y1<=.015f && current.y1<=.015f) ||
        (previous.y2>=.985f && current.y2>=.985f);
    return shared_edge && area(current)>area(previous)*.20f && area(current)<area(previous);
}
bool bridges_occlusion(const Config& config, const Track& track, const Box& box, uint64_t gap) {
    if (gap<=config.max_crossing_gap_ms || gap>config.max_occlusion_gap_ms || !track.side || track.hits<3) return false;
    const float along=config.vertical?track.x:track.y;
    if (along<config.line-config.half_band || along>config.line+config.half_band) return false;
    // An observed approach into the band can survive a short occlusion. Do
    // not infer an entire crossing between two sightings outside the band.
    const float direction=-float(track.side);
    const float velocity=config.vertical?track.vx:track.vy;
    const float next=config.vertical?cx(box):cy(box);
    if (direction*velocity<.04f || direction*(next-along)<-.015f || !compatible_after_occlusion(track.box,box)) return false;
    const float dt=float(gap)/1000.f;
    const float dx=cx(box)-(track.x+track.vx*dt);
    const float dy=cy(box)-(track.y+track.vy*dt);
    return dx*dx+dy*dy<=.10f*.10f;
}
// Rectangular Hungarian assignment, including a dummy column for every track.
std::vector<int> assign(const std::vector<std::vector<float>>& cost) {
    int n = cost.size();
    if (!n) return {};
    int m = cost[0].size();
    std::vector<float> u(n+1), v(m+1);
    std::vector<int> p(m+1), way(m+1), answer(n,-1);
    for (int i=1; i<=n; ++i) {
        p[0]=i;
        int j0=0;
        std::vector<float> minv(m+1, std::numeric_limits<float>::infinity());
        std::vector<bool> used(m+1,false);
        do {
            used[j0]=true;
            int i0=p[j0], j1=0;
            float delta=std::numeric_limits<float>::infinity();
            for (int j=1; j<=m; ++j) if (!used[j]) {
                float cur=cost[i0-1][j-1]-u[i0]-v[j];
                if (cur<minv[j]) { minv[j]=cur; way[j]=j0; }
                if (minv[j]<delta) { delta=minv[j]; j1=j; }
            }
            for (int j=0; j<=m; ++j) {
                if (used[j]) { u[p[j]]+=delta; v[j]-=delta; }
                else minv[j]-=delta;
            }
            j0=j1;
        } while (p[j0]!=0);
        do { int j1=way[j0]; p[j0]=p[j1]; j0=j1; } while (j0);
    }
    for (int j=1; j<=m; ++j) if (p[j]) answer[p[j]-1]=j-1;
    return answer;
}
}
bool full_body_visible(const Box& b) {
    // Reject extreme close-ups spanning both vertical edges. Merely touching
    // one edge must not discard the first observations of a legitimate crossing.
    return !(b.y1<=.01f && b.y2>=.98f);
}
bool covers_image(const Box& b) {
    return b.x1<=.015f && b.y1<=.015f && b.x2>=.985f && b.y2>=.985f;
}
void Counter::configure(Config c) { config_=c; tracks_.clear(); }
void Counter::set_totals(uint64_t in, uint64_t out) { entries_=in; exits_=out; }
void Counter::reset() { set_totals(0,0); tracks_.clear(); }
void Counter::observe(Track& t, bool continuous, std::vector<Event>& events) {
    float along=config_.vertical ? t.x : t.y;
    float across=config_.vertical ? t.y : t.x;
    if (!continuous || across<config_.roi_min || across>config_.roi_max) {
        t.side=0; t.pending_side=0; t.pending_hits=0;
    }
    // A trajectory outside the gate cannot establish a counting origin.
    if (across<config_.roi_min || across>config_.roi_max) return;
    int side=along<config_.line-config_.half_band ? -1 :
             along>config_.line+config_.half_band ? 1 : 0;
    if (!side) { t.pending_side=0; t.pending_hits=0; return; }
    if (side!=t.pending_side) { t.pending_side=side; t.pending_hits=1; }
    else ++t.pending_hits;
    if (t.pending_hits<config_.confirm_frames) return;
    if (t.side && t.side!=side && t.hits>=3) {
        bool entry=(side==1)==config_.positive_is_entry;
        if (entry) ++entries_; else ++exits_;
        events.push_back({t.id,entry});
    }
    t.side=side;
}
std::vector<Event> Counter::update(const std::vector<Box>& input, uint64_t now) {
    std::vector<Detection> scored;
    for(const auto& b:input) scored.push_back({b,1.f});
    return update_scored(scored,now);
}
std::vector<Event> Counter::update_scored(const std::vector<Detection>& input, uint64_t now) {
    std::vector<Box> boxes;
    std::vector<float> scores;
    for (const auto& d:input) {
        const auto& b=d.box;
        if(!std::isfinite(d.score) || d.score<config_.continuation_score || d.score>1) continue;
        if (!std::isfinite(b.x1) || !std::isfinite(b.y1) || !std::isfinite(b.x2) || !std::isfinite(b.y2)) continue;
        Box c{std::clamp(b.x1,0.f,1.f),std::clamp(b.y1,0.f,1.f),
              std::clamp(b.x2,0.f,1.f),std::clamp(b.y2,0.f,1.f)};
        if(covers_image(c) || (config_.full_body_only && !full_body_visible(c))) continue;
        if (c.x1<c.x2 && c.y1<c.y2) { boxes.push_back(c);scores.push_back(d.score); }
        if (boxes.size()==max_tracks) break;
    }
    tracks_.erase(std::remove_if(tracks_.begin(),tracks_.end(),[&](const Track& t) {
        return now<t.last_ms || now-t.last_ms>config_.expire_ms;
    }),tracks_.end());
    const int n=tracks_.size(), m=boxes.size();
    constexpr float unmatched=1.f, forbidden=1000.f;
    std::vector<std::vector<float>> cost(n,std::vector<float>(m+n,unmatched));
    std::vector<std::vector<bool>> bridged(n,std::vector<bool>(m,false));
    for (int i=0;i<n;++i) {
        auto& t=tracks_[i]; t.visible=false;
        const uint64_t gap=now-t.last_ms;
        float dt=std::min(0.4f,float(gap)/1000.f);
        float px=t.x+t.vx*dt, py=t.y+t.vy*dt;
        for (int j=0;j<m;++j) {
            float dx=px-cx(boxes[j]), dy=py-cy(boxes[j]);
            float distance=std::sqrt(dx*dx+dy*dy);
            // A table can shorten a full-body box into a torso in one frame.
            // Its centre shift is not physical velocity. Keep prediction as
            // the preferred association, but bound its influence with the
            // measured displacement (and a small cost penalty).
            const float measured_dx=t.x-cx(boxes[j]), measured_dy=t.y-cy(boxes[j]);
            distance=std::min(distance,std::sqrt(measured_dx*measured_dx+measured_dy*measured_dy)+.03f);
            bridged[i][j]=bridges_occlusion(config_,t,boxes[j],gap);
            if (bridged[i][j]) {
                const float seconds=float(gap)/1000.f;
                const float predicted_dx=t.x+t.vx*seconds-cx(boxes[j]);
                const float predicted_dy=t.y+t.vy*seconds-cy(boxes[j]);
                distance=std::min(distance,std::sqrt(predicted_dx*predicted_dx+predicted_dy*predicted_dy));
            }
            cost[i][j]=distance>config_.max_distance ? forbidden :
                .8f*distance/config_.max_distance+.2f*(1.f-iou(t.box,boxes[j]));
        }
    }
    // Strong detections get first choice. Weak detections can only continue a
    // recently established track; they never create identities on their own.
    auto strong_cost=cost;
    for(int i=0;i<n;++i) for(int j=0;j<m;++j)
        if(scores[j]<config_.start_score) strong_cost[i][j]=forbidden;
    auto matches=assign(strong_cost);
    std::vector<bool> used(m,false);
    for(int i=0;i<n;++i) {
        int j=matches[i];
        if(j>=0 && j<m && strong_cost[i][j]<unmatched) used[j]=true;
        else matches[i]=-1;
    }
    std::vector<int> remaining;
    for(int i=0;i<n;++i) if(matches[i]<0) remaining.push_back(i);
    std::vector<std::vector<float>> weak_cost(remaining.size(),std::vector<float>(m+remaining.size(),unmatched));
    for(size_t r=0;r<remaining.size();++r) {
        int i=remaining[r];const auto& t=tracks_[i];
        for(int j=0;j<m;++j) {
            const float overlap=iou(t.box,boxes[j]);
            const auto gap=now-t.last_ms;
            const bool short_match=gap<=config_.max_crossing_gap_ms && overlap>.10f &&
                (t.hits>=2 || (gap<=200 && overlap>.25f)) && similar_area(t.box,boxes[j]);
            const bool eligible=!used[j] && scores[j]<config_.start_score &&
                now-t.strong_ms<=config_.expire_ms &&
                (short_match || bridged[i][j]);
            weak_cost[r][j]=eligible?cost[i][j]:forbidden;
        }
    }
    auto weak_matches=assign(weak_cost);
    for(size_t r=0;r<remaining.size();++r) {
        int j=weak_matches[r];
        if(j>=0 && j<m && weak_cost[r][j]<unmatched) {matches[remaining[r]]=j;used[j]=true;}
    }
    std::vector<Event> events;
    for (int i=0;i<n;++i) {
        int j=matches[i];
        if (j<0) continue; // Keep side evidence through a short missed frame.
        used[j]=true;
        auto& t=tracks_[i];
        uint64_t gap=now-t.last_ms;
        float x=cx(boxes[j]), y=cy(boxes[j]);
        if (gap>0) {
            float dt=float(gap)/1000.f;
            t.vx=.5f*t.vx+.5f*(x-t.x)/dt;
            t.vy=.5f*t.vy+.5f*(y-t.y)/dt;
        }
        t.x=x; t.y=y; t.box=boxes[j]; t.last_ms=now; t.visible=true; ++t.hits;
        t.score=scores[j];t.recovered=scores[j]<config_.start_score;
        if(!t.recovered) t.strong_ms=now;
        observe(t,gap<=config_.max_crossing_gap_ms || bridged[i][j],events);
    }
    for (int j=0;j<m && tracks_.size()<max_tracks;++j) if (!used[j] && scores[j]>=config_.start_score) {
        Track t;
        t.id=next_id_++; t.box=boxes[j]; t.x=cx(t.box); t.y=cy(t.box);
        t.last_ms=now; t.visible=true; t.hits=1;
        t.score=scores[j];t.strong_ms=now;
        observe(t,true,events); tracks_.push_back(t);
    }
    return events;
}
} // namespace pc
