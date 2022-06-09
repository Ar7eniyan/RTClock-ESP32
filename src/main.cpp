// clang-format off
#include <vector>
#include <memory>

#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <lwip/err.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <fcntl.h>
#include <lwip/apps/sntp.h>

#include "NTPClient.h"
#include "Audio.h"
#include "RTClib.h"
#include "HT16K33.h"
#include "libssh_esp32.h"
#include "libssh/libssh.h"
#include "../mongoose.h"
#include "ArduinoJson.h"

#include "AlarmService.hpp"
#include "WebApi.hpp"
#include "UrlParser.hpp"
#include "Tools.hpp"
#include "config.hpp"

// TODO divide into main.cpp, sshTunnel.cpp and config.h

//=================  Sound amplifier pins  =====================
#define I2S_DOUT               32
#define I2S_BCLK               33
#define I2S_LRC                25
//=================  Other controls' pins  =====================
#define ALARM_STOP_BTN_PIN     39
#define VOLUME_PIN             36
#define RTC_INTERRUPT_PIN      34
//====================  Second I2C bus  ========================
#define SDA2                   16
#define SCL2                   17
//========================  Delays  ============================
#define BLINK_DELAY            500
//=======================  Time units  =========================
#define HOUR_IN_SECS           3600
#define HOUR_IN_MILLIS         3600000
#define NTP_UPDATE_INTERVAL    20000      // 24 * HOUR_IN_MILLIS
//====================  WiFi configuration  ====================
#define LOCAL_IP               IPAddress(192, 168, 1, 200)
#define GATEWAY                IPAddress(192, 168, 1, 1)
#define SUBNET                 IPAddress(255, 255, 255, 0)
#define DNS1                   IPAddress(8, 8, 8, 8)
#define DNS2                   IPAddress(8, 8, 4, 4)
//======================  Display dots  ========================
#define NO_DOTS                0
#define COLON                  2
#define DOT_LEFT_TOP           4
#define DOT_LEFT_BOTTOM        8
#define DOT_RIGHT_UP           16
#define LEFT_COLON             (DOT_LEFT_TOP | DOT_LEFT_BOTTOM)
//==============================================================
#define SSH_TUNNEL_HOST        "129.80.77.29"
#define SSH_TUNNEL_PORT        31337
#define SSH_TUNNEL_USER        "ubuntu"
#define SSH_TUNNEL_REMOTE_PORT 8081
//==============================================================
#define HTTP_SERVER_PORT       8080
// clang-format on

typedef struct {
    SemaphoreHandle_t   sem;
    int                 type;
} esp_pthread_mutex_t;  // FIXME VERY UNSAFE
extern const uint8_t ssh_key_start[] asm("_binary_src_keys_server_key_start");

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3 * HOUR_IN_SECS);
TimerHandle_t ntpUpdateTimer;

HT16K33 seg(0x70, &Wire1);  // default ht16k33 I2C address
RTC_DS3231 rtc;
std::shared_ptr<std::mutex> rtcMutex;
Audio audio;

static const uint8_t buildHour = (__TIME__[0] - '0') * 10 + (__TIME__[1] - '0');
static const uint8_t buildMinute = (__TIME__[3] - '0') * 10 + (__TIME__[4] - '0');
uint16_t potentiometer;
byte volume;


bool isDateTimeValid(const DateTime *dt);
bool setup7segDisplay();
bool setupRtc();
void changeClockMode();
void updateDisplayTask(void *pvParameters);
void sshTunnelTask(void *pvParameters);
void webServerTask(void *pvParameters);
void updateTimeFromNtp();
void loop()
{
    vTaskDelete(NULL);
}

void updateVolume()
{
    potentiometer = analogRead(VOLUME_PIN);
    volume = map(potentiometer, 0, 4095, 0, 21);
    MainAlarmService.setVolume(volume);
}

