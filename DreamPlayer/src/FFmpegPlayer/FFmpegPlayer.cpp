//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <common/ffcore.hpp>
#include <FFmpegPlayer/FFmpegPlayer.h>
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
    demuxer_->setOnDemuxStatusChangedCallback([this]() {
        tryComplete();
    });

    const AVPacket& flushPkt = demuxer_->getFlushPkt();
    videoDecodec_ = std::make_unique<VideoDecodec>(flushPkt, clock_, FRAME_VIDEO_SIZE);
    videoDecodec_->setOnDecodecStatusChangedCallback([this]() {
        tryComplete();
    });
    audioDecodec_ = std::make_unique<AudioDecodec>(flushPkt, clock_, FRAME_AUDIO_SIZE);
    audioDecodec_->setOnDecodecStatusChangedCallback([this]() {
        tryComplete();
    });
}

FFmpegPlayer::~FFmpegPlayer() {
    stop();

    avformat_network_deinit();
}

bool FFmpegPlayer::open(const char* url) {
    std::lock_guard lock(mtx_);
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
    // open 可能持锁等待 FFmpeg，先中断 I/O 再等初始化退出。
    demuxer_->cancelIO();
    {
        std::lock_guard lock(mtx_);
        stopping_ = true;
    }

    demuxer_->stopDemux();

    if (audioDecodec_) {
        audioDecodec_->shutdown();
        audioDecodec_->stop();
        audioDecodec_->setOnSeekCallback(nullptr);
    }

    if (videoDecodec_) {
        videoDecodec_->shutdown();
        videoDecodec_->stop();
        videoDecodec_->setOnSeekCallback(nullptr);
    }

    setOnPlaybackCompletedCallback(nullptr);

    clock_.initClock(nullptr, nullptr);
}

uint32_t FFmpegPlayer::getWidth() {
    return videoDecodec_ ? videoDecodec_->getWidth() : 0;
}

uint32_t FFmpegPlayer::getHeight() {
    return videoDecodec_ ? videoDecodec_->getHeight() : 0;
}

void FFmpegPlayer::seek(double targetPos, double seekRel) {
    demuxer_->seek(targetPos, seekRel);
}

int64_t FFmpegPlayer::getDuration() const {
    return demuxer_->getDuration();
}

double FFmpegPlayer::getCurrentTime() {
    double currentTime = clock_.getMasterClock();
    return isnan(currentTime) ? 0 : currentTime;
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
    if (!videoDecodec_) {
        return nullptr;
    }

    VideoFrame* frame = videoDecodec_->getVideoFrame();
    if (demuxer_->isEof() && !playbackCompleted_) {
        tryComplete();
    }
    return frame;
}

AudioFrame* FFmpegPlayer::getAudioFrame() {
    if (!audioDecodec_) {
        return nullptr;
    }

    AudioFrame* frame = audioDecodec_->getAudioFrame();
    if (demuxer_->isEof() && !playbackCompleted_) {
        tryComplete();
    }
    return frame;
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
    if (masterMode == SyncType::AudioMaster && audioDecodec_) {
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

    clock_.setMasterMode(syncType);
    setOnSeekCallback(cb);
}

SyncType FFmpegPlayer::getMasterMode() const {
    return clock_.getMasterMode();
}

void FFmpegPlayer::handle(void* data) {
    AVPacket* pkt = static_cast<AVPacket*>(data);

    if (&demuxer_->getFlushPkt() == pkt) {
        playbackCompleted_ = false;
    }

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

void FFmpegPlayer::tryComplete() {
    auto isStreamDrained = [](Decodec* decodec) {
        if (!decodec) {
            return true;
        }

        return decodec->isDecodeFinished() && !decodec->hasBufferedFrames();
    };

    OnPlaybackCompletedCallback cb = nullptr;
    {
        std::lock_guard lock(mtx_);
        if (stopping_ || !demuxer_->isEof() || playbackCompleted_) {
            return;
        }

        bool isVideoStreamDrained = isStreamDrained(videoDecodec_.get());
        bool isAudioStreamDrained = isStreamDrained(audioDecodec_.get());
        if (!isVideoStreamDrained || !isAudioStreamDrained) {
            return;
        }

        playbackCompleted_ = true;
        clock_.pauseMasterClock();

        const double duration = demuxer_->getDuration();

        if (duration != AV_NOPTS_VALUE) {
            clock_.setMasterClock(duration, CURRENT_TIME);
        }

        cb = onPlaybackCompletedCallback_;
    }

    av_log(nullptr, AV_LOG_INFO, "current media player completed\n");
    if (cb) {
        cb();
    }
}
