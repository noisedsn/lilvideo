/**
 * Li'l Video player for Lilka board
 * created by noisedsn
 * based on a Mini TV project:
 * https://github.com/moononournation/MiniTV 
 * ...and MiniLegoTV project:
 * https://github.com/0015/ThatProject/tree/master/ESP32_VideoPlayer/MiniLegoTV
 * 
 * Lilka SDK:
 * https://github.com/and3rson/lilka
 * 
 * Additional libraries required:
 * https://github.com/moononournation/Arduino_GFX.git
 * https://github.com/pschatzmann/arduino-libhelix.git
 * https://github.com/bitbank2/JPEGDEC.git
 * 
 */


#include <lilka.h>
#include <driver/i2s.h>
#include "splash.h"


#define FPS 30
#define MJPEG_BUFFER_SIZE (288 * 240 * 2 / 8) // JPEG любить бути кратним 16
#define AUDIOASSIGNCORE 1
#define DECODEASSIGNCORE 0
#define DRAWASSIGNCORE 1
#define BASE_DIR "Video"
#define AAC_FILENAME "/44100.aac"
#define MJPEG_FILENAME "/288_30fps.mjpeg"
#define VIDEO_COUNT 2



/* Variables */
static int next_frame = 0;
static int skipped_frames = 0;
static unsigned long start_ms, curr_ms, next_frame_ms;
static unsigned int video_idx = 1;
char _alert[512];
int volume = 100;
int osdFrames = 60;
String osdStr = "";




lilka::Canvas buffer(280, 240);

/* Audio */
#include "audio_task.h"

/* MJPEG Video */
#include "video_task.h"




// pixel drawing callback
static int drawMCU(JPEGDRAW *pDraw) {
  unsigned long s = millis();
  buffer.draw16bitRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
  total_show_video_ms += millis() - s;
  return 1;
}

void drawOSD(String osdStr) {
  if (osdStr && osdFrames > 0) {
    buffer.setFont(FONT_10x20);
    buffer.setTextColor(lilka::colors::Green);
    buffer.setCursor(32, 32);
    buffer.print(osdStr);
    osdFrames --;
  }
}

void showAlert(String alertStr, int alertDelay) {
    Serial.println(alertStr);
    lilka::Alert Alert("Помилка:", alertStr);
    Alert.draw(&buffer);
    lilka::display.drawCanvas(&buffer);
    delay(alertDelay);
    return;
}

void playVideoWithAudio(String subdirStr) {
  char *subdir = const_cast<char*>(subdirStr.c_str());

  Serial.printf("Opening folder: %s\n", subdir);

  char aFilePath[255];
  sprintf(aFilePath, "/%s/%s%s", BASE_DIR, subdir, AAC_FILENAME);
  File aFile = SD.open(aFilePath);
  if (!aFile || aFile.isDirectory()) {
    snprintf(_alert, sizeof(_alert), "Не можу відкрити файл %s", aFilePath);
    showAlert(_alert, 3000);
    return;
  }

  char vFilePath[255];
  sprintf(vFilePath, "/%s/%s%s", BASE_DIR, subdir, MJPEG_FILENAME);
  File vFile = SD.open(vFilePath);
  if (!vFile || vFile.isDirectory()) {
    snprintf(_alert, sizeof(_alert), "Не можу відкрити файл %s", vFilePath);
    showAlert(_alert, 3000);
    return;
  }

  Serial.printf("Init video %s\n", vFilePath);
  mjpeg_setup(&vFile, MJPEG_BUFFER_SIZE, drawMCU, false, DECODEASSIGNCORE, DRAWASSIGNCORE);
  Serial.printf("Start play audio task %s\n", aFilePath);
  BaseType_t ret = aac_player_task_start(&aFile, AUDIOASSIGNCORE);
  if (ret != pdPASS) {
    snprintf(_alert, sizeof(_alert), "Audio player task start failed: %d\n", ret);
    showAlert(_alert, 3000);
  }

  Serial.println("Start playing video");

  start_ms = millis();
  curr_ms = millis();
  next_frame_ms = start_ms + (++next_frame * 1000 / FPS / 2);

  while (vFile.available() && mjpeg_read_frame())  // Read video
  {

    total_read_video_ms += millis() - curr_ms;
    curr_ms = millis();

    if (millis() < next_frame_ms)   // check show frame or skip frame
    {
      // Play video
      mjpeg_draw_frame();
      delay(2);                     // TODO: дуже некрасивий хак щоб зменшити тірінг, треба якось придумати щось інше...
      drawOSD(osdStr);              // TODO: solve osd flickering
      lilka::display.drawCanvas(&buffer);

      total_decode_video_ms += millis() - curr_ms;
      curr_ms = millis();
    } else {
      ++skipped_frames;
      Serial.println("Skip frame");
    }

    while (millis() < next_frame_ms) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }

    curr_ms = millis();
    next_frame_ms = start_ms + (++next_frame * 1000 / FPS);
  }

  int time_used = millis() - start_ms;
  int total_frames = next_frame - 1;
  Serial.println("AV end");
  vFile.close();
  aFile.close();

  esp_restart(); // back to Keira OS on video ends

}


