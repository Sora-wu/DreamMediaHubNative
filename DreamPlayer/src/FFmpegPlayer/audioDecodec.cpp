//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <FFmpegPlayer/audioDecodec.h>
#include <FFmpegPlayer/mediaClock.h>
#include <DreamPlayer/structs.h>
#include <DreamPlayer/DreamPlayerContext.h>

#include <algorithm>

using namespace Dream;

namespace {
    constexpr int SAMPLE_RATE = 44100;
    constexpr int CHANNEL_COUNT = 2;
    constexpr AVSampleFormat SAMPLE_FORMAT = AV_SAMPLE_FMT_S16;
    const uint64_t CHANNEL_LAYOUT = AV_CH_LAYOUT_STEREO;

    constexpr int AUDIO_DIFF_AVG_NB = 20;             // 用于移动平均的测量次数
    constexpr double AV_NOSYNC_THRESHOLD = 10.0;       // 超过10秒不进行同步，直接重置
    constexpr int SAMPLE_CORRECTION_PERCENT_MAX = 10; // 单次最大调整样本比例 (10%)
    constexpr double AUDIO_DIFF_THRESHOLD_MIN = 0.04; // 最小同步触发阈值 (40ms)
    constexpr double AUDIO_DIFF_THRESHOLD_MAX = 0.1;  // 最大同步触发阈值 (100ms)

    void updateAudioFrameSize(AudioFrame* audioFrame, int buffSize) {
        memset(audioFrame->data, 0, audioFrame->capacity);

        if (audioFrame->capacity >= static_cast<uint32_t>(buffSize)) {
            return;
        }

        delete[] audioFrame->data;
        audioFrame->data = new char[buffSize]{};
        audioFrame->len = buffSize;
        audioFrame->capacity = buffSize;
    }
}

AudioDecodec::~AudioDecodec() {
    if (swrCtx_) {
        swr_free(&swrCtx_);
        swrCtx_ = nullptr;
    }

    if (audioFrame_.data) {
        delete[] audioFrame_.data;
        audioFrame_.data = nullptr;
        audioFrame_.len = 0;
    }
}

bool AudioDecodec::openCodec(int streamIndex, AVStream* stream) {
    if (!Decodec::openCodec(streamIndex, stream)) {
        return false;
    }

    audioDiffCum_ = 0.0;
    audioDiffAvgCount_ = 0;
    // 加权系数算法：exp(log(0.01) / AUDIO_DIFF_AVG_NB)
    audioDiffAvgCoef_ = std::exp(std::log(0.01) / AUDIO_DIFF_AVG_NB);

    int bytesPerSec = SAMPLE_RATE * CHANNEL_COUNT * av_get_bytes_per_sample(SAMPLE_FORMAT);
    int dummyHwBufSize = DreamPlayerContext::AUDIO_BUFFER_SIZE * CHANNEL_COUNT * av_get_bytes_per_sample(SAMPLE_FORMAT);
    audioDiffThreshold_ = (double)(dummyHwBufSize) / bytesPerSec;
    audioDiffThreshold_ = std::clamp(audioDiffThreshold_, AUDIO_DIFF_THRESHOLD_MIN, AUDIO_DIFF_THRESHOLD_MAX);

    audioSpeedFilter_ = std::make_unique<AudioSpeedFilter>();
    audioSpeedFilter_->initFilter(speed_, SAMPLE_RATE, SAMPLE_FORMAT, CHANNEL_LAYOUT);

    return true;
}

void AudioDecodec::threadFunc(std::stop_token st) {
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        av_log(nullptr, AV_LOG_FATAL, "Could not allocate audio frame");
        return;
    }

    uint32_t serial = decodecSerial_;
    double pts{};
    while (!st.stop_requested()) {
        while (!taskQueue_.empty()) {
            if (auto task = taskQueue_.try_pop()) {
                (*task)(*this);
            }
        }

        pushFrames(paddingFrames_, decodecTimer_);
        paddingFrames_.clear();

        if (!decode(frame)) {
            if (st.stop_requested()) {
                break;
            }
            continue;
        }

        if (serial != decodecSerial_) {
            serial = decodecSerial_;
            swr_free(&swrCtx_);
            audioDiffAvgCount_ = 0;
            audioDiffCum_ = 0.0;
        }
        std::vector<AVFrame*> frames = processFrame(frame);
        pts = static_cast<double>(frame->pts) / frame->sample_rate;
        pushFrames(frames, pts);
    }

    av_frame_free(&frame);
    av_log(nullptr, AV_LOG_INFO, "Finished decoding audio stream\n");
}

