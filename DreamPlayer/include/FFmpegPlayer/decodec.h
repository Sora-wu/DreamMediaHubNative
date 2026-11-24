//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <common/dataHandler.hpp>
#include <FFmpegPlayer/packetQueue.h>
#include <DreamPlayer/structs.h>

#include <mutex>
#include <thread>

struct Frame {
    AVFrame* frame{};
    uint32_t serial{};
    double pts{};
    double duration{};
};

class Decodec : public DataHandler {
public:
    explicit Decodec(const AVPacket& flushPkt, std::size_t frameQueueSize) : flushPkt_(flushPkt), frameQue_(frameQueueSize) {}
    ~Decodec() override {
        Decodec::cleanup();
    }

    virtual bool openCodec(int streamIndex, AVStream* stream);
    virtual void reopen();
    virtual void start();
    virtual void stop();

    [[nodiscard]] bool isOpen() const {
        return streamIndex_ != -1;
    }

    [[nodiscard]] int getStreamIndex() const {
        return streamIndex_;
    }

    [[nodiscard]] const std::atomic<uint32_t>* getPktQueSerial() const {
        return pktQue_.getSerial();
    }

    [[nodiscard]] uint64_t getTimer() const {
        return static_cast<uint64_t>(decodecTimer_);
    }

    void handle(void* data) override;

    void shutdown();

    [[nodiscard]] bool isDecodeFinished();
    [[nodiscard]] bool hasBufferedFrames() const;
    void setPause(bool isPause) {
        isPause_ = isPause;
    }

    [[nodiscard]] Dream::OnSeekCallback& getOnSeekCallback() {
        std::lock_guard lock(mtx_);
        return onSeekCallback_;
    }
    void setOnSeekCallback(const Dream::OnSeekCallback& cb) {
        std::lock_guard lock(mtx_);
        onSeekCallback_ = cb;
    }

protected:
    virtual void threadFunc(std::stop_token st) = 0;

    virtual void cleanup();
    [[nodiscard]] virtual bool decode(AVFrame* frame);
    void pushFrame(AVFrame* frame, double pts, double duration);
    bool acquireFrame(Frame& out);
    static void releaseFrame(Frame& frame);

protected:
    std::jthread thread_;

    const AVPacket& flushPkt_;
    PacketQueue pktQue_{flushPkt_};
    ConcurrentQueue<Frame> frameQue_;
    std::mutex mtx_{};

    AVCodecContext* codecCtx_ = nullptr;
    AVStream* stream_ = nullptr;
    AVPacket* packet_ = nullptr;
    int streamIndex_ = -1;
    uint32_t decodecSerial_ = 0;
    std::atomic<int> finishedSerial_ = -1;
    double decodecTimer_ = 0;
    bool decodeTimerValid_ = false;

    bool isPause_ = true;

    Dream::OnSeekCallback onSeekCallback_ = nullptr;
};