// Controls
void controllerTask(void *parameter) {
  while (1) {
    
    lilka::State state = lilka::controller.getState();
    
    // back to Keira OS on A button press
    if (state.a.justPressed) {
      esp_restart();
    }

    // Volume
    if (state.up.justPressed) {
      if (volume < 100) volume += 10;

      osdStr = "Гучність: " + (String)volume;
      osdFrames = 60;
      Serial.println(osdStr);
    }
    if (state.down.justPressed) {
      if (volume > 0) volume -= 10;
      
      osdStr = "Гучність: " + (String)volume;
      osdFrames = 60;
      Serial.println(osdStr);
    }

    vTaskDelay(100);
  }
}




void setup() {

  lilka::display.setSplash(splash_img);
  lilka::begin();

  disableCore0WDT();
  Serial.begin(115200);

  // Чисте колдунство
  lilka::audio.initPins();
  esp_i2s::i2s_config_t cfg = {
      .mode = (esp_i2s::i2s_mode_t)(esp_i2s::I2S_MODE_MASTER | esp_i2s::I2S_MODE_TX),
      .sample_rate = 44100,
      .bits_per_sample = esp_i2s::I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = esp_i2s::I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format =
          (esp_i2s::i2s_comm_format_t)(esp_i2s::I2S_COMM_FORMAT_STAND_I2S | esp_i2s::I2S_COMM_FORMAT_STAND_MSB),
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 2,
      .dma_buf_len = 1024,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .bits_per_chan = esp_i2s::I2S_BITS_PER_CHAN_16BIT,
  };
  i2s_driver_install(esp_i2s::I2S_NUM_0, &cfg, 0, NULL);
  i2s_zero_dma_buffer(esp_i2s::I2S_NUM_0);

}



void loop() {

  // Read /Video folder on SD
  char *_dirname = const_cast<char*>(BASE_DIR);
  char _dirpath[255];
  sprintf(_dirpath, "/%s/", BASE_DIR);
  size_t _numEntries = lilka::fileutils.getEntryCount(&SD, _dirpath);

  if (_numEntries == 0) {
    snprintf(_alert, sizeof(_alert), "Директорія порожня, \nабо сталася помилка \nчитання директорії \n%s", _dirname);
    showAlert(_alert, 5000);
    esp_restart();
  }
  lilka::Entry* entries = new lilka::Entry[_numEntries];
  int numEntries = lilka::fileutils.listDir(&SD, _dirpath, entries);
  if (_numEntries != numEntries) {
    snprintf(_alert, sizeof(_alert), "Не вдалося прочитати \nдиректорію %s", _dirname);
    showAlert(_alert, 5000);
    esp_restart();
  }

  // Show UI to select a video or exit
  char menuTitle[40];
  sprintf(menuTitle, "Директорія %s", BASE_DIR);
  lilka::Menu menu(menuTitle);

  for (int i = 0; i < numEntries; i++) {
      String filename = entries[i].name;
      String lowerCasedName = filename;
      lowerCasedName.toLowerCase();
      if (!lowerCasedName.endsWith(".bin")) menu.addItem(filename);
  }
  menu.addItem("<< Вихід", 0, lilka::colors::Red);

  // Wait for user's choice
  while (!menu.isFinished()) {
      menu.update();
      menu.draw(&buffer);
      lilka::display.drawCanvas(&buffer);
  }
  int index = menu.getCursor();
  lilka::MenuItem item;
  menu.getItem(index, &item);
  
  if (item.title == "<< Вихід") esp_restart();

  xTaskCreate(
    controllerTask,
    "controllerTask",
    2000,
    NULL,
    1,
    NULL);

  playVideoWithAudio((String)item.title);

}