//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <common/dataHandler.hpp>
#include <FFmpegPlayer/mediaClock.h>
#include <common/ffcore.hpp>
#include <FFmpegPlayer/taskQueue.h>

#include <vector>

extern "C" {
    struct AVFormatContext;
    struct AVStream;
}

using OnDemuxFinishedCallback = std::function<void()>;

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
    void cancelIO() { taskQueue_.close(); }
    void threadFunc(std::stop_token st);
    void stopDemux();

    void seek(double targetPos, double seekRel);

    void setOnDemuxStatusChangedCallback(OnDemuxFinishedCallback callback) {
        onDemuxFinishedCallback_ = std::move(callback);
    }

private:
    void pushNullPacket();

private:
    std::jthread thread_;
    TaskQueue<Demuxer> taskQueue_;
    std::atomic<uint64_t> seekRevision_ = 0;
    std::atomic<bool> interruptOnTask_ = false;
    OnDemuxFinishedCallback onDemuxFinishedCallback_ = nullptr;

    AVFormatContext* fmtCtx_ = nullptr;
    int64_t duration_ = AV_NOPTS_VALUE;

    std::vector<AVStream*> streams_;
    std::atomic<bool> isEof_ = false;

    AVPacket flushPkt_{};
    MediaClock& mediaClock_;
};