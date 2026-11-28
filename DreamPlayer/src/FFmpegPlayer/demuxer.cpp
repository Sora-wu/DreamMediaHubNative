//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <FFmpegPlayer/demuxer.h>
#include <common/ffcore.hpp>

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
    if (!url || fmtCtx_ || taskQueue_.isClosed()) {
        return false;
    }
    fmtCtx_ = avformat_alloc_context();
    if (!fmtCtx_) {
        return false;
    }
    fmtCtx_->interrupt_callback = {[](void* opaque) -> int {
        auto& self = *static_cast<Demuxer*>(opaque);
        return self.taskQueue_.isClosed() || (self.interruptOnTask_ && !self.taskQueue_.empty());
    }, this};

    int res = avformat_open_input(&fmtCtx_, url, nullptr, nullptr);
    if (taskQueue_.isClosed()) {
        return false;
    }
    if (res < 0) {
        std::string errorStr = FFUtils::getError(res);
        av_log(nullptr, AV_LOG_FATAL, "Failed to open input, error code: %d, error message: %s path: %s\n", res, errorStr.c_str(), url);
        return false;
    }

    res = avformat_find_stream_info(fmtCtx_, nullptr);
    if (res < 0 || taskQueue_.isClosed()) {
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
    interruptOnTask_ = !fmtCtx_->pb || (fmtCtx_->pb->seekable & AVIO_SEEKABLE_NORMAL);
    thread_ = std::jthread([this](std::stop_token st) {
        threadFunc(st);
    });
    FFUtils::setThreadName(thread_, "Demuxer thread");
}

void Demuxer::threadFunc(std::stop_token st) {
    AVPacket pkt{};
    int res = 0;

    while (!st.stop_requested()) {
        while (auto task = taskQueue_.try_pop()) {
            if (st.stop_requested()) {
                return;
            }
            (*task)(*this);
        }

        res = av_read_frame(fmtCtx_, &pkt);
        if (res < 0) {
            // avio_feof 也可能补读。中断后先处理停止或排队的任务。
            const bool eof = res == AVERROR_EOF || (!isEof_ && avio_feof(fmtCtx_->pb));
            if (st.stop_requested() || !taskQueue_.empty()) {
                continue;
            }
            if (eof && !isEof_) {
                pushNullPacket();
                isEof_ = true;
                if (onDemuxFinishedCallback_) {
                    onDemuxFinishedCallback_();
                }
            }
            if (fmtCtx_->pb && fmtCtx_->pb->error) {
                break;
            }

            std::this_thread::sleep_for(10ms);
            continue;
        }

        handle(&pkt);
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

void Demuxer::seek(double targetPos, double seekRel) {
    const auto revision = ++seekRevision_;
    taskQueue_.try_push([this, targetPos, seekRel, revision](Demuxer&) {
        if (revision != seekRevision_) {
            return;
        }
        // 被新任务打断的读操作会在 AVIO 中留下错误，seek 前清掉本次中断。
        if (fmtCtx_->pb && fmtCtx_->pb->error == AVERROR_EXIT) {
            fmtCtx_->pb->error = 0;
            fmtCtx_->pb->eof_reached = 0;
        }

        int64_t pts = static_cast<int64_t>(targetPos * AV_TIME_BASE);
        int64_t offsetPts = static_cast<int64_t>(seekRel * AV_TIME_BASE);
        if (fmtCtx_->start_time != AV_NOPTS_VALUE) {
            pts += fmtCtx_->start_time;
        }

        const int64_t seekMin = seekRel > 0 ? pts - offsetPts + 2 : INT64_MIN;
        const int64_t seekMax = seekRel < 0 ? pts - offsetPts - 2 : INT64_MAX;
        int res = avformat_seek_file(fmtCtx_, -1, seekMin, pts, seekMax, 0);
        if (res < 0) {
            av_log(nullptr, AV_LOG_ERROR, "Seek failed\n");
        }
        else {
            isEof_ = false;
            // seek之后需要flush
            for (auto stream : streams_) {
                flushPkt_.stream_index = stream->index;
                handle(&flushPkt_);
            }

            av_log(nullptr, AV_LOG_INFO, "demuxer seek to %f\n", targetPos);
            mediaClock_.resumeClock();
            // seek成功更新时钟
            mediaClock_.setAudioClock(targetPos, CURRENT_TIME);
            mediaClock_.setVideoClock(targetPos, CURRENT_TIME);
        }
    });
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
