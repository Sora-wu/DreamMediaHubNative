//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <common/dataHandler.hpp>
#include <common/concurrentQueue.hpp>
#include <common/FFmpegPool.hpp>

#include <thread>

extern "C" {
    struct AVFormatContext;
    struct AVPacket;
}

class Muxer : public DataHandler {
public:
    Muxer() {}
    ~Muxer() override;

    AVFormatContext& init(const char* path);
    void cleanup();
    void start();
    void threadFunc(std::stop_token st);
    void close();
    void exit();

    void handle(void* data) override;

private:
    AVPacket* getPacket();

private:
    std::jthread thread_;

    AVFormatContext* fmtCtx_ = nullptr;
    ConcurrentQueue<AVPacket*> videoPktQue_;
    ConcurrentQueue<AVPacket*> audioPktQue_;
    AVPacketPool packetPool_;
};