void setup()
{
    libssh_begin();

    Serial.begin(115200);
    log_i("Initialized serial port");

    pinMode(VOLUME_PIN, ANALOG);
    pinMode(SS, OUTPUT);
    digitalWrite(SS, HIGH);

    if (!(setupRtc() && setup7segDisplay())) {
        delay(2500);
        ESP.restart();
    }

    SPI.begin(SCK, MISO, MOSI);
    SD.begin(SS, SPI);
    log_i("SD card type: %d, size: %llu", SD.cardType(), SD.cardSize());
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);

    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    log_i("Connecting to WiFi...");

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (!WiFi.isConnected()) {
        delay(250);
    }
    // WiFi.config(LOCAL_IP, GATEWAY, SUBNET, DNS1, DNS2);
    log_i("Connected to WiFi, IP: %s", WiFi.localIP().toString().c_str());
    srand(ESP.getFreeHeap() ^ ESP.getCycleCount());

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_DEBUG
    log_d("Printing WiFi configuration:");
    WiFi.printDiag(Serial);  // TODO make it print as log
#endif

    timeClient.begin();

    Alarm alarm0(buildHour, buildMinute - 1, Alarm::DaysOfWeek::everyDay, true);
    Alarm alarm1(buildHour, buildMinute + 1, Alarm::DaysOfWeek::noDays, true);
    Alarm alarm2(buildHour, buildMinute + 2, Alarm::DaysOfWeek::everyDay, true);
    Alarm alarm3(buildHour, buildMinute + 3, 0b00101101, true);
    Alarm alarm4(buildHour, buildMinute + 4, 0b01110110, true);
    Alarm alarm5(buildHour, buildMinute + 4, Alarm::DaysOfWeek::everyDay, true);

    MainAlarmService.begin(
        &rtc, &audio, rtcMutex, RTC_INTERRUPT_PIN, ALARM_STOP_BTN_PIN
    );
    MainAlarmService.setVolume(10);
    // MainAlarmService.addAlarm(alarm0);
    // MainAlarmService.addAlarm(alarm1);
    // MainAlarmService.addAlarm(alarm2);
    // MainAlarmService.addAlarm(alarm3);
    // MainAlarmService.addAlarm(alarm4);
    // MainAlarmService.addAlarm(alarm5);
    // MainAlarmService.setAlarmState(alarm3.id(), false);
    // MainAlarmService.setAlarmTime(alarm5.id(), buildHour, buildMinute + 5);
    // MainAlarmService.setAlarmDaysOfWeek(alarm0.id(), 0b01100110);

    MainAlarmService.dumpAlarms();

    ntpUpdateTimer = xTimerCreate(
        "NtpUpdateTime", pdMS_TO_TICKS(NTP_UPDATE_INTERVAL), pdTRUE, 0,
        [](TimerHandle_t hanlde) { updateTimeFromNtp(); }
    );
    xTimerStart(ntpUpdateTimer, portMAX_DELAY);

    updateTimeFromNtp();
    xTaskCreate(
        updateDisplayTask, "DisplayUpdate", 5120, NULL, TASK_HIGH_PRIORITY, NULL
    );
    xTaskCreate(
        sshTunnelTask, "SshTunnel", 12000, NULL, TASK_NORMAL_PRIORITY, NULL
    );
    xTaskCreate(
        webServerTask, "WebServer", 12000, NULL, TASK_HIGH_PRIORITY, NULL
    );
}

bool setup7segDisplay()
{
    if (!Wire1.setPins(SDA2, SCL2)) {
        log_e("Could not set I2C pins to sda = %d, scl = %d", SDA2, SCL2);
        return false;
    }
    if (!seg.begin()) {
        log_e("Could not find 7-seg at sda = %d, scl = %d", SDA2, SCL2);
        return false;
    }
    log_i("Started 7-seg display with sda = %d, scl = %d", SDA2, SCL2);
    seg.cacheOff();
    seg.brightness(15);
    seg.blink(0);
    seg.displayClear();
    log_i("Cleared display, set brightness");
    return true;
}

