#ifndef AlarmService_hpp
#define AlarmService_hpp

#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "RTClib.h"
#include "freertos/FreeRTOS.h"

#include "Alarm.hpp"
#include "AudioLooper.hpp"
#include "Tools.hpp"


class AlarmService {
    using AlarmPtr = std::unique_ptr<Alarm>;
    using AlarmMap = std::unordered_map<Alarm::id_t, AlarmPtr>;

public:
    ~AlarmService();
    
    void begin(
        RTC_DS3231 *rtc, Audio *audio, std::shared_ptr<std::mutex> rtcMutex, byte intrPin,
        byte alarmStopPin
    );
    void dumpAlarms();

    /*****************
     * public API,   *
     * takes m_lock  *
     *****************/
    Alarm::id_t addAlarm(Alarm &alarm);                  // also takes m_rtcLock
    bool removeAlarm(Alarm::id_t id);
    bool setAlarmState(Alarm::id_t id, bool enabled);
    bool setAlarmTime(Alarm::id_t id, byte hour, byte minute);
    bool setAlarmDaysOfWeek(Alarm::id_t id, Alarm::DaysOfWeek daysOfWeek);
    void setVolume(byte volume);
    bool clearMissedFlag(Alarm::id_t id);

    bool isAlarmRunning()       const { return m_runningAlarmId != 0; };
    Alarm::id_t runningAlarm()  const { return m_runningAlarmId; }
    const AlarmMap &getAlarms() const { return m_alarms; };

private:
    struct Command {
        enum Type { FireAlarm, StopAlarm };
        Type type;
    };

    // sets 1st enabled alarm on RTC's 2st slot
    void updateAlarms();                                        // takes m_rtcLock
    void setDs3231Alarm(const HwAlarm &alarm);                  // takes m_rtcLock
    void addDs3231Alarm(HwAlarm &alarm, DateTime *now = NULL);  // takes m_rtcLock if now == NULL
    void processAlarm(std::vector<HwAlarm>::iterator idx);      // non-blocking
    std::vector<HwAlarm>::iterator firstEnabledHwAlarm();       // non-blocking

    void eventLoop();          // takes m_lock on each command
    // eventloop commnads:
    void onAlarmFired();       // takes m_rtcLock
    void onAlarmStopped();     // non-blocking

    void alarmMissed();        // non-blocking

    static std::vector<HwAlarm> alarmToHwAlarms(Alarm *alarm);
    friend void IRAM_ATTR onAlarm(void *selfPtr);
    friend void IRAM_ATTR onAlarmStop(void *selfPtr);
    friend void updateDisplayTask(void *pvParameters); // TODO remove
    void _dumpAlarms();  // internal version of dumpAlarms(), does not take m_lock

    /*
     * we need to get alarms by id as fast as possible,
     * so unordered_map is the best solution
     * (with key = Alarm::id_t)
     */
    AlarmMap                     m_alarms;
    std::vector<HwAlarm>         m_hwAlarms;

    AudioLooper                 *m_alarmPlayer;
    Audio                       *m_audio;
    RTC_DS3231                  *m_rtc;
    byte                         m_interruptPin;
    byte                         m_alarmStopPin;
    Alarm::id_t                  m_runningAlarmId;

    TaskHandle_t                 m_eventLoopTask;
    QueueHandle_t                m_isrCmdQueue;

    /*
     * the order in which locks are taken is as following:
     *   1. m_lock
     *   2. m_rtcLock
     */
    std::shared_ptr<std::mutex>  m_rtcLock;
    std::mutex                   m_lock;
};

extern AlarmService MainAlarmService;

#endif  // #ifdef AlarmService_hpp