//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <FFmpegRecorder/FFmpegRecorder.h>
#include <FFmpegRecorder/ffinstance.h>

namespace {
    constexpr uint32_t VIDEO_QUE_SIZE = 5;
    constexpr uint32_t AUDIO_QUE_SIZE = 16;
}

using namespace Dream;

void FFmpegRecorder::start(const char* path, const VideoParam& videoParam, const AudioParam& audioParam) {
    mux_ = std::make_unique<Muxer>();
    mux_->init(path);

    do {
        if (!videoParam.enable) {
            break;
        }
        videoEncoder_ = std::make_unique<VideoEncoder>(videoParam, VIDEO_QUE_SIZE);
        if (!videoEncoder_->addStream(AV_CODEC_ID_H264)) {
            videoEncoder_ = nullptr;
            break;
        }
        videoEncoder_->setNextHandler(mux_.get());
        videoEncoder_->start("video encoder thread");
    } while (false);

    do {
        if (!audioParam.enable) {
            break;
        }
        if (audioParam.sampleFormat == SampleFormat::NONE) {
            av_log(nullptr, AV_LOG_ERROR, "unsupported audio format type, will output without audio stream!\n");
            break;
        }
        audioEncoder_ = std::make_unique<AudioEncoder>(audioParam, AUDIO_QUE_SIZE);
        if (!audioEncoder_->addStream(AV_CODEC_ID_AAC)) {
            audioEncoder_ = nullptr;
            break;
        }
        audioEncoder_->setNextHandler(mux_.get());
        audioEncoder_->start("audio encoder thread");
    } while (false);

    if (!audioEncoder_ && !videoEncoder_) {
        av_log(nullptr, AV_LOG_ERROR, "invalid media format!\n");
        return;
    }

    AVFormatContext* fmtCtx = FFINSTANCE()->getFormatContext();
    av_dump_format(fmtCtx, 0, nullptr, true);
    mux_->start("muxer thread");
}

void FFmpegRecorder::stop() {
    if (videoEncoder_) {
        videoEncoder_->close();
        videoEncoder_->exit();
        videoEncoder_ = nullptr;
    }

    if (audioEncoder_) {
        audioEncoder_->close();
        audioEncoder_->exit();
        audioEncoder_ = nullptr;
    }

    mux_->close();
    mux_->exit();
    mux_ = nullptr;
}

VideoFrameBuffer FFmpegRecorder::getVideoBuffer() {
    return videoEncoder_ ? videoEncoder_->getVideoBuffer() : VideoFrameBuffer{};
}

void FFmpegRecorder::commitVideoBuffer(const VideoFrameBuffer& buffer, uint64_t pts) {
    if (videoEncoder_) {
        videoEncoder_->commitVideoBuffer(buffer, pts);
    }
}

bool FFmpegRecorder::pushAudioData(const AudioFrameBuffer& buffer, uint64_t pts) {
    return audioEncoder_ ? audioEncoder_->pushData(buffer, pts) : false;
}