bool setupRtc()
{
    if (!rtc.begin(&Wire)) {
        log_e("Could not find RTC at sda = %d, scl = %d", SDA, SCL);
        return false;
    }
    log_i("Connected to RTC using sda = %d, scl = %d", SDA, SCL);
    if (rtc.lostPower()) {
        log_w("RTC lost power, battery may be low");
    }
    // rtc.adjust(DateTime(__DATE__, __TIME__));  // TODO remove in production
    rtcMutex = std::make_shared<std::mutex>();
    return true;
}

void updateTimeFromNtp()
{
    DateTime ntpTime;
    char ntpTimeStr[] = "DDD, DD MMM YYYY hh:mm:ss";

    log_d("Updating time from NTP...");
    timeClient.forceUpdate();
    
    ntpTime = DateTime(timeClient.getEpochTime());
    ntpTime.toString(ntpTimeStr);

    {
        std::lock_guard<std::mutex> lock(*rtcMutex);
        rtc.adjust(ntpTime);
    }
    log_d("Updated time to %s", ntpTimeStr);
}

void updateDisplayTask(void *pvParameters)
{
    unsigned long lastBlinked = millis();
    bool dotsState = false;

    DateTime now;
    char *time;
    bool lostPower;

    log_i("Entered task %s", pcTaskGetTaskName(NULL));

    while (true) {
        {
            std::lock_guard<std::mutex> lock(*rtcMutex);
            now = rtc.now();
            lostPower = rtc.lostPower();
        }
        
        if (!isDateTimeValid(&now) || lostPower) {
            updateTimeFromNtp();
            log_w("Lost power: %d, or time is invalid: %d", lostPower, !isDateTimeValid(&now));
        }
        char format[] = "DDD, DD MMM YYYY hh:mm:ss";
        time = now.toString(format);

        if ((millis() - lastBlinked) > BLINK_DELAY) {
            dotsState ^= 1;
            lastBlinked = millis();
            
            // FIXME VERY BAD HACK
            bool locked = xSemaphoreTake(((esp_pthread_mutex_t *)(*(MainAlarmService.m_lock).native_handle()))->sem, 0) == pdFALSE;
            if (!locked) {
                xSemaphoreGive(((esp_pthread_mutex_t *)(*(MainAlarmService.m_lock).native_handle()))->sem);
            }

            log_d(
                "RTC time: %s, free heap: %d, mutex state: %s",
                time, ESP.getFreeHeap(), locked ? "locked" : "free"
            );
        }

        seg.displayTime(now.hour(), now.minute(), false, false);
        seg.writePos(2, dotsState ? COLON : NO_DOTS);
        vPortYield();
    }

    vTaskDelete(NULL);
}

static void
    mgCallback(struct mg_connection *conn, int evt, void *evt_data, void *fn_data)
{
    static char buf[1024];
    if (evt == MG_EV_HTTP_MSG) {
        log_i("Got http message");

        mg_http_message *msg = (struct mg_http_message *)evt_data;
        UrlParser::Response resp;
        log_i("HTTP message: \n%.*s", msg->message.len, msg->message.ptr);

        StaticJsonDocument<1024> doc;
        resp.data = doc.to<JsonObject>();

        UrlParser::Result result = ApiUrlParser.match(*msg, resp);

        if (!resp.data.isNull()) {
            resp.headers += "Content-Type: application/json\r\n";
            serializeJson(resp.data, buf, sizeof(buf));

            log_i("Response: '%s'", buf);
            mg_http_reply(conn, result.code, resp.headers.c_str(), buf);
        }
    }
}

void webServerTask(void *pvParameters)
{
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    char url[24];  // 24 = strlen("http://localhost:65355") + 1
    sprintf(url, "http://localhost:%d", HTTP_SERVER_PORT);
    mg_http_listen(&mgr, url, mgCallback, &mgr);

    while (true) {
        mg_mgr_poll(&mgr, 1000);
    }

    mg_mgr_free(&mgr);
    vTaskDelete(NULL);
}

