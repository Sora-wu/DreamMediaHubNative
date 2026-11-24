//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <FFmpegPlayer/FFmpegPlayer.h>
#include <FFmpegPlayer/ffcore.h>
#include <FFmpegPlayer/demuxer.h>
#include <FFmpegPlayer/videoDecodec.h>
#include <FFmpegPlayer/audioDecodec.h>

#include <algorithm>

using namespace std::literals;
using namespace Dream;

namespace {
    constexpr std::size_t FRAME_VIDEO_SIZE = 3;
    constexpr std::size_t FRAME_AUDIO_SIZE = 16;
}

FFmpegPlayer::FFmpegPlayer() {
    avdevice_register_all();
    avformat_network_init();

    demuxer_ = std::make_unique<Demuxer>(clock_);
    demuxer_->setNextHandler(this);

    const AVPacket& flushPkt = demuxer_->getFlushPkt();
    videoDecodec_ = std::make_unique<VideoDecodec>(flushPkt, clock_, FRAME_VIDEO_SIZE);
    audioDecodec_ = std::make_unique<AudioDecodec>(flushPkt, clock_, FRAME_AUDIO_SIZE);
}

FFmpegPlayer::~FFmpegPlayer() {
    stop();

    avformat_network_deinit();
}

bool FFmpegPlayer::open(const char* url) {
    cachedTime_ = 0.0;

    if (!demuxer_->open(url)) {
        return false;
    }

    std::vector<AVStream*> streams = demuxer_->getStreams();
    for (uint32_t i = 0; i < streams.size(); ++i) {
        AVStream* stream = streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !videoDecodec_->isOpen()) {
            if (!videoDecodec_->openCodec(i, stream, *demuxer_->getAVFormatContext())) {
                return false;
            }
        }
        else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && !audioDecodec_->isOpen()) {
            if (!audioDecodec_->openCodec(i, stream)) {
                return false;
            }
        }
    }

    if (audioDecodec_->isOpen()) {
        audioDecodec_->reopen();
    }
    else {
        audioDecodec_ = nullptr;
        clock_.setMasterMode(SyncType::VideoMaster);
    }

    if (videoDecodec_->isOpen()) {
        videoDecodec_->reopen();
    }
    else {
        videoDecodec_ = nullptr;
        clock_.setMasterMode(SyncType::AudioMaster);
    }

    if (!videoDecodec_ && !audioDecodec_) {
        av_log(nullptr, AV_LOG_ERROR, "this media has no available track.\n");
        return false;
    }

    const auto* videoPktQueSerial = videoDecodec_ ? videoDecodec_->getPktQueSerial() : nullptr;
    const auto* audioPktQueSerial = audioDecodec_ ? audioDecodec_->getPktQueSerial() : nullptr;
    clock_.initClock(videoPktQueSerial, audioPktQueSerial);

    if (audioDecodec_) {
        audioDecodec_->start();
    }
    if (videoDecodec_) {
        videoDecodec_->start();
    }
    demuxer_->startDemux();

    thread_ = std::jthread([this](std::stop_token st) {
        threadFunc(st);
    });
    FFUtils::setThreadName(thread_, "monitor thread");

    return true;
}

void FFmpegPlayer::play() {
    clock_.resumeClock();

    if (videoDecodec_) {
        videoDecodec_->setPause(false);
    }

    if (audioDecodec_) {
        audioDecodec_->setPause(false);
    }
}

void FFmpegPlayer::pause() {
    clock_.pauseMasterClock();

    if (videoDecodec_) {
        videoDecodec_->setPause(true);
    }

    if (audioDecodec_) {
        audioDecodec_->setPause(true);
    }
}

bool FFmpegPlayer::isPlaying() const {
    if (clock_.getMasterMode() == SyncType::VideoMaster) {
        return !clock_.getVideoClockPause();
    }

    return !clock_.getAudioClockPause();
}

void FFmpegPlayer::stop() {
    thread_.request_stop();
    shutdown();
    if (thread_.joinable()) {
        thread_.join();
    }

    // 监控线程可能在退出前重开过队列。它退出后再确保队列已关闭。
    shutdown();

    if (audioDecodec_) {
        audioDecodec_->stop();
        audioDecodec_->setOnSeekCallback(nullptr);
    }

    if (videoDecodec_) {
        videoDecodec_->stop();
        videoDecodec_->setOnSeekCallback(nullptr);
    }

    demuxer_->stopDemux();

    setOnPlaybackCompletedCallback(nullptr);

    clock_.initClock(nullptr, nullptr);
    cachedTime_ = 0.0;
}

uint32_t FFmpegPlayer::getWidth() {
    return videoDecodec_ ? videoDecodec_->getWidth() : 0;
}

uint32_t FFmpegPlayer::getHeight() {
    return videoDecodec_ ? videoDecodec_->getHeight() : 0;
}

void FFmpegPlayer::seek(double targetPos, double seekRel) {
    demuxer_->seekRequest(targetPos, seekRel);
}

int64_t FFmpegPlayer::getDuration() const {
    return demuxer_->getDuration();
}

// 确保返回值不是nan或者大于duration
double FFmpegPlayer::getCurrentTime() {
    double currentTime = clock_.getMasterClock();
    if (!isnan(currentTime)) {
        cachedTime_ = currentTime;
    }

    const double duration = demuxer_->getDuration();
    if (playbackCompleted_) {
        return duration;
    }

    return std::min(cachedTime_.load(), duration);
}

