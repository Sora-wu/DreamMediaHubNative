//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <common/dataHandler.hpp>
#include <common/dreamThread.hpp>
#include <FFmpegPlayer/mediaClock.h>
#include <FFmpegPlayer/ffcore.h>

#include <vector>
#include <condition_variable>

extern "C" {
    struct AVFormatContext;
    struct AVStream;
}

class Demuxer : public DataHandler, public DreamThread {
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
    void run(std::stop_token st) override;

    void seekRequest(double targetPos, double seekRel);

private:
    void seek();
    void pushNullPacket();

private:
    AVFormatContext* fmtCtx_ = nullptr;
    int64_t duration_ = AV_NOPTS_VALUE;

    std::vector<AVStream*> streams_;
    bool isEof_ = false;

    AVPacket flushPkt_{};

    bool hasSeekRequest_ = false;
    double seekTargetPos_ = 0;
    double seekRel_ = 0;
    std::condition_variable cvRead_;
    std::mutex mtx_;
    MediaClock& mediaClock_;
};