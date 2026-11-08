#include "detection_policy.hpp"
#include <cassert>
#include <limits>
#include <iostream>

int main() {
    pc::Box body{.3f,.1f,.5f,.9f},closeup{.3f,0,.5f,1};
    using D=pc::DetectionDecision;
    assert(pc::detection_decision(body,.60f,.75f,true)==D::LowScore);
    assert(pc::detection_decision(body,.80f,.75f,true)==D::Trackable);
    assert(pc::detection_decision(closeup,.80f,.75f,true)==D::Closeup);
    assert(pc::detection_decision(closeup,.60f,.75f,true)==D::Closeup);
    assert(pc::detection_decision(closeup,.80f,.75f,false)==D::Trackable);
    // Structures and a covered/moving lens produced near-full-frame proposals
    // in the November recordings even with the close-up filter disabled.
    assert(pc::detection_decision({0,0,.9969f,.9958f},.80f,.50f,false)==D::Closeup);
    assert(pc::detection_decision({.1f,.05f,.9f,.95f},.80f,.50f,false)==D::Trackable);
    assert(pc::detection_decision(body,std::numeric_limits<float>::quiet_NaN(),.75f,true)==D::Invalid);
    assert(pc::detection_decision({.5f,.1f,.3f,.9f},.8f,.75f,true)==D::Invalid);
    // Weak observations cannot start an identity or produce a crossing alone.
    pc::Config config;config.start_score=.75f;pc::Counter counter(config);
    for(unsigned i=0;i<12;++i) {
        pc::Box moving{.3f,.1f+i*.05f,.5f,.3f+i*.05f};
        counter.update_scored({{moving,.6f}},i*100);
    }
    assert(counter.entries()==0 && counter.exits()==0 && counter.tracks().empty());
    std::cout<<"Weak candidates cannot create identities or counts alone\n";
}
