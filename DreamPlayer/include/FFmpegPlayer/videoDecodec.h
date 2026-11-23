//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <FFmpegPlayer/decodec.h>

namespace Dream {
    struct VideoFrame;
}

class MediaClock;

class VideoDecodec : public Decodec {
public:
    VideoDecodec(const AVPacket& flushPkt, MediaClock& clock, std::size_t frameQueueSize)
        : Decodec(flushPkt, frameQueueSize), mediaClock_(clock) {}
    ~VideoDecodec() override;

    bool openCodec(int streamIndex, AVStream* stream, AVFormatContext& input);
    void reopen() override;
    void start() override;

    uint32_t getWidth() const;
    uint32_t getHeight() const;

    Dream::VideoFrame* getVideoFrame();

protected:
    void threadFunc(std::stop_token st) override;
    void cleanup() override;

private:
    void resetSyncState();
    bool shouldDropLateFrame(double time) const;
    double getFrameDuration(const Frame& frame, const Frame& nextFrame) const;
    double calcDelay(double duration) const;
    bool fillData(AVFrame* frame);
    void updateVideoFrameSize(int width, int height, const int linesize[4]);

private:
    SwsContext* swsCtx_ = nullptr;
    AVFormatContext* input_ = nullptr;
    MediaClock& mediaClock_;

    Frame heldFrame_{};
    Dream::VideoFrame videoFrame_{};
    double maxFrameDuration_ = 3600.0;
    int cachedFormat_ = AV_PIX_FMT_NONE;
    int cachedWidth_ = 0;
    int cachedHeight_ = 0;
};