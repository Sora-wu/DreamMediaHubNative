//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <common/ffcore.hpp>

struct AVFilterGraph;
struct AVFilterContext;

class AudioSpeedFilter {
public:
    ~AudioSpeedFilter();

    int initFilter(float speed, uint32_t sampleRate, AVSampleFormat sampleFmt, uint64_t channelLayout);
    int addFrame(AVFrame* frame);
    int getFrame(AVFrame* frame);

private:
    AVFilterGraph* graph_ = nullptr;
    AVFilterContext* filterSrcCtx_ = nullptr;
    AVFilterContext* filterSinkCtx_ = nullptr;
};