void AudioDecodec::reopen() {
    Decodec::reopen();
}

void AudioDecodec::start() {
    Decodec::start();
    FFUtils::setThreadName(thread_, "audio thread");
}

AudioFrame* AudioDecodec::getAudioFrame() {
    audioFrame_.isUpdated = false;

    Frame ap{};
    if (!acquireFrame(ap)) {
        return &audioFrame_;
    }

    AVFrame* frame = ap.frame;
    // 更新音频时钟
    decodecTimer_ = NAN;
    if (!isnan(ap.pts)) {
        decodecTimer_ = ap.pts + ap.duration;
    }
    audioFrame_.pts = decodecTimer_;
    audioFrame_.serial = ap.serial;

    fillData(frame);
    audioFrame_.isUpdated = !isPause_;

    releaseFrame(ap);
    return &audioFrame_;
}

void AudioDecodec::setSpeed(float speed) {
    taskQueue_.try_push([this, speed](AudioDecodec&) {
        if (!FFUtils::isEqual(speed, speed_)) {
            speed_ = speed;

            // flush
            audioSpeedFilter_->addFrame(nullptr);
            while (true) {
                AVFrame* out = framePool_.get();
                int res = audioSpeedFilter_->getFrame(out);
                if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) {
                    break;
                }
                if (res < 0) {
                    av_log(nullptr, AV_LOG_ERROR, "Error occurred during filtering\n");
                    break;
                }

                paddingFrames_.push_back(out);
            }

            audioSpeedFilter_ = std::make_unique<AudioSpeedFilter>();
            audioSpeedFilter_->initFilter(speed_, SAMPLE_RATE, SAMPLE_FORMAT, CHANNEL_LAYOUT);
        }
    });
}

void AudioDecodec::cleanup() {
    if (swrCtx_) {
        swr_free(&swrCtx_);
        swrCtx_ = nullptr;
    }

    audioSpeedFilter_.reset();

    cachedSampleRate_ = 0;
    cachedChannelLayout_ = 0;
    cachedFormat_ = AV_SAMPLE_FMT_NONE;

    Decodec::cleanup();
}

bool AudioDecodec::refreshSwr(AVFrame* frame) {
    if (!swrCtx_ || frame->sample_rate != cachedSampleRate_ || frame->channel_layout != cachedChannelLayout_ || frame->format != cachedFormat_) {
        swr_free(&swrCtx_);
        swrCtx_ = swr_alloc_set_opts(nullptr,
                                     CHANNEL_LAYOUT, SAMPLE_FORMAT, SAMPLE_RATE,
                                     frame->channel_layout, (AVSampleFormat)frame->format, frame->sample_rate,
                                     0, nullptr);
        if (!swrCtx_ || swr_init(swrCtx_) < 0) {
            av_log(nullptr, AV_LOG_ERROR, "swr_alloc_set_opts failed\n");
            swr_free(&swrCtx_);
            cachedSampleRate_ = 0;
            cachedChannelLayout_ = 0;
            cachedFormat_ = 0;
            return false;
        }

        cachedSampleRate_ = frame->sample_rate;
        cachedChannelLayout_ = frame->channel_layout;
        cachedFormat_ = frame->format;
    }

    return true;
}

void AudioDecodec::fillData(AVFrame* frame) {
    const int bufferSize = av_samples_get_buffer_size(nullptr, frame->channels, frame->nb_samples, (AVSampleFormat)frame->format, 1);
    audioFrame_.len = bufferSize;
    updateAudioFrameSize(&audioFrame_, bufferSize);
    memcpy(audioFrame_.data, frame->data[0], bufferSize);
}

int AudioDecodec::wantedSamples(const AVFrame* frame) {
    const int samples = frame->nb_samples;
    const double diff = mediaClock_.getAudioClock() - mediaClock_.getMasterClock();
    if (!FFUtils::isEqual(1.0f, speed_) || mediaClock_.getMasterMode() != SyncType::VideoMaster
        || !std::isfinite(diff) || std::abs(diff) >= AV_NOSYNC_THRESHOLD) {
        audioDiffAvgCount_ = 0;
        audioDiffCum_ = 0.0;
        return samples;
    }

    audioDiffCum_ = diff + audioDiffAvgCoef_ * audioDiffCum_;
    if (audioDiffAvgCount_ < AUDIO_DIFF_AVG_NB) {
        ++audioDiffAvgCount_;
        return samples;
    }
    if (std::abs(audioDiffCum_ * (1.0 - audioDiffAvgCoef_)) < audioDiffThreshold_) {
        return samples;
    }

    const int limit = samples * SAMPLE_CORRECTION_PERCENT_MAX / 100;
    return samples + std::clamp(static_cast<int>(diff * frame->sample_rate), -limit, limit);
}

