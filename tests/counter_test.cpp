#include "people_counter.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

pc::Box person(float x,float y) { return {x-.04f,y-.09f,x+.04f,y+.09f}; }
void path(pc::Counter& c, const std::vector<float>& values, uint64_t& ms, bool vertical=false) {
    for(float v:values) { c.update({vertical?person(v,.5f):person(.5f,v)},ms); ms+=100; }
}
int main() {
    int passed=0;
    {
        pc::Counter c; uint64_t t=0;
        path(c,{.2,.25,.33,.42,.5,.58,.65,.72,.8,.8},t);
        assert(c.entries()==1 && c.exits()==0); ++passed;
        path(c,{.78,.70,.60,.51,.42,.32,.25,.2},t);
        assert(c.entries()==1 && c.exits()==1); ++passed;
    }
    {
        pc::Counter c; uint64_t t=0;
        path(c,{.3,.31,.43,.47,.51,.49,.53,.48,.52,.44,.3,.31},t);
        assert(c.entries()==0 && c.exits()==0); ++passed;
    }
    {
        pc::Counter c; uint64_t t=0;
        path(c,{.5,.58,.65,.73,.8,.8},t);
        assert(c.entries()==0 && c.exits()==0); ++passed; // appeared inside band
    }
    {
        pc::Counter c; uint64_t t=0;
        path(c,{.3,.3,.4,.43},t); c.update({},t); t+=600;
        path(c,{.57,.64,.72},t);
        assert(c.entries()==0 && c.exits()==0); ++passed; // occlusion across gate
    }
    {
        pc::Counter c;
        for(int i=0;i<14;++i) {
            float y=.2f+i*.045f;
            c.update({person(.35f,y),person(.65f,1-y)},i*100);
        }
        assert(c.entries()==1 && c.exits()==1); ++passed;
    }
    {
        pc::Config cfg; cfg.vertical=true; cfg.positive_is_entry=false;
        pc::Counter c(cfg); uint64_t t=0;
        path(c,{.2,.25,.33,.42,.5,.58,.65,.72},t,true);
        assert(c.entries()==0 && c.exits()==1); ++passed;
    }
    {
        pc::Counter c;
        for(int i=0;i<14;++i) c.update({person(.02f,.2f+i*.045f)},i*100);
        assert(c.entries()==0 && c.exits()==0); ++passed; // outside gate width
    }
    {
        pc::Counter c; uint64_t t=0;
        path(c,{.3,.3,.4,.5,.6},t); c.update({},t); t+=100;
        path(c,{.67,.7},t);
        assert(c.entries()==1 && c.exits()==0); ++passed; // brief missed detection
    }
    {
        pc::Counter c; uint64_t t=0;
        path(c,{.3,.31,.4,.5,.6,.7},t);
        c.set_totals(8,12);
        assert(c.balance()==-4); ++passed;
        c.reset(); assert(c.entries()==0 && c.exits()==0 && c.tracks().empty()); ++passed;
    }
    {
        pc::Counter c;
        float nan=std::numeric_limits<float>::quiet_NaN();
        c.update({{nan,0,1,1},{.5f,.5f,.1f,.1f}},0);
        assert(c.tracks().empty()); ++passed;
        std::vector<pc::Box> boxes(100,person(.4f,.3f));
        c.update(boxes,100); assert(c.tracks().size()<=pc::Counter::max_tracks); ++passed;
    }
    {
        pc::Counter c; uint64_t t=0;
        path(c,{.3,.3},t); c.update({},5000);
        assert(c.tracks().empty()); ++passed;
    }
    {
        // The installed upright-camera calibration: entering from right to left.
        pc::Config config; config.vertical=true; config.positive_is_entry=false;
        config.line=.515f; config.half_band=.105f; config.roi_min=0; config.roi_max=1;
        pc::Counter c(config); uint64_t t=0;
        path(c,{.85,.82,.72,.65,.55,.45,.35,.25,.2,.2},t,true);
        path(c,{.2,.22,.32,.4,.5,.6,.7,.8,.85,.85},t,true);
        assert(c.entries()==1 && c.exits()==1); ++passed;
        c.configure(config);
        assert(c.entries()==1 && c.exits()==1 && c.tracks().empty()); ++passed;
    }
    for(unsigned gap:{400u,401u}) {
        pc::Counter c;
        c.update({person(.5f,.42f)},0);c.update({person(.5f,.43f)},100);
        c.update({},200);
        c.update({person(.5f,.57f)},100+gap);c.update({person(.5f,.62f)},200+gap);
        assert(c.entries()==(gap==400?1u:0u) && c.exits()==0); ++passed;
    }
    {
        pc::Counter c;
        c.update({person(.5f,.3f)},10000);c.update({person(.5f,.31f)},10100);
        c.update({person(.5f,.65f)},100);c.update({person(.5f,.7f)},200);
        assert(c.entries()==0 && c.exits()==0); ++passed; // A reset clock cannot complete an old crossing.
    }
    {
        // Reproduce a near-camera torso whose inferred box spans the image height.
        // It crossed the band in the recorded sequence even though this was not
        // a complete-body passage through the intended access.
        for(bool enabled:{false,true}) {
            pc::Config config;config.vertical=true;config.full_body_only=enabled;
            pc::Counter c(config);
            unsigned t=0;
            for(float x:{.2f,.25f,.35f,.45f,.55f,.65f,.75f,.8f}) {
                c.update({{x-.08f,0,x+.08f,.9958f}},t);t+=100;
            }
            assert(c.entries()==(enabled?0u:1u) && c.exits()==0);++passed;
        }
        pc::Config config;config.vertical=true;
        pc::Counter c(config);uint64_t t=0;
        path(c,{.2,.25,.35,.45,.55,.65,.75,.8},t,true);
        assert(c.entries()==1 && c.exits()==0);++passed; // A fully visible body still counts.
        pc::Counter clipped(config);t=0;
        for(float x:{.2f,.25f,.35f,.45f,.55f,.65f,.75f,.8f}) {
            clipped.update({{x-.08f,.2f,x+.08f,1}},t);t+=100;
        }
        assert(clipped.entries()==1 && clipped.exits()==0);++passed; // One edge alone is not an extreme close-up.
        pc::Config installed;installed.vertical=true;installed.line=.515f;installed.half_band=.105f;installed.positive_is_entry=false;
        pc::Counter entering(installed);
        const std::vector<pc::Box> recorded={
            {.6375f,0,.85f,.95f},{.5219f,0,.7906f,.975f},
            {.5125f,.0125f,.7656f,.9458f},{.4656f,.025f,.7406f,.95f},
            {.4469f,.0542f,.6531f,.9125f},{.3812f,.1f,.5906f,.9167f},
            {.3281f,.1458f,.475f,.8f},{.3031f,.1583f,.4563f,.8f}};
        unsigned ms=0;for(const auto& box:recorded){entering.update({box},ms);ms+=120;}
        assert(entering.entries()==1 && entering.exits()==0);++passed;
    }
    {
        // A single missing frame between the two side observations used to
        // erase all evidence, even though the identity was still associated.
        pc::Counter c;
        c.update({person(.5f,.30f)},0);c.update({},100);
        c.update({person(.5f,.32f)},200);c.update({person(.5f,.43f)},300);
        c.update({person(.5f,.53f)},400);c.update({person(.5f,.62f)},500);
        c.update({},600);c.update({person(.5f,.65f)},700);
        assert(c.entries()==1 && c.exits()==0);++passed;
    }
    {
        // Score flicker: weak boxes continue a strong identity through the
        // gate. The same boxes with no strong origin must never count.
        for(bool strong_origin:{false,true}) {
            pc::Config cfg;cfg.vertical=true;cfg.start_score=.75f;
            pc::Counter c(cfg);unsigned ms=0;
            for(float x:{.25f,.28f,.34f,.40f,.46f,.52f,.58f,.64f,.70f}) {
                const float score=strong_origin && ms<200?.85f:.5f;
                c.update_scored({{person(x,.5f),score}},ms);ms+=100;
            }
            assert(c.entries()==(strong_origin?1u:0u) && c.exits()==0);++passed;
            if(strong_origin){assert(c.tracks().size()==1 && c.tracks()[0].recovered);}
        }
    }
    {
        // A weak false candidate cannot steal an association from a strong
        // observation or create an additional identity.
        pc::Config cfg;cfg.vertical=true;pc::Counter c(cfg);
        c.update_scored({{person(.3f,.5f),.9f}},0);
        c.update_scored({{person(.31f,.5f),.35f},{person(.34f,.5f),.9f}},100);
        assert(c.tracks().size()==1 && !c.tracks()[0].recovered && c.tracks()[0].x>.33f);++passed;
    }
    {
        // Continuation requires overlap, similar scale and recent strong
        // evidence; weak clutter cannot keep an identity alive indefinitely.
        pc::Config cfg;cfg.vertical=true;pc::Counter c(cfg);
        c.update_scored({{person(.3f,.5f),.9f}},0);
        c.update_scored({{person(.31f,.5f),.9f}},100);
        c.update_scored({{person(.5f,.5f),.4f}},200);
        assert(c.tracks().size()==1 && !c.tracks()[0].visible);++passed;
        for(unsigned ms=300;ms<=1500;ms+=100)c.update_scored({{person(.31f,.5f),.4f}},ms);
        for(const auto& t:c.tracks())assert(!t.visible);
        assert(c.entries()==0 && c.exits()==0);++passed;
    }
    {
        // Regression from an occluded walk: a torso box shifts the centre
        // upward, then 2 frames are lost. Extrapolating that height change as
        // speed used to reject the returning full box and lose the crossing.
        pc::Config cfg;cfg.vertical=true;cfg.positive_is_entry=false;
        cfg.line=.47f;cfg.half_band=.105f;cfg.start_score=.55f;
        pc::Counter c(cfg);unsigned ms=0;
        for(float x:{.75f,.72f,.65f,.60f,.55f,.50f,.48f}) {
            c.update_scored({{{x-.06f,.26f,x+.06f,.72f},.8f}},ms);ms+=125;
        }
        c.update_scored({{{.38125f,.2625f,.49062f,.49583f},.55199f}},ms);
        c.update_scored({},ms+125);c.update_scored({},ms+250);
        c.update_scored({{{.34375f,.2625f,.4375f,.6125f},.52f}},ms+375);
        c.update_scored({{{.29375f,.2625f,.40938f,.61667f},.38f}},ms+500);
        c.update_scored({{{.29375f,.25833f,.4125f,.62083f},.35f}},ms+625);
        assert(c.entries()==1 && c.exits()==0 && c.tracks().size()==1);++passed;
    }
    {
        // A person observed entering the band briefly disappears in a group.
        // Re-link only a plausible continuation and require two destination hits.
        for(bool reverse:{false,true}) {
            for(uint64_t gap:{750u,901u}) {
                pc::Config config; config.vertical=true;
                pc::Counter counter(config);
                auto sample=[&](float x,float score,uint64_t now) {
                    counter.update_scored({{person(reverse?1-x:x,.5f),score}},now);
                };
                uint64_t now=0;
                for(float x:{.23f,.25f,.27f,.29f,.32f,.35f,.38f,.41f,.44f,.47f,.50f}) {
                    sample(x,.8f,now); now+=125;
                }
                const uint64_t returned=now-125+gap;
                counter.update({},now);
                sample(.67f,.43f,returned);
                assert(counter.entries()==0 && counter.exits()==0);
                sample(.70f,.46f,returned+125);
                assert(counter.entries()==(!reverse && gap==750?1u:0u));
                assert(counter.exits()==(reverse && gap==750?1u:0u));
                ++passed;
            }
        }
    }
    {
        // A strong first sighting followed by an overlapping weaker one can
        // establish the origin. Weak observations still never create a track.
        pc::Config config; config.vertical=true;
        pc::Counter counter(config);
        uint64_t now=0;
        for(float x:{.27f,.29f,.33f,.38f,.44f,.50f,.56f,.62f,.67f}) {
            counter.update_scored({{person(x,.5f),now==0?.8f:.48f}},now);
            now+=100;
        }
        assert(counter.entries()==1 && counter.exits()==0);
        ++passed;
    }
    {
        // The foreground person's feet remained clipped when they reappeared.
        // The visible box shrinks, but the predicted centre remains compatible.
        pc::Config config; config.vertical=true; config.positive_is_entry=false;
        config.line=.47f; config.half_band=.105f; config.start_score=.50f;
        pc::Counter counter(config);
        const std::vector<float> xs={.828f,.820f,.778f,.753f,.698f,.675f,.616f,.614f,.511f};
        const std::vector<float> ys={.694f,.704f,.725f,.735f,.746f,.748f,.765f,.760f,.8125f};
        const std::vector<uint64_t> times={0,125,375,500,625,750,1000,1125,1500};
        for(size_t i=0;i<xs.size();++i) {
            counter.update_scored({{{xs[i]-.065f,ys[i]-.18f,xs[i]+.065f,ys[i]+.184f},.6f}},times[i]);
        }
        counter.update_scored({{{.30f,.80f,.375f,.996f},.468f}},2250);
        assert(counter.entries()==0);
        counter.update_scored({{{.222f,.729f,.369f,.996f},.3125f}},2375);
        assert(counter.entries()==1 && counter.exits()==0);++passed;
    }
    {
        pc::Config config; config.full_body_only=false;
        pc::Counter counter(config);
        counter.update_scored({{{0,0,.9969f,.9958f},.8f}},0);
        assert(counter.tracks().empty());++passed;
        counter.update_scored({{{.1f,.05f,.9f,.95f},.8f}},100);
        assert(counter.tracks().size()==1);++passed;
    }
    std::cout<<passed<<" counting scenarios passed\n";
}
