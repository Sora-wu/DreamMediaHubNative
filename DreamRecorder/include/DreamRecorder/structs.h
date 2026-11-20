//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <DreamRecorder/define.h>

#include <span>

namespace Dream {
    enum class PixelFormat {
        RGB = 2,
        BGR = 3,
    };

    enum class SampleFormat {
        NONE = -1,
        U8 = 0,
        S16 = 1,
        S32 = 2,
        FLT = 3,
        FLTP = 8
    };

    struct DREAM_RECORDER_API VideoParam {
        uint32_t width = 1920;
        uint32_t height = 1080;
        uint32_t fps = 30;
        uint32_t bitRate = 4000000;
        PixelFormat pixelFormat = PixelFormat::RGB;
        bool enable = true;
    };

    struct DREAM_RECORDER_API AudioParam {
        uint32_t sampleRate = 44100;
        uint32_t channels = 2;
        uint32_t bitRate = 64000;
        SampleFormat sampleFormat = SampleFormat::FLTP;
        bool enable = true;
    };

    struct DREAM_RECORDER_API VideoFrameBuffer {
        char* data = nullptr;
        uint32_t stride = 0;
        uint32_t size = 0;
        void* opaque = nullptr;             // native层自用
    };

    struct DREAM_RECORDER_API AudioFrameBuffer {
        // 交错格式使用第 0 项，平面格式每声道一项且字节数相等。
        // 未使用项的指针和字节数均为 0，PCM 在提交调用期间须保持有效。
        uint8_t* planes[8]{};
        uint32_t planeBytes[8]{};            // 各平面的有效 PCM 字节数。
    };
}
