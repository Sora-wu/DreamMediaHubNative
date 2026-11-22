//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <FFmpegPlayer/FFmpegPlayer.h>
#include <FFmpegPlayer/ffcore.h>
#include <FFmpegPlayer/demuxer.h>
#include <FFmpegPlayer/videoDecodec.h>
#include <FFmpegPlayer/audioDecodec.h>
#include <FFmpegPlayer/ffinstance.h>

#include <algorithm>

using namespace Dream;

namespace {
    constexpr std::size_t FRAME_VIDEO_SIZE = 3;
    constexpr std::size_t FRAME_AUDIO_SIZE = 16;
}

FFmpegPlayer::FFmpegPlayer() {
    avdevice_register_all();
    avformat_network_init();

    demuxer_ = std::make_unique<Demuxer>();
    demuxer_->setNextHandler(this);

    videoDecodec_ = std::make_unique<VideoDecodec>(FRAME_VIDEO_SIZE);
    audioDecodec_ = std::make_unique<AudioDecodec>(FRAME_AUDIO_SIZE);
}

FFmpegPlayer::~FFmpegPlayer() {
    stop();
}

bool FFmpegPlayer::open(const char* url) {
    shutdown();

    if (!demuxer_->open(url)) {
        return false;
    }

    std::vector<AVStream*> streams = demuxer_->getStreams();
    for (uint32_t i = 0; i < streams.size(); ++i) {
        AVStream* stream = streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !videoDecodec_->isOpen()) {
            if (!videoDecodec_->openCodec(i, stream)) {
                return false;
            }
        }
        else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && !audioDecodec_->isOpen()) {
            if (!audioDecodec_->openCodec(i, stream)) {
                return false;
            }
        }
    }

    exit();

    if (audioDecodec_->isOpen()) {
        audioDecodec_->reopen();
        audioDecodec_->startDecode();
    }
    else {
        audioDecodec_ = nullptr;
        FFINSTANCE()->setSyncType(SyncType::VideoMaster);
    }

    if (videoDecodec_->isOpen()) {
        videoDecodec_->reopen();
        videoDecodec_->startDecode();
    }
    else {
        videoDecodec_ = nullptr;
        FFINSTANCE()->setSyncType(SyncType::AudioMaster);
    }

    if (!videoDecodec_ && !audioDecodec_) {
        av_log(nullptr, AV_LOG_ERROR, "this media has no available track.\n");
        return false;
    }

    demuxer_->startDemux();

    uint32_t* videoPktQueSerial = videoDecodec_ ? videoDecodec_->getPktQueSerial() : nullptr;
    uint32_t* audioPktQueSerial = audioDecodec_ ? audioDecodec_->getPktQueSerial() : nullptr;
    FFINSTANCE()->initClock(videoPktQueSerial, audioPktQueSerial);

    start("monitor thread");

    return true;
}

void FFmpegPlayer::play() {
    FFINSTANCE()->resumeMasterClock();

    if (videoDecodec_) {
        videoDecodec_->setPause(false);
    }

    if (audioDecodec_) {
        audioDecodec_->setPause(false);
    }
}

void FFmpegPlayer::pause() {
    FFINSTANCE()->pauseMasterClock();

    if (videoDecodec_) {
        videoDecodec_->setPause(true);
    }

    if (audioDecodec_) {
        audioDecodec_->setPause(true);
    }
}

bool FFmpegPlayer::isPlaying() const {
    return FFINSTANCE()->getMasterClockPause();
}

void FFmpegPlayer::stop() {
    if (audioDecodec_) {
        audioDecodec_->setOnSeekCallback(nullptr);
        audioDecodec_->shutdown();
        audioDecodec_->exit();
    }

    if (videoDecodec_) {
        videoDecodec_->setOnSeekCallback(nullptr);
        videoDecodec_->shutdown();
        videoDecodec_->exit();
    }

    demuxer_->exit();
    onPlaybackCompletedCallback_ = nullptr;
    exit();
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
    static double cachedTime = 0;
    double currentTime = FFINSTANCE()->getMasterClock();
    if (!isnan(currentTime)) {
        cachedTime = currentTime;
    }

    const double duration = demuxer_->getDuration();
    if (playbackCompleted_) {
        return duration;
    }

    return std::min(cachedTime, duration);
}

float FFmpegPlayer::getSpeed() {
    return FFINSTANCE()->getSpeed();
}

void FFmpegPlayer::setSpeed(double speed) {
    double pts = FFINSTANCE()->getAudioPTS();
    FFINSTANCE()->setMasterClock(pts, CURRENT_TIME);

    FFINSTANCE()->setSpeed(static_cast<float>(speed));

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
    FFINSTANCE()->setAudioClock(pts, serial);
}

void FFmpegPlayer::setOnAudioSeekCallback(const OnSeekCallback& cb) {
    if (audioDecodec_) {
        audioDecodec_->setOnSeekCallback(cb);
    }
}

void FFmpegPlayer::setOnVideoSeekCallback(const OnSeekCallback& cb) {
    if (videoDecodec_) {
        videoDecodec_->setOnSeekCallback(cb);
    }
}

void FFmpegPlayer::setOnPlaybackCompletedCallback(const OnPlaybackCompletedCallback& cb) {
    onPlaybackCompletedCallback_ = cb;
}

void FFmpegPlayer::setMasterMode(SyncType syncType) {
    FFINSTANCE()->setSyncType(syncType);
}

SyncType FFmpegPlayer::getMasterMode() const {
    return FFINSTANCE()->getSyncType();
}

void FFmpegPlayer::handle(void* data) {
    AVPacket* pkt = static_cast<AVPacket*>(data);

    if (playbackCompleted_) {
        if (videoDecodec_) {
            videoDecodec_->reopen();
        }

        if (audioDecodec_) {
            audioDecodec_->reopen();
        }

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

    av_packet_unref(pkt);
}

void FFmpegPlayer::run(std::stop_token st) {
    while (!st.stop_requested()) {
        checkPlaybackCompleted();
        msleep(100);
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
    if (playbackCompleted_) {
        return true;
    }

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
    FFINSTANCE()->pauseMasterClock();

    const double duration = demuxer_->getDuration();
    const double currentTime = FFINSTANCE()->getMasterClock();
    if (isnan(currentTime)) {
        return;
    }
    if (currentTime > duration) {
        FFINSTANCE()->setMasterClock(duration, CURRENT_TIME);
    }

    shutdown();
    av_log(nullptr, AV_LOG_INFO, "current media player completed\n");
    setSpeed(1.0f);     // 当前媒体播放完毕之后重置速度

    if (onPlaybackCompletedCallback_) {
        onPlaybackCompletedCallback_();
    }
}