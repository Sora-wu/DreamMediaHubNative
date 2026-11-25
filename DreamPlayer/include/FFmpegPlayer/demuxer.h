//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <common/dataHandler.hpp>
#include <FFmpegPlayer/mediaClock.h>
#include <FFmpegPlayer/ffcore.h>
#include <FFmpegPlayer/taskQueue.h>

#include <vector>
#include <condition_variable>

extern "C" {
    struct AVFormatContext;
    struct AVStream;
}

class Demuxer : public DataHandler {
public:
    explicit Demuxer(MediaClock& mediaClock);
    ~Demuxer() override;

    [[nodiscard]] bool open(const char* url);
    [[nodiscard]] int64_t getDuration() const {
        return duration_;
    }

    [[nodiscard]] AVFormatContext* getAVFormatContext() const {
        return fmtCtx_;
    }

    [[nodiscard]] std::vector<AVStream*> getStreams() const {
        return streams_;
    }

    [[nodiscard]] const AVPacket& getFlushPkt() const { return flushPkt_; }

    [[nodiscard]] bool isEof() const {
        return isEof_;
    }

    void startDemux();
    void threadFunc(std::stop_token st);
    void stopDemux();

    void seek(double targetPos, double seekRel);

private:
    void seek();
    void pushNullPacket();

private:
    std::jthread thread_;
    TaskQueue<Demuxer> taskQueue_;

    AVFormatContext* fmtCtx_ = nullptr;
    int64_t duration_ = AV_NOPTS_VALUE;

    std::vector<AVStream*> streams_;
    std::atomic<bool> isEof_ = false;

    AVPacket flushPkt_{};
    MediaClock& mediaClock_;
};