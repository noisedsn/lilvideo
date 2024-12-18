#include <AACDecoderHelix.h>

static unsigned long total_read_audio_ms = 0;
static unsigned long total_decode_audio_ms = 0;
static unsigned long total_play_audio_ms = 0;

static int _samprate = 0;
static void audioDataCallback(AACFrameInfo &info, int16_t *pwm_buffer, size_t len, void*)
{
    unsigned long s = millis();
    if (_samprate != info.sampRateOut)
    {
        Serial.printf("bitRate: %d, nChans: %d, sampRateCore: %d, sampRateOut: %d, bitsPerSample: %d, outputSamps: %d, profile: %d, tnsUsed: %d, pnsUsed: %d",
                   info.bitRate, info.nChans, info.sampRateCore, info.sampRateOut, info.bitsPerSample, info.outputSamps, info.profile, info.tnsUsed, info.pnsUsed);
        // TODO: чомусь не працює перемикання частоти на льоту
        // esp_i2s::i2s_set_clk(esp_i2s::I2S_NUM_0, info.sampRateOut /* sample_rate */, info.bitsPerSample /* bits_cfg */, (info.nChans == 2) ? esp_i2s::I2S_CHANNEL_STEREO : esp_i2s::I2S_CHANNEL_MONO /* channel */);
        _samprate = info.sampRateOut;
    }
    size_t i2s_bytes_written = 0;
    esp_i2s::i2s_write(esp_i2s::I2S_NUM_0, pwm_buffer, len * 2, &i2s_bytes_written, portMAX_DELAY);
    // log_i("len: %d, i2s_bytes_written: %d", len, i2s_bytes_written);
    // Serial.printf(" chan: %d", info.nChans);
    total_play_audio_ms += millis() - s;
}

static libhelix::AACDecoderHelix _aac(audioDataCallback);
static uint8_t _frame[AAC_MAX_FRAME_SIZE];
static void aac_player_task(void *pvParam)
{
    Stream *input = (Stream *)pvParam;

    int r, w;
    unsigned long ms = millis();
    while (r = input->readBytes(_frame, AAC_MAX_FRAME_SIZE))
    {
        total_read_audio_ms += millis() - ms;
        ms = millis();

        while (r > 0)
        {
            w = _aac.write(_frame, r);
            // Serial.printf("r: %d, w: %d\n", r, w);
            r -= w;
        }
        total_decode_audio_ms += millis() - ms;
        ms = millis();
    }
    log_i("AAC stop.");

    vTaskDelete(NULL);
}

TaskHandle_t _audio_task;
static BaseType_t aac_player_task_start(Stream *input, int core)
{
    _aac.begin();

    return xTaskCreatePinnedToCore(
        (TaskFunction_t)aac_player_task,
        (const char *const)"AAC Player Task",
        (const uint32_t)2000,
        (void *const)input,
        (UBaseType_t)configMAX_PRIORITIES - 1,
        (TaskHandle_t *const)&_audio_task,
        (const BaseType_t)0);
}