//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <FFmpegPlayer/videoDecodec.h>
#include <FFmpegPlayer/mediaClock.h>
#include <DreamPlayer/structs.h>

#include <algorithm>

using namespace Dream;

namespace {
    constexpr AVPixelFormat PIXEL_FORMAT = AV_PIX_FMT_YUV420P;
    constexpr double SYNC_THRESHOLD_MIN = 0.04;
    constexpr double SYNC_THRESHOLD_MAX = 0.1;
    constexpr double SYNC_FRAMEDUP_THRESHOLD = 0.1;
    constexpr double SYNC_THRESHOLD_MAX_DRIFT = 0.1;
}

VideoDecodec::~VideoDecodec() {
    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }

    for (uint32_t i = 0; i < 3; ++i) {
        if (videoFrame_.data[i]) {
            delete[] videoFrame_.data[i];
            videoFrame_.data[i] = nullptr;
            videoFrame_.len[i] = 0;
            videoFrame_.stride[i] = 0;
        }
    }
}

bool VideoDecodec::openCodec(int streamIndex, AVStream* stream, AVFormatContext& input) {
    if (!Decodec::openCodec(streamIndex, stream)) {
        return false;
    }

    input_ = &input;
    maxFrameDuration_ = (input_->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;
    return true;
}

void VideoDecodec::reopen() {
    resetSyncState();
    Decodec::reopen();
}

void VideoDecodec::start() {
    Decodec::start();
    FFUtils::setThreadName(thread_, "video thread");
}

uint32_t VideoDecodec::getWidth() const {
    return codecCtx_ ? codecCtx_->width : 0;
}

uint32_t VideoDecodec::getHeight() const {
    return codecCtx_ ? codecCtx_->height : 0;
}

void VideoDecodec::resetSyncState() {
    releaseFrame(heldFrame_);
    decodeTimerValid_ = false;
}

VideoFrame* VideoDecodec::getVideoFrame() {
    for (;;) {
        if (!heldFrame_.frame) {
            Frame frame{};
            if (!acquireFrame(frame)) {
                return &videoFrame_;
            }

            heldFrame_ = frame;
            decodecTimer_ = CURRENT_TIME;
            decodeTimerValid_ = true;
            if (!isnan(heldFrame_.pts)) {
                mediaClock_.setVideoClock(heldFrame_.pts, heldFrame_.serial, decodecTimer_);
            }

            if (fillData(heldFrame_.frame)) {
                return &videoFrame_;
            }
            // fillData返回false，释放当前帧 取下一帧
            releaseFrame(heldFrame_);
            continue;
        }

        double delay = 0;
        frameQue_.visit_front([&](const Frame& next) {
            delay = calcDelay(getFrameDuration(heldFrame_, next));
        });

        const double time = CURRENT_TIME;
        if (!decodeTimerValid_) {
            decodecTimer_ = time;
            decodeTimerValid_ = true;
        }

        // 没有到达新帧的播放时间，使用老帧
        if (time < decodecTimer_ + delay || isPause_) {
            // fillData(heldFrame_.frame, videoFrame);
            return &videoFrame_;
        }

        decodecTimer_ += delay;
        // 如果和系统时间差距太大，就纠正为系统时间
        if (delay > 0 && time - decodecTimer_ > SYNC_THRESHOLD_MAX_DRIFT) {
            decodecTimer_ = time;
        }

        while (shouldDropLateFrame(time)) {
            std::optional<Frame> dropOpt = frameQue_.try_pop();
            if (!dropOpt) {
                break;
            }

            if (dropOpt->serial != decodecSerial_) {
                decodeTimerValid_ = false;
            }

            releaseFrame(*dropOpt);
        }

        // 当前帧已经播放完毕，释放
        releaseFrame(heldFrame_);
    }
}

void VideoDecodec::cleanup() {
    resetSyncState();
    Decodec::cleanup();

    sws_freeContext(swsCtx_);
}

void VideoDecodec::threadFunc(std::stop_token st) {
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        av_log(nullptr, AV_LOG_FATAL, "Could not allocate video frame");
        return;
    }

    AVRational timeBase = stream_->time_base;
    AVRational frameRate = av_guess_frame_rate(input_, stream_, nullptr);
    double duration{};
    double pts{};

    while (!st.stop_requested()) {
        if (!decode(frame)) {
            if (st.stop_requested()) {
                break;
            }
            continue;
        }

        frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(input_, stream_, frame);
        duration = av_q2d(frameRate) > 0 ? av_q2d(av_inv_q(frameRate)) : 0.0;
        pts = frame->pts == AV_NOPTS_VALUE ? NAN : frame->pts * av_q2d(timeBase);
        pushFrame(frame, pts, duration);
    }

    av_frame_free(&frame);
    av_log(nullptr, AV_LOG_INFO, "Finished decoding video stream\n");
}

