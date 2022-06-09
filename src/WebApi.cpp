#include "WebApi.hpp"

using Result = UrlParser::Result;

UrlParser ApiUrlParser({
    {1, "GET",    "/alarms",                      api::getAlarms},
    {1, "POST",   "/alarms",                      api::addAlarm},
    {1, "DELETE", "/alarms/{id}",                 api::removeAlarm},
    {1, "PATCH",  "/alarms/{id}",                 api::updateAlarm},
    {1, "GET",    "/alarms/{id}/enable",          api::setAlarmState},
    {1, "GET",    "/alarms/{id}/disable",         api::setAlarmState},
    {1, "GET",    "/alarms/{id}/clearMissedFlag", api::clearMissedFlag},
    {1, "PUT",    "/volume",                      api::setVolume},
    {1, "GET",    "/printAlarms",                 api::printAlarms}
});

static_assert(sizeof(unsigned long long) >= sizeof(Alarm::id_t));
// WebApi.cpp uses std::stoull, so alarm id must fit in unsigned long long

/**
 * sample request:
 * POST /alarms
 * {
 *      "time": "12:00",
 *      "daysOfWeek": [true, true, false, false, true, flase, true],
 *      "enabled": true  // optional, defaults to false
 * }
 * 
 * sample response:
 * {
 *     "id": 1337
 * }
 */
UrlParser::Result api::addAlarm(
    const UrlParser::Request &request, UrlParser::Response &response
)
{
    JsonVariant jsonData = request.data;
    bool enabled = false;

    if (jsonData.containsKey("enabled")) {
        if (jsonData["enabled"].is<bool>()) {
            enabled = jsonData["enabled"].as<bool>();
        } else {
            return httpResult::invalidEnabledField;
        }
    } else {
        return httpResult::missingField("enabled");
    }

    if (!jsonData.containsKey("daysOfWeek")) {
        return httpResult::missingField("daysOfWeek");
    } else if (!jsonData["time"].is<const char *>()) {
        return httpResult::invalidTimeField;
    }

    const char * const alarmTime = jsonData["time"].as<const char *>();
    byte hour, minute;

    if (sscanf(alarmTime, "%hhu:%hhu", &hour, &minute) != 2) {
        return httpResult::invalidTimeField;
    };

    if (!jsonData.containsKey("daysOfWeek")) {
        return httpResult::missingField("daysOfWeek");
    } else if (!jsonData["daysOfWeek"].is<JsonArray>()) {
        return httpResult::invalidDaysOfWeekField;
    }

    JsonArray daysOfWeek = jsonData["daysOfWeek"].as<JsonArray>();
    if (daysOfWeek.size() != 7) {
        return httpResult::invalidDaysOfWeekField;
    }

    Alarm::DaysOfWeek days(0);
    for (byte i = 0; i < 7; ++i) {
        days.set(i, daysOfWeek[i].as<bool>());
    }

    Alarm newAlarm(hour, minute, days, enabled);
    Alarm::id_t id = MainAlarmService.addAlarm(newAlarm);

    response.data["id"] = id;
    return httpResult::CREATED;
}

/**
 * sample request:
 * GET /alarms
 * 
 * sample response:
 * [
 *     {
 *        "id": 1337,
 *        "time": "12:00",
 *        "daysOfWeek": [true, true, false, false, true, flase, true],
 *        "enabled": false,
 *        "missed": true
 *     },
 *     {
 *        "id": 31337,
 *        "time": "13:00",
 *        "daysOfWeek": [true, true, false, false, true, flase, true],
 *        "enabled": true,
 *        "missed": false
 *     }
 * ]
 */
Result api::getAlarms(
    const UrlParser::Request &request, UrlParser::Response &response
)
{
    JsonArray alarms = response.data.to<JsonArray>();

    for (auto &val : MainAlarmService.getAlarms()) {
        Alarm &alarm = *val.second;
        JsonObject alarmJson = alarms.createNestedObject();
        alarmJson["id"] = alarm.id();

        char time[6];  // "hh:mm" + "\0"
        // always produces 6-chrachter string even if alarm is invalid
        sprintf(time, "%02hhu:%02hhu", alarm.hour % 100, alarm.minute % 100);
        alarmJson["time"] = time;
        
        JsonArray daysOfWeek = alarmJson.createNestedArray("daysOfWeek");
        for (byte i = 0; i < 7; ++i) {
            daysOfWeek.add(alarm.daysOfWeek.isSet(i));
        }
        alarmJson["enabled"] = alarm.enabled;
        alarmJson["missed"] = alarm.isMissed();
    }

    return httpResult::OK;
}

/**
 * sample request:
 * DELETE /alarms/{id}
 */