std::vector<AVFrame*> AudioDecodec::processFrame(AVFrame* frame) {
    std::vector<AVFrame*> frames;

    AVFrame* converted = convertFrame(frame);
    if (!converted) {
        return frames;
    }

    if (!audioSpeedFilter_ || audioSpeedFilter_->addFrame(converted) < 0) {
        frames.push_back(converted);
        return frames;
    }
    framePool_.put(converted);

    while (true) {
        AVFrame* out = framePool_.get();
        if (!out) {
            break;
        }

        const int res = audioSpeedFilter_->getFrame(out);
        if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) {
            framePool_.put(out);
            break;
        }
        if (res < 0) {
            av_log(nullptr, AV_LOG_ERROR, "Error while receiving audio from the filter\n");
            framePool_.put(out);
            break;
        }

        frames.push_back(out);
    }

    return frames;
}

AVFrame* AudioDecodec::convertFrame(AVFrame* frame) {
    if (!frame->channel_layout) {
        frame->channel_layout = av_get_default_channel_layout(frame->channels);
    }
    const int wanted = wantedSamples(frame);
    if (!swrCtx_ && wanted == frame->nb_samples && frame->sample_rate == SAMPLE_RATE
        && frame->format == SAMPLE_FORMAT && frame->channel_layout == CHANNEL_LAYOUT) {
        AVFrame* dup = framePool_.get();
        if (!dup || av_frame_ref(dup, frame) < 0) {
            framePool_.put(dup);
            return nullptr;
        }
        return dup;
    }

    if (!refreshSwr(frame)) {
        return nullptr;
    }

    if (wanted != frame->nb_samples) {
        const int delta = av_rescale_rnd(wanted - frame->nb_samples, SAMPLE_RATE, frame->sample_rate, AV_ROUND_ZERO);
        const int distance = av_rescale_rnd(wanted, SAMPLE_RATE, frame->sample_rate, AV_ROUND_UP);
        if (delta && swr_set_compensation(swrCtx_, delta, distance) < 0) {
            av_log(nullptr, AV_LOG_WARNING, "Could not apply audio sync compensation\n");
        }
    }

    const int outSampleCount = swr_get_out_samples(swrCtx_, frame->nb_samples);
    if (outSampleCount <= 0) {
        return nullptr;
    }
    AVFrame* dst = framePool_.get();
    if (!dst) {
        return nullptr;
    }

    dst->format = SAMPLE_FORMAT;
    dst->sample_rate = SAMPLE_RATE;
    dst->channel_layout = CHANNEL_LAYOUT;
    dst->channels = CHANNEL_COUNT;
    dst->nb_samples = outSampleCount;
    if (av_frame_get_buffer(dst, 0) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "av_frame_get_buffer failed in convertFrame\n");
        framePool_.put(dst);
        return nullptr;
    }

    const int converted = swr_convert(swrCtx_, dst->data, outSampleCount, (const uint8_t**)frame->extended_data, frame->nb_samples);
    if (converted <= 0) {
        framePool_.put(dst);
        return nullptr;
    }

    dst->nb_samples = converted;
    av_frame_copy_props(dst, frame);
    if (frame->pts != AV_NOPTS_VALUE) {
        dst->pts = av_rescale_q(frame->pts, {1, frame->sample_rate}, {1, SAMPLE_RATE});
    }

    // 确保所需的属性正确，因为已经经过重采样
    dst->format = SAMPLE_FORMAT;
    dst->sample_rate = SAMPLE_RATE;
    dst->channel_layout = CHANNEL_LAYOUT;
    dst->channels = CHANNEL_COUNT;

    return dst;
}

void AudioDecodec::pushFrames(const std::vector<AVFrame*>& frames, double pts) {
    if (frames.empty()) {
        return;
    }

    double duration{};
    // 经过滤镜处理可能有多个音频帧，需要累加pts
    for (AVFrame* f : frames) {
        duration = (double)f->nb_samples / f->sample_rate * speed_;
        pushFrame(f, pts, duration);
        pts += duration;
        decodecTimer_ = pts;

        framePool_.put(f);
    }
}
