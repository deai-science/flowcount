#include "device_web.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <vector>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_hosted.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "nvs.h"
#include "mdns.h"
#include "dns_server.h"
#include "network_policy.hpp"
#include "dhcpserver/dhcpserver.h"
#include "cJSON.h"
#include "mbedtls/base64.h"

namespace {
constexpr const char* TAG="device_web";
constexpr size_t jpeg_capacity=65536;
constexpr const char* ap_ip="192.168.4.1";
SemaphoreHandle_t mutex;
QueueHandle_t commands;
std::string current_state="{}", image_state="{}";
uint8_t* jpeg_bytes=nullptr;
size_t jpeg_size=0;
uint32_t jpeg_frame=0;
int64_t state_at=0;
std::deque<std::string> events;
uint32_t command_id=0, completed_id=0;
bool completed_ok=false;
std::string completed_name;
char ap_ssid[33]{}, access_key[33]{}, hostname[40]{};
char sta_ssid[33]{}, sta_password[65]{}, sta_ip[16]{};
struct StationCredentials { char ssid[33]; char password[65]; };
bool sta_connected=false, sta_connecting=false, change_pending=false, network_ready=false;
int disconnect_reason=0;
uint32_t disconnect_sequence=0, connection_attempts=0;
uint64_t retry_at_ms=0, connected_at_ms=0;
int startup_error=0, signal_dbm=0;
bool mdns_ready=false, scan_requested=false, scan_running=false, scan_finished=false;
std::string scan_json="{\"scanning\":false,\"networks\":[],\"error\":\"\"}";
std::string authorization;
nvs_handle_t network_nvs;
httpd_handle_t http_server=nullptr;

struct Lock {
    Lock() { xSemaphoreTake(mutex,portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(mutex); }
};
std::string json_text(cJSON* object) {
    char* text=cJSON_PrintUnformatted(object);
    std::string result=text ? text : "{}";
    cJSON_free(text); cJSON_Delete(object); return result;
}
std::string network_json_locked() {
    cJSON* root=cJSON_CreateObject();
    cJSON_AddStringToObject(root,"ap_ssid",ap_ssid);
    cJSON_AddStringToObject(root,"ap_ip",ap_ip);
    cJSON_AddStringToObject(root,"hostname",hostname);
    cJSON_AddStringToObject(root,"sta_ssid",sta_ssid);
    cJSON_AddStringToObject(root,"sta_ip",sta_ip);
    cJSON_AddBoolToObject(root,"sta_connected",sta_connected);
    cJSON_AddBoolToObject(root,"ready",network_ready);
    cJSON_AddNumberToObject(root,"disconnect_reason",disconnect_reason);
    const uint64_t now=esp_timer_get_time()/1000;
    const char* phase=!network_ready ? (startup_error?"error":"starting") : sta_connected?"connected" :
        !sta_ssid[0]?"setup":sta_connecting?"connecting":"retrying";
    const char* detail=sta_connected ? "Connected to venue Wi-Fi" : !sta_ssid[0] ? "Choose a 2.4 GHz network" :
        disconnect_reason==WIFI_REASON_NO_AP_FOUND ? "Network not found. Check the exact name or scan for networks." :
        disconnect_reason==WIFI_REASON_AUTH_FAIL || disconnect_reason==WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
        disconnect_reason==WIFI_REASON_HANDSHAKE_TIMEOUT ?
        "Authentication failed. Check the password and network security." :
        startup_error ? "Wi-Fi failed to start. Retrying while counting continues." :
        "Connecting. The device access point remains available.";
    cJSON_AddStringToObject(root,"phase",phase);
    cJSON_AddStringToObject(root,"message",detail);
    cJSON_AddNumberToObject(root,"retry_in_s",retry_at_ms>now ? (retry_at_ms-now+999)/1000 : 0);
    cJSON_AddNumberToObject(root,"attempts",connection_attempts);
    cJSON_AddNumberToObject(root,"startup_error",startup_error);
    cJSON_AddNumberToObject(root,"rssi",signal_dbm);
    cJSON_AddNumberToObject(root,"connected_for_s",sta_connected && connected_at_ms ? (now-connected_at_ms)/1000 : 0);
    cJSON_AddBoolToObject(root,"mdns_ready",mdns_ready);
    cJSON_AddBoolToObject(root,"scanning",scan_requested || scan_running);
    return json_text(root);
}
bool authenticated(httpd_req_t* request) {
    char header[160]{};
    if(httpd_req_get_hdr_value_str(request,"Authorization",header,sizeof(header))==ESP_OK && authorization==header)
        return true;
    httpd_resp_set_status(request,"401 Unauthorized");
    httpd_resp_set_hdr(request,"WWW-Authenticate","Basic realm=\"FlowCount\"");
    httpd_resp_sendstr(request,"Sign in with username admin and password admin.");
    return false;
}
esp_err_t send_json(httpd_req_t* request, const std::string& json) {
    httpd_resp_set_type(request,"application/json; charset=utf-8");
    httpd_resp_set_hdr(request,"Cache-Control","no-store");
    httpd_resp_set_hdr(request,"X-Content-Type-Options","nosniff");
    return httpd_resp_send(request,json.data(),json.size());
}
bool same_origin(httpd_req_t* request) {
    if(!httpd_req_get_hdr_value_len(request,"Origin")) return true;
    char origin[160]{}, host[128]{};
    if(httpd_req_get_hdr_value_str(request,"Origin",origin,sizeof(origin))!=ESP_OK ||
       httpd_req_get_hdr_value_str(request,"Host",host,sizeof(host))!=ESP_OK) return false;
    return std::string(origin)==std::string("http://")+host;
}
cJSON* read_json(httpd_req_t* request) {
    char type[80]{};
    if(!same_origin(request) || request->content_len<=0 || request->content_len>512 ||
       httpd_req_get_hdr_value_str(request,"Content-Type",type,sizeof(type))!=ESP_OK ||
       strncmp(type,"application/json",16)!=0) return nullptr;
    char body[513]{};
    int received=0;
    while(received<request->content_len) {
        int n=httpd_req_recv(request,body+received,request->content_len-received);
        if(n<=0) return nullptr;
        received+=n;
    }
    return cJSON_ParseWithLength(body,received);
}
esp_err_t root_get(httpd_req_t* request) {
    if(!authenticated(request)) return ESP_OK;
    extern const uint8_t ui_start[] asm("_binary_monitor_html_start");
    extern const uint8_t ui_end[] asm("_binary_monitor_html_end");
    httpd_resp_set_type(request,"text/html; charset=utf-8");
    httpd_resp_set_hdr(request,"Cache-Control","no-store");
    return httpd_resp_send(request,reinterpret_cast<const char*>(ui_start),ui_end-ui_start-1);
}
esp_err_t state_get(httpd_req_t* request) {
    if(!authenticated(request)) return ESP_OK;
    std::string json;
    {
        Lock lock;
        char age[40];
        snprintf(age,sizeof(age),"%.2f",state_at ? (esp_timer_get_time()-state_at)/1000000. : 0.);
        json="{\"state\":"+current_state+",\"image_state\":"+image_state+
             ",\"connected\":"+std::string(state_at?"true":"false")+",\"age_s\":"+age+",\"logs\":[],\"events\":[";
        for(size_t i=0;i<events.size();++i) { if(i) json+=","; json+=events[i]; }
        json+="],\"network\":"+network_json_locked()+"}";
    }
    return send_json(request,json);
}
esp_err_t image_get(httpd_req_t* request) {
    if(!authenticated(request)) return ESP_OK;
    char query[80]{}, after_text[16]{};
    uint32_t after=0;
    if(httpd_req_get_url_query_str(request,query,sizeof(query))==ESP_OK &&
       httpd_query_key_value(query,"after",after_text,sizeof(after_text))==ESP_OK) {
        char* end=nullptr;
        unsigned long value=strtoul(after_text,&end,10);
        if(end && !*end) after=uint32_t(value);
    }
    uint8_t* copy=nullptr;
    size_t length=0;
    std::string metadata;
    bool unchanged=false;
    {
        Lock lock;
        length=jpeg_size;
        unchanged=length && after && after==jpeg_frame;
        if(length && !unchanged) {
            copy=static_cast<uint8_t*>(heap_caps_malloc(length,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
            if(copy) { memcpy(copy,jpeg_bytes,length); metadata=image_state; }
        }
    }
    if(unchanged) {
        httpd_resp_set_status(request,"204 No Content");
        httpd_resp_set_hdr(request,"Cache-Control","no-store");
        return httpd_resp_send(request,nullptr,0);
    }
    if(!copy) {
        httpd_resp_set_status(request,"503 Service Unavailable");
        return send_json(request,"{\"ok\":false,\"error\":\"Imagen no disponible\"}");
    }
    httpd_resp_set_type(request,"image/jpeg");
    httpd_resp_set_hdr(request,"Cache-Control","no-store");
    // The header and JPEG are copied under the same lock: boxes cannot drift to another frame.
    httpd_resp_set_hdr(request,"X-Frame-State",metadata.c_str());
    esp_err_t err=httpd_resp_send(request,reinterpret_cast<char*>(copy),length);
    heap_caps_free(copy); return err;
}
esp_err_t command_post(httpd_req_t* request) {
    if(!authenticated(request)) return ESP_OK;
    cJSON* root=read_json(request);
    cJSON* item=root ? cJSON_GetObjectItemCaseSensitive(root,"command") : nullptr;
    if(!cJSON_IsString(item) || !item->valuestring || strlen(item->valuestring)>=128 ||
       strchr(item->valuestring,'\n') || strchr(item->valuestring,'\r')) {
        cJSON_Delete(root); return send_json(request,"{\"ok\":false,\"error\":\"Comando no valido\"}");
    }
    WebCommand command{};
    command.id=++command_id;
    strlcpy(command.text,item->valuestring,sizeof(command.text));
    cJSON_Delete(root);
    if(xQueueSend(commands,&command,0)!=pdTRUE)
        return send_json(request,"{\"ok\":false,\"error\":\"Dispositivo ocupado\"}");
    int64_t deadline=esp_timer_get_time()+2500000;
    while(esp_timer_get_time()<deadline) {
        std::string result;
        {
            Lock lock;
            if(completed_id==command.id) {
                result="{\"ok\":"+std::string(completed_ok?"true":"false")+
                    ",\"command\":\""+completed_name+"\"}";
            }
        }
        if(!result.empty()) return send_json(request,result);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return send_json(request,"{\"ok\":false,\"error\":\"Counter did not respond\"}");
}
esp_err_t network_get(httpd_req_t* request) {
    if(!authenticated(request)) return ESP_OK;
    std::string result;
    { Lock lock; result=network_json_locked(); }
    return send_json(request,result);
}
esp_err_t networks_get(httpd_req_t* request) {
    if(!authenticated(request)) return ESP_OK;
    return send_json(request,device_web_scan_results());
}
esp_err_t scan_post(httpd_req_t* request) {
    if(!authenticated(request)) return ESP_OK;
    cJSON* root=read_json(request);
    const bool ok=root && device_web_request_scan();
    cJSON_Delete(root);
    return send_json(request,ok?"{\"ok\":true}":"{\"ok\":false,\"error\":\"El Wi-Fi no esta listo\"}");
}
bool apply_network(cJSON* root) {
    if(root && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root,"reconnect"))) {
        Lock lock;
        if(!sta_ssid[0]) return false;
        change_pending=true; return true;
    }
    cJSON* ssid=root ? cJSON_GetObjectItemCaseSensitive(root,"ssid") : nullptr;
    cJSON* password=root ? cJSON_GetObjectItemCaseSensitive(root,"password") : nullptr;
    cJSON* forget=root ? cJSON_GetObjectItemCaseSensitive(root,"forget") : nullptr;
    bool clear=cJSON_IsTrue(forget);
    const char* name=cJSON_IsString(ssid) ? ssid->valuestring : "";
    const char* pass=cJSON_IsString(password) ? password->valuestring : "";
    size_t name_size=strlen(name), pass_size=strlen(pass);
    if(!root || (!clear && (!name_size || name_size>32 || !cJSON_IsString(password) ||
            (pass_size!=0 && (pass_size<8 || pass_size>63))))) {
        return false;
    }
    // Commit first. A failed save must not silently replace a working connection.
    StationCredentials credentials{};
    strlcpy(credentials.ssid,clear?"":name,sizeof(credentials.ssid));
    strlcpy(credentials.password,clear?"":pass,sizeof(credentials.password));
    esp_err_t err=nvs_set_blob(network_nvs,"station",&credentials,sizeof(credentials));
    if(err==ESP_OK) err=nvs_commit(network_nvs);
    if(err==ESP_OK) {
        Lock lock;
        strlcpy(sta_ssid,clear?"":name,sizeof(sta_ssid));
        strlcpy(sta_password,clear?"":pass,sizeof(sta_password));
        change_pending=true;
    }
    return err==ESP_OK;
}
esp_err_t network_post(httpd_req_t* request) {
    if(!authenticated(request)) return ESP_OK;
    cJSON* root=read_json(request);
    bool ok=apply_network(root);
    cJSON_Delete(root);
    return send_json(request,ok ? "{\"ok\":true,\"message\":\"Saved. The device access point remains available.\"}" :
        "{\"ok\":false,\"error\":\"Check SSID and password (8-63 characters, or empty for an open network)\"}");
}
esp_err_t captive_get(httpd_req_t* request) {
    // Trigger the OS's sign-in assistant; the actual UI remains protected.
    const char* path=request->uri;
    if(strstr(path,"generate_204") || strstr(path,"hotspot-detect") || strstr(path,"connecttest") ||
       strstr(path,"ncsi.txt") || strstr(path,"success.txt") || strstr(path,"redirect")) {
        httpd_resp_set_status(request,"302 Found");
        httpd_resp_set_hdr(request,"Location","http://192.168.4.1/");
        return httpd_resp_sendstr(request,"Abre http://192.168.4.1/");
    }
    return httpd_resp_send_err(request,HTTPD_404_NOT_FOUND,"No encontrado");
}
esp_err_t start_http() {
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();
    config.stack_size=8192;
    config.max_uri_handlers=12;
    config.max_open_sockets=5;
    config.lru_purge_enable=true;
    config.recv_wait_timeout=3;
    config.send_wait_timeout=3;
    config.uri_match_fn=httpd_uri_match_wildcard;
    esp_err_t err=httpd_start(&http_server,&config);
    if(err!=ESP_OK) return err;
    const struct Route { const char* uri; httpd_method_t method; esp_err_t (*handler)(httpd_req_t*); } routes[]={
        {"/",HTTP_GET,root_get},{"/state",HTTP_GET,state_get},{"/image.jpg",HTTP_GET,image_get},
        {"/command",HTTP_POST,command_post},{"/network",HTTP_GET,network_get},
        {"/network",HTTP_POST,network_post},{"/networks",HTTP_GET,networks_get},
        {"/scan",HTTP_POST,scan_post},{"/*",HTTP_GET,captive_get}};
    for(const auto& route:routes) {
        httpd_uri_t uri{}; uri.uri=route.uri; uri.method=route.method; uri.handler=route.handler;
        err=httpd_register_uri_handler(http_server,&uri);
        if(err!=ESP_OK) { httpd_stop(http_server); http_server=nullptr; return err; }
    }
    return ESP_OK;
}
void wifi_event(void*,esp_event_base_t base,int32_t id,void* data) {
    if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        auto* event=static_cast<ip_event_got_ip_t*>(data);
        Lock lock;
        snprintf(sta_ip,sizeof(sta_ip),IPSTR,IP2STR(&event->ip_info.ip));
        sta_connected=true; sta_connecting=false; disconnect_reason=0;
        connected_at_ms=esp_timer_get_time()/1000;
        ESP_LOGI(TAG,"LAN UI: http://%s/",sta_ip);
        printf("@NETWORK_STA {\"ip\":\"%s\",\"hostname\":\"%s.local\"}\n",sta_ip,hostname);
    } else if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        auto* event=static_cast<wifi_event_sta_disconnected_t*>(data);
        Lock lock;
        sta_connected=false; sta_connecting=false; sta_ip[0]=0; disconnect_reason=event->reason;
        connected_at_ms=0; ++disconnect_sequence;
    } else if(base==IP_EVENT && id==IP_EVENT_STA_LOST_IP) {
        Lock lock; sta_connected=false; sta_connecting=false; sta_ip[0]=0;
        connected_at_ms=0; ++disconnect_sequence; change_pending=true;
    } else if(base==WIFI_EVENT && id==WIFI_EVENT_SCAN_DONE) {
        Lock lock; scan_finished=true;
    }
}
void finish_scan(const char* error=nullptr) {
    cJSON* root=cJSON_CreateObject();
    cJSON_AddBoolToObject(root,"scanning",false);
    cJSON_AddStringToObject(root,"error",error?error:"");
    cJSON* list=cJSON_AddArrayToObject(root,"networks");
    if(!error) {
        wifi_ap_record_t records[16]{}; uint16_t count=16;
        if(esp_wifi_scan_get_ap_records(&count,records)==ESP_OK) {
            std::sort(records,records+count,[](const auto& a,const auto& b){return a.rssi>b.rssi;});
            std::vector<std::string> seen;
            for(unsigned i=0;i<count;++i) {
                std::string ssid(reinterpret_cast<char*>(records[i].ssid),strnlen(reinterpret_cast<char*>(records[i].ssid),32));
                if(ssid.empty() || std::find(seen.begin(),seen.end(),ssid)!=seen.end()) continue;
                seen.push_back(ssid);
                cJSON* item=cJSON_CreateObject();
                cJSON_AddStringToObject(item,"ssid",ssid.c_str());
                cJSON_AddNumberToObject(item,"rssi",records[i].rssi);
                cJSON_AddNumberToObject(item,"channel",records[i].primary);
                cJSON_AddBoolToObject(item,"open",records[i].authmode==WIFI_AUTH_OPEN);
                cJSON_AddItemToArray(list,item);
            }
        } else {
            cJSON_ReplaceItemInObject(root,"error",cJSON_CreateString("Could not read network list"));
        }
    }
    const auto result=json_text(root);
    { Lock lock; scan_json=result; scan_running=false; scan_finished=false; }
    printf("@SCAN %s\n",result.c_str());
}
void network_task(void*) {
    esp_err_t err=nvs_open("pc_network",NVS_READWRITE,&network_nvs);
    if(err!=ESP_OK) { ESP_LOGE(TAG,"NVS: %s",esp_err_to_name(err)); vTaskDelete(nullptr); return; }
    uint8_t mac[6]{};
    esp_efuse_mac_get_default(mac);
    snprintf(ap_ssid,sizeof(ap_ssid),"FlowCount-%02X%02X%02X",mac[3],mac[4],mac[5]);
    strlcpy(hostname,"flowcount",sizeof(hostname));
    size_t size=sizeof(access_key);
    if(nvs_get_str(network_nvs,"access_key",access_key,&size)!=ESP_OK || strlen(access_key)<8 || strlen(access_key)>32) {
        uint8_t random[8]; esp_fill_random(random,sizeof(random));
        for(size_t i=0;i<sizeof(random);++i) snprintf(access_key+2*i,3,"%02x",random[i]);
        if(nvs_set_str(network_nvs,"access_key",access_key)!=ESP_OK || nvs_commit(network_nvs)!=ESP_OK) {
            ESP_LOGE(TAG,"Could not save access key"); vTaskDelete(nullptr); return;
        }
    }
    StationCredentials credentials{};
    size=sizeof(credentials);
    if(nvs_get_blob(network_nvs,"station",&credentials,&size)==ESP_OK && size==sizeof(credentials)) {
        strlcpy(sta_ssid,credentials.ssid,sizeof(sta_ssid));
        strlcpy(sta_password,credentials.password,sizeof(sta_password));
    }
    unsigned char encoded[96]{}; size_t encoded_size=0;
    std::string user_password="admin:admin";
    mbedtls_base64_encode(encoded,sizeof(encoded),&encoded_size,
        reinterpret_cast<const unsigned char*>(user_password.data()),user_password.size());
    authorization="Basic "+std::string(reinterpret_cast<char*>(encoded),encoded_size);
    printf("@NETWORK {\"ap_ssid\":\"%s\",\"ap_password\":\"%s\",\"ap_ip\":\"%s\",\"user\":\"admin\",\"hostname\":\"%s.local\"}\n",
        ap_ssid,access_key,ap_ip,hostname);
    err=esp_netif_init();
    if(err==ESP_OK) err=esp_event_loop_create_default();
    if(err!=ESP_OK) { ESP_LOGE(TAG,"Network stack: %s",esp_err_to_name(err)); vTaskDelete(nullptr); return; }
    esp_netif_t* ap=esp_netif_create_default_wifi_ap();
    esp_netif_t* sta=esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(sta,hostname);
    esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,nullptr);
    esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,nullptr);
    // Keep transport startup out of the inference task. The P4 continues counting if Wi-Fi fails.
    wifi_config_t ap_config{};
    strlcpy(reinterpret_cast<char*>(ap_config.ap.ssid),ap_ssid,sizeof(ap_config.ap.ssid));
    strlcpy(reinterpret_cast<char*>(ap_config.ap.password),access_key,sizeof(ap_config.ap.password));
    ap_config.ap.ssid_len=strlen(ap_ssid); ap_config.ap.channel=6;
    ap_config.ap.max_connection=4; ap_config.ap.authmode=WIFI_AUTH_WPA2_PSK;
    wifi_config_t station{};
    memcpy(station.sta.ssid,sta_ssid,std::min(strlen(sta_ssid),sizeof(station.sta.ssid)));
    memcpy(station.sta.password,sta_password,std::min(strlen(sta_password),sizeof(station.sta.password)));
    station.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
    bool hosted_initialized=false;
    for(;;) {
        bool driver_initialized=false;
        err=ESP_OK;
        if(!hosted_initialized) {
            err=static_cast<esp_err_t>(esp_hosted_init());
            hosted_initialized=err==ESP_OK;
        }
        if(err==ESP_OK) err=static_cast<esp_err_t>(esp_hosted_connect_to_slave());
        wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
        if(err==ESP_OK) { err=esp_wifi_init(&init); driver_initialized=err==ESP_OK; }
        if(err==ESP_OK) err=esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if(err==ESP_OK) err=esp_wifi_set_mode(WIFI_MODE_APSTA);
        if(err==ESP_OK) err=esp_wifi_set_config(WIFI_IF_AP,&ap_config);
        if(err==ESP_OK && sta_ssid[0]) err=esp_wifi_set_config(WIFI_IF_STA,&station);
        if(err==ESP_OK) err=esp_wifi_start();
        { Lock lock; startup_error=err; }
        if(err==ESP_OK) break;
        ESP_LOGE(TAG,"Wi-Fi startup: %s; retry in 5 seconds",esp_err_to_name(err));
        if(driver_initialized) esp_wifi_deinit();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    esp_wifi_set_ps(WIFI_PS_NONE);
    // Announce the AP as DNS server and, where supported, advertise captive portal URI.
    esp_netif_dhcps_stop(ap);
    esp_netif_dns_info_t dns{};
    dns.ip.type=IPADDR_TYPE_V4; dns.ip.u_addr.ip4.addr=ESP_IP4TOADDR(192,168,4,1);
    esp_netif_set_dns_info(ap,ESP_NETIF_DNS_MAIN,&dns);
    dhcps_offer_t offer=OFFER_DNS;
    esp_netif_dhcps_option(ap,ESP_NETIF_OP_SET,ESP_NETIF_DOMAIN_NAME_SERVER,&offer,sizeof(offer));
    const char* portal="http://192.168.4.1/";
    esp_netif_dhcps_option(ap,ESP_NETIF_OP_SET,ESP_NETIF_CAPTIVEPORTAL_URI,const_cast<char*>(portal),strlen(portal));
    esp_netif_dhcps_start(ap);
    while((err=start_http())!=ESP_OK) {
        { Lock lock; startup_error=err; }
        ESP_LOGE(TAG,"HTTP startup: %s; retrying",esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    dns_server_config_t dns_config=DNS_SERVER_CONFIG_SINGLE("*","WIFI_AP_DEF");
    start_dns_server(&dns_config);
    if(mdns_init()==ESP_OK) {
        mdns_hostname_set(hostname); mdns_instance_name_set("FlowCount · DeAI Labs");
        mdns_service_add(nullptr,"_http","_tcp",80,nullptr,0);
        { Lock lock; mdns_ready=true; }
    }
    { Lock lock; network_ready=true; startup_error=0; }
    ESP_LOGI(TAG,"WEB READY: %s / http://192.168.4.1/ / http://%s.local/",ap_ssid,hostname);
    NetworkRetry retry; retry.reset(esp_timer_get_time()/1000);
    uint32_t last_disconnect=0;
    uint64_t health_at=0, scan_at=0, ignore_disconnect_until=0;
    unsigned unhealthy=0;
    for(;;) {
        const uint64_t now=esp_timer_get_time()/1000;
        bool changed, connected, connecting, configured, want_scan, scanning, scan_done;
        uint32_t sequence;
        {
            Lock lock;
            changed=change_pending; change_pending=false;
            connected=sta_connected; connecting=sta_connecting; configured=sta_ssid[0]!=0;
            sequence=disconnect_sequence;
            want_scan=scan_requested; scanning=scan_running; scan_done=scan_finished;
            if(changed) {
                memset(&station,0,sizeof(station));
                memcpy(station.sta.ssid,sta_ssid,std::min(strlen(sta_ssid),sizeof(station.sta.ssid)));
                memcpy(station.sta.password,sta_password,std::min(strlen(sta_password),sizeof(station.sta.password)));
                station.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
            }
        }
        if(changed) {
            if(scanning) { esp_wifi_scan_stop(); finish_scan("Configuracion de red modificada"); scanning=false; }
            esp_wifi_disconnect();
            err=esp_wifi_set_config(WIFI_IF_STA,&station);
            { Lock lock; sta_connected=false; sta_connecting=false; sta_ip[0]=0; }
            ignore_disconnect_until=now+1000;
            retry.reset(ignore_disconnect_until);
            connecting=false; connected=false; unhealthy=0;
            if(err!=ESP_OK) { retry.failed(now); ESP_LOGW(TAG,"Wi-Fi config: %s",esp_err_to_name(err)); }
        }
        if(sequence!=last_disconnect) {
            last_disconnect=sequence;
            if(!connected && now>=ignore_disconnect_until) retry.failed(now);
        }
        if(connected) {
            retry.connected();
            if(now-health_at>=5000) {
                health_at=now;
                wifi_ap_record_t ap_record{}; esp_netif_ip_info_t ip{};
                if(esp_wifi_sta_get_ap_info(&ap_record)==ESP_OK && esp_netif_get_ip_info(sta,&ip)==ESP_OK && ip.ip.addr) {
                    unhealthy=0; Lock lock; signal_dbm=ap_record.rssi;
                } else if(++unhealthy>=3) {
                    esp_wifi_disconnect(); retry.failed(now); connected=false;
                    Lock lock; sta_connected=false; sta_connecting=false; sta_ip[0]=0;
                }
            }
        }
        if(want_scan && !scanning) {
            if(connecting) {
                esp_wifi_disconnect();
                { Lock lock; sta_connecting=false; }
                retry.reset(now+1000);
            } else {
                wifi_scan_config_t config{};
                config.scan_type=WIFI_SCAN_TYPE_ACTIVE; config.scan_time.active.min=100; config.scan_time.active.max=150;
                { Lock lock; scan_requested=false; scan_running=true; scan_finished=false; }
                scan_at=now; scanning=true;
                err=esp_wifi_scan_start(&config,false);
                if(err!=ESP_OK) { finish_scan("Could not start scan. Try again."); scanning=false; }
            }
        }
        if(scanning && (scan_done || now-scan_at>=15000)) {
            if(!scan_done) esp_wifi_scan_stop();
            finish_scan(scan_done?nullptr:"La busqueda ha agotado el tiempo de espera");
            scanning=false;
            if(!connected) retry.reset(now+1000);
        }
        if(!connected && retry.expired(now)) {
            ESP_LOGW(TAG,"Wi-Fi attempt timed out; keeping recovery AP active");
            esp_wifi_disconnect(); retry.failed(now);
            { Lock lock; sta_connecting=false; }
        }
        if(configured && !connected && !scanning && !want_scan && retry.due(now)) {
            { Lock lock; sta_connecting=true; ++connection_attempts; }
            retry.begin(now);
            err=esp_wifi_connect();
            if(err!=ESP_OK) { retry.failed(now); Lock lock; sta_connecting=false; }
        }
        { Lock lock; retry_at_ms=connected || retry.active ? 0 : retry.next; }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
}
void device_web_start() {
    mutex=xSemaphoreCreateMutex(); commands=xQueueCreate(8,sizeof(WebCommand));
    jpeg_bytes=static_cast<uint8_t*>(heap_caps_malloc(jpeg_capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!mutex || !commands || !jpeg_bytes) { ESP_LOGE(TAG,"Web allocation failed"); return; }
    if(xTaskCreate(network_task,"pc_network",8192,nullptr,3,nullptr)!=pdPASS) ESP_LOGE(TAG,"Network task creation failed");
}
bool device_web_set_ap_key(const std::string& key) {
    if(key.size()<8 || key.size()>32 || key.find('\0')!=std::string::npos) return false;
    { Lock lock; if(!network_ready) return false; }
    esp_err_t err=nvs_set_str(network_nvs,"access_key",key.c_str());
    if(err==ESP_OK) err=nvs_commit(network_nvs);
    return err==ESP_OK;
}
void device_web_publish_state(const std::string& json) {
    if(!mutex) return;
    Lock lock; current_state=json; state_at=esp_timer_get_time();
}
void device_web_publish_jpeg(const uint8_t* bytes,size_t size,const std::string& state,uint32_t frame) {
    if(!mutex || !jpeg_bytes || size>jpeg_capacity) return;
    Lock lock; memcpy(jpeg_bytes,bytes,size); jpeg_size=size; image_state=state; jpeg_frame=frame;
}
void device_web_publish_event(const std::string& json) {
    if(!mutex) return;
    Lock lock; events.push_back(json); if(events.size()>25) events.pop_front();
}
bool device_web_take_command(WebCommand& command) {
    return commands && xQueueReceive(commands,&command,0)==pdTRUE;
}
void device_web_complete_command(uint32_t id,const char* name,bool ok) {
    if(!id || !mutex) return;
    Lock lock; completed_id=id; completed_ok=ok; completed_name=name;
}
std::string device_web_network_info() {
    if(!mutex) return "{}";
    Lock lock; return network_json_locked();
}
bool device_web_configure_network(const std::string& json) {
    if(!mutex) return false;
    { Lock lock; if(!network_ready) return false; }
    cJSON* root=cJSON_Parse(json.c_str());
    bool ok=apply_network(root);
    cJSON_Delete(root); return ok;
}
bool device_web_request_scan() {
    if(!mutex) return false;
    Lock lock;
    if(!network_ready) return false;
    if(!scan_running) {
        scan_requested=true;
        scan_json="{\"scanning\":true,\"networks\":[],\"error\":\"\"}";
    }
    return true;
}
std::string device_web_scan_results() {
    if(!mutex) return "{\"scanning\":false,\"networks\":[],\"error\":\"Wi-Fi no disponible\"}";
    Lock lock; return scan_json;
}