void sshTunnelTask(void *pvParameters)
{
    log_i("Entered task %s", pcTaskGetTaskName(NULL));
    int rc, sock = -1;
    ssh_channel channel = NULL;
    ssh_key key = NULL;
    char buffer[1024];

    ssh_session session = ssh_new();
    assert(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, SSH_TUNNEL_HOST);
    int port = SSH_TUNNEL_PORT;
    ssh_options_set(session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(session, SSH_OPTIONS_USER, SSH_TUNNEL_USER);

    log_i("Connecting to SSH server...");
    rc = ssh_connect(session);
    if (rc != SSH_OK) {
        log_e("SSH connection failed: %s", ssh_get_error(session));
        goto cleanup;
    }

    rc = ssh_pki_import_privkey_base64(
        (char *)&ssh_key_start, NULL, NULL, NULL, &key
    );
    if (rc != SSH_OK) {
        log_e("Failed to import private key: %s", ssh_get_error(session));
        goto cleanup;
    }

    rc = ssh_userauth_publickey(session, NULL, key);
    if (rc != SSH_AUTH_SUCCESS) {
        log_e("SSH authentication failed: %s", ssh_get_error(session));
        goto cleanup;
    }
    log_i("SSH connection established with %s", SSH_TUNNEL_HOST);

    rc = ssh_channel_listen_forward(session, NULL, SSH_TUNNEL_REMOTE_PORT, NULL);
    if (rc != SSH_OK) {
        log_e(
            "Failed to set the tunnel up on server port %d: %s",
            SSH_TUNNEL_REMOTE_PORT, ssh_get_error(session)
        );
        goto cleanup;
    }

    // address of localhost:8080, on which mongoose is listening
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HTTP_SERVER_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    while (true) {
        do {
            channel = ssh_channel_accept_forward(session, __INT_MAX__, NULL);
        } while (channel == NULL);

        log_i(
            "Accepted incoming connection on server port %d", SSH_TUNNEL_REMOTE_PORT
        );

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            log_e("Failed to create socket: %s", strerror(errno));
            goto cleanup;
        }

        // use socket to forward data from SSH channel to local mongoose server
        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
            log_e(
                "Failed to connect to local mongoose server: %s", strerror(errno)
            );
            goto cleanup;
        }
        fcntl(sock, F_SETFL, O_NONBLOCK);

        while (true) {
            rc = ssh_channel_read_nonblocking(channel, buffer, sizeof(buffer), 0);
            if (rc > 0) {
                log_v("Received %d bytes from SSH tunnel", rc);
                send(sock, buffer, rc, 0);
            } else if (rc == SSH_EOF) {
                log_i("Connection closed from tunnel (remote) side");
                break;
            } else if (rc == SSH_ERROR) {
                log_e("Error reading from SSH tunnel: %s", ssh_get_error(session));
                goto cleanup;
            }

            rc = recv(sock, buffer, sizeof(buffer), 0);
            if (rc > 0) {
                log_v("Sending %d bytes to SSH tunnel", rc);
                ssh_channel_write(channel, buffer, rc);
            } else if (rc == 0) {
                log_i("Connection closed from socket (local) side");
                break;
            } else if (rc == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                log_e("Error reading from socket: %s", strerror(errno));
                goto cleanup;
            }

            vTaskDelay(1);
        }

        close(sock);
        ssh_channel_free(channel);
    }

cleanup:
    if (sock != -1)
        close(sock);
    ssh_channel_free(channel);
    ssh_disconnect(session);
    ssh_free(session);
    vTaskDelete(NULL);
}

bool isDateTimeValid(const DateTime *dt)
{
    return (dt->hour() <= 23) && (dt->minute() <= 59) && (dt->second() <= 59)
           && (dt->dayOfTheWeek() <= 6) && (dt->day() >= 1 && dt->day() <= 31)
           && (dt->month() >= 1 && dt->month() <= 12)
           && (dt->year() <= 2099);
}

void audio_info(const char *info)
{
    Serial.print("info        ");
    Serial.println(info);
}
void audio_id3data(const char *info)
{  // id3 metadata
    Serial.print("id3data     ");
    Serial.println(info);
}
void audio_eof_mp3(const char *info)
{  // end of file
    Serial.print("eof_mp3     ");
    Serial.println(info);
}
