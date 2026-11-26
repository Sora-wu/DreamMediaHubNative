//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <FFmpegRecorder/audioEncoder.h>
#include <common/ffcore.hpp>
#include <DreamRecorder/DreamRecorderContext.h>

#include <algorithm>
#include <climits>
#include <utility>

using namespace Dream;

namespace {
    constexpr AVSampleFormat SAMPLER_FORMAT = AV_SAMPLE_FMT_FLTP;
    constexpr uint32_t MAX_PLANES = 8;
}

AudioEncoder::~AudioEncoder() {
    if (swrCtx_) {
        swr_free(&swrCtx_);
        swrCtx_ = nullptr;
    }

    if (audioFifo_) {
        av_audio_fifo_free(audioFifo_);
        audioFifo_ = nullptr;
    }

    if (converted_[0]) {
        av_freep(&converted_[0]);
    }
}

bool AudioEncoder::addStream(AVFormatContext& output, AVCodecID codecID) {
    if (!Encoder::addStream(output, codecID)) {
        return false;
    }

    stream_->time_base = AVRational{1, static_cast<int32_t>(param_.sampleRate)};

    swrCtx_ = swr_alloc_set_opts(nullptr,
        codecCtx_->channel_layout, SAMPLER_FORMAT, codecCtx_->sample_rate,
        codecCtx_->channel_layout, static_cast<AVSampleFormat>(param_.sampleFormat), codecCtx_->sample_rate,
        0, nullptr);
    if (!swrCtx_) {
        return false;
    }
    if (swr_init(swrCtx_) < 0) {
        return false;
    }

    audioFifo_ = av_audio_fifo_alloc(codecCtx_->sample_fmt, codecCtx_->channels, 1);
    if (!audioFifo_) {
        return false;
    }

    convertedSampleCount_ = DreamRecorderContext::AUDIO_BUFFER_SIZE / (param_.channels * av_get_bytes_per_sample(static_cast<AVSampleFormat>(param_.sampleFormat)));
    return av_samples_alloc(converted_, nullptr, codecCtx_->channels, convertedSampleCount_, SAMPLER_FORMAT, 0) >= 0;
}

bool AudioEncoder::initCodecContext(AVCodecID codecID) {
    if (param_.channels < 1 || param_.channels > MAX_PLANES) {
        av_log(nullptr, AV_LOG_ERROR, "AudioEncoder::initCodecContext param of channels out of range\n");
        return false;
    }

    const int64_t channelLayout = av_get_default_channel_layout(param_.channels);
    if (channelLayout <= 0 || av_get_bytes_per_sample(static_cast<AVSampleFormat>(param_.sampleFormat)) <= 0) {
        av_log(nullptr, AV_LOG_ERROR, "AudioEncoder::initCodecContext invalid PCM format or channel layout\n");
        return false;
    }

    codecCtx_->bit_rate = param_.bitRate;
    codecCtx_->sample_rate = param_.sampleRate;
    codecCtx_->sample_fmt = SAMPLER_FORMAT;
    codecCtx_->channels = param_.channels;
    codecCtx_->channel_layout = channelLayout;
    codecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    return true;
}

void AudioEncoder::threadFunc(std::stop_token st) {
    Encoder::threadFunc(st);
    av_log(nullptr, AV_LOG_INFO, "AudioEncoder::threadFunc exit\n");
}

