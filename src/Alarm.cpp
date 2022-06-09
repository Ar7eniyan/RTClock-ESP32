#include "Alarm.hpp"


/***************
 * class Alarm *
 ***************/

Alarm::Alarm(byte hour, byte minute, DaysOfWeek daysOfWeek, bool enabled) :
enabled(enabled), hour(hour), minute(minute), daysOfWeek(daysOfWeek)
{
    // unique alarm id:
    // | 48 low bits of esp32 uptime in us | 16 high bits of random() |
    m_id = (esp_timer_get_time() << 16) + (rand() >> 16);
    log_d("Created Alarm (%s)", CSTR(toString()));
}

std::string Alarm::toString() const
{
    int len = snprintf(
        nullptr, 0, "id=0x%016llx, time=%02d:%02d, enabled=%s", m_id, hour, minute,
        enabled ? "true" : "false"
    );
    char buf[len];
    sprintf(
        buf, "id=0x%016llx, time=%02d:%02d, enabled=%s", m_id, hour, minute,
        enabled ? "true" : "false"
    );

    return std::string(buf);
}


/***************************
 * class Alarm::DaysOfWeek *
 ***************************/

Alarm::DaysOfWeek::DaysOfWeek(uint8_t mask) : daysMask(mask) {};

/* This function takes day from 0 to 6 */
void Alarm::DaysOfWeek::set(byte day, bool value)
{
    if (value)
        daysMask |= 1 << day;
    else
        daysMask &= ~(1 << day);
}

/* This function takes day from 0 to 6 */
bool Alarm::DaysOfWeek::isSet(byte day) const
{
    return daysMask & (1 << day);
}


/*****************
 * class HwAlarm *
 *****************/

HwAlarm::HwAlarm(Alarm *parent) : m_parentAlarm(parent), m_dayOfWeek(0)
{
    log_d("Created HwAlarm (%s)", CSTR(toString()));
}

HwAlarm::HwAlarm(Alarm *parent, byte dayOfWeek) :
m_parentAlarm(parent), m_dayOfWeek(dayOfWeek)
{
    log_d("Created HwAlarm (%s)", CSTR(toString()));
}

std::string HwAlarm::toString() const
{
    int len = snprintf(
        nullptr, 0, "parent=0x%016llx, time=%02d:%02d, addr=0x%08x",
        parentAlarm().id(), parentAlarm().hour, parentAlarm().minute, (uint32_t)this
    );
    char buf[len];
    sprintf(
        buf, "parent=0x%016llx, time=%02d:%02d, addr=0x%08x", parentAlarm().id(),
        parentAlarm().hour, parentAlarm().minute, (uint32_t)this
    );

    /* if the instance is bound to a specific day of week */
    if (parentAlarm().usesDaysOfWeek()) {
        int dowLen = snprintf(nullptr, 0, ", dow=%d", dayOfWeek());
        char dowBuf[len + dowLen];
        sprintf(dowBuf, ", dow=%d", dayOfWeek());
        return std::string(strcat(buf, dowBuf));
    };

    return std::string(buf);
}

DateTime HwAlarm::nextFiring(const DateTime &now) const
{
    int8_t hour = parentAlarm().hour;
    int8_t minute = parentAlarm().minute;
    int8_t nowDayOfWeek = CONVERT_DOWS(now.dayOfTheWeek());
    DateTime ret(now.year(), now.month(), now.day(), hour, minute);

    bool firedToday =
        hour != now.hour() ? hour < now.hour() : minute <= now.minute();

    if (parentAlarm().usesDaysOfWeek()) {
        bool firedThisWeek = dayOfWeek() != nowDayOfWeek
                                 ? dayOfWeek() < nowDayOfWeek
                                 : firedToday;
        ret = ret + TimeSpan(dayOfWeek() - nowDayOfWeek, 0, 0, 0);

        /* if alarm has already fired this week, return the same day on the next week */
        return DateTime(firedThisWeek ? ret + TimeSpan(7, 0, 0, 0) : ret);
    } else {
        /* if the alarm has already fired today, return the same time, but a day later */
        return DateTime(firedToday ? ret + TimeSpan(1, 0, 0, 0) : ret);
    }
}

bool HwAlarm::hasFired(const DateTime &when) const
{
    int8_t hour = parentAlarm().hour;
    int8_t minute = parentAlarm().minute;
    int8_t whenDayOfWeek = CONVERT_DOWS(when.dayOfTheWeek());

    bool firedThatDay =
        hour != when.hour() ? hour < when.hour() : minute <= when.minute();

    if (parentAlarm().usesDaysOfWeek()) {
        bool firedThatWeek = dayOfWeek() != whenDayOfWeek
                                 ? dayOfWeek() < whenDayOfWeek
                                 : firedThatDay;

        return firedThatWeek;
    } else {
        return firedThatDay;
    }
}