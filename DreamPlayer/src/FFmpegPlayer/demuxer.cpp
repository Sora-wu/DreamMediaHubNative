//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <FFmpegPlayer/demuxer.h>
#include <FFmpegPlayer/ffcore.h>

#include <mutex>
#include <chrono>

using namespace::std::literals;

Demuxer::Demuxer(MediaClock& mediaClock) : mediaClock_(mediaClock) {
    av_init_packet(&flushPkt_);
    flushPkt_.data = reinterpret_cast<uint8_t*>(&flushPkt_);
}

Demuxer::~Demuxer() {
    if (fmtCtx_) {
        avformat_close_input(&fmtCtx_);
        fmtCtx_ = nullptr;
    }
}

bool Demuxer::open(const char* url) {
    int res = avformat_open_input(&fmtCtx_, url, nullptr, nullptr);
    if (res < 0) {
        std::string errorStr = FFUtils::getError(res);
        av_log(nullptr, AV_LOG_FATAL, "Failed to open input, error code: %d, error message: %s path: %s\n", res, errorStr.c_str(), url);
        return false;
    }

    res = avformat_find_stream_info(fmtCtx_, nullptr);
    if (res < 0) {
        av_log(nullptr, AV_LOG_WARNING, "%s: could not find codec parameters\n", url);
        return false;
    }

    for (uint32_t i = 0; i < fmtCtx_->nb_streams; ++i) {
        streams_.push_back(fmtCtx_->streams[i]);
    }

    // 如果是RTSP流，duration可能为AV_NOPTS_VALUE
    if (fmtCtx_->duration != AV_NOPTS_VALUE) {
        duration_ = fmtCtx_->duration / AV_TIME_BASE;
    }

    isEof_ = false;

    av_dump_format(fmtCtx_, 0, url, 0);

    return true;
}

void Demuxer::startDemux() {
    thread_ = std::jthread([this](std::stop_token st) {
        threadFunc(st);
    });
    FFUtils::setThreadName(thread_, "Demuxer thread");
}

void Demuxer::threadFunc(std::stop_token st) {
    AVPacket pkt{};
    int res = 0;

    while (!st.stop_requested()) {
        {
            std::lock_guard lock(mtx_);
            if (hasSeekRequest_) {
                seek();
                hasSeekRequest_ = false;
                continue;
            }
        }

        res = av_read_frame(fmtCtx_, &pkt);
        if (res < 0) {
            if ((res == AVERROR_EOF || avio_feof(fmtCtx_->pb)) && !isEof_) {
                pushNullPacket();
                isEof_ = true;
            }
            if (fmtCtx_->pb && fmtCtx_->pb->error) {
                break;
            }

            std::unique_lock lock(mtx_);
            cvRead_.wait_for(lock, 10ms);
            continue;
        }
        else {
            isEof_ = false;
            handle(&pkt);
        }

        av_packet_unref(&pkt);
    }

    av_log(nullptr, AV_LOG_INFO, "Demuxer exit\n");
}

void Demuxer::stopDemux() {
    thread_.request_stop();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Demuxer::seekRequest(double targetPos, double seekRel) {
    std::lock_guard lock(mtx_);

    seekTargetPos_ = targetPos;
    seekRel_ = seekRel;
    hasSeekRequest_ = true;
}

void Demuxer::seek() {
    int64_t pts = (int64_t)(seekTargetPos_ * AV_TIME_BASE);
    int64_t offsetPts = (int64_t)(seekRel_ * AV_TIME_BASE);
    if (fmtCtx_->start_time != AV_NOPTS_VALUE) {
        pts += fmtCtx_->start_time;
    }

    const int64_t seekMin = seekRel_ > 0 ? pts - offsetPts + 2 : INT64_MIN;
    const int64_t seekMax = seekRel_ < 0 ? pts - offsetPts - 2 : INT64_MAX;
    int res = avformat_seek_file(fmtCtx_, -1, seekMin, pts, seekMax, 0);
    if (res < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Seek failed\n");
    }
    else {
        // seek之后需要flush
        for (auto stream : streams_) {
            flushPkt_.stream_index = stream->index;
            handle(&flushPkt_);
        }

        av_log(nullptr, AV_LOG_INFO, "demuxer seek to %f\n", seekTargetPos_);
        mediaClock_.resumeClock();
        // seek成功更新时钟
        mediaClock_.setAudioClock(seekTargetPos_, CURRENT_TIME);
        mediaClock_.setVideoClock(seekTargetPos_, CURRENT_TIME);

        isEof_ = false;
    }
}

void Demuxer::pushNullPacket() {
    AVPacket pkt{};
    av_init_packet(&pkt);
    pkt.data = nullptr;
    pkt.size = 0;
    pkt.duration = 0;

    for (auto steam : streams_) {
        pkt.stream_index = steam->index;
        handle(&pkt);
    }
}
