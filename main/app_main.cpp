#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <inttypes.h>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_rom_crc.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "driver/jpeg_encode.h"
#include "camera.hpp"
#include "people_counter.hpp"
#include "pedestrian_detect.hpp"
#include "device_web.hpp"
#include "detection_policy.hpp"
#include "diagnostic_replay.hpp"

namespace {
constexpr const char* TAG="counter";
constexpr uint64_t save_interval_ms=300000;
uint64_t clock_ms() { return esp_timer_get_time()/1000; }
struct SavedTotals { uint64_t entries, exits; };
struct Settings {
    uint32_t version=2;
    uint32_t vertical=0, positive_entry=1;
    float line=.5f, half_band=.06f, roi_min=.05f, roi_max=.95f, score=.55f;
    uint32_t full_body_only=1;
};
bool valid(const Settings& s) {
    return s.version==2 && s.vertical<=1 && s.positive_entry<=1 && s.full_body_only<=1 &&
        std::isfinite(s.line) && std::isfinite(s.half_band) &&
        std::isfinite(s.roi_min) && std::isfinite(s.roi_max) && std::isfinite(s.score) &&
        s.half_band>=.01f && s.half_band<=.25f &&
        s.line>s.half_band && s.line<1.f-s.half_band &&
        s.roi_min>=0 && s.roi_max<=1 && s.roi_max-s.roi_min>=.1f &&
        s.score>=.3f && s.score<=.95f;
}
pc::Config make_config(const Settings& s) {
    pc::Config c;
    c.vertical=s.vertical; c.positive_is_entry=s.positive_entry;
    c.line=s.line; c.half_band=s.half_band; c.roi_min=s.roi_min; c.roi_max=s.roi_max;
    c.full_body_only=s.full_body_only;c.start_score=s.score;
    return c;
}
nvs_handle_t store;
Settings settings;
bool dirty=false, preview=true, camera_dirty=false;
Camera* active_camera=nullptr;
CameraSettings saved_camera_settings;
std::string last_state;
uint64_t last_save=0, last_preview=0, next_preview=0, last_serial_state=0, usb_preview_until=0, last_usb_jpeg=0;
float processing_fps=0, preview_fps=0, capture_ms=0, jpeg_ms=0;
uint32_t frame_id=0;
uint32_t boot_id=0;
uint64_t replay_until=0;
std::vector<pc::Box> ignored_detections;
struct Candidate { pc::Box box; float score; pc::DetectionDecision decision; };
std::vector<Candidate> candidates;
float best_score=0;
const char* reset_name() {
    switch(esp_reset_reason()) {
        case ESP_RST_POWERON:return "power_on";
        case ESP_RST_SW:return "software";
        case ESP_RST_PANIC:return "panic";
        case ESP_RST_INT_WDT:case ESP_RST_TASK_WDT:case ESP_RST_WDT:return "watchdog";
        case ESP_RST_BROWNOUT:return "brownout";
        default:return "other";
    }
}
float smooth_fps(float previous,uint64_t elapsed_ms) {
    // Average periods, not their reciprocals: alternating long/short frame
    // intervals must not inflate the displayed rate.
    return 1000.f/(previous ? .85f*(1000.f/previous)+.15f*elapsed_ms : float(elapsed_ms));
}

void reply(const char* command, bool ok, uint32_t request_id=0) {
    printf("@ACK {\"command\":\"%s\",\"ok\":%s}\n",command,ok ? "true":"false");
    device_web_complete_command(request_id,command,ok);
}
bool save(pc::Counter& counter, bool include_settings=false) {
    SavedTotals totals{counter.entries(),counter.exits()};
    esp_err_t err=nvs_set_blob(store,"totals",&totals,sizeof(totals));
    if(err==ESP_OK && include_settings) err=nvs_set_blob(store,"settings",&settings,sizeof(settings));
    if(err==ESP_OK) err=nvs_commit(store);
    if(err!=ESP_OK) { ESP_LOGE(TAG,"NVS save failed: %s",esp_err_to_name(err)); return false; }
    dirty=false; last_save=clock_ms(); return true;
}
void report(const pc::Counter& counter, uint64_t captured_ms, float inference_ms) {
    const auto& c=counter.config();
    char header[1280];
    snprintf(header,sizeof(header),"{\"frame\":%" PRIu32 ",\"ms\":%" PRIu64 ",\"entries\":%" PRIu64
           ",\"exits\":%" PRIu64 ",\"balance\":%" PRId64 ",\"inference_ms\":%.1f,"
           "\"dirty\":%s,\"preview\":%s,\"score\":%.3f,\"full_body_only\":%s,\"boot_id\":%" PRIu32 ",\"reset_reason\":\"%s\","
           "\"processing_fps\":%.2f,\"preview_fps\":%.2f,\"capture_ms\":%.1f,\"jpeg_ms\":%.1f,"
           "\"gate\":{\"vertical\":%s,\"line\":%.4f,\"band\":%.4f,\"positive_entry\":%s,"
           "\"roi_min\":%.4f,\"roi_max\":%.4f},\"tracks\":[",
           frame_id,captured_ms,counter.entries(),counter.exits(),counter.balance(),inference_ms,
           dirty?"true":"false",preview?"true":"false",settings.score,settings.full_body_only?"true":"false",boot_id,reset_name(),
           processing_fps,preview_fps,capture_ms,jpeg_ms,
           c.vertical?"true":"false",c.line,c.half_band,c.positive_is_entry?"true":"false",c.roi_min,c.roi_max);
    std::string json=header;
    bool comma=false;
    for(const auto& t:counter.tracks()) if(t.visible) {
        if(comma) json+=",";
        comma=true;
        char box[250];
        snprintf(box,sizeof(box),"{\"id\":%" PRIu32 ",\"box\":[%.4f,%.4f,%.4f,%.4f],\"score\":%.3f,\"recovered\":%s,\"side\":%d,\"pending_hits\":%d}",
            t.id,t.box.x1,t.box.y1,t.box.x2,t.box.y2,t.score,t.recovered?"true":"false",t.side,t.pending_hits);
        json+=box;
    }
    json+="],\"ignored_detections\":[";
    comma=false;
    for(const auto& b:ignored_detections) {
        if(comma) json+=",";
        comma=true;
        char box[100];snprintf(box,sizeof(box),"[%.4f,%.4f,%.4f,%.4f]",b.x1,b.y1,b.x2,b.y2);json+=box;
    }
    json+="],\"candidates\":[";
    comma=false;
    for(const auto& c:candidates) {
        if(comma) json+=",";
        comma=true;
        char candidate[190];
        snprintf(candidate,sizeof(candidate),"{\"box\":[%.4f,%.4f,%.4f,%.4f],\"score\":%.3f,\"reason\":\"%s\"}",
            c.box.x1,c.box.y1,c.box.x2,c.box.y2,c.score,pc::detection_reason(c.decision));
        json+=candidate;
    }
    char confidence[70];snprintf(confidence,sizeof(confidence),"],\"best_score\":%.3f,\"camera\":",best_score);
    json+=confidence;
    json+=(active_camera?active_camera->status(camera_dirty):"{}")+
        ",\"network\":"+device_web_network_info()+"}";
    last_state=json;
    if(clock_ms()-last_serial_state>=500) {
        printf("@STATE %s\n",json.c_str()); last_serial_state=clock_ms();
    }
    device_web_publish_state(json);
}
void send_jpeg(Camera& camera) {
    static jpeg_encoder_handle_t encoder=nullptr;
    static uint8_t* output=nullptr;
    static size_t output_size=0;
    if(!encoder) {
        jpeg_encode_engine_cfg_t engine{}; engine.timeout_ms=500;
        if(jpeg_new_encoder_engine(&engine,&encoder)!=ESP_OK) return;
        jpeg_encode_memory_alloc_cfg_t alloc{};
        alloc.buffer_direction=JPEG_ENC_ALLOC_OUTPUT_BUFFER;
        output=static_cast<uint8_t*>(jpeg_alloc_encoder_mem(65536,&alloc,&output_size));
    }
    if(!output) return;
    jpeg_encode_cfg_t cfg{};
    cfg.width=Camera::width; cfg.height=Camera::height;
    cfg.src_type=JPEG_ENCODE_IN_FORMAT_RGB565;
    cfg.sub_sample=JPEG_DOWN_SAMPLING_YUV420; cfg.image_quality=camera.settings().quality;
    uint32_t size=0;
    int64_t started=esp_timer_get_time();
    esp_err_t err=jpeg_encoder_process(encoder,&cfg,camera.pixels(),Camera::width*Camera::height*2,
                                      output,output_size,&size);
    if(err!=ESP_OK) { ESP_LOGW(TAG,"Preview JPEG failed: %s",esp_err_to_name(err)); return; }
    jpeg_ms=(esp_timer_get_time()-started)/1000.f;
    device_web_publish_jpeg(output,size,last_state,frame_id);
    // UART is opt-in and leased. An unattended board must never wait for USB video.
    const uint64_t now=clock_ms();
    if(now>=usb_preview_until || now-last_usb_jpeg<500) return;
    last_usb_jpeg=now;
    uint32_t crc=esp_rom_crc32_le(0,output,size);
    // stdout lock prevents interleaving log messages with the binary JPEG payload.
    flockfile(stdout);
    printf("@STATE %s\n",last_state.c_str());
    printf("@JPEG {\"size\":%" PRIu32 ",\"crc32\":%" PRIu32 ",\"frame\":%" PRIu32 "}\n",size,crc,frame_id);
    fwrite(output,1,size,stdout); printf("\n"); fflush(stdout); funlockfile(stdout);
}
void command(const std::string& line, pc::Counter& counter, PedestrianDetect& model, uint32_t request_id=0) {
    auto respond=[&](const char* name,bool ok) { reply(name,ok,request_id); };
    if(line.rfind("replay ",0)==0) {
        if(request_id) { respond("replay",false);return; }
        counter.configure(make_config(settings));
        if(line=="replay end") { replay_until=0;respond("replay",true);return; }
        replay_until=clock_ms()+15000;
        diagnostic_replay(line,model);
        replay_until=clock_ms()+15000;
    }
    else if(line=="reset") { counter.reset(); dirty=true; respond("reset",save(counter)); }
    else if(line=="save") respond("save",save(counter,true));
    else if(line.rfind("apkey ",0)==0) respond("apkey",!request_id && device_web_set_ap_key(line.substr(6)));
    else if(line=="preview 1") { preview=true; next_preview=0; last_preview=0; preview_fps=0; respond("preview",true); }
    else if(line=="preview 0") { preview=false; preview_fps=0; respond("preview",true); }
    else if(line=="status") { report(counter,clock_ms(),0); respond("status",true); }
    else if(line=="scan") respond("scan",device_web_request_scan());
    else if(line=="body 1" || line=="body 0") {
        settings.full_body_only=line=="body 1";
        counter.configure(make_config(settings));
        respond("body",save(counter,true));
    }
    else if(line=="usbpreview 1") { usb_preview_until=clock_ms()+15000; respond("usbpreview",true); }
    else if(line=="usbpreview 0") { usb_preview_until=0; respond("usbpreview",true); }
    else if(line.rfind("camera ",0)==0 && active_camera) {
        if(line=="camera save") {
            const auto& current=active_camera->settings();
            esp_err_t err=nvs_set_blob(store,"camera",&current,sizeof(current));
            if(err==ESP_OK) err=nvs_commit(store);
            if(err==ESP_OK) { saved_camera_settings=current; camera_dirty=false; }
            respond("camera",err==ESP_OK); return;
        }
        CameraSettings next=active_camera->settings(); char tail=0;
        int a=0,b=0,c=0,d=0,e=0; bool parsed=false;
        if(line=="camera defaults") { next=CameraSettings{}; parsed=true; }
        else if(line=="camera restore") { next=saved_camera_settings; parsed=true; }
        else if(sscanf(line.c_str(),"camera stream %d %d %c",&a,&b,&tail)==2) {
            next.fps=a; next.quality=b; parsed=true;
        } else if(sscanf(line.c_str(),"camera auto %d %c",&a,&tail)==1) {
            next.automatic=1; next.max_exposure_us=a; parsed=true;
        } else if(sscanf(line.c_str(),"camera manual %d %d %c",&a,&b,&tail)==2) {
            next.automatic=0; next.exposure_us=a; next.gain=b; parsed=true;
        } else if(sscanf(line.c_str(),"camera image %d %d %d %d %d %c",&a,&b,&c,&d,&e,&tail)==5) {
            next.brightness=a; next.contrast=b; next.saturation=c; next.mirror=d; next.flip=e; parsed=true;
        }
        const auto previous=active_camera->settings();
        const bool ok=parsed && active_camera->configure(next)==ESP_OK;
        if(ok) {
            if(next.fps!=previous.fps) next_preview=0;
            camera_dirty=memcmp(&next,&saved_camera_settings,sizeof(next))!=0;
            // An image transform must not turn an existing trajectory into a crossing.
            if(next.mirror!=previous.mirror || next.flip!=previous.flip) counter.configure(make_config(settings));
        } else if(parsed) active_camera->configure(previous);
        respond("camera",ok);
    }
    else if(line.rfind("wifi ",0)==0) respond("wifi",device_web_configure_network(line.substr(5)));
    else if(line.rfind("gate ",0)==0) {
        char axis=0, tail=0; unsigned positive=0;
        Settings next=settings;
        int n=sscanf(line.c_str(),"gate %c %f %f %u %f %f %c",&axis,&next.line,
            &next.half_band,&positive,&next.roi_min,&next.roi_max,&tail);
        next.vertical=(axis=='v'); next.positive_entry=positive;
        if(n==6 && (axis=='h'||axis=='v') && valid(next)) {
            settings=next; counter.configure(make_config(settings));
            respond("gate",save(counter,true));
        } else respond("gate",false);
    } else if(line.rfind("score ",0)==0) {
        Settings next=settings; char tail=0;
        if(sscanf(line.c_str(),"score %f %c",&next.score,&tail)==1 && valid(next)) {
            settings=next; model.set_score_thr(pc::diagnostic_score_floor); counter.configure(make_config(settings));
            respond("score",save(counter,true));
        } else respond("score",false);
    } else respond("unknown",false);
}
void poll_commands(pc::Counter& counter, PedestrianDetect& model) {
    static std::string line;
    uint8_t bytes[128];
    int n=uart_read_bytes(UART_NUM_0,bytes,sizeof(bytes),0);
    for(int i=0;i<n;++i) {
        char ch=bytes[i];
        if(ch=='\r') continue;
        if(ch=='\n') { if(!line.empty()) command(line,counter,model); line.clear(); }
        else if(ch>=32 && ch<127 && line.size()<512) line+=ch;
        else { line.clear(); }
    }
    WebCommand pending;
    while(device_web_take_command(pending)) command(pending.text,counter,model,pending.id);
}
}
extern "C" void app_main() {
    boot_id=esp_random();
    // JPEG bytes must pass unchanged: CRLF translation would corrupt binary data.
    uart_vfs_dev_port_set_tx_line_endings(UART_NUM_0,ESP_LINE_ENDINGS_LF);
    ESP_LOGI(TAG,"FlowCount / GUITION JC-ESP32P4-M3 / IMX219 / on-device counting");
    esp_err_t err=nvs_flash_init();
    if(err==ESP_ERR_NVS_NO_FREE_PAGES || err==ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); err=nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(nvs_open("people_counter",NVS_READWRITE,&store));
    size_t size=sizeof(settings);
    Settings loaded;
    if(nvs_get_blob(store,"settings",&loaded,&size)==ESP_OK) {
        bool compatible=size==sizeof(loaded) && loaded.version==2;
        // Version 1 is the same prefix, before the explicit body filter existed.
        if(loaded.version==1 && size==offsetof(Settings,full_body_only)) {
            loaded.version=2; loaded.full_body_only=1;
            compatible=true;
        }
        if(compatible && valid(loaded)) settings=loaded;
    }
    pc::Counter counter(make_config(settings));
    SavedTotals totals{}; size=sizeof(totals);
    if(nvs_get_blob(store,"totals",&totals,&size)==ESP_OK && size==sizeof(totals))
        counter.set_totals(totals.entries,totals.exits);
    if(!uart_is_driver_installed(UART_NUM_0))
        ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0,2048,0,0,nullptr,0));
    device_web_start();
    Camera camera;
    err=camera.start();
    if(err!=ESP_OK) {
        ESP_LOGE(TAG,"Camera unavailable: %s",esp_err_to_name(err));
        printf("@ERROR {\"error\":\"camera_init_failed\",\"code\":%d}\n",err);
        return;
    }
    active_camera=&camera;
    CameraSettings loaded_camera;
    size=sizeof(loaded_camera);
    if(nvs_get_blob(store,"camera",&loaded_camera,&size)==ESP_OK && size==sizeof(loaded_camera) &&
        valid_camera_settings(loaded_camera)) saved_camera_settings=loaded_camera;
    if(camera.configure(saved_camera_settings)!=ESP_OK) {
        ESP_LOGW(TAG,"Saved camera settings unavailable; using defaults");
        saved_camera_settings=CameraSettings{};
        camera.configure(saved_camera_settings);
    }
    // Allow auto-exposure and white balance to settle before tracking people.
    for(int i=0;i<45;++i) { if(camera.capture()!=ESP_OK) break; vTaskDelay(1); }
    PedestrianDetect model(PedestrianDetect::PICO_S8_V1,false);
    model.set_score_thr(pc::diagnostic_score_floor);
    model.set_nms_thr(.5f);
    last_save=clock_ms();
    ESP_LOGI(TAG,"READY: inference and counting run entirely on ESP32-P4");
    unsigned capture_errors=0;
    uint64_t previous_frame_ms=0;
    for(;;) {
        poll_commands(counter,model);
        if(clock_ms()<replay_until) { vTaskDelay(pdMS_TO_TICKS(10));continue; }
        int64_t capture_start=esp_timer_get_time();
        err=camera.capture();
        capture_ms=(esp_timer_get_time()-capture_start)/1000.f;
        if(err!=ESP_OK) {
            if(++capture_errors%30==1) ESP_LOGE(TAG,"Capture failed: %s",esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(30)); continue;
        }
        capture_errors=0;
        uint64_t now=clock_ms(); ++frame_id;
        if(previous_frame_ms && now>previous_frame_ms) {
            processing_fps=smooth_fps(processing_fps,now-previous_frame_ms);
        }
        previous_frame_ms=now;
        dl::image::img_t img{camera.pixels(),Camera::width,Camera::height,dl::image::DL_IMAGE_PIX_TYPE_RGB565LE};
        int64_t start=esp_timer_get_time();
        auto& detected=model.run(img);
        float inference_ms=(esp_timer_get_time()-start)/1000.f;
        std::vector<pc::Detection> boxes; ignored_detections.clear(); candidates.clear(); best_score=0;
        for(auto& detection:detected) if(detection.box.size()>=4) {
            pc::Box box{detection.box[0]/float(Camera::width),detection.box[1]/float(Camera::height),
                        detection.box[2]/float(Camera::width),detection.box[3]/float(Camera::height)};
            const auto decision=pc::detection_decision(box,detection.score,settings.score,settings.full_body_only);
            if(decision==pc::DetectionDecision::Invalid) continue;
            candidates.push_back({box,detection.score,decision});best_score=std::max(best_score,detection.score);
            if(decision==pc::DetectionDecision::Closeup) ignored_detections.push_back(box);
            else boxes.push_back({box,detection.score});
        }
        for(const auto& event:counter.update_scored(boxes,now)) {
            dirty=true;
            char event_json[160];
            snprintf(event_json,sizeof(event_json),"{\"ms\":%" PRIu64 ",\"id\":%" PRIu32 ",\"direction\":\"%s\"}",
                now,event.id,event.entry?"entry":"exit");
            printf("@EVENT %s\n",event_json);
            device_web_publish_event(event_json);
        }
        if(dirty && now-last_save>=save_interval_ms) save(counter);
        report(counter,now,inference_ms);
        if(preview && now>=next_preview) {
            if(last_preview && now>last_preview) {
                preview_fps=smooth_fps(preview_fps,now-last_preview);
            }
            send_jpeg(camera); last_preview=now;
            const uint64_t interval=1000/camera.settings().fps;
            next_preview+=interval;
            if(next_preview<=now) next_preview=now+interval;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