float FFmpegPlayer::getSpeed() {
    return clock_.getSpeed();
}

void FFmpegPlayer::setSpeed(double speed) {
    double pts = clock_.getAudioPTS();
    clock_.setMasterClock(pts, CURRENT_TIME);

    clock_.setSpeed(speed);

    if (audioDecodec_) {
        audioDecodec_->setSpeed(static_cast<float>(speed));
    }
}

VideoFrame* FFmpegPlayer::getVideoFrame() {
    return videoDecodec_ ? videoDecodec_->getVideoFrame() : nullptr;
}

AudioFrame* FFmpegPlayer::getAudioFrame() {
    return audioDecodec_ ? audioDecodec_->getAudioFrame() : nullptr;
}

void FFmpegPlayer::updateAudioClock(double pts, uint32_t serial) {
    clock_.setAudioClock(pts, serial);
}

void FFmpegPlayer::setOnSeekCallback(const OnSeekCallback& cb) {
    // 先清理回掉，后面再设置
    if (videoDecodec_) {
        videoDecodec_->setOnSeekCallback(nullptr);
    }
    if (audioDecodec_) {
        audioDecodec_->setOnSeekCallback(nullptr);
    }

    if (!cb) {
        return;
    }

    const SyncType masterMode = clock_.getMasterMode();
    if (masterMode == SyncType::VideoMaster && videoDecodec_) {
        videoDecodec_->setOnSeekCallback(cb);
        return;
    }
    else if (masterMode == SyncType::AudioMaster && audioDecodec_) {
        audioDecodec_->setOnSeekCallback(cb);
        return;
    }

    av_log(nullptr, AV_LOG_WARNING, "setOnSeekCallback failed, no available master track\n");
}

void FFmpegPlayer::setOnPlaybackCompletedCallback(const OnPlaybackCompletedCallback& cb) {
    std::lock_guard lock(mtx_);
    onPlaybackCompletedCallback_ = cb;
}

void FFmpegPlayer::setMasterMode(SyncType syncType) {
    const SyncType currentMode = clock_.getMasterMode();
    if (currentMode == syncType) {
        return;
    }

    OnSeekCallback cb = nullptr;
    if (currentMode == SyncType::VideoMaster && videoDecodec_) {
        cb = videoDecodec_->getOnSeekCallback();
    }
    else if (currentMode == SyncType::AudioMaster && audioDecodec_) {
        cb = audioDecodec_->getOnSeekCallback();
    }

    setOnSeekCallback(cb);
    clock_.setMasterMode(syncType);
}

SyncType FFmpegPlayer::getMasterMode() const {
    return clock_.getMasterMode();
}

void FFmpegPlayer::handle(void* data) {
    AVPacket* pkt = static_cast<AVPacket*>(data);

    if (videoDecodec_ && pkt->stream_index == videoDecodec_->getStreamIndex()) {
        videoDecodec_->handle(pkt);
        return;
    }
    if (audioDecodec_ && pkt->stream_index == audioDecodec_->getStreamIndex()) {
        audioDecodec_->handle(pkt);
        return;
    }

    if (pkt != &demuxer_->getFlushPkt()) {
        av_packet_unref(pkt);
    }
}

void FFmpegPlayer::threadFunc(std::stop_token st) {
    while (!st.stop_requested()) {
        checkPlaybackCompleted();
        std::this_thread::sleep_for(100ms);
    }
}

void FFmpegPlayer::shutdown() const {
    if (videoDecodec_) {
        videoDecodec_->shutdown();
    }

    if (audioDecodec_) {
        audioDecodec_->shutdown();
    }
}

bool FFmpegPlayer::checkPlaybackCompleted() {
    if (!demuxer_->isEof()) {
        return false;
    }

    auto isStreamDrained = [](Decodec* decodec) {
        if (!decodec) {
            return true;
        }

        return decodec->isDecodeFinished() && !decodec->hasBufferedFrames();
    };

    // 播放完毕把对应的流关闭，避免后面继续解码
    bool isVideoStreamDrained = isStreamDrained(videoDecodec_.get());
    if (isVideoStreamDrained && videoDecodec_) {
        videoDecodec_->shutdown();
    }
    bool isAudioStreamDrained = isStreamDrained(audioDecodec_.get());
    if (isAudioStreamDrained && audioDecodec_) {
        audioDecodec_->shutdown();
    }

    if (!isVideoStreamDrained || !isAudioStreamDrained) {
        if (playbackCompleted_) {
            // 被重新打开，比如seek
            if (videoDecodec_) {
                videoDecodec_->reopen();
            }
            if (audioDecodec_) {
                audioDecodec_->reopen();
            }

            playbackCompleted_ = false;
        }

        return false;
    }

    markPlaybackCompleted();
    return true;
}

void FFmpegPlayer::markPlaybackCompleted() {
    if (playbackCompleted_) {
        return;
    }

    playbackCompleted_ = true;
    clock_.pauseMasterClock();

    const double duration = demuxer_->getDuration();
    const double currentTime = clock_.getMasterClock();
    if (isnan(currentTime)) {
        return;
    }
    if (currentTime > duration) {
        clock_.setMasterClock(duration, CURRENT_TIME);
    }

    shutdown();
    av_log(nullptr, AV_LOG_INFO, "current media player completed\n");
    setSpeed(1.0f);     // 当前媒体播放完毕之后重置速度

    std::lock_guard lock(mtx_);
    if (onPlaybackCompletedCallback_) {
        onPlaybackCompletedCallback_();
    }
}