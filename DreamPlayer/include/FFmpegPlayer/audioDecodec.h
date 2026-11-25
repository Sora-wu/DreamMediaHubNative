//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <FFmpegPlayer/decodec.h>
#include <FFmpegPlayer/audioSpeedFilter.h>
#include <FFmpegPlayer/taskQueue.h>
#include <common/FFmpegPool.hpp>

#include <atomic>

extern "C" {
    struct SwrContext;
}

namespace Dream {
    struct AudioFrame;
}

class MediaClock;

class AudioDecodec : public Decodec {
public:
    AudioDecodec(const AVPacket& flushPkt, MediaClock& clock, std::size_t frameQueueSize)
        : Decodec(flushPkt, frameQueueSize), mediaClock_(clock) {}
    ~AudioDecodec() override;

    bool openCodec(int streamIndex, AVStream* stream) override;
    void reopen() override;
    void start() override;

    Dream::AudioFrame* getAudioFrame();

    void setSpeed(float speed);

protected:
    void threadFunc(std::stop_token st) override;
    void cleanup() override;

private:
    bool refreshSwr(AVFrame* frame);
    bool fillData(AVFrame* frame);
    // 直接参考ffplay的实现
    bool adjustSamples(AVFrame* frame);

    std::vector<AVFrame*> processFrame(AVFrame* frame);
    AVFrame* convertFrame(AVFrame* frame);
    // 将重采样/过滤镜的输出结果推入队列
    void pushFrames(const std::vector<AVFrame*>& frames, double pts);

private:
    TaskQueue<AudioDecodec> taskQueue_;

    SwrContext* swrCtx_ = nullptr;
    std::unique_ptr<AudioSpeedFilter> audioSpeedFilter_;
    std::atomic<float> speed_ = 1.0f;
    std::vector<AVFrame*> paddingFrames_;
    MediaClock& mediaClock_;

    uint64_t cachedChannelLayout_ = 0;
    int cachedFormat_ = AV_SAMPLE_FMT_NONE;
    int cachedSampleRate_ = 0;

    double audioDiffCum_ = 0.0;         // 累计时钟差值
    double audioDiffAvgCoef_ = 0.0;     // 加权平均系数
    int audioDiffAvgCount_ = 0;         // 差值测量计数器
    double audioDiffThreshold_ = 0.0;   // 触发采点调整的阈值

    AVFramePool framePool_;
    Dream::AudioFrame audioFrame_{};
};