double VideoDecodec::getFrameDuration(const Frame& frame, const Frame& nextFrame) const {
    if (frame.serial != nextFrame.serial) {
        return 0;
    }

    const double duration = nextFrame.pts - frame.pts;
    if (isnan(duration) || duration <= 0 || duration > maxFrameDuration_) {
        return frame.duration;
    }

    return duration;
}

double VideoDecodec::calcDelay(double duration) const {
    if (mediaClock_.getMasterMode() == SyncType::VideoMaster) {
        return duration;
    }

    double delay = duration;
    const double diff = mediaClock_.getVideoClock() - mediaClock_.getMasterClock();
    const double syncThreshold = std::clamp(delay, SYNC_THRESHOLD_MIN, SYNC_THRESHOLD_MAX);
    if (!isnan(diff) && std::fabs(diff) < maxFrameDuration_) {
        if (diff <= -syncThreshold) {
            delay = std::max(0.0, delay + diff);
        }
        else if (diff >= syncThreshold && delay > SYNC_FRAMEDUP_THRESHOLD) {
            delay += diff;
        }
        else if (diff >= syncThreshold) {
            delay *= 2;
        }
    }

    return delay;
}

bool VideoDecodec::shouldDropLateFrame(double time) const {
    if (mediaClock_.getMasterMode() == SyncType::VideoMaster || frameQue_.size() <= 1) {
        return false;
    }

    bool drop = false;
    frameQue_.visit_front([&](const Frame& next) {
        const double duration = getFrameDuration(heldFrame_, next);
        drop = duration > 0 && time > decodecTimer_ + duration;
    });

    return drop;
}

bool VideoDecodec::fillData(AVFrame* frame) {
    const int width = frame->width;
    const int height = frame->height;

    videoFrame_.picWidthHeightRatio = frame->sample_aspect_ratio.den == 0 ? 0 : av_q2d(frame->sample_aspect_ratio);

    int outLinesize[4]{};
    if (av_image_fill_linesizes(outLinesize, PIXEL_FORMAT, width) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Cannot fill linesize\n");
        return false;
    }
    updateVideoFrameSize(width, height, outLinesize);

    // 如果不是PIXEL_FORMAT的AVFrame都需要转换
    if (frame->format != PIXEL_FORMAT || frame->width != cachedWidth_ || frame->height != cachedHeight_) {
        // 没有命中缓存的统一更新swsCtx_
        if (frame->format != cachedFormat_ || frame->width != cachedWidth_ || frame->height != cachedHeight_) {
            swsCtx_ = sws_getCachedContext(swsCtx_,
                width, height, (AVPixelFormat)frame->format,
                width, height, PIXEL_FORMAT,
                SWS_BICUBIC, nullptr, nullptr, nullptr);
            if (!swsCtx_) {
                av_log(nullptr, AV_LOG_ERROR, "Cannot initialize the conversion context\n");
                return false;
            }
            cachedFormat_ = frame->format;
            cachedWidth_ = frame->width;
            cachedHeight_ = frame->height;
        }

        sws_scale(swsCtx_, frame->data, frame->linesize, 0, height,
            reinterpret_cast<uint8_t* const*>(videoFrame_.data), videoFrame_.stride);
    }
    else {
        const int halfHeight = (height + 1) / 2;            // 当遇到奇数高度时，需要向上取整
        const int rows[3] = { height, halfHeight, halfHeight };
        for (uint32_t i = 0; i < 3; ++i) {
            const int rowBytes = outLinesize[i];
            const int srcStride = frame->linesize[i];
            const int dstStride = videoFrame_.stride[i];

            if (!frame->data[i] || !videoFrame_.data[i] || std::abs(srcStride) < rowBytes || dstStride < rowBytes) {
                return false;
            }

            av_image_copy_plane(reinterpret_cast<uint8_t*>(videoFrame_.data[i]), dstStride,
                frame->data[i], srcStride,
                rowBytes, rows[i]);
        }
    }

    return true;
}

void VideoDecodec::updateVideoFrameSize(const int width, const int height, const int linesize[4]) {
    bool needsUpdate = false;
    if (width != videoFrame_.frameWidth || height != videoFrame_.frameHeight) {
        // 输入的宽高不一致
        needsUpdate = true;
    }

    const int halfHeight = (height + 1) / 2;
    const int planeHeights[3] = { height, halfHeight, halfHeight };
    for (uint32_t i = 0; i < 3; ++i) {
        if (linesize[i] != videoFrame_.stride[i]) {
            needsUpdate = true;
            break;
        }
    }

    if (needsUpdate) {
        videoFrame_.frameWidth = width;
        videoFrame_.frameHeight = height;

        for (uint32_t i = 0; i < 3; ++i) {
            if (videoFrame_.data[i]) {
                delete[] videoFrame_.data[i];
                videoFrame_.data[i] = nullptr;
                videoFrame_.len[i] = 0;
                videoFrame_.stride[i] = 0;
            }

            const int bufSize = linesize[i] * planeHeights[i];
            videoFrame_.data[i] = new char[bufSize]{};
            videoFrame_.len[i] = bufSize;
            videoFrame_.stride[i] = linesize[i];
        }
    }
}
