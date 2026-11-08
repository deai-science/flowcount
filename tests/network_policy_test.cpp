#include "network_policy.hpp"
#include <cassert>
#include <iostream>

int main() {
    NetworkRetry retry; retry.reset(100);
    assert(retry.due(100)); retry.begin(100);
    assert(!retry.due(200) && !retry.expired(20099) && retry.expired(20100));
    retry.failed(20100);assert(!retry.due(21099) && retry.due(21100));
    retry.begin(21100);retry.failed(22000);assert(retry.next==24000);
    for(int i=0;i<20;++i)retry.failed(100000);
    assert(retry.next==130000); // Wrong credentials must not produce a busy retry loop.
    retry.connected();assert(!retry.active && retry.failures==0);
    retry.failed(200000);assert(retry.next==201000);
    retry.begin(201000);retry.reset(201500);
    assert(retry.due(201500) && !retry.active); // A corrected SSID retries immediately.
    std::cout<<"Network retry, timeout and recovery scenarios passed\n";
}