bool AudioEncoder::pushData(const AudioFrameBuffer& buffer, uint64_t pts) {
    int inSampleCount = 0;
    if (!checkAudioBufferValid(buffer, inSampleCount)) {
        av_log(nullptr, AV_LOG_ERROR, "AudioEncoder::pushData invalid PCM buffer\n");
        return false;
    }

    std::lock_guard lock(mtx_);
    if (!converted_[0]) {
        return false;
    }

    const int outputCapacity = swr_get_out_samples(swrCtx_, inSampleCount);
    const int fifoSize = av_audio_fifo_size(audioFifo_);
    if (outputCapacity < 0 || outputCapacity > INT_MAX - fifoSize) {
        return false;
    }

    if (outputCapacity > convertedSampleCount_) {
        uint8_t* enlarged[MAX_PLANES]{};
        if (av_samples_alloc(enlarged, nullptr, codecCtx_->channels, outputCapacity, SAMPLER_FORMAT, 0) < 0) {
            return false;
        }
        std::swap(enlarged, converted_);
        av_free(enlarged[0]);
        convertedSampleCount_ = outputCapacity;
    }

    // Reserve FIFO space before conversion can consume the input.
    if (outputCapacity > av_audio_fifo_space(audioFifo_) &&
            av_audio_fifo_realloc(audioFifo_, fifoSize + outputCapacity) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "AudioEncoder::pushData av_audio_fifo_realloc error\n");
        return false;
    }

    const uint8_t* inputPlanes[MAX_PLANES];
    std::copy_n(buffer.planes, MAX_PLANES, inputPlanes);
    const int convertedSampleCount = swr_convert(swrCtx_, converted_, convertedSampleCount_, inputPlanes, inSampleCount);
    if (convertedSampleCount < 0) {
        return false;
    }

    void* fifoPlanes[MAX_PLANES];
    std::copy_n(converted_, MAX_PLANES, fifoPlanes);
    if (convertedSampleCount > 0 && av_audio_fifo_write(audioFifo_, fifoPlanes, convertedSampleCount) != convertedSampleCount) {
        return false;
    }

    const int frameSize = codecCtx_->frame_size;
    while (av_audio_fifo_size(audioFifo_) > frameSize) {
        AVFrame* frame = framePool_.get();
        if (!frame) {
            return false;
        }
        int frameResult;
        if (frame->channels != param_.channels || frame->sample_rate != param_.sampleRate || frame->format != SAMPLER_FORMAT
                || frame->channel_layout != codecCtx_->channel_layout || frame->nb_samples != frameSize) {
            frame->channels = param_.channels;
            frame->sample_rate = param_.sampleRate;
            frame->format = SAMPLER_FORMAT;
            frame->channel_layout = codecCtx_->channel_layout;
            frame->nb_samples = frameSize;
            frameResult = av_frame_get_buffer(frame, 0);
        }
        else {
            frameResult = av_frame_make_writable(frame);
        }
        if (frameResult < 0) {
            av_frame_free(&frame);
            return false;
        }

        std::copy_n(frame->data, MAX_PLANES, fifoPlanes);
        if (av_audio_fifo_read(audioFifo_, fifoPlanes, frameSize) != frameSize) {
            av_frame_free(&frame);
            return false;
        }

        // 如果是第一次赋值，取pts换时间基
        if (currentPts_ == AV_NOPTS_VALUE) {
            currentPts_ = av_rescale_q(pts, AV_TIME_BASE_Q, codecCtx_->time_base);
        }
        frame->pts = currentPts_;
        if (!frameQue_.push(frame)) {
            av_frame_free(&frame);
            return false;
        }
        currentPts_ += frameSize;
    }

    return true;
}

bool AudioEncoder::checkAudioBufferValid(const AudioFrameBuffer& buffer, int& sampleCount) const {
    const auto format = static_cast<AVSampleFormat>(param_.sampleFormat);
    const int sampleBytes = av_get_bytes_per_sample(format);
    if (sampleBytes <= 0 || param_.channels == 0 || param_.channels > MAX_PLANES) {
        return false;
    }

    const bool planar = av_sample_fmt_is_planar(format);
    const uint32_t planeCount = planar ? param_.channels : 1;
    const uint64_t sampleBlockBytes = static_cast<uint64_t>(sampleBytes) * (planar ? 1 : param_.channels);
    const uint32_t planeBytes = buffer.planeBytes[0];
    if (!buffer.planes[0] || planeBytes == 0 || planeBytes % sampleBlockBytes != 0) {
        return false;
    }

    for (uint32_t i = 1; i < planeCount; ++i) {
        if (!buffer.planes[i] || buffer.planeBytes[i] != planeBytes) {
            return false;
        }
    }

    for (uint32_t i = planeCount; i < MAX_PLANES; ++i) {
        if (buffer.planes[i] || buffer.planeBytes[i] != 0) {
            return false;
        }
    }

    const uint64_t samples = planeBytes / sampleBlockBytes;
    if (samples > INT_MAX) {
        return false;
    }

    sampleCount = static_cast<int>(samples);
    return true;
}
