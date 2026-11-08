#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

struct WebCommand { uint32_t id; char text[128]; };
void device_web_start();
void device_web_publish_state(const std::string& json);
void device_web_publish_jpeg(const uint8_t* bytes, size_t size, const std::string& state, uint32_t frame);
void device_web_publish_event(const std::string& json);
bool device_web_take_command(WebCommand& command);
void device_web_complete_command(uint32_t id, const char* name, bool ok);
std::string device_web_network_info();
bool device_web_configure_network(const std::string& json);
bool device_web_set_ap_key(const std::string& key); // UART maintenance; effective after reboot.
bool device_web_request_scan();
std::string device_web_scan_results();
