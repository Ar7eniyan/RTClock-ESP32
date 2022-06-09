#include "AlarmService.hpp"


void IRAM_ATTR onAlarm(void *selfPtr)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    AlarmService *self = (AlarmService *)selfPtr;
    AlarmService::Command cmd {AlarmService::Command::FireAlarm};

    xQueueSendFromISR(self->m_isrCmdQueue, &cmd, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void IRAM_ATTR onAlarmStop(void *selfPtr)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    AlarmService *self = (AlarmService *)selfPtr;
    AlarmService::Command cmd {AlarmService::Command::StopAlarm};

    xQueueSendFromISR(self->m_isrCmdQueue, &cmd, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void AlarmService::begin(
    RTC_DS3231 *rtc, Audio *audio, std::shared_ptr<std::mutex> rtcMutex,
    byte intrPin, byte alarmStopPin
)
{
    m_rtcLock = rtcMutex;  // must be initialized
    m_audio = audio;       // must be initialized too
    m_interruptPin = intrPin;
    m_alarmStopPin = alarmStopPin;
    m_rtc = rtc;
    m_alarmPlayer = new AudioLooper(m_audio, std::string("/test.mp3"));
    m_alarmPlayer->begin(std::bind(&AlarmService::alarmMissed, this));
    m_isrCmdQueue = xQueueCreate(3, sizeof(Command));

    xTaskCreate(
        methodToTaskFun<AlarmService, &AlarmService::eventLoop>(), "eventLoop",
        4096, this, TASK_HIGH_PRIORITY, &m_eventLoopTask
    );

    {
        std::lock_guard lock(*m_rtcLock);
        m_rtc->clearAlarm(2);
        m_rtc->disableAlarm(1);
    }

    pinMode(intrPin, INPUT_PULLUP);
    attachInterruptArg(digitalPinToInterrupt(intrPin), onAlarm, this, FALLING);
    pinMode(alarmStopPin, INPUT);
    attachInterruptArg(
        digitalPinToInterrupt(alarmStopPin), onAlarmStop, this, RISING
    );
    log_i("Started AlarmService");
}

AlarmService::~AlarmService()
{
    detachInterrupt(m_alarmStopPin);
    detachInterrupt(m_interruptPin);
}

void AlarmService::dumpAlarms()
{
    std::lock_guard lock(m_lock);
    _dumpAlarms();
}

void AlarmService::_dumpAlarms()
{
    log_w("Dump of m_alarms: ");
    for (auto &alarm : m_alarms) {
        log_w("\t(%s)", CSTR(alarm.second->toString()));
    }

    log_w("Dump of m_hwAlarms: ");
    for (auto &hwAlarm : m_hwAlarms) {
        log_w(
            "\t[%s](%s)", hwAlarm.parentAlarm().enabled ? "X" : " ",
            hwAlarm.toString().c_str()
        );
    }
}

Alarm::id_t AlarmService::addAlarm(Alarm &alarm)
{
    DateTime now;
    Alarm *inserted;
    std::lock_guard lock(m_lock);

    // clang-format off
    log_d("Adding alarm (%s)", CSTR(alarm.toString()));
    inserted = m_alarms.insert({alarm.id(), std::make_unique<Alarm>(alarm)})
        .first        // iterator
        .operator*()  // inserted key-value pair
        .second       // unique_ptr value
        .get();       // raw pointer

    std::vector<HwAlarm> newHwAlarms = alarmToHwAlarms(inserted);

    {
        std::lock_guard rtcLock(*m_rtcLock);
        now = m_rtc->now();
    }

    for (auto &newAlarm : newHwAlarms)
        addDs3231Alarm(newAlarm, &now);
    updateAlarms();
    return inserted->id();
    // clang-format on
}

void AlarmService::processAlarm(std::vector<HwAlarm>::iterator it)
{
    HwAlarm &alarm = *it;
    if (alarm.parentAlarm().isOneshot())
        alarm.parentAlarm().enabled = false;
    else {
        HwAlarm copy = alarm;
        m_hwAlarms.erase(it);
        addDs3231Alarm(copy);
    };
}

std::vector<HwAlarm>::iterator AlarmService::firstEnabledHwAlarm()
{
    return std::find_if(
        m_hwAlarms.begin(), m_hwAlarms.end(),
        [](const HwAlarm &alarm) { return alarm.parentAlarm().enabled; }
    );
}

void AlarmService::eventLoop()
{
    Command cmd;

    while (true) {
        if (xQueueReceive(m_isrCmdQueue, &cmd, portMAX_DELAY)) {
            std::lock_guard lock(m_lock);

            switch (cmd.type) {
            case Command::FireAlarm:
                onAlarmFired();
                break;

            case Command::StopAlarm:
                onAlarmStopped();
                break;
            }
        }
    }
}

void AlarmService::onAlarmFired()
{
    DateTime now, alarmTime;
    bool firedNow;

    {
        std::lock_guard rtcLock(*m_rtcLock);
        m_rtc->clearAlarm(2);
        now = m_rtc->now();
    }

    auto justFired = firstEnabledHwAlarm();
    Alarm::id_t parentId = justFired->parentAlarm().id();

    log_w(
        "HwAlarm (%s)(%s) fired", CSTR(justFired->toString()),
        CSTR(justFired->parentAlarm().toString())
    );
    processAlarm(justFired);

    // Also process alarms that fired at the same time
    // TODO SIMPLIFY
    for (auto it = justFired + 1, end = m_hwAlarms.end(); it != end; ++it) {
        firedNow = it->parentAlarm().hour == now.hour()
                   && it->parentAlarm().minute == now.minute()
                   && (!it->parentAlarm().usesDaysOfWeek()
                       || it->dayOfWeek() == CONVERT_DOWS(now.dayOfTheWeek()));

        if (!firedNow)
            break;
        if (!it->parentAlarm().enabled)
            continue;

        log_w(
            "HwAlarm (%s)(%s) fired at the same time as (%s), so skipping it",
            it->toString(), it->parentAlarm().toString().c_str(),
            justFired->toString().c_str()
        );
        it->parentAlarm().m_missed = true;
        processAlarm(it);
    }

    updateAlarms();
    _dumpAlarms();

    if (!isAlarmRunning()) {
        m_runningAlarmId = parentId;
        m_alarmPlayer->start(100);  // TODO there goes time from config
        log_w("Started alarm playing");
    } else {
        log_w(
            "Other alarm is running, so (%s) is skipped",
            CSTR(justFired->parentAlarm().toString())
        );
    }
}

void AlarmService::onAlarmStopped()
{
    if (!isAlarmRunning())
        return;

    m_runningAlarmId = 0;
    m_alarmPlayer->stop();
    log_w("Stopped alarm playing");
}

void AlarmService::alarmMissed()
{   
    try {
        m_alarms.at(m_runningAlarmId)->m_missed = true;
    }
    catch (std::out_of_range &e) {
        log_w("Missed alarm which was deleted");
    }
    m_runningAlarmId = 0;
    log_w("Missed alarm");
}

void AlarmService::updateAlarms()
{
    DateTime now;

    // reinsert disabled alarms before processing the fired one to avoid
    // having them in the wrong place once they are enabled
    {
        std::lock_guard rtcLock(*m_rtcLock);
        now = m_rtc->now();
    }
    
    auto it = m_hwAlarms.begin();
    for (auto end = m_hwAlarms.end(); it != end; ++it) {
        if (it->parentAlarm().enabled)
            break;

        HwAlarm copy = *it;
        m_hwAlarms.erase(it);
        addDs3231Alarm(copy, &now);
    }

    // now `it` points to the first enabled alarm
    if (it != m_hwAlarms.end())
        setDs3231Alarm(*it);
    else {
        log_w("None of alarms can fire until enabled one");
        // There's no alarm that can fire, so we need to disable the 1st alarm slot
        std::lock_guard rtcLock(*m_rtcLock);
        m_rtc->clearAlarm(2);
    };
}

std::vector<HwAlarm> AlarmService::alarmToHwAlarms(Alarm *alarm)
{
    std::vector<HwAlarm> parsedAlarms;
    log_d("Parsing alarm (%s)", CSTR(alarm->toString()));

    if (!alarm->usesDaysOfWeek())
        parsedAlarms.emplace_back(alarm);
    else
        for (int i = 0; i <= 6; ++i)
            if (alarm->daysOfWeek.isSet(i))
                parsedAlarms.emplace_back(alarm, i);

    return parsedAlarms;
}

void AlarmService::addDs3231Alarm(HwAlarm &newAlarm, DateTime *now)
{
    DateTime rtcNow;
    if (now == NULL) {
        std::lock_guard rtcLock(*m_rtcLock);

        rtcNow = m_rtc->now();
        now = &rtcNow;
    }

    DateTime newAlarmTime = newAlarm.nextFiring(*now);
    DateTime alarmTime;

    for (auto iter = m_hwAlarms.begin(); iter != m_hwAlarms.end(); ++iter) {
        alarmTime = iter->nextFiring(*now);
        if (iter->parentAlarm().enabled && newAlarmTime <= alarmTime) {
            log_w(
                "Adding HwAlarm (%s) at position %d", CSTR(newAlarm.toString()),
                std::distance(m_hwAlarms.begin(), iter)
            );
            m_hwAlarms.insert(iter, newAlarm);
            return;
        }
    }

    log_w(
        "Adding HwAlarm (%s) %s", CSTR(newAlarm.toString()),
        m_hwAlarms.empty() ? "to the empty vector" : "to the end"
    );
    m_hwAlarms.emplace_back(newAlarm);
}

bool AlarmService::setAlarmState(Alarm::id_t id, bool enabled)
{
    std::lock_guard lock(m_lock);

    try {
        m_alarms.at(id)->enabled = enabled;
    }
    catch (const std::out_of_range &e) {
        log_e("There's no such ID - %d", id);
        return false;
    }

    log_i(
        "Alarm (%s) is %s", CSTR(m_alarms[id]->toString()),
        enabled ? "enabled" : "disabled"
    );
    return true;
}

bool AlarmService::setAlarmTime(Alarm::id_t id, byte hour, byte minute)
{
    DateTime now;
    Alarm *alarm;
    std::lock_guard lock(m_lock);

    try {
        alarm = m_alarms.at(id).get();
        alarm->hour = hour;
        alarm->minute = minute;
    }
    catch (const std::out_of_range &e) {
        log_e("There's no such ID - %d", id);
        return false;
    }

    auto it = m_hwAlarms.begin();
    auto end = m_hwAlarms.end();
    std::vector<HwAlarm> newHwAlarms;
    while (it != end) {
        if (it->parentAlarm().id() == id) {
            newHwAlarms.push_back(*it);
            m_hwAlarms.erase(it);
        } else {
            ++it;
        }
    }

    {
        std::lock_guard rtcLock(*m_rtcLock);
        now = m_rtc->now();
    }

    for (auto &newAlarm : newHwAlarms)
        addDs3231Alarm(newAlarm, &now);
    updateAlarms();

    return true;
}

bool AlarmService::setAlarmDaysOfWeek(Alarm::id_t id, Alarm::DaysOfWeek daysOfWeek)
{
    DateTime now;
    Alarm *alarm;

    std::lock_guard lock(m_lock);
    try {
        alarm = m_alarms.at(id).get();
        alarm->daysOfWeek = daysOfWeek;
    }
    catch (const std::out_of_range &e) {
        log_e("There's no such ID - %d", id);
        return false;
    }

    m_hwAlarms.erase(std::remove_if(
        m_hwAlarms.begin(), m_hwAlarms.end(),
        [id](const HwAlarm &alarm) { return alarm.parentAlarm().id() == id; }
    ));

    std::vector<HwAlarm> newHwAlarms = alarmToHwAlarms(alarm);

    {
        std::lock_guard rtcLock(*m_rtcLock);
        now = m_rtc->now();
    }

    for (auto &newAlarm : newHwAlarms)
        addDs3231Alarm(newAlarm, &now);
    updateAlarms();

    return true;
}

bool AlarmService::removeAlarm(Alarm::id_t id)
{
    std::lock_guard lock(m_lock);
    
    auto it = m_alarms.find(id);
    if (it == m_alarms.end()) {
        return false;  // there is no alarm with such id
    }

    log_i("Removing Alarm (%s)", CSTR(it->second->toString()));
    m_alarms.erase(it);

    m_hwAlarms.erase(
        remove_if(
            m_hwAlarms.begin(), m_hwAlarms.end(),
            [id](const HwAlarm &alarm) { return alarm.parentAlarm().id() == id; }
        ),
        m_hwAlarms.end()
    );

    updateAlarms();
    return true;
}

void AlarmService::setDs3231Alarm(const HwAlarm &alarm)
{
    DateTime dt(2000, 1, 3, alarm.parentAlarm().hour, alarm.parentAlarm().minute);

    // 2000/01/03 is Monday, so we can directly add the day of week value (0-6)
    if (alarm.parentAlarm().usesDaysOfWeek())
        dt = dt + TimeSpan(alarm.dayOfWeek(), 0, 0, 0);

    log_d("Setting on the 2nd slot of DS3231 HwAlarm (%s)", CSTR(alarm.toString()));

    std::lock_guard rtcLock(*m_rtcLock);
    m_rtc->setAlarm2(
        dt, alarm.parentAlarm().usesDaysOfWeek() ? DS3231_A2_Day : DS3231_A2_Hour
    );
}

void AlarmService::setVolume(byte volume)
{
    m_audio->setVolume(volume);
}

bool AlarmService::clearMissedFlag(Alarm::id_t id)
{
    std::lock_guard lock(m_lock);

    try {
        m_alarms.at(id)->clearMissedFlag();
    }
    catch (const std::out_of_range &e) {
        log_e("There's no such ID - %d", id);
        return false;
    }

    log_i("Clearing missed flag for Alarm (%s)", CSTR(m_alarms[id]->toString()));
    return true;
}

AlarmService MainAlarmService;