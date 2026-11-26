//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <common/ffcore.hpp>
#include <FFmpegPlayer/mediaClock.h>

using namespace Dream;

///////////////////////////Clock///////////////////////////////////////////
void MediaClock::Clock::init(const std::atomic<uint32_t>* qSerial) {
    pts = NAN;
    lastUpdatedTime = CURRENT_TIME;
    speed = 1.0;
    paused = false;
    serial = 0;
    queSerial = qSerial;
}

double MediaClock::Clock::getTime() const {
    if (!queSerial || *queSerial != serial) {
        return NAN;
    }

    if (paused) {
        return pts;
    }

    double now = CURRENT_TIME;
    return pts + (now - lastUpdatedTime) * speed;
}

void MediaClock::Clock::setTime(double pts, uint32_t serial) {
    double time = CURRENT_TIME;
    setTime(pts, serial, time);
}

void MediaClock::Clock::setTime(double pts, uint32_t serial, double time) {
    this->pts = pts;
    this->lastUpdatedTime = time;
    this->serial = serial;
}

void MediaClock::Clock::setTime(double pts, double time) {
    this->pts = pts;
    this->lastUpdatedTime = time;
}

//////////////////////////MediaClock///////////////////////////////////////
SyncType MediaClock::getMasterMode() const {
    std::lock_guard lock(mutex_);
    return syncType_;
}

void MediaClock::setMasterMode(SyncType mode) {
    std::lock_guard lock(mutex_);
    syncType_ = mode;
}

void MediaClock::initClock(const std::atomic<uint32_t>* videoPktQueSerial, const std::atomic<uint32_t>* audioPktQueSerial) {
    std::lock_guard lock(mutex_);
    videoClockAvailable_ = videoPktQueSerial ? true : false;
    audioClockAvailable_ = audioPktQueSerial ? true : false;

    videoClock_.init(videoPktQueSerial);
    audioClock_.init(audioPktQueSerial);
}

double MediaClock::getMasterClock() const {
    std::lock_guard lock(mutex_);
    if (syncType_ == SyncType::VideoMaster) {
        return videoClockAvailable_ ? videoClock_.getTime() : 0.0;
    }

    return audioClockAvailable_ ? audioClock_.getTime() : 0.0;
}

double MediaClock::getVideoClock() const {
    std::lock_guard lock(mutex_);
    return videoClockAvailable_ ? videoClock_.getTime() : 0.0;
}

double MediaClock::getAudioClock() const {
    std::lock_guard lock(mutex_);
    return audioClockAvailable_ ? audioClock_.getTime() : 0.0;
}

uint32_t MediaClock::getVideoSerial() const {
    std::lock_guard lock(mutex_);
    return videoClock_.serial;
}

uint32_t MediaClock::getAudioSerial() const {
    std::lock_guard lock(mutex_);
    return audioClock_.serial;
}

double MediaClock::getVideoPTS() const {
    std::lock_guard lock(mutex_);
    if (videoClockAvailable_) {
        return videoClock_.pts;
    }

    return NAN;
}

double MediaClock::getAudioPTS() const {
    std::lock_guard lock(mutex_);
    if (audioClockAvailable_) {
        return audioClock_.pts;
    }

    return NAN;
}

void MediaClock::setVideoClock(double pts, uint32_t serial, double time) {
    std::lock_guard lock(mutex_);
    if (videoClockAvailable_) {
        videoClock_.setTime(pts, serial, time);
    }
}

void MediaClock::setAudioClock(double pts, uint32_t serial, double time) {
    std::lock_guard lock(mutex_);
    if (audioClockAvailable_) {
        audioClock_.setTime(pts, serial, time);
    }
}

void MediaClock::setMasterClock(double pts, uint32_t serial) {
    std::lock_guard lock(mutex_);
    if (syncType_ == SyncType::VideoMaster && videoClockAvailable_) {
        videoClock_.setTime(pts, serial);
    }
    if (audioClockAvailable_) {
        audioClock_.setTime(pts, serial);
    }
}

void MediaClock::setVideoClock(double pts, uint32_t serial) {
    std::lock_guard lock(mutex_);
    if (videoClockAvailable_) {
        videoClock_.setTime(pts, serial);
    }
}

void MediaClock::setAudioClock(double pts, uint32_t serial) {
    std::lock_guard lock(mutex_);
    if (audioClockAvailable_) {
        audioClock_.setTime(pts, serial);
    }
}

void MediaClock::setMasterClock(double pts, double time) {
    std::lock_guard lock(mutex_);
    if (syncType_ == SyncType::VideoMaster && videoClockAvailable_) {
        videoClock_.setTime(pts, time);
    }
    if (audioClockAvailable_) {
        audioClock_.setTime(pts, time);
    }
}

void MediaClock::setVideoClock(double pts, double time) {
    std::lock_guard lock(mutex_);
    if (videoClockAvailable_) {
        videoClock_.setTime(pts, time);
    }
}

void MediaClock::setAudioClock(double pts, double time) {
    std::lock_guard lock(mutex_);
    if (audioClockAvailable_) {
        audioClock_.setTime(pts, time);
    }
}

float MediaClock::getSpeed() const {
    std::lock_guard lock(mutex_);
    float speed = audioClockAvailable_ ? static_cast<float>(audioClock_.speed) : 1.0;
    if (syncType_ == SyncType::VideoMaster) {
        speed = videoClockAvailable_ ? static_cast<float>(videoClock_.speed) : 1.0;
    }
    return speed;
}

void MediaClock::setSpeed(float speed) {
    std::lock_guard lock(mutex_);
    if (videoClockAvailable_) {
        videoClock_.speed = speed;
    }

    if (audioClockAvailable_) {
        audioClock_.speed = speed;
    }
}

void MediaClock::pauseMasterClock() {
    std::lock_guard lock(mutex_);
    if (syncType_ == SyncType::VideoMaster && videoClockAvailable_) {
        videoClock_.paused = true;
    }
    if (audioClockAvailable_) {
        audioClock_.paused = true;
    }
}

void MediaClock::pauseVideoClock() {
    std::lock_guard lock(mutex_);
    if (videoClockAvailable_) {
        videoClock_.paused = true;
    }
}

void MediaClock::pauseAudioClock() {
    std::lock_guard lock(mutex_);
    if (audioClockAvailable_) {
        audioClock_.paused = true;
    }
}

bool MediaClock::getVideoClockPause() const {
    std::lock_guard lock(mutex_);
    return videoClockAvailable_ ? videoClock_.paused : true;
}

bool MediaClock::getAudioClockPause() const {
    std::lock_guard lock(mutex_);
    return audioClockAvailable_ ? audioClock_.paused : true;
}

void MediaClock::resumeClock() {
    std::lock_guard lock(mutex_);
    if (videoClockAvailable_) {
        videoClock_.paused = false;
    }

    if (audioClockAvailable_) {
        audioClock_.paused = false;
    }
}
