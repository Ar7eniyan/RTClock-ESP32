#include <freertos/FreeRTOS.h>
#include <functional>
#include <string>

#include "Audio.h"
#include "SD.h"


class AudioLooper {
public:
    AudioLooper(Audio *audio, std::string filePath);
    ~AudioLooper();

    void begin(std::function<void()> timeoutExpiredCallback);
    void start(unsigned long timeoutSeconds);
    void start();
    void stop();
    void setAudioPath(std::string &filePath);
    static void onTimer(TimerHandle_t handle);

private:
    void looperTask();
    enum CommandType { StopCmd, StartCmd };
    struct AudioCmd {
        CommandType  type;
        unsigned int arg;  // timeout duration in seconds
    };

    Audio                   *m_audio;
    TaskHandle_t             m_audioTask = nullptr;
    TimerHandle_t            m_autoStopTimer;
    QueueHandle_t            m_cmdQueue;
    std::string              m_filePath;
    unsigned long            m_startTime;
    std::function<void()>    m_timeoutExpiredCallback;
};