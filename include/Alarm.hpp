#include <string>

#include "Arduino.h"
#include "RTClib.h"

#include "Tools.hpp"


class Alarm {
public:
    using id_t = uint64_t;
    friend class AlarmService;

    class DaysOfWeek {
    public:
        DaysOfWeek(uint8_t mask);

        bool isSet(byte day) const;
        void set(byte day, bool value);

        static const uint8_t everyDay = 0b01111111;
        static const uint8_t noDays   = 0b00000000; 
        // noDays means that alarm is non-repeating

        /**
         * days of week are stored in a bit mask, where bits
         * from 0 to 6 correspond to days from Monday to Sunday
         */
        uint8_t daysMask;
    };

    Alarm(byte hour, byte minute, DaysOfWeek daysOfWeek, bool enabled = false);
    std::string toString() const;

    id_t   id()             const { return m_id; }
    bool   isOneshot()      const { return daysOfWeek.daysMask == DaysOfWeek::noDays; }
    bool   isEveryDay()     const { return daysOfWeek.daysMask == DaysOfWeek::everyDay; }
    bool   usesDaysOfWeek() const { return !(isOneshot() || isEveryDay()); }
    bool   isMissed()       const { return m_missed; }
    void   clearMissedFlag()      { m_missed = false; }

    bool       enabled;
    byte       hour;
    byte       minute;
    DaysOfWeek daysOfWeek;

private:
    bool m_missed = true;
    id_t m_id;
};


/**
 * Class that stores an alarm as is's represented in DS3231.
 * It can be bound to the exact day of week or fire every day.
 */
class HwAlarm {
public:
    HwAlarm(Alarm *parent, byte dayOfWeek);
    HwAlarm(Alarm *parent);

    Alarm &parentAlarm() const { return *m_parentAlarm; };
    byte  dayOfWeek()    const { return m_dayOfWeek; };

    std::string toString() const;
    DateTime nextFiring(const DateTime &now) const;
    bool     hasFired(const DateTime &when)  const;

private:
    Alarm *m_parentAlarm;
    byte   m_dayOfWeek;  // Stored from 0(Monday) to 6(Sunday)
};