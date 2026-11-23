//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <FFmpegPlayer/ffcore.h>

std::string FFUtils::getError(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    std::string ret;

    av_strerror(errnum, buf, AV_ERROR_MAX_STRING_SIZE);
    ret = buf;
    
    return ret;
}

bool FFUtils::isEqual(float a, float b) {
    return fabs(a - b) < 1e-6;
}

bool FFUtils::isEqual(double a, double b) {
    return fabs(a - b) < 1e-6;
}

void FFUtils::setThreadName(std::jthread &thread, const std::string &name) {
#ifdef PLATFORM_LINUX
    auto threadHandle = thread.native_handle();
    pthread_setname_np(threadHandle, name.c_str());
#endif
}
