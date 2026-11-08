#include "diagnostic_replay.hpp"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <inttypes.h>
#include "driver/uart.h"
#include "driver/jpeg_decode.h"
#include "esp_timer.h"
#include "esp_rom_crc.h"
#include "pedestrian_detect.hpp"

bool diagnostic_replay(const std::string& line, PedestrianDetect& model) {
    unsigned size=0, crc=0; uint64_t timestamp=0; char tail=0;
    if(sscanf(line.c_str(),"replay %u %u %" SCNu64 " %c",&size,&crc,&timestamp,&tail)!=3 ||
       size<4 || size>65536) return false;
    jpeg_decode_memory_alloc_cfg_t alloc{};
    alloc.buffer_direction=JPEG_DEC_ALLOC_INPUT_BUFFER;
    size_t input_size=0, output_size=0;
    auto* input=static_cast<uint8_t*>(jpeg_alloc_decoder_mem(size,&alloc,&input_size));
    alloc.buffer_direction=JPEG_DEC_ALLOC_OUTPUT_BUFFER;
    auto* output=static_cast<uint8_t*>(jpeg_alloc_decoder_mem(320*240*2,&alloc,&output_size));
    jpeg_decoder_handle_t engine=nullptr;
    jpeg_decode_engine_cfg_t engine_config{}; engine_config.timeout_ms=1000;
    bool ok=input && output && jpeg_new_decoder_engine(&engine_config,&engine)==ESP_OK;
    if(ok) {
        printf("@REPLAY_READY\n");fflush(stdout);
        size_t received=0;const int64_t deadline=esp_timer_get_time()+10000000;
        while(received<size && esp_timer_get_time()<deadline) {
            const size_t end=std::min(received+1024,size_t(size));
            while(received<end && esp_timer_get_time()<deadline) {
                int n=uart_read_bytes(UART_NUM_0,input+received,end-received,pdMS_TO_TICKS(100));
                if(n>0) received+=n;
            }
            printf("@REPLAY_CHUNK %u\n",unsigned(received));fflush(stdout);
        }
        jpeg_decode_picture_info_t info{};
        ok=received==size && esp_rom_crc32_le(0,input,size)==crc &&
           jpeg_decoder_get_info(input,size,&info)==ESP_OK && info.width==320 && info.height==240;
        jpeg_decode_cfg_t config{};
        config.output_format=JPEG_DECODE_OUT_FORMAT_RGB565;
        // IDF calls this BGR but defines it as little-endian RGB565 output.
        config.rgb_order=JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
        config.conv_std=JPEG_YUV_RGB_CONV_STD_BT601;
        uint32_t decoded=0;
        if(ok) ok=jpeg_decoder_process(engine,&config,input,size,output,output_size,&decoded)==ESP_OK;
        if(ok) {
            dl::image::img_t image{output,320,240,dl::image::DL_IMAGE_PIX_TYPE_RGB565LE};
            int64_t start=esp_timer_get_time();auto& detections=model.run(image);
            flockfile(stdout);
            printf("@REPLAY {\"ms\":%" PRIu64 ",\"inference_ms\":%.1f,\"detections\":[",timestamp,(esp_timer_get_time()-start)/1000.f);
            bool comma=false;
            for(const auto& d:detections) if(d.box.size()>=4) {
                printf("%s{\"box\":[%.5f,%.5f,%.5f,%.5f],\"score\":%.5f}",comma?",":"",
                    d.box[0]/320.f,d.box[1]/240.f,d.box[2]/320.f,d.box[3]/240.f,d.score);comma=true;
            }
            printf("]}\n");fflush(stdout);funlockfile(stdout);
        }
    }
    if(engine) jpeg_del_decoder_engine(engine);
    free(input);free(output);
    if(!ok) {printf("@REPLAY_ERROR\n");fflush(stdout);}
    return ok;
}