Result api::removeAlarm(
    const UrlParser::Request &request, UrlParser::Response &response
)
{
    Alarm::id_t id;

    try {
        id = std::stoull(request.urlParams.at("id"));
    }
    catch (std::exception &e) {
        return httpResult::invalidId;
    }

    if (!MainAlarmService.removeAlarm(id)) {
        return httpResult::alarmNotFound(id);
    }

    return httpResult::NO_CONTENT;
}

/**
 * sample request 1:
 * PATCH /alarms/{id}
 * {
 *      "time": "12:00"
 * }
 * 
 * sample request 2:
 * PATCH /alarms/{id}
 * {
 *     "daysOfWeek": [true, true, false, false, true, flase, true]
 * }
 * 
 * TODO should I add enabling/disabling alarm throung this endpoint?
 */
Result api::updateAlarm(
    const UrlParser::Request &request, UrlParser::Response &response
)
{
    JsonVariant jsonData = request.data;
    Alarm::id_t id;

    try {
        id = std::stoull(request.urlParams.at("id"));
    }
    catch (std::exception &e) {
        return httpResult::invalidId;
    }

    if (jsonData.containsKey("time")){
        if (jsonData["time"].is<const char *>()){

            const char * alarmTime = jsonData["time"].as<const char *>();
            byte hour, minute;

            if (sscanf(alarmTime, "%hhu:%hhu", &hour, &minute) != 2) {
                return httpResult::invalidTimeField;
            }

            if (!MainAlarmService.setAlarmTime(id, hour, minute)) {
                return httpResult::alarmNotFound(id);
            }

        } else {
            return httpResult::invalidTimeField;
        }
    }

    if (jsonData.containsKey("daysOfWeek")) {
        if (!jsonData["daysOfWeek"].is<JsonArray>()) {
            return httpResult::invalidDaysOfWeekField;
        }
        JsonArray daysOfWeek = jsonData["daysOfWeek"].as<JsonArray>();

        if (daysOfWeek.size() != 7) {
            return httpResult::invalidDaysOfWeekField;
        }

        Alarm::DaysOfWeek days = 0;
        for (byte i = 0; i < 7; ++i) {
            days.set(i, daysOfWeek[i].as<bool>());
        }
        if (!MainAlarmService.setAlarmDaysOfWeek(id, days)) {
            return httpResult::alarmNotFound(id);
        }
    }

    return httpResult::NO_CONTENT;
}

/**
 * sample request 1:
 * GET /alarms/{id}/enable
 * 
 * sample request 2:
 * GET /alarms/{id}/disable
 */
Result api::setAlarmState(
    const UrlParser::Request &request, UrlParser::Response &response
)
{
    Alarm::id_t id;

    try {
        id = std::stoull(request.urlParams.at("id"));
    }
    catch (std::exception &e) {
        return httpResult::invalidId;
    }
    
    std::string_view uri(request.rawMessage.uri.ptr, request.rawMessage.uri.len);
    // url is /alarms/{id}/enable or /alarms/{id}/disable,
    // so check the last url part
    bool enable = uri.substr(uri.find_last_of('/') + 1) == "enable";

    if (!MainAlarmService.setAlarmState(id, enable)) {
        return httpResult::alarmNotFound(id);
    }

    return httpResult::NO_CONTENT;
}

/**
 * sample request:
 * PUT /volume
 * {
 *    "volume": 50
 * }
 */
Result api::setVolume(
    const UrlParser::Request &request, UrlParser::Response &response
)
{
    JsonVariant jsonData = request.data;

    if (!jsonData.containsKey("volume")) {
        return httpResult::missingField("volume");
    }

    if (jsonData["volume"].is<int>()) {

        int volume = jsonData["volume"].as<int>();
        if (volume < 0 || volume > 100) {
            return httpResult::invalidVolumeField;
        }

        MainAlarmService.setVolume(map(volume, 0, 100, 0, 21));
        return httpResult::NO_CONTENT;

    } else {
        return httpResult::invalidVolumeField;
    }
}

/**
 * sample request:
 * GET /alarms/{id}/clearMissedFlag
 */
Result api::clearMissedFlag(
    const UrlParser::Request &request, UrlParser::Response &response
)
{
    Alarm::id_t id;

    try {
        id = std::stoull(request.urlParams.at("id"));
    }
    catch (std::exception &e) {
        return httpResult::invalidId;
    }

    if (!MainAlarmService.clearMissedFlag(id)) {
        return httpResult::alarmNotFound(id);
    }

    return httpResult::NO_CONTENT;
}

Result api::printAlarms(
    const UrlParser::Request &request, UrlParser::Response &response
)
{
    MainAlarmService.dumpAlarms();
    return httpResult::NO_CONTENT;
}