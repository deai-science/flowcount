#include "camera.hpp"
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "esp_video_ioctl.h"
#include "esp_video_isp_pipeline.h"
#include "linux/videodev2.h"

static const char* TAG="camera";
esp_err_t Camera::start() {
#ifndef CONFIG_PC_BOARD_PINS_CONFIRMED
    ESP_LOGE(TAG,"Board pinout unconfirmed. Configure 'FlowCount people counter' in menuconfig.");
    return ESP_ERR_INVALID_STATE;
#endif
    esp_video_init_csi_config_t csi{};
    csi.sccb_config.init_sccb=true;
    csi.sccb_config.i2c_config.port=0;
    csi.sccb_config.i2c_config.sda_pin=static_cast<gpio_num_t>(CONFIG_PC_CAMERA_SDA);
    csi.sccb_config.i2c_config.scl_pin=static_cast<gpio_num_t>(CONFIG_PC_CAMERA_SCL);
    csi.sccb_config.freq=100000;
    csi.reset_pin=static_cast<gpio_num_t>(CONFIG_PC_CAMERA_RESET);
    csi.pwdn_pin=static_cast<gpio_num_t>(CONFIG_PC_CAMERA_PWDN);
    esp_video_init_config_t init{};
    init.csi=&csi;
    esp_err_t err=esp_video_init(&init);
    if (err!=ESP_OK) return err;
    fd_=open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME,O_RDONLY);
    if(fd_<0) return ESP_FAIL;
    auto fail=[&]() {
        ESP_LOGE(TAG,"V4L2 failure: errno=%d",errno);
        close(fd_); fd_=-1; esp_video_deinit(); return ESP_FAIL;
    };
    timeval timeout{};
    timeout.tv_sec=1;
    if(ioctl(fd_,VIDIOC_S_DQBUF_TIMEOUT,&timeout)!=0) return fail();
    v4l2_format fmt{};
    fmt.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(fd_,VIDIOC_G_FMT,&fmt)!=0) return fail();
    fmt.fmt.pix.pixelformat=V4L2_PIX_FMT_RGB565;
    if(ioctl(fd_,VIDIOC_S_FMT,&fmt)!=0) return fail();
    if(fmt.fmt.pix.pixelformat!=V4L2_PIX_FMT_RGB565) return fail();
    sensor_width_=fmt.fmt.pix.width; sensor_height_=fmt.fmt.pix.height;
    stride_=fmt.fmt.pix.bytesperline ? fmt.fmt.pix.bytesperline : sensor_width_*2;
    ESP_LOGI(TAG,"IMX219 %lux%lu RGB565 stride=%lu",(unsigned long)sensor_width_,
             (unsigned long)sensor_height_,(unsigned long)stride_);
    v4l2_requestbuffers req{};
    req.count=2; req.type=V4L2_BUF_TYPE_VIDEO_CAPTURE; req.memory=V4L2_MEMORY_MMAP;
    if(ioctl(fd_,VIDIOC_REQBUFS,&req)!=0 || req.count<2) return fail();
    for(int i=0;i<2;++i) {
        v4l2_buffer b{};
        b.type=req.type; b.memory=req.memory; b.index=i;
        if(ioctl(fd_,VIDIOC_QUERYBUF,&b)!=0) return fail();
        buffers_[i]=static_cast<uint8_t*>(mmap(nullptr,b.length,PROT_READ|PROT_WRITE,MAP_SHARED,fd_,b.m.offset));
        if(buffers_[i]==MAP_FAILED) return fail();
        lengths_[i]=b.length;
        // Flush dirty cache lines before the first DMA transfer.
        err=esp_cache_msync(buffers_[i],b.length,ESP_CACHE_MSYNC_FLAG_DIR_C2M|ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        if(err!=ESP_OK || ioctl(fd_,VIDIOC_QBUF,&b)!=0) return fail();
    }
    pixels_=static_cast<uint8_t*>(heap_caps_aligned_alloc(128,width*height*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!pixels_) return fail();
    int type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(fd_,VIDIOC_STREAMON,&type)!=0) return fail();
    esp_cam_sensor_format_t sensor{};
    if(ioctl(fd_,VIDIOC_G_SENSOR_FMT,&sensor)==0 && sensor.isp_info) {
        line_ns_=sensor.isp_info->isp_v1_info.tline_ns;
        sensor_fps_=sensor.fps;
    }
    v4l2_query_ext_ctrl query{}; query.id=V4L2_CID_EXPOSURE;
    if(line_ns_ && ioctl(fd_,VIDIOC_QUERY_EXT_CTRL,&query)==0) {
        exposure_supported_=true;
        exposure_min_=std::max(1,int((uint64_t(query.minimum)*line_ns_+999)/1000));
        exposure_max_=int(uint64_t(query.maximum)*line_ns_/1000);
    }
    query={}; query.id=V4L2_CID_GAIN;
    if(ioctl(fd_,VIDIOC_QUERY_EXT_CTRL,&query)==0) {
        gain_supported_=true; gain_max_=int(query.maximum);
    }
    return ESP_OK;
}
int Camera::read_control(uint32_t id) const {
    v4l2_ext_control c{}; c.id=id;
    v4l2_ext_controls cs{}; cs.ctrl_class=V4L2_CTRL_ID2CLASS(id); cs.count=1; cs.controls=&c;
    return ioctl(fd_,VIDIOC_G_EXT_CTRLS,&cs)==0 ? c.value : -1;
}
bool Camera::write_control(uint32_t id,int value) const {
    v4l2_ext_control c{}; c.id=id; c.value=value;
    v4l2_ext_controls cs{}; cs.ctrl_class=V4L2_CTRL_ID2CLASS(id); cs.count=1; cs.controls=&c;
    return ioctl(fd_,VIDIOC_S_EXT_CTRLS,&cs)==0;
}
esp_err_t Camera::configure(const CameraSettings& next) {
    if(!valid_camera_settings(next)) return ESP_ERR_INVALID_ARG;
    if(exposure_supported_ && (next.max_exposure_us<exposure_min_ || next.max_exposure_us>exposure_max_ ||
        (!next.automatic && (next.exposure_us<exposure_min_ || next.exposure_us>exposure_max_))))
        return ESP_ERR_INVALID_ARG;
    if(!next.automatic && (!exposure_supported_ || !gain_supported_ || next.gain>gain_max_))
        return ESP_ERR_NOT_SUPPORTED;
    if(exposure_supported_) {
        // The IPA caches the last automatic values. Restore those before handing
        // control back, otherwise it may suppress a write equal to its cache.
        if(next.automatic && !settings_.automatic && auto_exposure_lines_>=0 && auto_gain_>=0) {
            if(!write_control(V4L2_CID_EXPOSURE,auto_exposure_lines_) || !write_control(V4L2_CID_GAIN,auto_gain_))
                return ESP_FAIL;
        }
        esp_err_t err=esp_video_isp_pipeline_set_agc_status(next.automatic ?
            ESP_VIDEO_ISP_PIPELINE_AGC_ENABLE : ESP_VIDEO_ISP_PIPELINE_AGC_DISABLE);
        if(err!=ESP_OK) return err;
        if(!next.automatic && settings_.automatic) {
            auto_exposure_lines_=read_control(V4L2_CID_EXPOSURE);
            auto_gain_=read_control(V4L2_CID_GAIN);
        }
        if(next.automatic) {
            err=esp_video_isp_pipeline_set_agc_max_exposure(next.max_exposure_us);
            if(err!=ESP_OK) return err;
        } else {
            const int lines=int((uint64_t(next.exposure_us)*1000+line_ns_/2)/line_ns_);
            if(!write_control(V4L2_CID_EXPOSURE,lines) || !write_control(V4L2_CID_GAIN,next.gain))
                return ESP_FAIL;
        }
    }
    settings_=next;
    for(int x=0;x<width;++x)
        column_offsets_[x]=(next.mirror ? width-1-x : x)*sensor_width_/width;
    for(int y=0;y<height;++y)
        row_offsets_[y]=((next.flip ? height-1-y : y)*sensor_height_/height)*stride_;
    return ESP_OK;
}
std::string Camera::status(bool dirty) {
    const auto& s=settings_;
    int lines=exposure_supported_ ? read_control(V4L2_CID_EXPOSURE) : -1;
    int gain=gain_supported_ ? read_control(V4L2_CID_GAIN) : -1;
    v4l2_querymenu menu{}; menu.id=V4L2_CID_GAIN; menu.index=std::max(0,gain);
    double multiplier=gain>=0 && ioctl(fd_,VIDIOC_QUERYMENU,&menu)==0 ? menu.value/1000. : 0;
    char json[1200];
    snprintf(json,sizeof(json),"{\"fps\":%ld,\"quality\":%ld,\"automatic\":%s,\"exposure_us\":%ld,"
        "\"max_exposure_us\":%ld,\"gain\":%ld,\"brightness\":%ld,\"contrast\":%ld,\"saturation\":%ld,"
        "\"mirror\":%s,\"flip\":%s,\"dirty\":%s,\"actual_exposure_us\":%d,\"actual_gain\":%.2f,"
        "\"exposure_supported\":%s,\"gain_supported\":%s,\"exposure_min_us\":%d,\"exposure_max_us\":%d,"
        "\"gain_max\":%d,\"sensor_fps\":%d,\"width\":320,\"height\":240,"
        "\"mean_luma\":%.1f,\"luma_stddev\":%.1f,\"dark_percent\":%d,\"bright_percent\":%d,\"image_health\":\"%s\"}",
        long(s.fps),long(s.quality),s.automatic?"true":"false",long(s.exposure_us),long(s.max_exposure_us),
        long(s.gain),long(s.brightness),long(s.contrast),long(s.saturation),s.mirror?"true":"false",
        s.flip?"true":"false",dirty?"true":"false",lines>=0?int(uint64_t(lines)*line_ns_/1000):-1,
        multiplier,exposure_supported_?"true":"false",gain_supported_?"true":"false",exposure_min_,exposure_max_,gain_max_,sensor_fps_,
        mean_luma_,luma_stddev_,dark_percent_,bright_percent_,
        mean_luma_<22 || dark_percent_>90 ? "dark" : bright_percent_>65 ? "bright" : luma_stddev_<4 ? "flat" : "ok");
    return json;
}
esp_err_t Camera::capture() {
    v4l2_buffer b{};
    b.type=V4L2_BUF_TYPE_VIDEO_CAPTURE; b.memory=V4L2_MEMORY_MMAP;
    if(ioctl(fd_,VIDIOC_DQBUF,&b)!=0) return ESP_FAIL;
    if(b.index>=2) return ESP_FAIL;
    if(b.bytesused<stride_*sensor_height_ || b.bytesused>lengths_[b.index]) {
        ioctl(fd_,VIDIOC_QBUF,&b); return ESP_ERR_INVALID_SIZE;
    }
    // Invalidate the CPU cache after DMA, before reading the frame.
    esp_err_t err=esp_cache_msync(buffers_[b.index],lengths_[b.index],
        ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    if(err==ESP_OK) {
        auto* dst=reinterpret_cast<uint16_t*>(pixels_);
        const bool neutral=!settings_.brightness && settings_.contrast==100 && settings_.saturation==100;
        for(int y=0;y<height;++y) {
            auto* src=reinterpret_cast<const uint16_t*>(buffers_[b.index]+row_offsets_[y]);
            if(neutral) {
                for(int x=0;x<width;++x) dst[y*width+x]=src[column_offsets_[x]];
                continue;
            }
            for(int x=0;x<width;++x) {
                uint16_t pixel=src[column_offsets_[x]];
                // Transform after demosaicing: sensor flips would change the Bayer phase.
                {
                    int r=((pixel>>11)&31)*255/31, g=((pixel>>5)&63)*255/63, blue=(pixel&31)*255/31;
                    const int luma=(77*r+150*g+29*blue)>>8;
                    auto adjust=[&](int value) {
                        value=luma+(value-luma)*settings_.saturation/100;
                        return std::clamp<int>((value-128)*settings_.contrast/100+128+settings_.brightness,0,255);
                    };
                    r=adjust(r); g=adjust(g); blue=adjust(blue);
                    pixel=uint16_t((r>>3)<<11 | (g>>2)<<5 | (blue>>3));
                }
                dst[y*width+x]=pixel;
            }
        }
    }
    if(err==ESP_OK) {
        // Advisory only: never disable counting on a simple brightness heuristic.
        uint64_t sum=0, sum_squared=0; unsigned count=0, dark=0, bright=0;
        const auto* image=reinterpret_cast<const uint16_t*>(pixels_);
        for(int y=0;y<height;y+=8) for(int x=0;x<width;x+=8) {
            const uint16_t p=image[y*width+x];
            const unsigned r=((p>>11)&31)*255/31, g=((p>>5)&63)*255/63, blue=(p&31)*255/31;
            const unsigned luma=(77*r+150*g+29*blue)>>8;
            sum+=luma; sum_squared+=luma*luma; ++count;
            if(luma<32) ++dark;
            if(luma>239) ++bright;
        }
        mean_luma_=float(sum)/count;
        luma_stddev_=std::sqrt(std::max(0.f,float(sum_squared)/count-mean_luma_*mean_luma_));
        dark_percent_=dark*100/count; bright_percent_=bright*100/count;
    }
    if(ioctl(fd_,VIDIOC_QBUF,&b)!=0) return ESP_FAIL;
    return err;
}
