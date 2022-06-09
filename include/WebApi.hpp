#include "stdio.h"
#include "Arduino.h"

#include "ArduinoJson.h"
#include "AlarmService.hpp"
#include "UrlParser.hpp"


namespace httpResult {
static const UrlParser::Result OK(200);
static const UrlParser::Result CREATED(201);
static const UrlParser::Result NO_CONTENT(204);

static const UrlParser::Result invalidTimeField(
    400, "Invalid 'time' field, must be 'hh:mm' string"
);
static const UrlParser::Result invalidDaysOfWeekField(
    400, "Invalid 'daysOfWeek' field, must be an array of 7 elements"
);
static const UrlParser::Result invalidEnabledField(
    400, "Invalid 'enabled' field, must be a boolean"
);
static const UrlParser::Result invalidVolumeField(
    400, "Invalid 'volume' field, must be a number between 0 and 100"
);
static const UrlParser::Result invalidId(
    400, "Invalid (or too large) id in url, must be a number"
);

inline auto missingField(std::string fieldName) {
    return UrlParser::Result(400, "Missing '" + fieldName + "' field");
}

inline auto alarmNotFound(Alarm::id_t id) {
    return UrlParser::Result(404, "Alarm with id " + std::to_string(id) + " not found");
}
}

namespace api {
    UrlParser::Result addAlarm(
        const UrlParser::Request &request, UrlParser::Response &response
    );
    UrlParser::Result getAlarms(
        const UrlParser::Request &request, UrlParser::Response &response
    );
    UrlParser::Result removeAlarm(
        const UrlParser::Request &request, UrlParser::Response &response
    );
    UrlParser::Result updateAlarm(
        const UrlParser::Request &request, UrlParser::Response &response
    );
    UrlParser::Result setAlarmState(
        const UrlParser::Request &request, UrlParser::Response &response
    );
    UrlParser::Result setVolume(
        const UrlParser::Request &request, UrlParser::Response &response
    );
    UrlParser::Result clearMissedFlag(
        const UrlParser::Request &request, UrlParser::Response &response
    );
    UrlParser::Result printAlarms(
        const UrlParser::Request &request, UrlParser::Response &response
    );
}

extern UrlParser ApiUrlParser;