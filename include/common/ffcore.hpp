//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <string>
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavutil/avutil.h>
#include <libavutil/audio_fifo.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
}

// FFmpeg 4.x uses a C99 compound literal here. Preserve the macro's meaning
// while providing the equivalent C++ initializer for MSVC.
#if defined(PLATFORM_WINDOWS) && defined(_MSC_VER)
#undef AV_TIME_BASE_Q
#define AV_TIME_BASE_Q AVRational{1, AV_TIME_BASE}
#endif

#define CURRENT_TIME (double)av_gettime_relative() / 1e6

class FFUtils {
public:
    static std::string getError(int errnum) {
        char buf[AV_ERROR_MAX_STRING_SIZE]{};
        std::string ret;

        av_strerror(errnum, buf, AV_ERROR_MAX_STRING_SIZE);
        ret = buf;
        
        return ret;
    }

    static bool isEqual(float a, float b) {
        return fabs(a - b) < 1e-6;
    }

    static bool isEqual(double a, double b) {
        return fabs(a - b) < 1e-6;
    }

    static void setThreadName(std::jthread& thread, const std::string& name) {
#ifdef PLATFORM_LINUX
        auto threadHandle = thread.native_handle();
        pthread_setname_np(threadHandle, name.c_str());
#endif
    }
};
