#pragma once

// =============================================================================
// tcxMidiTimecode.h - MIDI Time Code (MTC) decoder (counterpart to ofxMidiTimecode)
// =============================================================================
// Feed every incoming message to update(); it assembles SMPTE timecode from
// either quarter-frame messages (0xF1, 8 of them = 2 frames) or a full-frame
// SysEx. getFrame() returns the latest hh:mm:ss:ff.
//
//   MidiTimecode mtc;
//   midiIn.onMessage.listen([&](MidiMessage& m){
//       if (mtc.update(m)) { auto f = mtc.getFrame(); ... }
//   });
//
// Ported from danomatika/ofxMidi (BSD).
// =============================================================================

#include "tcxMidiMessage.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace tcx {

class MidiTimecode;  // fwd for frame helpers

// One SMPTE timecode frame.
struct MidiTimecodeFrame {
    int hours = 0;    // 0-23
    int minutes = 0;  // 0-59
    int seconds = 0;  // 0-59
    int frames = 0;   // 0-29 (depending on rate)
    unsigned char rate = 0x0;  // 0x0:24, 0x1:25, 0x2:29.97, 0x3:30

    double getFps() const;             // defined after MidiTimecode
    std::string toString() const;
    double toSeconds() const;
    void fromSeconds(double s);
    void fromSeconds(double s, unsigned char r);
};

class MidiTimecode {
public:
    enum Framerate : unsigned char {
        FRAMERATE_24 = 0x0,
        FRAMERATE_25 = 0x1,
        FRAMERATE_30_DROP = 0x2,  // 29.97
        FRAMERATE_30 = 0x3,
    };

    MidiTimecode() = default;

    // Returns true when a complete frame has been decoded.
    bool update(const MidiMessage& m) {
        if (m.bytes.empty()) return false;
        unsigned char status = m.bytes[0];
        if (status == 0xF1) return decodeQuarterFrame(m.bytes);
        if (status == 0xF0) return decodeFullFrame(m.bytes);
        return false;
    }

    void reset() {
        frame = MidiTimecodeFrame();
        quarterFrame = QuarterFrame();
    }

    MidiTimecodeFrame getFrame() const { return frame; }

    // -------------------------------------------------------------------------
    // Rate helpers
    // -------------------------------------------------------------------------
    static int framesToMs(int frames, unsigned char rate) {
        return (int)(rateToMultiplier(rate) * (double)frames * 1000.0);
    }
    static int msToFrames(int ms, unsigned char rate) {
        return (int)((double)ms / rateToMultiplier(rate) / 1000.0);
    }
    static double rateToFps(unsigned char rate) {
        switch (rate) {
            default:
            case FRAMERATE_24:      return 24.0;
            case FRAMERATE_25:      return 25.0;
            case FRAMERATE_30_DROP: return 29.97;
            case FRAMERATE_30:      return 30.0;
        }
    }
    static unsigned char fpsToRate(double fps) {
        if (fps < 25) return FRAMERATE_24;
        if (fps < 29) return FRAMERATE_25;
        if (fps < 30) return FRAMERATE_30_DROP;
        return FRAMERATE_30;
    }
    static double rateToMultiplier(unsigned char rate) {
        switch (rate) {
            case FRAMERATE_24:      return 0.04166666667;  // 1/24
            case FRAMERATE_25:      return 0.04000000000;  // 1/25
            case FRAMERATE_30_DROP: return 0.03336670003;  // 1/29.97
            case FRAMERATE_30:
            default:                return 0.03333333333;  // 1/30
        }
    }

private:
    static constexpr int FULLFRAME_LEN = 10;
    static constexpr int QUARTERFRAME_LEN = 8;

    struct QuarterFrame {
        enum Direction { UNKNOWN, FORWARDS, BACKWARDS };
        Direction direction = UNKNOWN;
        MidiTimecodeFrame frame;
        unsigned int count = 0;
        bool receivedFirst = false;
        bool receivedLast = false;
        unsigned int lastDataByte = 0x00;
    };

