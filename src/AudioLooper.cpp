#include "AudioLooper.hpp"

#include "SD.h"

#include "Tools.hpp"


AudioLooper::AudioLooper(Audio *audio, std::string filePath) :
m_audio(audio), m_filePath(filePath)
{}

AudioLooper::~AudioLooper()
{
    vQueueDelete(m_cmdQueue);
    xTimerDelete(m_autoStopTimer, portMAX_DELAY);
    if (m_audioTask != nullptr)
        vTaskDelete(m_audioTask);

    m_audio->stopSong();
}

void AudioLooper::begin(std::function<void()> timeoutExpiredCallback)
{
    // clang-format off
    m_timeoutExpiredCallback = timeoutExpiredCallback;
    m_cmdQueue = xQueueCreate(10, sizeof(AudioCmd));

    m_autoStopTimer = xTimerCreate(
        "AudioLooperTimer",
        1,        // timer tick period is changed when starting audio
        pdFALSE,  // timer is activated when starting audio, so autoreload is not needed
        this,     // `this` is used in callback to access AudioLooper instance
        AudioLooper::onTimer
    );

    xTaskCreate(
        methodToTaskFun<AudioLooper, &AudioLooper::looperTask>(),
        "AudioLooperTask", 7000, this, TASK_REALTIME_PRIORITY, &m_audioTask
    );
    // clang-format on
}

void AudioLooper::start(unsigned long seconds)
{
    AudioCmd cmd = {.type = StartCmd, .arg = seconds};
    xQueueSend(m_cmdQueue, &cmd, portMAX_DELAY);
}
void AudioLooper::start()
{
    start(0);
}

void AudioLooper::stop()
{
    AudioCmd cmd = {.type = StopCmd, .arg = 0};
    xQueueSend(m_cmdQueue, &cmd, portMAX_DELAY);
}

void AudioLooper::setAudioPath(std::string &filePath)
{
    m_filePath = filePath;
}

void AudioLooper::onTimer(TimerHandle_t handle)
{
    configASSERT(handle);

    // pointer to the instance is stored in timer's ID for each AudioLooper
    AudioLooper *instance = (AudioLooper *)pvTimerGetTimerID(handle);
    instance->stop();
    instance->m_timeoutExpiredCallback();
}

void AudioLooper::looperTask()
{
    AudioCmd lastCmd;

    // skipping commands until StartCmd is received
    // when this loop exits, there is the StartCmd on top of `m_cmdQueue`
    while (true) {
        xQueuePeek(m_cmdQueue, &lastCmd, portMAX_DELAY);
        if (lastCmd.type == StartCmd) {
            break;
        } else {
            xQueueReceive(m_cmdQueue, &lastCmd, portMAX_DELAY);
        }
    }

    while (true) {
        // 1-tick delay prevents watchdog timer from triggering
        if (xQueueReceive(m_cmdQueue, &lastCmd, 1)) {
            switch (lastCmd.type) {
            case StartCmd:
                m_audio->connecttoSD(CSTR(m_filePath));
                log_i("aboba: %d", SD.exists("/test.mp3"));

                if (lastCmd.arg != 0) {
                    // also starts the timer
                    xTimerChangePeriod(
                        m_autoStopTimer, pdMS_TO_TICKS(1000 * lastCmd.arg),
                        portMAX_DELAY
                    );
                }

                log_i("Processed StartCmd");
                break;

            case StopCmd:
                m_audio->stopSong();  // TODO fade out audio
                log_i("Processed StopCmd, suspending the task");

                if (xTimerIsTimerActive(m_autoStopTimer)) {
                    xTimerStop(m_autoStopTimer, portMAX_DELAY);
                }
                // now the task can be suspended until the next command arrives
                // because the audio player is stopped, so using portMAX_DELAY
                xQueuePeek(m_cmdQueue, &lastCmd, portMAX_DELAY);
                // skip the end of the iteration to prevent audio from looping again
                continue;
                break;
            }
        }

        if (!m_audio->isRunning())
            m_audio->connecttoSD(CSTR(m_filePath));

        m_audio->loop();
    }

    vTaskDelete(NULL);
}