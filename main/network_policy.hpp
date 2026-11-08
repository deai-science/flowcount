#pragma once
#include <algorithm>
#include <cstdint>

// Pure timing policy: driver callbacks stay outside this class.
struct NetworkRetry {
    uint64_t started=0, next=0;
    unsigned failures=0;
    bool active=false;
    static constexpr uint64_t attempt_timeout=20000;
    void reset(uint64_t now) { active=false; failures=0; next=now; }
    void begin(uint64_t now) { active=true; started=now; }
    void connected() { active=false; failures=0; next=0; }
    void failed(uint64_t now) {
        active=false;
        failures=std::min(failures+1,6u);
        next=now+std::min<uint64_t>(30000,1000ull<<(failures-1));
    }
    bool expired(uint64_t now) const { return active && now>=started && now-started>=attempt_timeout; }
    bool due(uint64_t now) const { return !active && now>=next; }
};
