//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <DreamRecorder/DreamRecorder.h>

namespace Dream {
    class DREAM_RECORDER_API DreamRecorderContext {
    public:
        static DreamRecorder* createDreamRecorder();
        static void destroyDreamRecorder(DreamRecorder** recorder);

        // 建议的采集块大小，也用于初始化录制转换缓冲
        static constexpr uint32_t AUDIO_BUFFER_SIZE = 4096;
    };
}
