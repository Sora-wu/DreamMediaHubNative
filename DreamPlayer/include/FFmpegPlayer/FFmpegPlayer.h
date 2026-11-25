//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <DreamPlayer/DreamPlayer.h>
#include <common/dataHandler.hpp>
#include <FFmpegPlayer/mediaClock.h>

#include <memory>
#include <thread>
#include <mutex>

class Demuxer;
class VideoDecodec;
class AudioDecodec;

class FFmpegPlayer final : public Dream::DreamPlayer, public DataHandler {
public:
    FFmpegPlayer();
    ~FFmpegPlayer() override;

    bool open(const char* url) override;
    void play() override;
    void pause() override;
    bool isPlaying() const override;
    void stop() override;       // TODO: 改成异步

    [[nodiscard]] uint32_t getWidth() override;
    [[nodiscard]] uint32_t getHeight() override;

    void seek(double targetPos, double seekRel) override;
    [[nodiscard]] int64_t getDuration() const override;
    [[nodiscard]] double getCurrentTime() override;

    float getSpeed() override;
    void setSpeed(double speed) override;

    [[nodiscard]] Dream::VideoFrame* getVideoFrame() override;
    [[nodiscard]] Dream::AudioFrame* getAudioFrame() override;

    void updateAudioClock(double pts, uint32_t serial) override;

    void setOnSeekCallback(const Dream::OnSeekCallback& cb) override;
    void setOnPlaybackCompletedCallback(const Dream::OnPlaybackCompletedCallback& cb) override;

    void setMasterMode(Dream::SyncType syncType) override;
    [[nodiscard]] Dream::SyncType getMasterMode() const override;

    // 处理来自解封装器的数据
    void handle(void* data) override;
private:
    // 检测线程 TODO: 看看后续是否能够去掉
    void threadFunc(std::stop_token st);

    void shutdown() const;
    // 仅检测线程
    bool checkPlaybackCompleted();
    void markPlaybackCompleted();

private:
    std::jthread thread_;
    std::mutex mtx_;

    MediaClock clock_;
    std::atomic<double> cachedTime_{0.0};
    std::atomic<bool> playbackCompleted_ = false;

    Dream::OnPlaybackCompletedCallback onPlaybackCompletedCallback_ = nullptr;

    std::unique_ptr<Demuxer> demuxer_;
    std::unique_ptr<VideoDecodec> videoDecodec_;
    std::unique_ptr<AudioDecodec> audioDecodec_;

};