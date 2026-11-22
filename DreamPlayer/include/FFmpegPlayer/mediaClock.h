//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <DreamPlayer/structs.h>

#include <cstdint>
#include <cmath>
#include <atomic>
#include <mutex>

class MediaClock {
    struct Clock {
        void init(const std::atomic<uint32_t>* qSerial);
        double getTime() const;
        void setTime(double pts, uint32_t serial);
        void setTime(double pts, uint32_t serial, double time);
        void setTime(double pts, double time);
        uint32_t getSerial() const {
            return serial;
        }

        double pts = NAN;          // 单位：秒
        double lastUpdatedTime = NAN;
        double speed = 1.0;
        bool paused = true;
        uint32_t serial = 0;
        const std::atomic<uint32_t>* queSerial = nullptr;
    };

public:
    Dream::SyncType getMasterMode() const;
    void setMasterMode(Dream::SyncType mode);

    void initClock(const std::atomic<uint32_t>* videoPktQueSerial, const std::atomic<uint32_t>* audioPktQueSerial);
    [[nodiscard]] double getMasterClock() const;
    [[nodiscard]] double getVideoClock() const;
    [[nodiscard]] double getAudioClock() const;
    [[nodiscard]] uint32_t getVideoSerial() const;
    [[nodiscard]] uint32_t getAudioSerial() const;
    [[nodiscard]] double getVideoPTS() const;
    [[nodiscard]] double getAudioPTS() const;
    void setVideoClock(double pts, uint32_t serial, double time);
    void setAudioClock(double pts, uint32_t serial, double time);
    void setMasterClock(double pts, uint32_t serial);
    void setVideoClock(double pts, uint32_t serial);
    void setAudioClock(double pts, uint32_t serial);
    void setMasterClock(double pts, double time);
    void setVideoClock(double pts, double time);
    void setAudioClock(double pts, double time);

    float getSpeed() const;
    void setSpeed(float speed);

    void pauseMasterClock();
    void pauseVideoClock();
    void pauseAudioClock();
    bool getVideoClockPause() const;
    bool getAudioClockPause() const;

    void resumeClock();

private:
    Clock videoClock_;
    bool videoClockAvailable_ = false;

    Clock audioClock_;
    bool audioClockAvailable_ = false;

    mutable std::mutex mutex_;
    Dream::SyncType syncType_ = Dream::SyncType::AudioMaster;
};