    bool decodeQuarterFrame(const std::vector<unsigned char>& message) {
        bool complete = false;
        unsigned char dataByte = message[1];
        unsigned char msgType = dataByte & 0xF0;

        if (quarterFrame.direction == QuarterFrame::UNKNOWN && quarterFrame.count > 1) {
            unsigned char lastMsgType = quarterFrame.lastDataByte & 0xF0;
            if (lastMsgType < msgType)      quarterFrame.direction = QuarterFrame::FORWARDS;
            else if (lastMsgType > msgType) quarterFrame.direction = QuarterFrame::BACKWARDS;
        }
        quarterFrame.lastDataByte = dataByte;

        switch (msgType) {
            case 0x00:  // frame LSB
                quarterFrame.frame.frames = (int)(dataByte & 0x0F);
                quarterFrame.count += 1;
                quarterFrame.receivedFirst = true;
                if (quarterFrame.count >= QUARTERFRAME_LEN &&
                    quarterFrame.direction == QuarterFrame::BACKWARDS &&
                    quarterFrame.receivedLast) {
                    complete = true;
                }
                break;
            case 0x10:  // frame MSB
                quarterFrame.frame.frames |= (int)((dataByte & 0x01) << 4);
                quarterFrame.count += 1;
                break;
            case 0x20:  // second LSB
                quarterFrame.frame.seconds = (int)(dataByte & 0x0F);
                quarterFrame.count += 1;
                break;
            case 0x30:  // second MSB
                quarterFrame.frame.seconds |= (int)((dataByte & 0x03) << 4);
                quarterFrame.count += 1;
                break;
            case 0x40:  // minute LSB
                quarterFrame.frame.minutes = (int)(dataByte & 0x0F);
                quarterFrame.count += 1;
                break;
            case 0x50:  // minute MSB
                quarterFrame.frame.minutes |= (int)((dataByte & 0x03) << 4);
                quarterFrame.count += 1;
                break;
            case 0x60:  // hours LSB
                quarterFrame.frame.hours = (int)(dataByte & 0x0F);
                quarterFrame.count += 1;
                break;
            case 0x70:  // hours MSB & framerate
                quarterFrame.frame.hours |= (int)((dataByte & 0x01) << 4);
                quarterFrame.frame.rate = (dataByte & 0x06) >> 1;
                quarterFrame.count += 1;
                quarterFrame.receivedLast = true;
                if (quarterFrame.count >= QUARTERFRAME_LEN &&
                    quarterFrame.direction == QuarterFrame::FORWARDS &&
                    quarterFrame.receivedFirst) {
                    complete = true;
                }
                break;
            default:
                return false;
        }

        if (complete) {
            // add 2 frames to compensate for the time it takes to receive 8 QFs
            quarterFrame.frame.frames += 2;
            frame = quarterFrame.frame;
            quarterFrame = QuarterFrame();
            return true;
        }
        return false;
    }

    bool decodeFullFrame(const std::vector<unsigned char>& message) {
        if ((int)message.size() == FULLFRAME_LEN && isFullFrame(message)) {
            frame.hours = (int)(message[5] & 0x1F);
            frame.rate = (int)((message[5] & 0x60) >> 5);
            frame.minutes = (int)(message[6]);
            frame.seconds = (int)(message[7]);
            frame.frames = (int)(message[8]);
            return true;
        }
        return false;
    }

    static bool isFullFrame(const std::vector<unsigned char>& message) {
        return (message[1] == 0x7F) &&  // universal message
               (message[2] == 0x7F) &&  // global broadcast
               (message[3] == 0x01) &&  // time code
               (message[4] == 0x01) &&  // full frame
               (message[9] == 0xF7);    // end of sysex
    }

    MidiTimecodeFrame frame;
    QuarterFrame quarterFrame;
};

// ---------------------------------------------------------------------------
// MidiTimecodeFrame methods (need MidiTimecode's static helpers)
// ---------------------------------------------------------------------------
inline double MidiTimecodeFrame::getFps() const {
    return MidiTimecode::rateToFps(rate);
}

inline std::string MidiTimecodeFrame::toString() const {
    std::stringstream s;
    s << std::setw(2) << std::setfill('0') << hours << ":"
      << std::setw(2) << std::setfill('0') << minutes << ":"
      << std::setw(2) << std::setfill('0') << seconds << ":"
      << std::setw(2) << std::setfill('0') << frames;
    return s.str();
}

inline double MidiTimecodeFrame::toSeconds() const {
    double t = (double)hours * 3600.0;
    t += (double)minutes * 60.0;
    t += (double)seconds;
    t += (double)MidiTimecode::framesToMs(frames, rate) / 1000.0;
    return t;
}

inline void MidiTimecodeFrame::fromSeconds(double s) { fromSeconds(s, 0x0); }

inline void MidiTimecodeFrame::fromSeconds(double s, unsigned char r) {
    seconds = (int)s % 60;
    minutes = (int)((s - seconds) * 0.016666667) % 60;
    hours = (int)(s * 0.00027777778) % 60;
    double ms = (int)(std::floor((s - std::floor(s)) * 1000.0) + 0.5);
    frames = MidiTimecode::msToFrames((int)ms, r);
    rate = r;
}

}  // namespace tcx
