//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <FFmpegPlayer/decodec.h>
#include <FFmpegPlayer/ffinstance.h>

bool Decodec::openCodec(int streamIndex, AVStream* stream) {
    AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        av_log(nullptr, AV_LOG_FATAL, "avcodec_find_decoder fail!");
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        av_log(nullptr, AV_LOG_FATAL, "avcodec_alloc_context3 fail!");
        return false;
    }

    int res = avcodec_parameters_to_context(codecCtx_, stream->codecpar);
    if (res < 0) {
        av_log(nullptr, AV_LOG_FATAL, "avcodec_parameters_to_context fail!");
    }

    res = avcodec_open2(codecCtx_, codec, nullptr);
    if (res < 0) {
        av_log(nullptr, AV_LOG_FATAL, "avcodec_open2 fail! codec_id: %d", stream->codecpar->codec_id);
        return false;
    }

    stream_ =  stream;
    packet_ = av_packet_alloc();
    if (!packet_) {
        avcodec_free_context(&codecCtx_);
        return false;
    }
    streamIndex_ = streamIndex;
    isPause_ = false;

    return true;
}

void Decodec::reopen() {
    pktQue_.reopen();
    while (std::optional<Frame> frameOpt = frameQue_.try_pop()) {
        av_frame_unref(frameOpt->frame);
        av_frame_free(&frameOpt->frame);
    }
    frameQue_.reopen();

    finishedSerial_ = 0;
}

void Decodec::startDecode() {
    start();
}

void Decodec::handle(void* data) {
    pktQue_.push((AVPacket*)data);
}

void Decodec::shutdown() {
    pktQue_.close();
    frameQue_.close();
}

void Decodec::cleanup() {
    std::lock_guard lock(mtx_);

    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }

    if (packet_) {
        av_packet_unref(packet_);
        av_packet_free(&packet_);
        packet_ = nullptr;
    }

    streamIndex_ = -1;
    isPause_ = true;
}


bool Decodec::decode(AVFrame* frame) {
    // 先Frame后Packet，两个操作错开，避免更多的AVERROR(EAGAIN)
    int res = AVERROR(EAGAIN);

    while (true) {
        if (*pktQue_.getSerial() == decodecSerial_) {
            do {
                res = avcodec_receive_frame(codecCtx_, frame);
                if (res >= 0) {
                    frame->pts = frame->best_effort_timestamp;
                    return true;
                }
                if (res == AVERROR_EOF) {
                    finishedSerial_ = decodecSerial_;
                    avcodec_flush_buffers(codecCtx_);
                    return false;
                }
            } while (res != AVERROR(EAGAIN));
        }

        do {
            std::optional<Packet> packetOpt = pktQue_.pop();
            if (!packetOpt) {
                return false;
            }

            auto& [pkt, serial] = *packetOpt;
            decodecSerial_ = serial;
            av_packet_move_ref(packet_, &pkt);

            if (*pktQue_.getSerial() != decodecSerial_) {
                av_packet_unref(packet_);
            }
        } while (*pktQue_.getSerial() != decodecSerial_);

        std::lock_guard lock(mtx_);
        if (packet_->data == FFINSTANCE()->getFlushPkt()->data) {
            avcodec_flush_buffers(codecCtx_);
            finishedSerial_ = 0;
            if (onSeekCallback_) {
                onSeekCallback_(decodecSerial_);
            }
            continue;
        }

        if (AVERROR(EAGAIN) == avcodec_send_packet(codecCtx_, packet_)) {
            av_log(nullptr, AV_LOG_WARNING, "avcodec_send_packet returns EAGAIN\n");
        }

        av_packet_unref(packet_);
    }
}

void Decodec::pushFrame(AVFrame* frame, double pts, double duration) {
    // TODO: 对象池优化
    AVFrame* dup = av_frame_alloc();
    if (!dup || av_frame_ref(dup, frame) < 0) {
        av_frame_free(&dup);
        return;
    }

    Frame f{};
    f.frame = dup;
    f.serial = decodecSerial_;
    f.pts = pts;
    f.duration = duration;

    frameQue_.push(f);
}

bool Decodec::acquireFrame(Frame& out) {
    while (true) {
        std::optional<Frame> frameOpt = frameQue_.try_pop();
        if (!frameOpt) {
            return false;
        }

        if (frameOpt->serial != *pktQue_.getSerial()) {
            releaseFrame(*frameOpt);
            decodeTimerValid_ = false;
            continue;
        }

        out = *frameOpt;
        return true;
    }
}

void Decodec::releaseFrame(Frame& frame) {
    if (!frame.frame) {
        return;
    }

    av_frame_unref(frame.frame);
    av_frame_free(&frame.frame);
    frame = {};
}

bool Decodec::isDecodeFinished() {
    if (finishedSerial_ == 0) {
        return false;
    }

    return finishedSerial_ == *pktQue_.getSerial();
}

bool Decodec::hasBufferedFrames() const {
    return !frameQue_.empty();
}
