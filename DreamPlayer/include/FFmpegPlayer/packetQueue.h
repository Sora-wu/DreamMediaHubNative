//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <atomic>
#include <common/concurrentQueue.hpp>
#include <common/ffcore.hpp>

struct Packet {
    AVPacket packet{};
    uint32_t serial = 0;
};

class PacketQueue {
public:
    explicit PacketQueue(const AVPacket& flushPkt) : flushPkt_(flushPkt) {}
    PacketQueue(const AVPacket& flushPkt, std::size_t maxSize) : flushPkt_(flushPkt), que_(maxSize) {}

    void push(AVPacket* pkt);
    std::optional<Packet> pop();
    void clear();
    void close();
    void reopen();

    void setSerial(uint32_t serial) {
        serial_ = serial;
    }

    [[nodiscard]] const std::atomic<uint32_t>* getSerial() const {
        return &serial_;
    }

    [[nodiscard]] uint64_t getDuration() {
        std::lock_guard lk(mutex_);
        return duration_;
    }

    [[nodiscard]]  uint32_t getPacketSize() {
        std::lock_guard lk(mutex_);
        return packetSize_;
    }

private:
    const AVPacket& flushPkt_;
    ConcurrentQueue<Packet> que_{};
    std::atomic<uint32_t> serial_ = 0;
    uint64_t duration_ = 0;
    uint32_t packetSize_ = 0;

    mutable std::mutex mutex_{};
};