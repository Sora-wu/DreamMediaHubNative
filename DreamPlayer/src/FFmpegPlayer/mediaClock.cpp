//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <FFmpegPlayer/mediaClock.h>
#include <FFmpegPlayer/ffinstance.h>

using namespace Dream;

///////////////////////////Clock///////////////////////////////////////////
void MediaClock::Clock::init(uint32_t* qSerial) {
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
void MediaClock::initClock(uint32_t* videoPktQueSerial, uint32_t* audioPktQueSerial) {
    videoClockAvailable_ = videoPktQueSerial ? true : false;
    audioClockAvailable_ = audioPktQueSerial ? true : false;

    videoClock_.init(videoPktQueSerial);
    audioClock_.init(audioPktQueSerial);
}

double MediaClock::getVideoClock() const {
    return videoClockAvailable_ ? videoClock_.getTime() : 0.0;
}

double MediaClock::getAudioClock() const {
    return audioClockAvailable_ ? audioClock_.getTime() : 0.0;
}

uint32_t MediaClock::getVideoSerial() const {
    return videoClock_.serial;
}

uint32_t MediaClock::getAudioSerial() const {
    return audioClock_.serial;
}

double MediaClock::getVideoPTS() const {
    if (videoClockAvailable_) {
        return videoClock_.pts;
    }

    return NAN;
}

double MediaClock::getAudioPTS() const {
    if (audioClockAvailable_) {
        return audioClock_.pts;
    }

    return NAN;
}

void MediaClock::setVideoClock(double pts, uint32_t serial, double time) {
    if (videoClockAvailable_) {
        videoClock_.setTime(pts, serial, time);
    }
}

void MediaClock::setAudioClock(double pts, uint32_t serial, double time) {
    if (audioClockAvailable_) {
        audioClock_.setTime(pts, serial, time);
    }
}

void MediaClock::setVideoClock(double pts, uint32_t serial) {
    if (videoClockAvailable_) {
        videoClock_.setTime(pts, serial);
    }
}

void MediaClock::setAudioClock(double pts, uint32_t serial) {
    if (audioClockAvailable_) {
        audioClock_.setTime(pts, serial);
    }
}

void MediaClock::setVideoClock(double pts, double time) {
    if (videoClockAvailable_) {
        videoClock_.setTime(pts, time);
    }
}

void MediaClock::setAudioClock(double pts, double time) {
    if (audioClockAvailable_) {
        audioClock_.setTime(pts, time);
    }
}

float MediaClock::getSpeed() const {
    float speed = audioClockAvailable_ ? static_cast<float>(audioClock_.speed) : 1.0;
    if (FFINSTANCE()->getSyncType() == SyncType::VideoMaster) {
        speed = videoClockAvailable_ ? static_cast<float>(videoClock_.speed) : 1.0;
    }
    return speed;
}

void MediaClock::setSpeed(float speed) {
    if (videoClockAvailable_) {
        videoClock_.speed = speed;
    }

    if (audioClockAvailable_) {
        audioClock_.speed = speed;
    }
}

void MediaClock::pauseVideoClock() {
    if (videoClockAvailable_) {
        videoClock_.paused = true;
    }
}

void MediaClock::pauseAudioClock() {
    if (audioClockAvailable_) {
        audioClock_.paused = true;
    }
}

bool MediaClock::getVideoClockPause() const {
    return videoClockAvailable_ ? videoClock_.paused : true;
}

bool MediaClock::getAudioClockPause() const {
    return audioClockAvailable_ ? audioClock_.paused : true;
}

void MediaClock::resumeClock() {
    if (videoClockAvailable_) {
        videoClock_.paused = false;
    }

    if (audioClockAvailable_) {
        audioClock_.paused = false;
    }
}
