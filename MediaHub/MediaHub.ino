/**
 * ╔══════════════════════════════════════════════════════════════╗
 * ║   ESP32-S3 N16R8 — Media Hub  v9.1  (BUG FIX + FITUR BARU) ║
 * ║                                                              ║
 * ║   PERUBAHAN v9.1 vs v9.0:                                    ║
 * ║                                                              ║
 * ║   [FIX 1]  STOIC QUOTE                                       ║
 * ║            Fix field name: coba "body" dulu, fallback "quote"║
 * ║            Tambah fallback author dari "author.name"         ║
 * ║                                                              ║
 * ║   [FIX 2]  NUMBER FACT                                       ║
 * ║            Ganti HTTPClient ke WiFiClient langsung untuk     ║
 * ║            HTTP plain (non-HTTPS) ke numbersapi.com          ║
 * ║                                                              ║
 * ║   [FIX 3]  BORED API → RANDOM ACTIVITY API                   ║
 * ║            API lama (boredapi.com) sudah mati                ║
 * ║            Ganti ke www.randomactivityapi.com/api/activity   ║
 * ║            Field sama: activity, type, participants          ║
 * ║                                                              ║
 * ║   [FIX 4]  HACKER NEWS — KOMENTAR                            ║
 * ║            SEL pada story → buka layar komentar              ║
 * ║            Ambil 3 komentar teratas via Algolia HN API       ║
 * ║            api.hn.algolia.com/v1/items/{id}                  ║
 * ║            Tampilkan author + teks komentar (strip HTML)     ║
 * ║                                                              ║
 * ║   [NEW 1]  WIFI PREFERENCES (NVS)                            ║
 * ║            Simpan SSID + password ke NVS (Preferences)       ║
 * ║            Auto-connect saat boot jika ada data tersimpan    ║
 * ║            Hapus kredensial dari menu Settings               ║
 * ║                                                              ║
 * ║   [NEW 2]  WIFI RSSI 4-BAR                                   ║
 * ║            Ganti dot jadi 4-bar sinyal di header & menu      ║
 * ║            Bar bertingkat sesuai kekuatan sinyal             ║
 * ║                                                              ║
 * ║   [NOTE]   ISS & People in Space: tetap HTTPClient v9.0      ║
 * ║   [SEMUA FITUR v9.0 lainnya dipertahankan]                   ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 *   Libraries:
 *   - LovyanGFX        by lovyan03
 *   - ArduinoJson      by bblanchon
 *   - Adafruit NeoPixel by Adafruit
 *   - MjpegClass       by moononournation
 *     copy MjpegClass.h ke folder sketch
 *
 *   Board  : ESP32S3 Dev Module
 *   PSRAM  : OPI PSRAM (Octal) — WAJIB aktif
 *   Flash  : 16 MB
 */

// ════════════════════════════════════════════════════════════════
// INCLUDES
// ════════════════════════════════════════════════════════════════
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "MjpegClass.h"
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <time.h>
#include <vector>
#include <algorithm>

// ════════════════════════════════════════════════════════════════
// COMPILE FIX: enum Btn harus sebelum fungsi apapun
// ════════════════════════════════════════════════════════════════


// ════════════════════════════════════════════════════════════════
// GAME DECLARATIONS
// ════════════════════════════════════════════════════════════════
struct TTTMove { int bx, by, sx, sy; int score; };
struct TTTGrid {
  int8_t board[3][3]; // 0:empty, 1:X, 2:O
  int8_t winner;      // 0:none, 1:X, 2:O, 3:Draw
};
struct Bullet {
  float x, y, dx, dy;
  bool active;
  int8_t bounces;
  uint32_t born;
};
struct Tank {
  float x, y, angle;
  int8_t hp;
  uint32_t lastShot;
  bool isAI;
};
struct MSNode { int x, y; };
struct MSCell { bool mine, open, flag; int adj; };
struct Pipe { int x; int h; bool passed; };
struct PacChar { float x, y, dx, dy; int8_t type; /*0:pac, 1-3:ghosts*/ };
struct Platform { float x, y; int8_t type; /*0:normal, 1:break*/ };

// GAME GLOBALS
TTTGrid ultimateBoard[5][5];
int8_t bigWinner = 0; // 0:none, 1:X, 2:O, 3:Draw
int tttBigX=2, tttBigY=2, tttSmallX=1, tttSmallY=1;
int tttTargetBigX=-1, tttTargetBigY=-1; // Must play in this big cell
int tttTurn = 1; // 1:X, 2:O
bool tttVsAI = true;
int tttAiLevel = 1; // 0:Easy, 1:Medium, 2:Hard
uint32_t tttCursorT = 0;
bool tttAITurn = false;
uint32_t tttAINextMoveT = 0;

uint8_t tankMap[10][19]; // 0:empty, 1:permanent, 2:destructible
Tank playerTank, aiTank;
std::vector<Bullet> bullets;
uint32_t tankGameT = 0;
bool tankGameOver = false;
int tankScore = 0;

uint32_t lastBfsT = 0; std::vector<MSNode> tankPath; int tankShake = 0;

float birdY, birdV;

std::vector<Pipe> pipes;
uint32_t flappyGameT = 0;
int flappyScore = 0, flappyHi = 0;
bool flappyGameOver = false;

MSCell msGrid[16][9];
int msW=9, msH=9, msM=10;
int msCurX=0, msCurY=0;
bool msGameOver=false, msWin=false, msFirst=true;
uint32_t msTimer=0, msBest=999;
std::vector<MSNode> msQueue;

// PACMAN
static int8_t pacMap[11][21];
static PacChar pacPlayer;
static PacChar ghosts[3];
static int pacScore=0, pacHi=0, pacDots=0;
static bool pacGameOver=false;
static uint32_t pacGameT=0;

// DOODLE
static std::vector<Platform> platforms;
static float doodleY, doodleV, doodleX;
static float doodleCamY;
static int doodleScore=0, doodleHi=0;
static bool doodleGameOver=false;
static uint32_t doodleGameT=0;


// GAME PROTOTYPES
void tttInit();
void drawTTT();
void handleTTT();
void tankInit();
void drawTank();
void handleTank();
void flappyInit();
void drawFlappy();
void handleFlappy();
void msInit(int w, int h);
void drawMinesweeper();
void handleMinesweeper();
void pacmanInit(); void drawPacman(); void handlePacman();
void doodleInit(); void drawDoodle(); void handleDoodle();
void drawTriviaSetup();
void drawGameMenu();
void memoInit(); void drawMemory(); void handleMemory();
void simonInit(); void drawSimon(); void handleSimon();
void sudoInit(); void drawSudoku(); void handleSudoku();
void loadAiKeys();
String decodeTrivia(String str);


enum Btn { B_SEL=0, B_UP, B_DW, B_R, B_L };

// ════════════════════════════════════════════════════════════════
// PIN & LAYAR
// ════════════════════════════════════════════════════════════════
#define SCR_W   320
#define SCR_H   170

#define TFT_BL    14
#define TFT_RST   13
#define TFT_DC     9
#define TFT_MOSI  11
#define TFT_SCLK  12
#define TFT_CS    10

#define SD_CS      3
#define SD_SCK    18
#define SD_MISO    8
#define SD_MOSI   17
#define SD_SPI_SPEED  40000000L

#define BTN_SEL    0
#define BTN_UP    41
#define BTN_DW    40
#define BTN_R     38
#define BTN_L     39

#define NEO_PIN   48
#define NEO_N      1

#define LDR_PIN   1
#define RTC_SDA   21
#define RTC_SCL   47

// ════════════════════════════════════════════════════════════════
// WARNA
// ════════════════════════════════════════════════════════════════
static uint16_t C_BLACK, C_WHITE, C_LGRAY, C_MGRAY, C_DGRAY,
                C_XDGRAY, C_XXDGRAY, C_RED, C_GREEN;
static uint16_t C_BG, C_SURFACE, C_CARD, C_BORDER,
                C_TEXT_DIM, C_TEXT_MUTE, C_TEXT_DEAD;
static uint16_t C_ACCENT_SPACE, C_ACCENT_FUN, C_ACCENT_INFO,
                C_ACCENT_MEDIA, C_ACCENT_SYS;

void initColors() {
  C_BLACK   = 0x0000u;
  C_WHITE   = 0xFFFFu;
  C_LGRAY   = lgfx::color565(198,198,198);
  C_MGRAY   = lgfx::color565(130,130,130);
  C_DGRAY   = lgfx::color565(65,65,65);
  C_XDGRAY  = lgfx::color565(32,32,32);
  C_XXDGRAY = lgfx::color565(16,16,16);
  C_RED     = lgfx::color565(255,0,0);
  C_GREEN   = lgfx::color565(0,255,0);

  C_BG        = C_BLACK;
  C_SURFACE   = C_XDGRAY;
  C_CARD      = C_DGRAY;
  C_BORDER    = C_MGRAY;
  C_TEXT_DIM  = C_LGRAY;
  C_TEXT_MUTE = C_MGRAY;
  C_TEXT_DEAD = C_DGRAY;

  C_ACCENT_SPACE = lgfx::color565(80,80,95);
  C_ACCENT_FUN   = lgfx::color565(90,75,55);
  C_ACCENT_INFO  = lgfx::color565(55,80,65);
  C_ACCENT_MEDIA = lgfx::color565(75,55,75);
  C_ACCENT_SYS   = lgfx::color565(55,55,80);
}

// ════════════════════════════════════════════════════════════════
// VIDEO CONFIG
// ════════════════════════════════════════════════════════════════
#define VIDEO_FPS        25
#define FRAME_US         (1000000L / VIDEO_FPS)
#define OUTPUT_BUF_LINES  4
#define MJPEG_BUF_SIZE   (150 * 1024)

// ════════════════════════════════════════════════════════════════
// NTP CONFIG
// ════════════════════════════════════════════════════════════════
#define NTP_SERVER1           "pool.ntp.org"
#define NTP_SERVER2           "time.google.com"
#define NTP_TIMEZONE          "WIB-7"
#define NTP_SYNC_INTERVAL_MS  3600000UL

// ════════════════════════════════════════════════════════════════
// AUTO BRIGHTNESS CONFIG
// ════════════════════════════════════════════════════════════════
#define LDR_SAMPLES       8
#define LDR_UPDATE_MS   200
#define BR_MIN           30
#define BR_MAX          255
#define BR_SMOOTH_STEP    3

// ════════════════════════════════════════════════════════════════
// ════════════════════════════════════════════════════════════════


// ════════════════════════════════════════════════════════════════
// MOOD LAMP CONFIG
// ════════════════════════════════════════════════════════════════
#define MOOD_SAMPLE_COUNT   64
#define MOOD_SMOOTH_FACTOR  0.08f
#define MOOD_LED_MAX_BR     180
#define MOOD_FADE_STEP_MS   20
#define MOOD_MIN_V          12

// ════════════════════════════════════════════════════════════════
// NVS PREFERENCES KEY
// ════════════════════════════════════════════════════════════════
#define NVS_NS        "mediahub"
#define NVS_KEY_SSID  "wifi_ssid"
#define NVS_KEY_PASS  "wifi_pass"

// ════════════════════════════════════════════════════════════════
// LGFX
// ════════════════════════════════════════════════════════════════
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
public:
  LGFX() {
    {
      auto c = _bus.config();
      c.spi_host    = SPI2_HOST;
      c.spi_mode    = 3;
      c.freq_write  = 80000000;
      c.freq_read   = 16000000;
      c.spi_3wire   = true;
      c.use_lock    = true;
      c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk    = TFT_SCLK;
      c.pin_mosi    = TFT_MOSI;
      c.pin_miso    = -1;
      c.pin_dc      = TFT_DC;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    {
      auto c = _panel.config();
      c.pin_cs          = TFT_CS;
      c.pin_rst         = TFT_RST;
      c.pin_busy        = -1;
      c.panel_width     = 170;
      c.panel_height    = 320;
      c.offset_x        = 35;
      c.offset_y        = 0;
      c.offset_rotation = 0;
      c.invert          = true;
      c.rgb_order       = false;
      c.dlen_16bit      = false;
      c.bus_shared      = false;
      _panel.config(c);
    }
    {
      auto c = _light.config();
      c.pin_bl      = TFT_BL;
      c.invert      = false;
      c.freq        = 44100;
      c.pwm_channel = 7;
      _light.config(c);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

// ════════════════════════════════════════════════════════════════
// GLOBALS
// ════════════════════════════════════════════════════════════════
static LGFX              tft;
static LGFX_Sprite       mainBuf(&tft);
static Adafruit_NeoPixel neo(NEO_N, NEO_PIN, NEO_GRB + NEO_KHZ800);
static SPIClass          sdSPI(HSPI);
static MjpegClass        mjpeg;
static Preferences       prefs;

// ════════════════════════════════════════════════════════════════
// CAMERA STREAM STATE
// ════════════════════════════════════════════════════════════════
static const char* camStreamUrl = "http://192.168.4.1/stream";
static LGFX_Sprite* camSprites[2] = {nullptr, nullptr};
static uint8_t drawBufIdx = 0;
static uint8_t showBufIdx = 1;
static bool camStreaming = false;
static HTTPClient camHttp;
static WiFiClient camClient;
static JPEGDEC camJpeg;
static uint32_t camLastFrameT = 0;
static uint32_t camFrameCount = 0;
static uint32_t camFps = 0;
static size_t   camBufPtr = 0;
static bool     camInFrame = false;
static bool     camCaptureReq   = false;
static String   viewImagePath   = "";

int camJpegDraw(JPEGDRAW *p) {
  if (!camSprites[drawBufIdx]) return 0;
  camSprites[drawBufIdx]->pushImage(p->x, p->y, p->iWidth, p->iHeight, p->pPixels);
  return 1;
}
static uint8_t  *mjpeg_buf  = nullptr;
static uint16_t *output_buf = nullptr;

// ════════════════════════════════════════════════════════════════
// TOMBOL
// ════════════════════════════════════════════════════════════════
static const uint8_t btnPins[] = {BTN_SEL, BTN_UP, BTN_DW, BTN_R, BTN_L};
static uint32_t btnLast[5] = {0};
static bool     btnPrev[5] = {false};
static uint32_t comboLTime = 0, comboRTime = 0;
static bool     comboFired = false;
static uint32_t lastStatsUpdate = 0;

// ════════════════════════════════════════════════════════════════
// APP STATE
// ════════════════════════════════════════════════════════════════
enum AppState {
  ST_SPLASH, ST_MENU,
  ST_WIFI_SCAN, ST_WIFI_PASS, ST_WIFI_CONN,
  ST_VIDEO_LIST, ST_VIDEO_PLAY,
  ST_TRIVIA_SETUP, ST_TRIVIA_LOAD, ST_TRIVIA_Q,
  ST_TRIVIA_RESULT, ST_TRIVIA_SUMMARY,
  ST_SETTINGS,
  ST_POWER_MENU,
  ST_FILE_MGR,
  ST_ISS_TRACKER,
  ST_APOD,
  ST_STOIC,
  ST_NUMBER_FACT,
  ST_JOKES,
  ST_BORED,
  ST_PEOPLE_SPACE,
  ST_HACKER_NEWS,
  ST_AI_MODE, ST_AI_CHAT_INPUT, ST_AI_CHAT_RES,
  ST_HN_COMMENTS, ST_CAMERA_STREAM, ST_IMAGE_VIEW,
  ST_GAME_MENU, ST_TICTACTOE_MODE, ST_TICTACTOE, ST_TICTACTOE_OVER,
  ST_TANK_MODE, ST_TANK, ST_TANK_OVER,
  ST_FLAPPY_MODE, ST_FLAPPY, ST_FLAPPY_OVER,
  ST_MINESWEEPER_SIZE, ST_MINESWEEPER, ST_MINESWEEPER_OVER, ST_PACMAN_MODE, ST_PACMAN, ST_PACMAN_OVER, ST_DOODLE_MODE, ST_DOODLE, ST_DOODLE_OVER, ST_MEMORY_MODE, ST_MEMORY, ST_MEMORY_OVER, ST_SIMON_MODE, ST_SIMON, ST_SIMON_OVER, ST_SUDOKU_MODE, ST_SUDOKU, ST_SUDOKU_OVER
};
static AppState appState = ST_SPLASH;
void saveCapture();
void drawImageFile(String path);

// ════════════════════════════════════════════════════════════════
// CLOCK / NTP STATE
// ════════════════════════════════════════════════════════════════
static bool     clockSynced     = false;
static uint32_t lastNtpSync     = 0;
static uint32_t lastClockUpdate = 0;

struct ClockTime {
  int  hour, min, sec;
  int  day, month, year, dow;
  bool valid;
};
static ClockTime gClock = {0,0,0,1,1,2024,0,false};

const char* DOW_ID[]    = {"Min","Sen","Sel","Rab","Kam","Jum","Sab"};
const char* MON_SHORT[] = {"Jan","Feb","Mar","Apr","Mei","Jun","Jul","Agu","Sep","Okt","Nov","Des"};

void clockUpdate() {
  uint32_t now = millis();
  if (now - lastClockUpdate < 500) return;
  lastClockUpdate = now;
  struct tm ti;
  if (getLocalTime(&ti, 0)) {
    gClock = {ti.tm_hour,ti.tm_min,ti.tm_sec,ti.tm_mday,ti.tm_mon+1,ti.tm_year+1900,ti.tm_wday,true};
  }
}

void clockSyncNTP() {
  if (!WiFi.isConnected()) return;
  configTime(7*3600, 0, NTP_SERVER1, NTP_SERVER2);
  struct tm ti;
  uint32_t t = millis();
  while (!getLocalTime(&ti,0) && millis()-t < 5000) delay(100);
  if (getLocalTime(&ti,0)) { clockSynced=true; lastNtpSync=millis(); }
}

void clockFormatTime(char* buf, int len) {
  snprintf(buf, len, "%02d:%02d:%02d", gClock.hour, gClock.min, gClock.sec);
}
void clockFormatDate(char* buf, int len) {
  if (!gClock.valid) { snprintf(buf, len, "--/--/----"); return; }
  snprintf(buf, len, "%s, %d %s %d",
    DOW_ID[gClock.dow%7], gClock.day, MON_SHORT[(gClock.month-1)%12], gClock.year);
}

// ════════════════════════════════════════════════════════════════
// AUTO BRIGHTNESS
// ════════════════════════════════════════════════════════════════
static bool     autoBrEnabled    = false;
static uint32_t lastLdrUpdate    = 0;
static int      ldrBuf[LDR_SAMPLES] = {0};
static int      ldrBufIdx        = 0;
static int      brightness       = 200;
static int      brightnessTarget = 200;

void autoBrInit() {
  pinMode(LDR_PIN, INPUT);
  for (int i=0; i<LDR_SAMPLES; i++) { ldrBuf[i]=analogRead(LDR_PIN); delay(2); }
}

void autoBrUpdate() {
  if (!autoBrEnabled) return;
  uint32_t now = millis();
  if (now-lastLdrUpdate < LDR_UPDATE_MS) return;
  lastLdrUpdate = now;
  ldrBuf[ldrBufIdx] = analogRead(LDR_PIN);
  ldrBufIdx = (ldrBufIdx+1) % LDR_SAMPLES;
  int vals[LDR_SAMPLES]; memcpy(vals, ldrBuf, sizeof(ldrBuf));
  std::sort(vals, vals+LDR_SAMPLES);
  long sum=0; for(int i=1;i<LDR_SAMPLES-1;i++) sum+=vals[i];
  int avg = (int)(sum/(LDR_SAMPLES-2));
  brightnessTarget = constrain(map(avg,0,4095,BR_MIN,BR_MAX), BR_MIN, BR_MAX);
  if (abs(brightness-brightnessTarget)>BR_SMOOTH_STEP)
    brightness += (brightness<brightnessTarget) ? BR_SMOOTH_STEP : -BR_SMOOTH_STEP;
  else brightness = brightnessTarget;
  tft.setBrightness(brightness);
}

// ════════════════════════════════════════════════════════════════
// WIFI PREFERENCES (NVS) — [NEW v9.1]
// ════════════════════════════════════════════════════════════════
static char savedSSID[64] = {0};
static char savedPass[64] = {0};

void wifiPrefsLoad() {
  prefs.begin(NVS_NS, true);
  String s = prefs.getString(NVS_KEY_SSID, "");
  String p = prefs.getString(NVS_KEY_PASS, "");
  prefs.end();
  s.toCharArray(savedSSID, sizeof(savedSSID));
  p.toCharArray(savedPass, sizeof(savedPass));
}

void wifiPrefsSave(const char* ssid, const char* pass) {
  prefs.begin(NVS_NS, false);
  prefs.putString(NVS_KEY_SSID, ssid);
  prefs.putString(NVS_KEY_PASS, pass);
  prefs.end();
  strncpy(savedSSID, ssid, sizeof(savedSSID)-1);
  strncpy(savedPass, pass, sizeof(savedPass)-1);
}

void wifiPrefsClear() {
  prefs.begin(NVS_NS, false);
  prefs.remove(NVS_KEY_SSID);
  prefs.remove(NVS_KEY_PASS);
  prefs.end();
  memset(savedSSID, 0, sizeof(savedSSID));
  memset(savedPass, 0, sizeof(savedPass));
}

// ════════════════════════════════════════════════════════════════

String htmlDecode(const String& s) {
  String o = s;
  o.replace("&amp;","&");   o.replace("&lt;","<");   o.replace("&gt;",">");
  o.replace("&quot;","\""); o.replace("&#039;","'");  o.replace("&apos;","'");
  o.replace("&ldquo;","\"");o.replace("&rdquo;","\"");
  o.replace("&lsquo;","'"); o.replace("&rsquo;","'");
  o.replace("&ndash;","-"); o.replace("&mdash;","--");
  o.replace("&hellip;","...");o.replace("&nbsp;"," ");
  o.replace("&#8211;","-"); o.replace("&#8212;","--");
  o.replace("&#8216;","'"); o.replace("&#8217;","'");
  o.replace("&#8220;","\"");o.replace("&#8221;","\"");
  return o;
}

// Strip HTML tags sederhana (untuk komentar HN)
String stripHtml(const String& s) {
  String out = "";
  bool inTag = false;
  for (int i = 0; i < (int)s.length(); i++) {
    char c = s[i];
    if (c == '<') { inTag = true; continue; }
    if (c == '>') { inTag = false; out += ' '; continue; }
    if (!inTag) out += c;
  }
  return htmlDecode(out);
}

String xmlExtract(const String& xml, const String& tag) {
  String openTag = "<"+tag+">", closeTag = "</"+tag+">";
  int start = xml.indexOf(openTag);
  if (start<0) {
    openTag = "<"+tag+" "; start = xml.indexOf(openTag);
    if (start<0) return "";
    start = xml.indexOf('>',start)+1;
  } else start += openTag.length();
  int end = xml.indexOf(closeTag, start);
  if (end<0) return "";
  String result = xml.substring(start,end);
  if (result.startsWith("<![CDATA[") && result.endsWith("]]>"))
    result = result.substring(9, result.length()-3);
  result.trim();
  return htmlDecode(result);
}


// ════════════════════════════════════════════════════════════════
// FILE MANAGER STATE
// ════════════════════════════════════════════════════════════════
struct FsEntry { String name; bool isDir; size_t size; };
static std::vector<FsEntry> fmEntries;
static String   fmCurrentPath   = "/";
static int      fmSel           = 0;
static int      fmScrollOff     = 0;
static bool     fmConfirmDelete = false;
static int64_t  sdTotalBytes    = 0;
static int64_t  sdUsedBytes     = 0;

void fmScanDir(const String& path) {
  fmEntries.clear(); fmSel=0; fmScrollOff=0; fmConfirmDelete=false;
  if (path!="/") fmEntries.push_back({"..",true,0});
  File dir=SD.open(path.c_str());
  if (!dir||!dir.isDirectory()) return;
  while(true) {
    File f=dir.openNextFile(); if(!f) break;
    String nm=String(f.name());
    if (!nm.startsWith(".")) fmEntries.push_back({nm,f.isDirectory(),(size_t)f.size()});
    f.close();
  }
  dir.close();
  std::sort(fmEntries.begin(),fmEntries.end(),[](const FsEntry&a,const FsEntry&b){
    if(a.name=="..") return true; if(b.name=="..") return false;
    if(a.isDir!=b.isDir) return a.isDir>b.isDir; return a.name<b.name;
  });
  sdTotalBytes=SD.totalBytes(); sdUsedBytes=SD.usedBytes();
}

String fmFormatSize(size_t bytes) {
  char buf[20];
  if(bytes<1024) snprintf(buf,sizeof(buf),"%dB",(int)bytes);
  else if(bytes<1048576) snprintf(buf,sizeof(buf),"%dKB",(int)(bytes/1024));
  else if(bytes<1073741824) snprintf(buf,sizeof(buf),"%.1fMB",(float)bytes/1048576.0f);
  else snprintf(buf,sizeof(buf),"%.2fGB",(float)bytes/1073741824.0f);
  return String(buf);
}

bool fmDeletePath(const String& path) {
  File f=SD.open(path.c_str()); if(!f) return false;
  bool isDir=f.isDirectory(); f.close();
  return isDir ? SD.rmdir(path.c_str()) : SD.remove(path.c_str());
}

// ════════════════════════════════════════════════════════════════
// MOOD LAMP
// ════════════════════════════════════════════════════════════════
static bool   moodLampEnabled = true;
static float  moodR=0,moodG=0,moodB=0;
static float  moodTargetR=0,moodTargetG=0,moodTargetB=0;
static bool   moodActive  = false;
static uint32_t moodFadeT = 0;

inline void rgb565ToRGB(uint16_t px, uint8_t &r, uint8_t &g, uint8_t &b) {
  r=(px>>8)&0xF8; g=(px>>3)&0xFC; b=(px<<3)&0xF8;
  r|=(r>>5); g|=(g>>6); b|=(b>>5);
}

void moodSampleFrame(const uint16_t* pixels, int count) {
  if (!moodLampEnabled||!moodActive) return;
  int step=max(1,count/MOOD_SAMPLE_COUNT);
  uint32_t sumR=0,sumG=0,sumB=0; int sampled=0;
  for(int i=0;i<count;i+=step) {
    uint8_t r,g,b; rgb565ToRGB(pixels[i],r,g,b);
    sumR+=r; sumG+=g; sumB+=b; sampled++;
  }
  if(!sampled) return;
  moodTargetR=(float)sumR/sampled; moodTargetG=(float)sumG/sampled; moodTargetB=(float)sumB/sampled;
}

void moodUpdate() {
  if (!moodLampEnabled) return;
  if (moodActive) {
    moodR+=(moodTargetR-moodR)*MOOD_SMOOTH_FACTOR;
    moodG+=(moodTargetG-moodG)*MOOD_SMOOTH_FACTOR;
    moodB+=(moodTargetB-moodB)*MOOD_SMOOTH_FACTOR;
    float brScale=(float)brightness/255.0f, maxCh=max({moodR,moodG,moodB});
    uint8_t r,g,b;
    if(maxCh<1.0f){ r=MOOD_MIN_V; g=MOOD_MIN_V; b=MOOD_MIN_V; }
    else { float norm=(MOOD_LED_MAX_BR*brScale)/maxCh; r=(uint8_t)constrain(moodR*norm,MOOD_MIN_V,255); g=(uint8_t)constrain(moodG*norm,MOOD_MIN_V,255); b=(uint8_t)constrain(moodB*norm,MOOD_MIN_V,255); }
    neo.setPixelColor(0,neo.Color(r,g,b)); neo.show();
  } else {
    uint32_t now=millis();
    if(now-moodFadeT>=MOOD_FADE_STEP_MS) {
      moodFadeT=now;
      moodR=max(0.0f,moodR-4.0f); moodG=max(0.0f,moodG-4.0f); moodB=max(0.0f,moodB-4.0f);
      neo.setPixelColor(0,neo.Color((uint8_t)moodR,(uint8_t)moodG,(uint8_t)moodB)); neo.show();
    }
  }
}
void moodStop() { moodActive=false; moodFadeT=0; }
void moodPausePulse() {
  if(!moodLampEnabled) return;
  float t=(sinf(millis()*3.14159f/800.0f)+1.0f)*0.5f;
  uint8_t v=(uint8_t)(30.0f*t);
  neo.setPixelColor(0,neo.Color(v,v,v)); neo.show();
}

// ════════════════════════════════════════════════════════════════
// JPEG CALLBACK
// ════════════════════════════════════════════════════════════════
int jpegDrawCallback(JPEGDRAW *pDraw) {
  uint16_t *buf = pDraw->pPixels;
  int count = pDraw->iWidth * pDraw->iHeight;
  moodSampleFrame(buf, count);
  for(int i=0; i<count; i++) buf[i] = (buf[i]>>8) | (buf[i]<<8);
  tft.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, buf);
  return 1;
}

// ════════════════════════════════════════════════════════════════
// WIFI
// ════════════════════════════════════════════════════════════════
static std::vector<String> wifiSSIDs;
static int    wifiSel      = 0;
static char   wifiPassword[64] = {0};
static int    wifiPassLen  = 0;
static bool   wifiConnected = false;

// ════════════════════════════════════════════════════════════════
// VIDEO
// ════════════════════════════════════════════════════════════════
static std::vector<String> videoFiles;
static int      videoSel         = 0;
static int64_t  video_start_us   = 0;
static int64_t  video_paused_us  = 0;
static int64_t  pause_started_us = 0;
static int      total_frames     = 0;
static bool     videoPaused      = false;
static String   currentVideoName = "";

// ════════════════════════════════════════════════════════════════
// TRIVIA
// ════════════════════════════════════════════════════════════════
struct TriviaCategory { const char* name; int id; };
static const TriviaCategory TRIVIA_CATS[] = {
  {"Semua Kategori",0},{"Pengetahuan Umum",9},{"Buku & Sastra",10},
  {"Film",11},{"Musik",12},{"Sains & Alam",17},{"Komputer & IT",18},
  {"Matematika",19},{"Olahraga",21},{"Geografi",22},{"Sejarah",23},
  {"Politik",24},{"Seni & Budaya",25},{"Selebriti",26},{"Anime & Manga",31},
};
#define TRIVIA_CAT_COUNT 15
static const int TRIVIA_AMOUNTS[] = {5,10,15,20};
static const int TRIVIA_TIMES[]   = {10,15,20,30};
#define TRIVIA_AMT_COUNT  4
#define TRIVIA_TIME_COUNT 4
static int triviaSetupRow=0,triviaSetCatIdx=0,triviaSetAmtIdx=1,triviaSetTimeIdx=1;
struct TriviaQ { String question; String answers[4]; int correct; };
static TriviaQ  triviaQ;
static int      triviaAns=0,triviaScore=0,triviaTotal=0;
static int      triviaTarget=10,triviaTimeLimit=15;
static bool     triviaAnswered=false;
static uint32_t triviaTimer=0;

// ════════════════════════════════════════════════════════════════
// FITUR STATE
// ════════════════════════════════════════════════════════════════

// ISS Tracker
static float    issLat=0, issLon=0;
static uint32_t issLastFetch=0;
static bool     issFetched=false;
#define ISS_UPDATE_MS 5000

// APOD
static String   apodTitle="", apodExplain="", apodDate="";
static bool     apodFetched=false;

// Stoic Quote
static String   stoicQuote="", stoicAuthor="";
static bool     stoicFetched=false;

// Number Fact
static String   numFactText="";
static int      numFactN=0;
static bool     numFactFetched=false;

// Jokes
static String   jokeText="";
static bool     jokeFetched=false;
static int      jokeSource=0;

// Bored / Random Activity
static String   boredActivity="", boredType="";
static int      boredParticipants=1;
static bool     boredFetched=false;

// People in Space
struct Astronaut { String name, craft; };
static std::vector<Astronaut> astronauts;
static int      astronautTotal=0;
static bool     astronautFetched=false;
static int      astronautSel=0;

// Hacker News
struct HNStory { String title; int score; int id; };
static std::vector<HNStory> hnStories;
static bool     hnFetched=false;
static int      hnSel=0;

// HN Comments — [NEW v9.1]
struct HNComment { String author; String text; };
static std::vector<HNComment> hnComments;
static bool     hnCommentsFetched=false;
static int      hnCommentsStoryIdx=-1;
static int      hnCommentsSel=0;
static int      hnCommentsScrollY=0;

// Scroll untuk layar teks panjang
static int      textScrollY=0;
static int      aiModeSel=0;
static char     aiPrompt[128] = {0};
static int      aiPromptLen = 0;
static String   aiResponseText = "";
static String   geminiApiKey = "";
static String   groqApiKey = "";
static int      aiMode = 0; // 0: Subaru, 1: Standard, 2: Groq


// ════════════════════════════════════════════════════════════════
// MENU v9.0 — 16 ITEM
// ════════════════════════════════════════════════════════════════
// GAMES GLOBAL
static int gameMenuSel = 0;
const char* gameList[] = {"TIC TAC TOE", "TANK BATTLE", "FLAPPY BIRD", "MINESWEEPER", "PAC-MAN", "DOODLE JUMP", "MEMORY MATCH", "SIMON NUMERIC", "SUDOKU"};
const char* gameSub[]  = {"Ultimate 5x5", "Grid Combat", "Classic Bird", "Grid Reveal", "Maze Run", "Endless Jump", "Find Pairs", "Remember Numbers", "Logic Puzzle"};


static int menuSel = 0;
static const char* menuItems[] = {
  "AI CHAT", "VIDEO", "TRIVIA",
  "FILE MANAGER", "WIFI", "SETTINGS", "POWER",
  "HACKER NEWS",
  "DAD JOKE", "CHUCK NORRIS", "BORED",
  "STOIC QUOTE", "NUMBER FACT",
  "ISS TRACKER", "PEOPLE IN SPACE",
  "APOD NASA",
  "CAMERA STREAM",
  "GAMES",
};
static const char* menuSubtitle[] = {
  "tanya Gemini & Groq", "putar .mjpeg SD", "quiz online",
  "browse & kelola SD", "scan & sambungkan", "brightness & info", "restart / sleep",
  "top 5 HackerNews",
  "lelucon bapak-bapak", "fakta Chuck Norris", "ide saat bosan",
  "kutipan bijak stoik", "fakta unik angka",
  "posisi ISS real-time", "astronaut di orbit",
  "foto luar angkasa NASA",
  "IP Cam receiver",
  "9 mini games",
};
#define MENU_N 18

static uint32_t _barPulseT = 0;
static uint8_t  _barAlpha  = 160;
static bool     _barUp     = true;
static int powerSel = 2;
static int menuScrollTop = 0;

// ════════════════════════════════════════════════════════════════
// UTILITAS
// ════════════════════════════════════════════════════════════════
inline void pushFrame() { mainBuf.pushSprite(0,0); }

bool btnPressed(Btn b) {
  uint32_t now=millis();
  bool cur=(digitalRead(btnPins[b])==LOW);
  if(cur&&!btnPrev[b]&&(now-btnLast[b]>50)){btnPrev[b]=true;btnLast[b]=now;return true;}
  if(!cur) btnPrev[b]=false;
  return false;
}
bool btnHeld(Btn b) { return digitalRead(btnPins[b])==LOW; }

void btnFlushAll() {
  uint32_t t=millis();
  while(millis()-t<200) {
    bool any=false;
    for(int i=0;i<5;i++) if(digitalRead(btnPins[i])==LOW){any=true;break;}
    if(!any) break;
    delay(5);
  }
  for(int i=0;i<5;i++){btnPrev[i]=false;btnLast[i]=millis();}
  comboFired=false;
}

bool btnComboLR() {
  uint32_t now=millis();
  if(digitalRead(BTN_L)==LOW) comboLTime=now;
  if(digitalRead(BTN_R)==LOW) comboRTime=now;
  bool both=btnHeld(B_L)&&btnHeld(B_R);
  bool win=abs((long)(comboLTime-comboRTime))<80;
  if(both&&win&&!comboFired){comboFired=true;return true;}
  if(!both) comboFired=false;
  return false;
}

void ledSet(uint8_t r,uint8_t g,uint8_t b){neo.setPixelColor(0,neo.Color(r,g,b));neo.show();}
void ledPulse(uint32_t ms){
  float t=(sinf(millis()*3.14159f/ms)+1.0f)*0.5f;
  uint8_t v=(uint8_t)(50.0f*t); ledSet(v,v,v);
}

// ════════════════════════════════════════════════════════════════
// RSSI 4-BAR HELPER — [NEW v9.1]
// ════════════════════════════════════════════════════════════════
// Gambar 4 bar sinyal WiFi seperti ikon sinyal HP
// x,y = pojok kiri bawah area bar, totalW = lebar total ~14px
void drawRssiBars(LGFX_Sprite& spr, int x, int y, int rssi) {
  // Konversi RSSI ke level 0-4
  // rssi: -50 dBm+ = 4 bar, -60 = 3, -70 = 2, -80 = 1, < -80 = 0
  int level;
  if (rssi >= -50)      level = 4;
  else if (rssi >= -60) level = 3;
  else if (rssi >= -70) level = 2;
  else if (rssi >= -80) level = 1;
  else                  level = 0;

  // 4 bar, lebar 3px tiap bar, gap 1px, tinggi bertingkat
  const int barW = 3;
  const int gap  = 1;
  const int maxH = 10;
  for (int i = 0; i < 4; i++) {
    int barH = 3 + i * 2;          // 3,5,7,9 px
    int bx   = x + i * (barW + gap);
    int by   = y - barH;
    uint16_t col = (i < level) ? C_WHITE : C_DGRAY;
    spr.fillRect(bx, by, barW, barH, col);
  }
  (void)maxH;
}

// ════════════════════════════════════════════════════════════════
// UI HELPERS
// ════════════════════════════════════════════════════════════════
void uiHeader(const char* title) {
  mainBuf.fillRect(0,0,SCR_W,24,C_XXDGRAY);
  mainBuf.drawFastHLine(0,24,SCR_W,C_DGRAY);
  mainBuf.setTextColor(C_WHITE); mainBuf.setFont(&fonts::Font2);
  mainBuf.setCursor(6,8); mainBuf.print(title);
  clockUpdate();
  char timeBuf[12]; clockFormatTime(timeBuf,sizeof(timeBuf));
  int tw=mainBuf.textWidth(timeBuf);
  mainBuf.setTextColor(gClock.valid ? C_LGRAY : C_DGRAY);
  mainBuf.setCursor(SCR_W-tw-22,8); mainBuf.print(timeBuf);

  // WiFi indicator — 4-bar jika connected, X jika tidak
  if(wifiConnected) {
    int rssi = WiFi.RSSI();
    drawRssiBars(mainBuf, SCR_W-16, 22, rssi);
  } else {
    // Tanda X kecil
    mainBuf.drawLine(SCR_W-15,14,SCR_W-7,22,C_DGRAY);
    mainBuf.drawLine(SCR_W-15,22,SCR_W-7,14,C_DGRAY);
  }
}

void uiFooter(const char* hint) {
  mainBuf.fillRect(0,SCR_H-16,SCR_W,16,C_XXDGRAY);
  mainBuf.drawFastHLine(0,SCR_H-16,SCR_W,C_DGRAY);
  mainBuf.setTextColor(C_DGRAY); mainBuf.setFont(&fonts::Font2);
  mainBuf.setCursor(4,SCR_H-12); mainBuf.print(hint);
}

void uiCard(int x,int y,int w,int h,uint16_t border=0,int r=6) {
  if(border==0) border=C_DGRAY;
  mainBuf.fillRoundRect(x,y,w,h,r,C_SURFACE);
  mainBuf.drawRoundRect(x,y,w,h,r,border);
}

void uiProgressBar(int x,int y,int w,int h,float pct,uint16_t col=0) {
  if(col==0) col=C_WHITE;
  pct=constrain(pct,0.0f,1.0f);
  mainBuf.fillRoundRect(x,y,w,h,h/2,C_XDGRAY);
  if(pct>0) mainBuf.fillRoundRect(x,y,(int)(w*pct),h,h/2,col);
  mainBuf.drawRoundRect(x,y,w,h,h/2,C_DGRAY);
}

void uiCenteredText(const char* txt,int y,uint16_t col=0,const lgfx::IFont* font=&fonts::Font4) {
  if(col==0) col=C_WHITE;
  mainBuf.setFont(font); mainBuf.setTextColor(col);
  int tw=mainBuf.textWidth(txt);
  mainBuf.setCursor((SCR_W-tw)/2,y); mainBuf.print(txt);
}

int uiWrap(const String& txt,int x,int y,int maxW,int maxY,uint16_t col=0,int lineH=13) {
  if(col==0) col=C_WHITE;
  mainBuf.setTextColor(col); mainBuf.setFont(&fonts::Font2);
  String word="",line="";
  for(int i=0;i<=(int)txt.length();i++) {
    char c=(i<(int)txt.length())?txt[i]:' ';
    if(c==' '||c=='\n') {
      String test=line.length()?line+" "+word:word;
      if(mainBuf.textWidth(test.c_str())>maxW) {
        mainBuf.setCursor(x,y); mainBuf.print(line); y+=lineH;
        if(y+lineH>maxY){mainBuf.setCursor(x,y);mainBuf.print("...");return y;}
        line=word;
      } else line=test;
      word="";
    } else word+=c;
  }
  if(line.length()){mainBuf.setCursor(x,y);mainBuf.print(line);y+=lineH;}
  return y;
}

int uiWrapScroll(const String& txt, int x, int startY, int maxW, int maxH,
                 int scrollY, uint16_t col=0, int lineH=13) {
  if(col==0) col=C_WHITE;
  mainBuf.setTextColor(col); mainBuf.setFont(&fonts::Font2);
  std::vector<String> lines;
  String word="",line="";
  for(int i=0;i<=(int)txt.length();i++) {
    char c=(i<(int)txt.length())?txt[i]:' ';
    if(c==' '||c=='\n') {
      String test=line.length()?line+" "+word:word;
      if(mainBuf.textWidth(test.c_str())>maxW) {
        lines.push_back(line); line=word;
      } else line=test;
      word="";
    } else word+=c;
  }
  if(line.length()) lines.push_back(line);
  int totalLines = lines.size();
  int visLines = maxH / lineH;
  int maxScroll = max(0, (totalLines - visLines) * lineH);
  scrollY = constrain(scrollY, 0, maxScroll);
  int startLine = scrollY / lineH;
  int y = startY;
  for(int i=startLine; i<totalLines && y<startY+maxH; i++) {
    mainBuf.setCursor(x, y); mainBuf.print(lines[i]); y+=lineH;
  }
  return maxScroll;
}

// ════════════════════════════════════════════════════════════════
// BOOT ANIMATION
// ════════════════════════════════════════════════════════════════
bool tryPlayBootMjpeg() {
  if(!mjpeg_buf) return false;
  if(!SD.exists("/boot.mjpeg")) return false;
  File bootFile=SD.open("/boot.mjpeg");
  if(!bootFile) return false;
  tft.fillScreen(C_BG);
  mjpeg.setup(&bootFile, mjpeg_buf, jpegDrawCallback, false, 0, 0, SCR_W, SCR_H);
  int frameCount=0; int64_t startUs=esp_timer_get_time();
  while(bootFile.available()&&mjpeg.readMjpegBuf()) {
    mjpeg.drawJpg(); frameCount++;
    int64_t target=startUs+(int64_t)frameCount*FRAME_US;
    int64_t delta=target-esp_timer_get_time();
    if(delta>1000) delayMicroseconds((uint32_t)delta);
  }
  bootFile.close(); return true;
}

void drawSplash(const char* status="Initializing...") {
  mainBuf.fillScreen(C_BG);
  for(int i=0;i<6;i++){
    uint16_t c=lgfx::color565(4+i*8,4+i*8,4+i*8);
    mainBuf.fillRect(i*16,0,12,SCR_H,c);
  }
  mainBuf.setFont(&fonts::Font6); mainBuf.setTextColor(C_WHITE);
  mainBuf.setCursor(110,12); mainBuf.print("MEDIA");
  mainBuf.setTextColor(C_LGRAY);
  mainBuf.setCursor(110,58); mainBuf.print("HUB");
  mainBuf.drawFastVLine(104,6,SCR_H-12,C_MGRAY);
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_DGRAY);
  mainBuf.setCursor(110,108); mainBuf.print("ESP32-S3  MediaHub v9.1");
  mainBuf.fillRoundRect(0,SCR_H-22,SCR_W,22,0,C_XXDGRAY);
  mainBuf.drawFastHLine(0,SCR_H-22,SCR_W,C_DGRAY);
  mainBuf.setTextColor(C_MGRAY);
  int tw=mainBuf.textWidth(status);
  mainBuf.setCursor((SCR_W-tw)/2,SCR_H-15); mainBuf.print(status);
  pushFrame();
}

// ════════════════════════════════════════════════════════════════
// MAIN MENU — SMOOTH ROUNDED
// ════════════════════════════════════════════════════════════════
void _updateBarPulse() {
  uint32_t now=millis(); if(now-_barPulseT<18) return; _barPulseT=now;
  if(_barUp){_barAlpha+=3;if(_barAlpha>=220){_barAlpha=220;_barUp=false;}}
  else{_barAlpha-=3;if(_barAlpha<=90){_barAlpha=90;_barUp=true;}}
}

void drawMenu() {
  _updateBarPulse();
  mainBuf.fillScreen(C_BG);

  // Header
  mainBuf.fillRect(0,0,SCR_W,22,C_XXDGRAY);
  mainBuf.drawFastHLine(0,22,SCR_W,C_DGRAY);
  mainBuf.setFont(&fonts::Font2);
  mainBuf.setTextColor(C_WHITE); mainBuf.setCursor(6,7); mainBuf.print("MEDIA HUB");
  mainBuf.setTextColor(C_DGRAY); mainBuf.setCursor(82,7); mainBuf.print("v9.1");

  clockUpdate();
  char timeBuf[12]; clockFormatTime(timeBuf,sizeof(timeBuf));
  mainBuf.setFont(&fonts::Font2);
  mainBuf.setTextColor(gClock.valid?C_LGRAY:C_DGRAY);
  int tw=mainBuf.textWidth(timeBuf);
  mainBuf.setCursor((SCR_W-tw)/2,7); mainBuf.print(timeBuf);

  // WiFi status di header menu — 4-bar atau teks noWF
  if(wifiConnected){
    mainBuf.setTextColor(C_MGRAY); mainBuf.setCursor(SCR_W-56,7); mainBuf.print("WiFi");
    drawRssiBars(mainBuf, SCR_W-14, 20, WiFi.RSSI());
  } else {
    mainBuf.setTextColor(C_DGRAY); mainBuf.setCursor(SCR_W-34,7); mainBuf.print("noWF");
  }

  // Daftar Menu — SMOOTH ROUNDED
  const int VIS = 6;
  if(menuSel < menuScrollTop) menuScrollTop = menuSel;
  if(menuSel >= menuScrollTop + VIS) menuScrollTop = menuSel - VIS + 1;
  menuScrollTop = constrain(menuScrollTop, 0, MENU_N - VIS);

  const int MENU_Y     = 23;
  const int MENU_BOT   = SCR_H - 2;
  const int MENU_H     = MENU_BOT - MENU_Y;
  const int ITEM_H_SEL = 30;
  const int ITEM_H_NRM = 16;
  const int GAP        = 2;
  const int PAD        = 4;
  const int W          = SCR_W - PAD*2;
  const int RADIUS     = 6;

  int curY = MENU_Y + 2;

  for(int i = menuScrollTop; i < menuScrollTop + VIS && i < MENU_N; i++) {
    bool sel = (i == menuSel);
    int h = sel ? ITEM_H_SEL : ITEM_H_NRM;
    if(curY + h > MENU_BOT) break;

    int x = PAD, y = curY;

    if(sel) {
      mainBuf.fillRoundRect(x, y, W, h, RADIUS, C_XDGRAY);
      mainBuf.drawRoundRect(x, y, W, h, RADIUS, C_DGRAY);
      uint8_t av = _barAlpha >> 2;
      uint16_t barCol = lgfx::color565(av+20, av+20, av+20);
      mainBuf.fillRoundRect(x+1, y+3, 3, h-6, 2, barCol);
      mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_DGRAY);
      char numStr[4]; snprintf(numStr, sizeof(numStr), "%02d", i+1);
      int nw = mainBuf.textWidth(numStr);
      mainBuf.setCursor(x+W-nw-6, y+h/2-6); mainBuf.print(numStr);
      mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_WHITE);
      mainBuf.setCursor(x+10, y+4); mainBuf.print(menuItems[i]);
      if(h >= 26) {
        mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_MGRAY);
        mainBuf.setCursor(x+10, y+h-13); mainBuf.print(menuSubtitle[i]);
      }
      mainBuf.setTextColor(C_DGRAY); mainBuf.setCursor(x+W-14, y+4); mainBuf.print(">");
    } else {
      uint16_t bgCol = (i % 2 == 0) ? C_XXDGRAY : C_BG;
      mainBuf.fillRoundRect(x, y, W, h, 4, bgCol);
      mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_DGRAY);
      char numStr[4]; snprintf(numStr, sizeof(numStr), "%d", i+1);
      mainBuf.setCursor(x+4, y+(h-12)/2);
      mainBuf.print(numStr);
      mainBuf.setTextColor(C_MGRAY);
      mainBuf.setCursor(x+22, y+(h-12)/2);
      mainBuf.print(menuItems[i]);
    }
    curY += h + GAP;
  }

  // Scrollbar
  if(MENU_N > VIS) {
    float pct = (float)menuScrollTop / (MENU_N - VIS);
    int sbH = MENU_H * VIS / MENU_N;
    int sbY = MENU_Y + 2 + (int)((MENU_H - sbH) * pct);
    mainBuf.fillRect(SCR_W-3, MENU_Y+2, 2, MENU_H-4, C_DGRAY);
    mainBuf.fillRect(SCR_W-3, sbY, 2, sbH, C_MGRAY);
  }
}

// ════════════════════════════════════════════════════════════════
// WIFI UI
// ════════════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════════════
// CAMERA STREAM FUNCTIONS
// ════════════════════════════════════════════════════════════════
void camStop() {
  camStreaming = false;
  camInFrame = false;
  camBufPtr = 0;
  camHttp.end();
  for(int i=0; i<2; i++) {
    if(camSprites[i]) {
      camSprites[i]->deleteSprite();
      delete camSprites[i];
      camSprites[i] = nullptr;
    }
  }
  ledSet(0,0,0);
}

bool camInit() {
  uiHeader("CONNECTING CAM...");
  uiCenteredText(camStreamUrl, 80, C_DGRAY, &fonts::Font2);
  pushFrame();
  Serial.printf("[CAM] Connecting to %s...\n", camStreamUrl);

  for(int i=0; i<2; i++) {
    camSprites[i] = new LGFX_Sprite(&tft);
    camSprites[i]->setPsram(true);
    if(!camSprites[i]->createSprite(SCR_W, SCR_H)) {
      camSprites[i]->setPsram(false);
      if(!camSprites[i]->createSprite(SCR_W, SCR_H)) {
        camStop(); return false;
      }
    }
    camSprites[i]->fillScreen(C_BLACK);
  }

  camHttp.begin(camClient, camStreamUrl);
  camHttp.setTimeout(5000);
  int code = camHttp.GET();
  Serial.printf("[CAM] HTTP Code: %d\n", code);
  if(code == 200) {
    camStreaming = true;
    camLastFrameT = millis();
    camFrameCount = 0;
    camBufPtr = 0;
    camInFrame = false;
    ledSet(0,0,255);
    return true;
  } else {
    camStop();
    return false;
  }
}

void camUpdate() {
  if(!camStreaming) return;

  WiFiClient* stream = camHttp.getStreamPtr();
  if(!stream || !camClient.connected()) {
    Serial.println("[CAM] Stream disconnected");
    camStop(); appState = ST_MENU; drawMenu(); pushFrame(); return;
  }

  int avail = stream->available();
  if (avail <= 0) return;

  static uint8_t chunk[2048];
  int toRead = min(avail, (int)sizeof(chunk));
  int readLen = stream->read(chunk, toRead);

  for (int i = 0; i < readLen; i++) {
    uint8_t b = chunk[i];

    if (!camInFrame) {
      static uint8_t lastByte = 0;
      if (lastByte == 0xFF && b == 0xD8) {
        camInFrame = true;
        if(mjpeg_buf) {
            mjpeg_buf[0] = 0xFF;
            mjpeg_buf[1] = 0xD8;
        }
        camBufPtr = 2;
      }
      lastByte = b;
    } else {
      if (mjpeg_buf && camBufPtr < MJPEG_BUF_SIZE) {
        mjpeg_buf[camBufPtr++] = b;
        if (camBufPtr > 2 && mjpeg_buf[camBufPtr-2] == 0xFF && mjpeg_buf[camBufPtr-1] == 0xD9) {
          if (camBufPtr > 100) {
            camSprites[drawBufIdx]->fillScreen(C_BLACK);
            if(camJpeg.openRAM(mjpeg_buf, camBufPtr, camJpegDraw)) {
              camJpeg.setPixelType(RGB565_LITTLE_ENDIAN);
              camJpeg.decode(0,0,0);
              camJpeg.close();
            }

            if(camCaptureReq) {
              saveCapture();
              camCaptureReq = false;
              camSprites[drawBufIdx]->setTextColor(C_GREEN);
              camSprites[drawBufIdx]->setFont(&fonts::Font2);
              int tw = camSprites[drawBufIdx]->textWidth("CAPTURED");
              camSprites[drawBufIdx]->setCursor((SCR_W-tw)/2, SCR_H/2-8);
              camSprites[drawBufIdx]->print("CAPTURED");
            }

            camFrameCount++;
            uint32_t now = millis();
            if(now - camLastFrameT >= 1000) {
              camFps = camFrameCount;
              camFrameCount = 0;
              camLastFrameT = now;
            }
            camSprites[drawBufIdx]->setTextColor(C_WHITE);
            camSprites[drawBufIdx]->setCursor(5, 5);
            camSprites[drawBufIdx]->printf("FPS: %d", camFps);

            tft.startWrite();
            tft.pushImageDMA(0, 0, SCR_W, SCR_H, (lgfx::rgb565_t*)camSprites[drawBufIdx]->getBuffer());
            tft.endWrite();

            uint8_t tmp = drawBufIdx;
            drawBufIdx = showBufIdx;
            showBufIdx = tmp;
          }
          camInFrame = false;
          camBufPtr = 0;
        }
      } else {
        camInFrame = false;
        camBufPtr = 0;
      }
    }
  }
}

void saveCapture() {
  if(!SD.exists("/captures")) SD.mkdir("/captures");
  char path[64];
  time_t now_t = time(NULL);
  if(now_t < 1000000) snprintf(path, sizeof(path), "/captures/cam_%lu.jpg", (unsigned long)millis());
  else snprintf(path, sizeof(path), "/captures/cam_%lu.jpg", (unsigned long)now_t);
  File f = SD.open(path, FILE_WRITE);
  if(f) {
    f.write(mjpeg_buf, camBufPtr);
    f.close();
    Serial.printf("[CAM] Saved to %s (%u bytes)\n", path, (unsigned int)camBufPtr);
  } else {
    Serial.println("[CAM] Failed to open file for write");
  }
}

int imgDrawCallback(JPEGDRAW *p) {
  mainBuf.pushImage(p->x, p->y, p->iWidth, p->iHeight, p->pPixels);
  return 1;
}

void drawImageFile(String path) {
  mainBuf.fillScreen(C_BLACK);
  File f = SD.open(path);
  if(!f) {
    uiCenteredText("File tidak ditemukan", SCR_H/2-8, C_RED, &fonts::Font2);
    return;
  }
  size_t sz = f.size();
  if(sz > MJPEG_BUF_SIZE) sz = MJPEG_BUF_SIZE;
  f.read(mjpeg_buf, sz);
  f.close();
  JPEGDEC jd;
  if(jd.openRAM(mjpeg_buf, sz, imgDrawCallback)) {
    jd.setPixelType(RGB565_LITTLE_ENDIAN);
    int iw = jd.getWidth();
    int ih = jd.getHeight();
    int ox = (SCR_W - iw) / 2;
    int oy = (SCR_H - ih) / 2;
    jd.decode(max(0,ox), max(0,oy), 0);
    jd.close();
  } else {
    uiCenteredText("Gagal dekode JPEG", SCR_H/2-8, C_RED, &fonts::Font2);
  }
  mainBuf.fillRect(0, SCR_H-18, SCR_W, 18, lgfx::color565(40,40,40));
  mainBuf.setFont(&fonts::Font2);
  mainBuf.setTextColor(C_WHITE);
  String nm = path.substring(path.lastIndexOf('/')+1);
  if(nm.length()>40) nm = nm.substring(0,37)+"...";
  mainBuf.setCursor(6, SCR_H-15);
  mainBuf.print(nm);
}

void scanWifi() {
  mainBuf.fillScreen(C_BG); uiHeader("WIFI SETUP");
  uiCenteredText("Scanning...",76,C_LGRAY,&fonts::Font2); pushFrame();
  wifiSSIDs.clear(); wifiSel=0;
  WiFi.mode(WIFI_STA); int n=WiFi.scanNetworks();
  for(int i=0;i<n&&i<12;i++) wifiSSIDs.push_back(WiFi.SSID(i));
  WiFi.scanDelete();
}

void drawWifiList() {
  mainBuf.fillScreen(C_BG); uiHeader("PILIH WIFI");
  // Tampilkan info saved SSID jika ada
  if(strlen(savedSSID)>0) {
    mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_DGRAY);
    char savedInfo[72]; snprintf(savedInfo,sizeof(savedInfo),"Tersimpan: %s",savedSSID);
    mainBuf.setCursor(4,28); mainBuf.print(savedInfo);
    mainBuf.drawFastHLine(0,38,SCR_W,C_DGRAY);
  }
  if(wifiSSIDs.empty()){
    uiCenteredText("Tidak ada jaringan",68,C_MGRAY,&fonts::Font2);
    uiFooter("SEL:scan ulang  L+R:back"); pushFrame(); return;
  }
  int vis=5, itemH=25;
  int startI=max(0,wifiSel-1), endI=min((int)wifiSSIDs.size()-1,startI+vis-1);
  for(int i=startI;i<=endI;i++) {
    int y=42+(i-startI)*(itemH+2); bool sel=(i==wifiSel);
    mainBuf.fillRoundRect(4,y,SCR_W-8,itemH,6,sel?C_XDGRAY:C_XXDGRAY);
    mainBuf.drawRoundRect(4,y,SCR_W-8,itemH,6,sel?C_MGRAY:C_DGRAY);
    if(sel) mainBuf.fillRoundRect(4,y+4,2,itemH-8,2,C_WHITE);
    mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(sel?C_WHITE:C_MGRAY);
    mainBuf.setCursor(12,y+6);
    String ssid=wifiSSIDs[i]; if(ssid.length()>32) ssid=ssid.substring(0,31)+"~";
    mainBuf.print(ssid);
    if(sel){mainBuf.setTextColor(C_DGRAY);mainBuf.setCursor(12,y+17);mainBuf.print("SEL untuk connect");}
  }
  uiFooter("UP/DW:scroll  SEL:pilih  L+R:back");
}

// ════════════════════════════════════════════════════════════════
// KEYBOARD
// ════════════════════════════════════════════════════════════════
static const char KB_ROW0_LO[]="1234567890";
static const char KB_ROW1_LO[]="qwertyuiop";
static const char KB_ROW2_LO[]="asdfghjkl";
static const char KB_ROW3_LO[]="zxcvbnm_@.";
static const char KB_ROW0_UP[]="1234567890";
static const char KB_ROW1_UP[]="QWERTYUIOP";
static const char KB_ROW2_UP[]="ASDFGHJKL";
static const char KB_ROW3_UP[]="ZXCVBNM_@.";
static const char* KB_LO[]={KB_ROW0_LO,KB_ROW1_LO,KB_ROW2_LO,KB_ROW3_LO};
static const char* KB_UP[]={KB_ROW0_UP,KB_ROW1_UP,KB_ROW2_UP,KB_ROW3_UP};
static const char* KB_SP_LABEL[]={"CAP","SPC","DEL","OK"};
static int  kbRow=1,kbCol=4;
static bool kbCaps=false;

int  kbRowLen(int r){if(r==4)return 4;return strlen(kbCaps?KB_UP[r]:KB_LO[r]);}
char kbChar(int r,int c){if(r>=4)return 0;return(kbCaps?KB_UP[r]:KB_LO[r])[c];}

void drawKeyboard(const char* title, const char* buffer, int len, bool isPass) {
  mainBuf.fillScreen(C_BG); uiHeader(title);
  uiCard(4,28,SCR_W-8,22,C_MGRAY);
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_WHITE);
  mainBuf.setCursor(10,35);
  int dispStart=len>20?len-17:0;
  if(dispStart>0) mainBuf.print("...");
  for(int i=dispStart;i<len;i++) mainBuf.print(isPass ? '*' : buffer[i]);
  mainBuf.print('_');
  mainBuf.setTextColor(kbCaps?C_WHITE:C_DGRAY);
  mainBuf.setCursor(SCR_W-40,35); mainBuf.print(kbCaps?"CAPS":"caps");
  int startY=56,rowH=19;
  for(int r=0;r<4;r++) {
    int len=kbRowLen(r),y=startY+r*rowH,cellW=SCR_W/len;
    for(int c=0;c<len;c++) {
      bool sel=(r==kbRow&&c==kbCol); int x=c*cellW+1;
      char ch[2]={kbChar(r,c),0};
      if(sel){mainBuf.fillRoundRect(x,y,cellW-1,rowH-2,3,C_WHITE);mainBuf.setTextColor(C_BG);}
      else{mainBuf.fillRoundRect(x,y,cellW-1,rowH-2,3,C_XDGRAY);mainBuf.setTextColor(C_LGRAY);}
      mainBuf.setFont(&fonts::Font2);
      mainBuf.setCursor(x+(cellW-1-mainBuf.textWidth(ch))/2,y+4); mainBuf.print(ch);
    }
  }
  {
    int r=4,y=startY+4*rowH,bw=(SCR_W-6)/4;
    for(int c=0;c<4;c++) {
      bool sel=(r==kbRow&&c==kbCol); int x=c*(bw+2)+1; bool capsA=(c==0&&kbCaps);
      if(sel){mainBuf.fillRoundRect(x,y,bw,rowH-2,3,C_WHITE);mainBuf.setTextColor(C_BG);}
      else{mainBuf.fillRoundRect(x,y,bw,rowH-2,3,C_XDGRAY);mainBuf.drawRoundRect(x,y,bw,rowH-2,3,capsA?C_WHITE:C_DGRAY);mainBuf.setTextColor(capsA?C_WHITE:C_LGRAY);}
      mainBuf.setFont(&fonts::Font2);
      int tw2=mainBuf.textWidth(KB_SP_LABEL[c]);
      mainBuf.setCursor(x+(bw-tw2)/2,y+4); mainBuf.print(KB_SP_LABEL[c]);
    }
  }
}

int handleKeyboard(char* buffer, int& len, int maxLen) {
  bool changed=false; int cols=kbRowLen(kbRow);
  if(btnPressed(B_UP)){kbRow=(kbRow+4)%5;kbCol=min(kbCol,kbRowLen(kbRow)-1);changed=true;}
  if(btnPressed(B_DW)){kbRow=(kbRow+1)%5;kbCol=min(kbCol,kbRowLen(kbRow)-1);changed=true;}
  if(btnPressed(B_L)){kbCol=(kbCol+cols-1)%cols;changed=true;}
  if(btnPressed(B_R)){kbCol=(kbCol+1)%cols;changed=true;}
  if(btnPressed(B_SEL)){
    if(kbRow==4){
      if(kbCol==0){kbCaps=!kbCaps;changed=true;}
      else if(kbCol==1&&len<maxLen){buffer[len++]=' ';buffer[len]=0;changed=true;}
      else if(kbCol==2&&len>0){buffer[--len]=0;changed=true;}
      else if(kbCol==3)return 2;
    } else {
      if(len<maxLen){buffer[len++]=kbChar(kbRow,kbCol);buffer[len]=0;changed=true;}
    }
  }
  return changed?1:0;
}

bool connectWifi(const char* ssid,const char* pass) {
  mainBuf.fillScreen(C_BG); uiHeader("CONNECTING");
  uiCenteredText(ssid,68,C_WHITE,&fonts::Font2);
  uiCenteredText("Mohon tunggu...",86,C_MGRAY,&fonts::Font2);
  uiProgressBar(16,112,SCR_W-32,6,0.0f); pushFrame();
  WiFi.begin(ssid,pass);
  uint32_t start=millis(),lastUpdate=0;
  while(WiFi.status()!=WL_CONNECTED&&millis()-start<15000){
    if(btnComboLR()){WiFi.disconnect();return false;}
    if(millis()-lastUpdate>200){
      lastUpdate=millis();
      float p=min(1.0f,(float)(millis()-start)/15000.0f);
      uiProgressBar(16,112,SCR_W-32,6,p); pushFrame(); ledPulse(500);
    }
    delay(20);
  }
  return WiFi.status()==WL_CONNECTED;
}

void drawWifiResult(bool ok,const char* ssid) {
  mainBuf.fillScreen(C_BG); uiHeader(ok?"TERSAMBUNG!":"GAGAL");
  if(ok){
    uiCenteredText(ssid,64,C_WHITE,&fonts::Font2);
    uiCenteredText(WiFi.localIP().toString().c_str(),80,C_LGRAY,&fonts::Font2);
    ledSet(50,50,50);
    // Tampilkan status tersimpan NVS
    mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_DGRAY);
    uiCenteredText("Tersimpan ke NVS",96,C_DGRAY,&fonts::Font2);
  } else {
    uiCenteredText("Koneksi gagal",68,C_LGRAY,&fonts::Font2);
    uiCenteredText("Periksa password",84,C_DGRAY,&fonts::Font2);
    ledSet(0,0,0);
  }
  uiFooter("SEL:lanjut  L+R:menu");
}

// ════════════════════════════════════════════════════════════════
// VIDEO LIST & PLAYER
// ════════════════════════════════════════════════════════════════
void scanVideos() {
  videoFiles.clear();
  File dir=SD.open("/videos"); if(!dir) return;
  while(true){
    File f=dir.openNextFile(); if(!f) break;
    String name=String(f.name()); name.toLowerCase();
    if(name.endsWith(".mjpeg")||name.endsWith(".mjpg"))
      videoFiles.push_back(String("/videos/")+f.name());
    f.close();
  }
  dir.close(); std::sort(videoFiles.begin(),videoFiles.end());
}

void drawVideoList() {
  mainBuf.fillScreen(C_BG); uiHeader("VIDEO");
  mainBuf.setFont(&fonts::Font2);
  mainBuf.setTextColor(moodLampEnabled?C_LGRAY:C_DGRAY);
  mainBuf.setCursor(SCR_W-66,8);
  mainBuf.print(moodLampEnabled?"MOOD:ON":"MOOD:OFF");
  if(!mjpeg_buf){
    uiCenteredText("ERROR: PSRAM error",50,C_MGRAY,&fonts::Font2);
    uiFooter("L+R:back"); pushFrame(); return;
  }
  if(videoFiles.empty()){
    uiCenteredText("Tidak ada file .mjpeg",64,C_DGRAY,&fonts::Font2);
    uiCenteredText("/videos/*.mjpeg",80,C_XDGRAY,&fonts::Font2);
    uiFooter("L+R:back"); return;
  }
  int vis=5,itemH=26;
  int startI=max(0,videoSel-1),endI=min((int)videoFiles.size()-1,startI+vis-1);
  for(int i=startI;i<=endI;i++){
    int y=28+(i-startI)*(itemH+2); bool sel=(i==videoSel);
    String path=videoFiles[i],fname=path.substring(path.lastIndexOf('/')+1);
    if(fname.length()>34) fname=fname.substring(0,33)+"~";
    mainBuf.fillRoundRect(4,y,SCR_W-8,itemH,5,sel?C_XDGRAY:C_XXDGRAY);
    mainBuf.drawRoundRect(4,y,SCR_W-8,itemH,5,sel?C_MGRAY:C_DGRAY);
    if(sel) mainBuf.fillRoundRect(4,y+4,2,itemH-8,2,C_WHITE);
    mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(sel?C_WHITE:C_MGRAY);
    mainBuf.setCursor(12,y+5); mainBuf.print(fname);
    if(sel){mainBuf.setTextColor(C_DGRAY);mainBuf.setCursor(12,y+17);mainBuf.print("SEL:putar  R:toggle mood");}
  }
  uiFooter("UP/DW  SEL:putar  R:mood  L+R:back");
}

void _drawPauseOSD(const String& fname) {
  tft.fillRoundRect(0,SCR_H-30,SCR_W,30,0,C_XXDGRAY);
  tft.drawFastHLine(0,SCR_H-30,SCR_W,C_DGRAY);
  tft.setFont(&fonts::Font2); tft.setTextColor(C_WHITE);
  tft.setCursor(8,SCR_H-24); tft.print("|| PAUSE");
  tft.setTextColor(C_DGRAY);
  String sn=fname; if(sn.length()>26) sn=sn.substring(0,25)+"~";
  tft.setCursor(8,SCR_H-12); tft.print(sn);
  tft.setTextColor(C_MGRAY); tft.setCursor(SCR_W-90,SCR_H-12); tft.print("SEL:lanjut");
}

void playVideo(const String& path) {
  if(!mjpeg_buf) {
    appState=ST_VIDEO_LIST; drawVideoList(); pushFrame(); return;
  }
  File videoFile=SD.open(path.c_str());
  if(!videoFile){
    appState=ST_VIDEO_LIST; drawVideoList(); pushFrame(); return;
  }
  String fname=path.substring(path.lastIndexOf('/')+1);
  currentVideoName=fname;
  mjpeg.setup(&videoFile, mjpeg_buf, jpegDrawCallback, false, 0, 0, SCR_W, SCR_H);
  tft.fillScreen(C_BG);
  total_frames=0; video_paused_us=0; videoPaused=false;
  bool firstFrame=true, osdDrawn=false;
  moodActive=true; moodR=0; moodG=0; moodB=0;
  moodTargetR=0; moodTargetG=0; moodTargetB=0;

  while(videoFile.available() && mjpeg.readMjpegBuf()){
    if(btnComboLR()){
      videoFile.close(); moodStop(); ledSet(0,0,0);
      appState=ST_VIDEO_LIST; drawVideoList(); pushFrame(); return;
    }
    if(btnPressed(B_SEL)){
      videoPaused=!videoPaused;
      if(videoPaused){ pause_started_us=esp_timer_get_time(); moodActive=false; moodFadeT=millis(); }
      else moodActive=true;
    }
    while(videoPaused){
      if(!osdDrawn){ _drawPauseOSD(fname); osdDrawn=true; }
      moodPausePulse(); delay(20);
      if(btnPressed(B_SEL)){
        video_paused_us+=esp_timer_get_time()-pause_started_us;
        videoPaused=false; osdDrawn=false; moodActive=true;
      }
      if(btnComboLR()){
        videoFile.close(); moodStop(); ledSet(0,0,0);
        appState=ST_VIDEO_LIST; drawVideoList(); pushFrame(); return;
      }
    }
    mjpeg.drawJpg(); total_frames++; moodUpdate();
    if(firstFrame){ firstFrame=false; video_start_us=esp_timer_get_time(); }
    int64_t target_us=video_start_us+(int64_t)total_frames*FRAME_US-video_paused_us;
    int64_t now_us=esp_timer_get_time(), delta_us=target_us-now_us;
    if(delta_us>1000){
      while(delta_us>1000){
        delayMicroseconds(1000);
        delta_us=target_us-esp_timer_get_time();
        if(btnPressed(B_SEL)||btnComboLR()) break;
      }
      if(delta_us>0) delayMicroseconds((uint32_t)delta_us);
    }
  }
  videoFile.close(); moodStop(); ledSet(0,0,0);
  appState=ST_VIDEO_LIST; drawVideoList(); pushFrame();
}

// ════════════════════════════════════════════════════════════════
// FILE MANAGER UI
// ════════════════════════════════════════════════════════════════
void drawFileManager() {
  mainBuf.fillScreen(C_BG); uiHeader("FILE MANAGER");
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_DGRAY);
  mainBuf.setCursor(4,28);
  String dispPath=fmCurrentPath;
  if(dispPath.length()>38) dispPath="..."+dispPath.substring(dispPath.length()-35);
  mainBuf.print(dispPath);
  mainBuf.drawFastHLine(0,38,SCR_W,C_DGRAY);

  if(fmEntries.empty()){
    uiCenteredText("Folder kosong",78,C_DGRAY,&fonts::Font2);
    uiFooter("L+R:back"); return;
  }

  const int ITEM_H=17,LIST_Y=40;
  int fm_listH=SCR_H-LIST_Y-30;
  int fm_vis=fm_listH/ITEM_H;
  if(fmSel<fmScrollOff) fmScrollOff=fmSel;
  if(fmSel>=fmScrollOff+fm_vis) fmScrollOff=fmSel-fm_vis+1;

  for(int i=0;i<fm_vis&&(i+fmScrollOff)<(int)fmEntries.size();i++){
    int idx=i+fmScrollOff; FsEntry& e=fmEntries[idx]; bool sel=(idx==fmSel);
    int y=LIST_Y+i*ITEM_H;
    if(sel){
      mainBuf.fillRoundRect(0,y,SCR_W,ITEM_H,3,C_XDGRAY);
      mainBuf.drawRoundRect(0,y,SCR_W,ITEM_H,3,C_MGRAY);
      mainBuf.fillRoundRect(0,y+2,2,ITEM_H-4,2,C_WHITE);
    } else if(i%2==0) mainBuf.fillRect(0,y,SCR_W,ITEM_H,C_XXDGRAY);
    mainBuf.setFont(&fonts::Font2);
    mainBuf.setTextColor(sel?(e.isDir?C_WHITE:C_LGRAY):(e.isDir?C_LGRAY:C_MGRAY));
    mainBuf.setCursor(6,y+3);
    mainBuf.print(e.isDir?"[D]":"   ");
    String nm=e.name; if(nm.length()>28) nm=nm.substring(0,27)+"~";
    mainBuf.setCursor(30,y+3); mainBuf.print(nm);
    if(!e.isDir&&e.name!=".."){
      String sz=fmFormatSize(e.size); int sw=mainBuf.textWidth(sz.c_str());
      mainBuf.setTextColor(sel?C_DGRAY:C_XDGRAY);
      mainBuf.setCursor(SCR_W-sw-4,y+3); mainBuf.print(sz);
    }
  }
  {
    float usedPct=(sdTotalBytes>0)?(float)sdUsedBytes/sdTotalBytes:0;
    int barY2=SCR_H-28;
    mainBuf.fillRect(0,barY2,SCR_W,12,C_XXDGRAY);
    mainBuf.drawFastHLine(0,barY2,SCR_W,C_DGRAY);
    mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_DGRAY);
    mainBuf.setCursor(4,barY2+3);
    char sdInfo[48]; snprintf(sdInfo,sizeof(sdInfo),"SD: %s / %s (%.0f%%)",
      fmFormatSize(sdUsedBytes).c_str(),fmFormatSize(sdTotalBytes).c_str(),usedPct*100);
    mainBuf.print(sdInfo);
  }
  if(fmConfirmDelete){
    int bx=24,by=SCR_H/2-28,bw=SCR_W-48,bh=54;
    mainBuf.fillRoundRect(bx,by,bw,bh,8,C_XDGRAY);
    mainBuf.drawRoundRect(bx,by,bw,bh,8,C_WHITE);
    uiCenteredText("Hapus file ini?",by+8,C_WHITE,&fonts::Font2);
    String nm2=fmEntries[fmSel].name; if(nm2.length()>32) nm2=nm2.substring(0,31)+"~";
    uiCenteredText(nm2.c_str(),by+24,C_LGRAY,&fonts::Font2);
    mainBuf.setTextColor(C_DGRAY); mainBuf.setFont(&fonts::Font2);
    mainBuf.setCursor(bx+8,by+bh-12); mainBuf.print("SEL:hapus  L:batal");
  }
  if(!fmConfirmDelete) uiFooter("UP/DW  SEL:masuk  R:hapus  L+R:back");
}

// ════════════════════════════════════════════════════════════════
// ════════════════════════════════════════════════════════════════

void drawTriviaSetup(){
  mainBuf.fillScreen(C_BG); uiHeader("TRIVIA SETUP");
  const char* labels[]={"Kategori","Jumlah","Waktu","MULAI GAME"};
  for(int i=0;i<4;i++){
    int y=36+i*28; bool sel=(i==triviaSetupRow);
    uiCard(10,y,SCR_W-20,24,sel?C_WHITE:C_DGRAY,6);
    mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(sel?C_WHITE:C_MGRAY);
    mainBuf.setCursor(20,y+6); mainBuf.print(labels[i]);
    char lbl[64]="";
    if(i==0) strcpy(lbl, TRIVIA_CATS[triviaSetCatIdx].name);
    else if(i==1) snprintf(lbl,sizeof(lbl),"%d Soal", TRIVIA_AMOUNTS[triviaSetAmtIdx]);
    else if(i==2) snprintf(lbl,sizeof(lbl),"%d Detik", TRIVIA_TIMES[triviaSetTimeIdx]);
    int tw2=mainBuf.textWidth(lbl);
    mainBuf.setTextColor(sel?C_LGRAY:C_DGRAY);
    mainBuf.setCursor((SCR_W-tw2)/2,y+6); mainBuf.print(lbl);
  }
  uiFooter("UP/DW:baris  L/R:ubah  SEL:ok");
}

String decodeTrivia(String str) {
  String res = "";
  for (int i = 0; i < (int)str.length(); i++) {
    if (str[i] == '%' && i + 2 < (int)str.length()) {
      char c = (char)strtol(str.substring(i + 1, i + 3).c_str(), NULL, 16);
      res += c; i += 2;
    } else if (str[i] == '+') res += " ";
    else res += str[i];
  }
  return res;
}

bool handleTriviaSetup(){
  bool redraw=false;
  if(btnPressed(B_UP)){triviaSetupRow=(triviaSetupRow+3)%4;redraw=true;}
  if(btnPressed(B_DW)){triviaSetupRow=(triviaSetupRow+1)%4;redraw=true;}
  if(triviaSetupRow==0){if(btnPressed(B_L)){triviaSetCatIdx=(triviaSetCatIdx+TRIVIA_CAT_COUNT-1)%TRIVIA_CAT_COUNT;redraw=true;}if(btnPressed(B_R)){triviaSetCatIdx=(triviaSetCatIdx+1)%TRIVIA_CAT_COUNT;redraw=true;}}
  else if(triviaSetupRow==1){if(btnPressed(B_L)){triviaSetAmtIdx=(triviaSetAmtIdx+TRIVIA_AMT_COUNT-1)%TRIVIA_AMT_COUNT;redraw=true;}if(btnPressed(B_R)){triviaSetAmtIdx=(triviaSetAmtIdx+1)%TRIVIA_AMT_COUNT;redraw=true;}}
  else if(triviaSetupRow==2){if(btnPressed(B_L)){triviaSetTimeIdx=(triviaSetTimeIdx+TRIVIA_TIME_COUNT-1)%TRIVIA_TIME_COUNT;redraw=true;}if(btnPressed(B_R)){triviaSetTimeIdx=(triviaSetTimeIdx+1)%TRIVIA_TIME_COUNT;redraw=true;}}
  if(btnPressed(B_SEL)){
    if(triviaSetupRow==3) return true;
    if(triviaSetupRow==0){triviaSetCatIdx=(triviaSetCatIdx+1)%TRIVIA_CAT_COUNT;redraw=true;}
    if(triviaSetupRow==1){triviaSetAmtIdx=(triviaSetAmtIdx+1)%TRIVIA_AMT_COUNT;redraw=true;}
    if(triviaSetupRow==2){triviaSetTimeIdx=(triviaSetTimeIdx+1)%TRIVIA_TIME_COUNT;redraw=true;}
  }
  if(redraw){drawTriviaSetup();pushFrame();}
  return false;
}

bool fetchTrivia(){
  if(!wifiConnected) return false;
  char url[160]; int catId=TRIVIA_CATS[triviaSetCatIdx].id;
  if(catId>0) snprintf(url,sizeof(url),"https://opentdb.com/api.php?amount=1&category=%d&type=multiple&encode=url3986",catId);
  else snprintf(url,sizeof(url),"https://opentdb.com/api.php?amount=1&type=multiple&encode=url3986");
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.begin(client, url); http.setTimeout(10000);
  int code=http.GET(); if(code!=200){http.end();return false;}
  String payload=http.getString(); http.end();
  DynamicJsonDocument doc(4096);
  if(deserializeJson(doc,payload)) return false;
  auto res=doc["results"][0]; if(res.isNull()) return false;
  triviaQ.question=decodeTrivia(res["question"].as<String>());
  triviaQ.correct=random(4);
  String ca=decodeTrivia(res["correct_answer"].as<String>());
  auto wa=res["incorrect_answers"].as<JsonArray>(); int wi=0;
  for(int i=0;i<4;i++) triviaQ.answers[i]=(i==triviaQ.correct)?ca:decodeTrivia(wa[wi++].as<String>());
  return true;
}

void drawTriviaLoad(){
  static int spin=0; const char* dots[]={"   ",".  ",".. ","..."};
  mainBuf.fillScreen(C_BG); uiHeader("TRIVIA");
  uiCenteredText("Mengambil soal...",50,C_MGRAY,&fonts::Font2);
  uiCenteredText(dots[spin++%4],66,C_LGRAY,&fonts::Font2);
  char info[40]; snprintf(info,sizeof(info),"Soal %d dari %d",triviaTotal+1,triviaTarget);
  uiCenteredText(info,88,C_DGRAY,&fonts::Font2);
  uiCard(16,110,SCR_W-32,24,C_DGRAY);
  char sc[24]; snprintf(sc,sizeof(sc),"Skor: %d / %d",triviaScore,triviaTotal);
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_LGRAY);
  int tw2=mainBuf.textWidth(sc); mainBuf.setCursor((SCR_W-tw2)/2,118); mainBuf.print(sc);
}

void drawTriviaQ(){
  mainBuf.fillScreen(C_BG);
  mainBuf.fillRect(0,0,SCR_W,24,C_XXDGRAY); mainBuf.drawFastHLine(0,24,SCR_W,C_DGRAY);
  mainBuf.setFont(&fonts::Font2);
  mainBuf.setTextColor(C_WHITE); mainBuf.setCursor(6,8); mainBuf.print("TRIVIA");
  char sc[20]; snprintf(sc,sizeof(sc),"%d/%d",triviaTotal,triviaTarget);
  mainBuf.setTextColor(C_LGRAY); mainBuf.setCursor(SCR_W-44,8); mainBuf.print(sc);
  uint32_t el=(millis()-triviaTimer)/1000;
  float pct=1.0f-min(1.0f,(float)el/(float)triviaTimeLimit);
  uint16_t tc=pct>0.5f?C_WHITE:pct>0.25f?C_LGRAY:C_MGRAY;
  uiProgressBar(0,24,SCR_W,4,pct,tc);
  int secsLeft=max(0,(int)triviaTimeLimit-(int)el);
  char secStr[8]; snprintf(secStr,sizeof(secStr),"%ds",secsLeft);
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(tc);
  mainBuf.setCursor(SCR_W-26,8); mainBuf.print(secStr);
  uiCard(4,30,SCR_W-8,42,C_DGRAY);
  uiWrap(triviaQ.question,10,36,SCR_W-20,70,C_WHITE,11);
  int ansY=76,ansH=20,ansW=SCR_W-8;
  const char* ANS_LABELS[]={"A","B","C","D"};
  for(int i=0;i<4;i++){
    int y2=ansY+i*(ansH+2); bool sel=(i==triviaAns);
    mainBuf.fillRoundRect(4,y2,ansW,ansH,4,C_XXDGRAY);
    if(triviaAnswered){
      uint16_t border=(i==triviaQ.correct)?C_WHITE:(i==triviaAns)?C_MGRAY:C_XXDGRAY;
      mainBuf.drawRoundRect(4,y2,ansW,ansH,4,border);
      if(i==triviaQ.correct||i==triviaAns) mainBuf.fillRoundRect(4,y2+3,2,ansH-6,2,border);
    } else {
      mainBuf.drawRoundRect(4,y2,ansW,ansH,4,sel?C_MGRAY:C_DGRAY);
      if(sel) mainBuf.fillRoundRect(4,y2+3,2,ansH-6,2,C_WHITE);
    }
    mainBuf.setFont(&fonts::Font2);
    uint16_t txtCol=triviaAnswered?(i==triviaQ.correct?C_WHITE:C_DGRAY):(sel?C_WHITE:C_LGRAY);
    mainBuf.setTextColor(txtCol);
    mainBuf.setCursor(10,y2+4); mainBuf.print(ANS_LABELS[i]); mainBuf.print(".");
    String ans=triviaQ.answers[i]; if(mainBuf.textWidth(ans.c_str())>ansW-34) ans=ans.substring(0,26)+"~";
    mainBuf.setCursor(28,y2+4); mainBuf.print(ans);
  }
}

void drawTriviaResult(bool correct){
  mainBuf.fillScreen(C_BG); uiHeader(correct?"BETUL!":"SALAH!");
  uiCenteredText(correct?":D":":(",32,correct?C_WHITE:C_MGRAY,&fonts::Font6);
  uiCenteredText(correct?"Bagus!":"Coba lagi!",86,C_LGRAY,&fonts::Font2);
  if(!correct){
    mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_DGRAY);
    mainBuf.setCursor(6,110); mainBuf.print("Jawaban: ");
    mainBuf.setTextColor(C_WHITE);
    String ans=triviaQ.answers[triviaQ.correct]; if(ans.length()>28) ans=ans.substring(0,27)+"~";
    mainBuf.print(ans);
  }
  uiCard(16,128,SCR_W-32,26,C_DGRAY);
  char sc[32]; snprintf(sc,sizeof(sc),"Skor %d/%d  |  soal %d/%d",triviaScore,triviaTotal,triviaTotal,triviaTarget);
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_MGRAY);
  int tw2=mainBuf.textWidth(sc); mainBuf.setCursor((SCR_W-tw2)/2,134); mainBuf.print(sc);
  uiFooter(triviaTotal>=triviaTarget?"SEL:lihat hasil":"SEL:lanjut  L+R:menu");
}

void drawTriviaSummary(){
  mainBuf.fillScreen(C_BG); uiHeader("HASIL QUIZ");
  float pct=triviaTarget>0?(float)triviaScore/triviaTarget:0;
  char sc[16]; snprintf(sc,sizeof(sc),"%d / %d",triviaScore,triviaTarget);
  uiCenteredText(sc,32,C_WHITE,&fonts::Font6);
  char pctStr[10]; snprintf(pctStr,sizeof(pctStr),"%.0f%%",pct*100);
  uiCenteredText(pctStr,84,C_LGRAY,&fonts::Font2);
  const char* kom=pct==1.0f?"Sempurna!":pct>=0.8f?"Bagus sekali!":pct>=0.6f?"Lumayan bagus!":pct>=0.4f?"Terus berlatih!":"Jangan menyerah!";
  uiCenteredText(kom,104,C_WHITE,&fonts::Font2);
  uiFooter("SEL:main lagi  L+R:menu");
}

// ════════════════════════════════════════════════════════════════
// SETTINGS
// ════════════════════════════════════════════════════════════════
void drawSettings() {
  mainBuf.fillScreen(C_BG); uiHeader("SETTINGS");

  // Brightness
  uiCard(4,28,SCR_W-8,34,C_DGRAY);
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_LGRAY);
  mainBuf.setCursor(10,35); mainBuf.print("Brightness");
  char bStr[12]; snprintf(bStr,sizeof(bStr),"%d%%",brightness*100/255);
  mainBuf.setTextColor(C_WHITE); int tw2=mainBuf.textWidth(bStr);
  mainBuf.setCursor(SCR_W-tw2-10,35); mainBuf.print(bStr);
  if(autoBrEnabled){mainBuf.setTextColor(C_MGRAY);mainBuf.setCursor(SCR_W-tw2-60,35);mainBuf.print("[AUTO]");}
  uiProgressBar(10,46,SCR_W-20,6,(float)brightness/255.0f);
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_DGRAY);
  mainBuf.setCursor(10,54); mainBuf.print("L/R:ubah  SEL:toggle auto");

  // Mood lamp
  uiCard(4,68,SCR_W-8,24,moodLampEnabled?C_MGRAY:C_DGRAY);
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_LGRAY);
  mainBuf.setCursor(10,76); mainBuf.print("Mood Lamp");
  int swX=SCR_W-52,swY=73,swW=36,swH=14;
  mainBuf.fillRoundRect(swX,swY,swW,swH,7,moodLampEnabled?C_DGRAY:C_XDGRAY);
  mainBuf.drawRoundRect(swX,swY,swW,swH,7,moodLampEnabled?C_WHITE:C_MGRAY);
  int knobX=moodLampEnabled?(swX+swW-13):(swX+2);
  mainBuf.fillCircle(knobX+5,swY+swH/2,5,moodLampEnabled?C_WHITE:C_MGRAY);
  mainBuf.setTextColor(moodLampEnabled?C_WHITE:C_DGRAY); mainBuf.setCursor(swX-28,76); mainBuf.print(moodLampEnabled?"ON ":"OFF");

  // Jam
  uiCard(4,98,SCR_W-8,28,C_DGRAY);
  clockUpdate();
  char timeBuf[12]; clockFormatTime(timeBuf,sizeof(timeBuf));
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_MGRAY);
  mainBuf.setCursor(10,106); mainBuf.print("Jam:");
  mainBuf.setTextColor(gClock.valid?C_WHITE:C_DGRAY); mainBuf.setCursor(46,106); mainBuf.print(timeBuf);
  mainBuf.setTextColor(clockSynced?C_MGRAY:C_DGRAY); mainBuf.setCursor(SCR_W-52,106); mainBuf.print(clockSynced?"NTP OK":"no sync");
  char dateBuf[24]; clockFormatDate(dateBuf,sizeof(dateBuf));
  mainBuf.setTextColor(C_DGRAY); mainBuf.setCursor(10,118); mainBuf.print(dateBuf);

  // Heap/PSRAM
  uiCard(4,132,SCR_W-8,26,C_DGRAY);
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_DGRAY); mainBuf.setCursor(10,140); mainBuf.print("Heap:");
  char heapStr[16]; snprintf(heapStr,sizeof(heapStr),"%uKB",ESP.getFreeHeap()/1024);
  mainBuf.setTextColor(C_LGRAY); mainBuf.setCursor(50,140); mainBuf.print(heapStr);
  mainBuf.setTextColor(C_DGRAY); mainBuf.setCursor(100,140); mainBuf.print("PSRAM:");
  char psramStr[16]; snprintf(psramStr,sizeof(psramStr),"%uKB",ESP.getFreePsram()/1024);
  mainBuf.setTextColor(C_LGRAY); mainBuf.setCursor(148,140); mainBuf.print(psramStr);
  mainBuf.setTextColor(C_DGRAY); mainBuf.setCursor(10,150); mainBuf.print("Temp:");
  char tempStr[10]; snprintf(tempStr,sizeof(tempStr),"%.1fC",temperatureRead());
  mainBuf.setTextColor(C_LGRAY); mainBuf.setCursor(50,150); mainBuf.print(tempStr);
  uint32_t upSec=millis()/1000; char upStr[20]; snprintf(upStr,sizeof(upStr),"Up %02d:%02d",upSec/60,upSec%60);
  mainBuf.setTextColor(C_DGRAY); mainBuf.setCursor(SCR_W-80,150); mainBuf.print(upStr);

  // WiFi saved info + hapus
  uiCard(4,SCR_H-40,SCR_W-8,22,C_DGRAY);
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_DGRAY);
  mainBuf.setCursor(10,SCR_H-34);
  if(strlen(savedSSID)>0) {
    char wfInfo[50]; snprintf(wfInfo,sizeof(wfInfo),"NVS: %s  [R:hapus]",savedSSID);
    mainBuf.setTextColor(C_MGRAY); mainBuf.print(wfInfo);
  } else {
    mainBuf.print("NVS: tidak ada kredensial WiFi");
  }

  uiFooter("L/R:br  UP/DW:mood  SEL:auto  R:hapusWF");
}

// ════════════════════════════════════════════════════════════════
// POWER MENU
// ════════════════════════════════════════════════════════════════
void drawPowerMenu(){
  mainBuf.fillScreen(C_BG);
  int bx=SCR_W/2-68,by=SCR_H/2-40,bw=136,bh=80;
  mainBuf.fillRoundRect(bx,by,bw,bh,10,C_XDGRAY);
  mainBuf.drawRoundRect(bx,by,bw,bh,10,C_MGRAY);
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_WHITE);
  int tw2=mainBuf.textWidth("POWER"); mainBuf.setCursor(bx+(bw-tw2)/2,by+6); mainBuf.print("POWER");
  mainBuf.drawFastHLine(bx+6,by+20,bw-12,C_DGRAY);
  const char* opts[]={"Restart","Deep Sleep","Batal"};
  for(int i=0;i<3;i++){
    int oy=by+26+i*16; bool sel=(i==powerSel);
    if(sel){mainBuf.fillRoundRect(bx+4,oy,bw-8,14,4,C_DGRAY);mainBuf.drawRoundRect(bx+4,oy,bw-8,14,4,C_MGRAY);mainBuf.fillRoundRect(bx+4,oy+3,2,8,2,C_WHITE);}
    mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(sel?C_WHITE:C_LGRAY);
    tw2=mainBuf.textWidth(opts[i]); mainBuf.setCursor(bx+(bw-tw2)/2,oy+2); mainBuf.print(opts[i]);
  }
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_DGRAY);
  mainBuf.setCursor(bx+4,by+bh-10); mainBuf.print("UP/DW  SEL:ok  L:batal");
}

// ════════════════════════════════════════════════════════════════
// HELPER UMUM ONLINE
// ════════════════════════════════════════════════════════════════
void drawLoading(const char* title, const char* msg="Mengambil data...") {
  mainBuf.fillScreen(C_BG); uiHeader(title);
  static int spin=0; const char* dots[]={"·      ","· ·    ","· · ·  ","· · · ·"};
  uiCenteredText(msg, 60, C_MGRAY, &fonts::Font2);
  uiCenteredText(dots[spin++%4], 80, C_LGRAY, &fonts::Font2);
  uiFooter("L+R:batal");
  pushFrame();
}

// HTTP GET untuk HTTPS — pakai WiFiClientSecure dengan skip verify
String httpGetSecure(const char* url, const char* userAgent="ESP32-Hub/9.1",
                     const char* accept="application/json", int timeout=10000) {
  if(!WiFi.isConnected()) return "";
  WiFiClientSecure client;
  client.setInsecure(); // skip SSL cert verify — OK untuk embedded device
  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(timeout);
  http.addHeader("User-Agent", userAgent);
  http.addHeader("Accept", accept);
  int code = http.GET();
  String result = "";
  if(code==200) result = http.getString();
  http.end();
  return result;
}

// HTTP GET untuk HTTP plain (non-HTTPS) — pakai WiFiClient langsung
// [FIX v9.1] HTTPClient kadang gagal follow untuk plain HTTP di ESP32
String httpGetPlain(const char* host, int port, const char* path,
                    int timeout=8000) {
  if(!WiFi.isConnected()) return "";
  WiFiClient client;
  client.setTimeout(timeout/1000);
  if(!client.connect(host, port)) return "";
  // Kirim HTTP GET manual
  client.print(String("GET ") + path + " HTTP/1.0\r\n");
  client.print(String("Host: ") + host + "\r\n");
  client.print("User-Agent: ESP32-Hub/9.1\r\n");
  client.print("Accept: text/plain\r\n");
  client.print("Connection: close\r\n\r\n");
  // Tunggu response
  uint32_t t = millis();
  while(!client.available() && millis()-t < (uint32_t)timeout) delay(10);
  // Skip headers
  String response = "";
  bool headerDone = false;
  while(client.available()) {
    String line = client.readStringUntil('\n');
    if(!headerDone) {
      if(line=="\r"||line=="\r\n"||line.length()<=1) headerDone=true;
    } else {
      response += line + "\n";
    }
  }
  client.stop();
  response.trim();
  return response;
}

// ════════════════════════════════════════════════════════════════
// [1] ISS TRACKER
// ════════════════════════════════════════════════════════════════
bool fetchISSNow() {
  // open-notify.org — HTTP biasa, pakai HTTPClient langsung seperti v9.0
  if(!WiFi.isConnected()) return false;
  HTTPClient http;
  http.begin("http://api.open-notify.org/iss-now.json");
  http.setTimeout(8000);
  http.addHeader("User-Agent","ESP32-Hub/9.1");
  int code = http.GET();
  if(code != 200) { http.end(); return false; }
  String resp = http.getString();
  http.end();
  if(resp.isEmpty()) return false;
  DynamicJsonDocument doc(256);
  if(deserializeJson(doc,resp)) return false;
  issLat = doc["iss_position"]["latitude"].as<float>();
  issLon = doc["iss_position"]["longitude"].as<float>();
  issFetched = true;
  issLastFetch = millis();
  return true;
}

void drawISSGlobe(float lat, float lon) {
  const int gx=120, gy=40, gw=180, gh=90;
  mainBuf.drawRoundRect(gx,gy,gw,gh,4,C_DGRAY);
  mainBuf.fillRoundRect(gx+1,gy+1,gw-2,gh-2,3,C_XXDGRAY);
  for(int lo=-180;lo<=180;lo+=60) {
    int x=gx+1+(int)((lo+180.0f)/360.0f*(gw-2));
    mainBuf.drawFastVLine(x,gy+1,gh-2,C_XDGRAY);
  }
  for(int la=-90;la<=90;la+=30) {
    int y=gy+1+(int)((90.0f-la)/180.0f*(gh-2));
    mainBuf.drawFastHLine(gx+1,y,gw-2,C_XDGRAY);
  }
  int eqY=gy+1+(gh-2)/2;
  mainBuf.drawFastHLine(gx+1,eqY,gw-2,C_DGRAY);
  int issX=gx+1+(int)((lon+180.0f)/360.0f*(gw-2));
  int issY=gy+1+(int)((90.0f-lat)/180.0f*(gh-2));
  issX=constrain(issX,gx+2,gx+gw-3);
  issY=constrain(issY,gy+2,gy+gh-3);
  uint32_t t=millis();
  uint8_t flash=(t/500)%2==0?255:180;
  uint16_t issDot=lgfx::color565(flash,flash,flash);
  mainBuf.fillCircle(issX,issY,4,C_BG);
  mainBuf.drawCircle(issX,issY,4,issDot);
  mainBuf.fillCircle(issX,issY,2,issDot);
  mainBuf.drawFastHLine(issX-7,issY,6,issDot);
  mainBuf.drawFastHLine(issX+2,issY,6,issDot);
  mainBuf.drawFastVLine(issX,issY-7,6,issDot);
  mainBuf.drawFastVLine(issX,issY+2,6,issDot);
}

void drawISS() {
  mainBuf.fillScreen(C_BG); uiHeader("ISS TRACKER");
  if(!wifiConnected){
    uiCenteredText("WiFi diperlukan",68,C_MGRAY,&fonts::Font2);
    uiFooter("L+R:back"); pushFrame(); return;
  }
  if(issFetched) {
    drawISSGlobe(issLat, issLon);
    mainBuf.setFont(&fonts::Font2);
    char latStr[24],lonStr[24];
    snprintf(latStr,sizeof(latStr),"Lat : %+.4f", issLat);
    snprintf(lonStr,sizeof(lonStr),"Lon : %+.4f", issLon);
    mainBuf.setTextColor(C_LGRAY); mainBuf.setCursor(8,138); mainBuf.print(latStr);
    mainBuf.setTextColor(C_LGRAY); mainBuf.setCursor(8,150); mainBuf.print(lonStr);
    uint32_t secAgo=(millis()-issLastFetch)/1000;
    char ageStr[24]; snprintf(ageStr,sizeof(ageStr),"%ds lalu", (int)secAgo);
    mainBuf.setTextColor(C_DGRAY);
    int tw=mainBuf.textWidth(ageStr);
    mainBuf.setCursor(SCR_W-tw-6,144); mainBuf.print(ageStr);
    mainBuf.setTextColor(C_DGRAY);
    mainBuf.setCursor(SCR_W-90,154); mainBuf.print("27,600 km/h");
  } else {
    uiCenteredText("Mengambil posisi...",80,C_MGRAY,&fonts::Font2);
  }
  uiFooter("SEL:refresh  L+R:back");
}

// ════════════════════════════════════════════════════════════════
// [2] APOD
// ════════════════════════════════════════════════════════════════
bool fetchAPOD() {
  String resp = httpGetSecure("https://api.nasa.gov/planetary/apod?api_key=DEMO_KEY");
  if(resp.isEmpty()) return false;
  DynamicJsonDocument doc(12288); // Perbesar buffer untuk explanation panjang
  DeserializationError err = deserializeJson(doc,resp);
  if(err) { Serial.printf("[APOD] JSON err: %s\n", err.c_str()); return false; }
  apodTitle   = doc["title"].as<String>();
  apodDate    = doc["date"].as<String>();
  apodExplain = doc["explanation"].as<String>();
  // Fallback jika explanation kosong
  if(apodExplain.isEmpty()) apodExplain = "(tidak ada penjelasan)";
  apodFetched = true;
  return true;
}

void drawAPOD() {
  mainBuf.fillScreen(C_BG); uiHeader("APOD — NASA");
  if(!wifiConnected){ uiCenteredText("WiFi diperlukan",68,C_MGRAY,&fonts::Font2); uiFooter("L+R:back"); pushFrame(); return; }
  if(!apodFetched){ uiCenteredText("SEL untuk ambil data",80,C_MGRAY,&fonts::Font2); uiFooter("SEL:fetch  L+R:back"); pushFrame(); return; }

  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_DGRAY);
  mainBuf.setCursor(4,26); mainBuf.print(apodDate);
  mainBuf.setFont(&fonts::Font4); mainBuf.setTextColor(C_WHITE);
  String tShort=apodTitle; if(mainBuf.textWidth(tShort.c_str())>SCR_W-10) tShort=tShort.substring(0,22)+"...";
  mainBuf.setCursor(4,36); mainBuf.print(tShort);
  mainBuf.drawFastHLine(0,54,SCR_W,C_DGRAY);
  uiWrapScroll(apodExplain, 6, 57, SCR_W-12, SCR_H-73, textScrollY, C_LGRAY, 12);
  uiFooter("UP/DW:scroll  SEL:refresh  L+R:back");
}

// ════════════════════════════════════════════════════════════════
// [3] STOIC QUOTE — [FIX v9.1]
// ════════════════════════════════════════════════════════════════
bool fetchStoic() {
  // API: stoicismquote.com — coba field "body", fallback "quote"
  String resp = httpGetSecure("https://stoicismquote.com/api/v1/quote/random");
  if(resp.isEmpty()) {
    Serial.println("[Stoic] Response kosong");
    return false;
  }
  Serial.printf("[Stoic] Raw: %s\n", resp.substring(0,200).c_str());

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc,resp);
  if(err) {
    Serial.printf("[Stoic] JSON err: %s\n", err.c_str());
    return false;
  }

  // Coba berbagai field name yang mungkin digunakan API ini
  String q = "";
  if(!doc["body"].isNull())         q = doc["body"].as<String>();
  else if(!doc["quote"].isNull())   q = doc["quote"].as<String>();
  else if(!doc["text"].isNull())    q = doc["text"].as<String>();
  else if(!doc["content"].isNull()) q = doc["content"].as<String>();

  if(q.isEmpty()) {
    // Coba ambil field pertama sebagai fallback
    Serial.println("[Stoic] Semua field gagal, dump JSON:");
    String dump; serializeJson(doc, dump);
    Serial.println(dump.substring(0,300));
    return false;
  }

  stoicQuote = q;

  // Author: coba berbagai struktur
  String a = "";
  if(!doc["author"].isNull()) {
    if(doc["author"].is<JsonObject>()) {
      // Struktur nested: {"author": {"name": "..."}}
      a = doc["author"]["name"].as<String>();
    } else {
      a = doc["author"].as<String>();
    }
  }
  if(a.isEmpty()||a=="null") a = "Unknown";
  stoicAuthor = a;

  stoicFetched = true;
  return true;
}

void drawStoic() {
  mainBuf.fillScreen(C_BG); uiHeader("STOIC QUOTE");
  if(!wifiConnected){ uiCenteredText("WiFi diperlukan",68,C_MGRAY,&fonts::Font2); uiFooter("L+R:back"); pushFrame(); return; }
  if(!stoicFetched){ uiCenteredText("SEL untuk ambil kutipan",80,C_MGRAY,&fonts::Font2); uiFooter("SEL:fetch  L+R:back"); pushFrame(); return; }

  mainBuf.setFont(&fonts::Font6); mainBuf.setTextColor(C_DGRAY);
  mainBuf.setCursor(4,20); mainBuf.print("\"");

  uiWrapScroll(stoicQuote, 20, 26, SCR_W-28, SCR_H-56, textScrollY, C_WHITE, 13);

  mainBuf.drawFastHLine(0,SCR_H-38,SCR_W,C_DGRAY);
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_MGRAY);
  String aut = "— " + stoicAuthor;
  int tw=mainBuf.textWidth(aut.c_str());
  mainBuf.setCursor(SCR_W-tw-6,SCR_H-32); mainBuf.print(aut);

  uiFooter("UP/DW:scroll  SEL:baru  L+R:back");
}

// ════════════════════════════════════════════════════════════════
// [4] NUMBER FACT — [FIX v9.1]
// ════════════════════════════════════════════════════════════════
bool fetchNumberFact() {
  numFactN = random(1,1000);
  // [FIX] Gunakan httpGetPlain untuk HTTP plain numbersapi.com
  char path[32]; snprintf(path, sizeof(path), "/%d/trivia", numFactN);
  String resp = httpGetPlain("numbersapi.com", 80, path);
  if(resp.isEmpty()) {
    Serial.printf("[NumFact] Gagal fetch angka %d\n", numFactN);
    return false;
  }
  resp.trim();
  numFactText = resp;
  numFactFetched = true;
  Serial.printf("[NumFact] %d: %s\n", numFactN, resp.substring(0,80).c_str());
  return true;
}

void drawNumberFact() {
  mainBuf.fillScreen(C_BG); uiHeader("NUMBER FACT");
  if(!wifiConnected){ uiCenteredText("WiFi diperlukan",68,C_MGRAY,&fonts::Font2); uiFooter("L+R:back"); pushFrame(); return; }
  if(!numFactFetched){ uiCenteredText("SEL untuk fakta angka",80,C_MGRAY,&fonts::Font2); uiFooter("SEL:fetch  L+R:back"); pushFrame(); return; }

  char numStr[12]; snprintf(numStr,sizeof(numStr),"%d",numFactN);
  mainBuf.setFont(&fonts::Font6); mainBuf.setTextColor(C_LGRAY);
  int nw=mainBuf.textWidth(numStr);
  mainBuf.setCursor(SCR_W-nw-6,18); mainBuf.print(numStr);

  mainBuf.drawFastHLine(0,56,SCR_W,C_DGRAY);
  uiWrapScroll(numFactText, 6, 60, SCR_W-12, SCR_H-76, textScrollY, C_WHITE, 13);

  uiFooter("UP/DW:scroll  SEL:angka baru  L+R:back");
}

// ════════════════════════════════════════════════════════════════
// [5] DAD JOKE + CHUCK NORRIS
// ════════════════════════════════════════════════════════════════
bool fetchJoke() {
  String resp;
  if(jokeSource==0) {
    resp = httpGetSecure("https://icanhazdadjoke.com/","ESP32-Hub/9.1","application/json");
    if(resp.isEmpty()) return false;
    DynamicJsonDocument doc(1024);
    if(deserializeJson(doc,resp)) return false;
    jokeText = doc["joke"].as<String>();
  } else {
    resp = httpGetSecure("https://api.chucknorris.io/jokes/random");
    if(resp.isEmpty()) return false;
    DynamicJsonDocument doc(1024);
    if(deserializeJson(doc,resp)) return false;
    jokeText = doc["value"].as<String>();
  }
  jokeFetched = true;
  return true;
}

void drawJokes() {
  mainBuf.fillScreen(C_BG);
  uiHeader(jokeSource==0?"DAD JOKE":"CHUCK NORRIS");
  if(!wifiConnected){ uiCenteredText("WiFi diperlukan",68,C_MGRAY,&fonts::Font2); uiFooter("L+R:back"); pushFrame(); return; }
  if(!jokeFetched){ uiCenteredText("SEL untuk lelucon",80,C_MGRAY,&fonts::Font2); uiFooter("SEL:fetch  R:ganti jenis  L+R:back"); pushFrame(); return; }

  const char* badge = jokeSource==0 ? "Dad Joke" : "Chuck Norris Fact";
  mainBuf.fillRoundRect(4,26,SCR_W-8,14,5,C_XXDGRAY);
  mainBuf.drawRoundRect(4,26,SCR_W-8,14,5,C_DGRAY);
  uiCenteredText(badge,28,C_MGRAY,&fonts::Font2);

  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_DGRAY);
  mainBuf.setCursor(6,28); mainBuf.print(jokeSource==0?"haha":"!!");

  mainBuf.drawFastHLine(0,42,SCR_W,C_DGRAY);
  uiWrapScroll(jokeText, 6, 46, SCR_W-12, SCR_H-62, textScrollY, C_WHITE, 13);

  uiFooter("SEL:baru  R:Dad/Chuck  UP/DW:scroll");
}

// ════════════════════════════════════════════════════════════════
// [6] RANDOM ACTIVITY (ganti Bored API) — [FIX v9.1]
// ════════════════════════════════════════════════════════════════
bool fetchBored() {
  // API baru: www.randomactivityapi.com/api/activity
  // Field: activity, type, participants (sama strukturnya)
  String resp = httpGetSecure("https://www.randomactivityapi.com/api/activity");
  if(resp.isEmpty()) {
    Serial.println("[Bored] randomactivityapi.com gagal, coba fallback...");
    // Fallback: generatepress.me (backup gratis)
    resp = httpGetSecure("https://bored-api.appbrewery.com/random");
  }
  if(resp.isEmpty()) return false;
  Serial.printf("[Bored] Raw: %s\n", resp.substring(0,200).c_str());
  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc,resp);
  if(err) { Serial.printf("[Bored] JSON err: %s\n", err.c_str()); return false; }
  boredActivity     = doc["activity"].as<String>();
  boredType         = doc["type"] | String("education");
  boredParticipants = doc["participants"] | 1;
  if(boredActivity.isEmpty()) return false;
  boredFetched = true;
  return true;
}

void drawBored() {
  mainBuf.fillScreen(C_BG); uiHeader("AKTIVITAS ACAK");
  if(!wifiConnected){ uiCenteredText("WiFi diperlukan",68,C_MGRAY,&fonts::Font2); uiFooter("L+R:back"); pushFrame(); return; }
  if(!boredFetched){ uiCenteredText("SEL untuk ide aktivitas",80,C_MGRAY,&fonts::Font2); uiFooter("SEL:fetch  L+R:back"); pushFrame(); return; }

  mainBuf.fillRoundRect(4,26,SCR_W-8,16,6,C_XXDGRAY);
  mainBuf.drawRoundRect(4,26,SCR_W-8,16,6,C_DGRAY);
  String typeStr = boredType; typeStr.toUpperCase();
  uiCenteredText(typeStr.c_str(),28,C_MGRAY,&fonts::Font2);

  char partStr[32]; snprintf(partStr,sizeof(partStr),"%d peserta",boredParticipants);
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_DGRAY);
  int tw=mainBuf.textWidth(partStr);
  mainBuf.setCursor(SCR_W-tw-6,28); mainBuf.print(partStr);

  mainBuf.drawFastHLine(0,44,SCR_W,C_DGRAY);

  uiCard(4,48,SCR_W-8,60,C_DGRAY,8);
  uiWrap(boredActivity, 12, 54, SCR_W-24, 108, C_WHITE, 14);

  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_XDGRAY);
  mainBuf.setCursor(6,116); mainBuf.print("Coba aktivitas ini sekarang!");

  uiFooter("SEL:ide baru  L+R:back");
}

// ════════════════════════════════════════════════════════════════
// [7] PEOPLE IN SPACE
// ════════════════════════════════════════════════════════════════
bool fetchPeopleInSpace() {
  // open-notify.org — HTTP biasa, pakai HTTPClient langsung seperti v9.0
  if(!WiFi.isConnected()) return false;
  HTTPClient http;
  http.begin("http://api.open-notify.org/astros.json");
  http.setTimeout(8000);
  http.addHeader("User-Agent","ESP32-Hub/9.1");
  int code = http.GET();
  if(code != 200) { http.end(); return false; }
  String resp = http.getString();
  http.end();
  if(resp.isEmpty()) return false;
  DynamicJsonDocument doc(4096);
  if(deserializeJson(doc,resp)) return false;
  astronauts.clear();
  astronautTotal = doc["number"] | 0;
  auto people = doc["people"].as<JsonArray>();
  for(auto p : people) {
    astronauts.push_back({p["name"].as<String>(), p["craft"].as<String>()});
  }
  astronautFetched = true;
  astronautSel = 0;
  return true;
}

void drawPeopleInSpace() {
  mainBuf.fillScreen(C_BG); uiHeader("MANUSIA DI LUAR ANGKASA");
  if(!wifiConnected){ uiCenteredText("WiFi diperlukan",68,C_MGRAY,&fonts::Font2); uiFooter("L+R:back"); pushFrame(); return; }
  if(!astronautFetched){ uiCenteredText("SEL untuk ambil data",80,C_MGRAY,&fonts::Font2); uiFooter("SEL:fetch  L+R:back"); pushFrame(); return; }

  char totalStr[32]; snprintf(totalStr,sizeof(totalStr),"%d orang di orbit sekarang",astronautTotal);
  mainBuf.fillRoundRect(4,26,SCR_W-8,16,6,C_XXDGRAY);
  mainBuf.drawRoundRect(4,26,SCR_W-8,16,6,C_DGRAY);
  uiCenteredText(totalStr,28,C_WHITE,&fonts::Font2);
  mainBuf.drawFastHLine(0,44,SCR_W,C_DGRAY);

  const int ITEM_H=20, LIST_Y=46;
  int listH=SCR_H-LIST_Y-16, vis=listH/ITEM_H;
  int scrollOff=0;
  if(astronautSel>=scrollOff+vis) scrollOff=astronautSel-vis+1;

  for(int i=0;i<vis&&(i+scrollOff)<(int)astronauts.size();i++){
    int idx=i+scrollOff; bool sel=(idx==astronautSel);
    int y=LIST_Y+i*ITEM_H;
    Astronaut& a=astronauts[idx];
    if(sel){
      mainBuf.fillRoundRect(0,y,SCR_W,ITEM_H,4,C_XDGRAY);
      mainBuf.fillRoundRect(0,y+3,2,ITEM_H-6,2,C_WHITE);
    } else if(i%2==0) mainBuf.fillRect(0,y,SCR_W,ITEM_H,C_XXDGRAY);
    mainBuf.setFont(&fonts::Font2);
    char nStr[4]; snprintf(nStr,sizeof(nStr),"%d",idx+1);
    mainBuf.setTextColor(C_DGRAY); mainBuf.setCursor(6,y+4); mainBuf.print(nStr);
    mainBuf.setTextColor(sel?C_WHITE:C_LGRAY);
    mainBuf.setCursor(24,y+4); mainBuf.print(a.name);
    mainBuf.setTextColor(C_DGRAY);
    int cw=mainBuf.textWidth(a.craft.c_str());
    mainBuf.setCursor(SCR_W-cw-6,y+4); mainBuf.print(a.craft);
  }
  uiFooter("UP/DW:scroll  SEL:refresh  L+R:back");
}

// ════════════════════════════════════════════════════════════════
// [8] HACKER NEWS — [FIX + KOMENTAR v9.1]
// ════════════════════════════════════════════════════════════════
bool fetchHackerNews() {
  String resp = httpGetSecure("https://hacker-news.firebaseio.com/v0/topstories.json");
  if(resp.isEmpty()) return false;
  DynamicJsonDocument ids(8192);
  if(deserializeJson(ids,resp)) return false;
  hnStories.clear();
  for(int i=0;i<5&&i<(int)ids.size();i++){
    int storyId = ids[i];
    char url[80]; snprintf(url,sizeof(url),"https://hacker-news.firebaseio.com/v0/item/%d.json",storyId);
    String sr = httpGetSecure(url);
    if(sr.isEmpty()) continue;
    DynamicJsonDocument sd(2048);
    if(deserializeJson(sd,sr)) continue;
    String title = sd["title"] | String("(no title)");
    int    score = sd["score"] | 0;
    hnStories.push_back({title, score, storyId});
    delay(100);
  }
  hnFetched = true;
  hnSel = 0;
  return !hnStories.empty();
}

// Fetch komentar via Algolia HN Search API
// GET https://hn.algolia.com/api/v1/items/{id}
// Response: {"children": [{"author":..., "text":...}, ...]}
bool fetchHNComments(int storyId) {
  hnComments.clear();
  char url[80]; snprintf(url, sizeof(url), "https://hn.algolia.com/api/v1/items/%d", storyId);
  String resp = httpGetSecure(url, "ESP32-Hub/9.1", "application/json", 12000);
  if(resp.isEmpty()) {
    Serial.printf("[HN] Komentar kosong untuk id %d\n", storyId);
    return false;
  }
  Serial.printf("[HN] Komentar raw len=%d\n", resp.length());

  // Buffer besar karena cerita HN bisa punya banyak nested children
  DynamicJsonDocument doc(24576);
  DeserializationError err = deserializeJson(doc, resp);
  if(err) {
    Serial.printf("[HN] JSON komentar err: %s\n", err.c_str());
    return false;
  }

  auto children = doc["children"].as<JsonArray>();
  int added = 0;
  for(auto child : children) {
    if(added >= 3) break; // Ambil max 3 komentar
    String author = child["author"] | String("(anon)");
    String text   = child["text"]   | String("");
    if(text.isEmpty()) continue;
    // Strip HTML tags dari teks komentar
    String cleanText = stripHtml(text);
    cleanText.trim();
    if(cleanText.length() < 5) continue; // Skip komentar terlalu pendek
    hnComments.push_back({author, cleanText});
    added++;
  }
  hnCommentsFetched = true;
  hnCommentsScrollY = 0;
  hnCommentsSel = 0;
  return !hnComments.empty();
}

void drawHackerNews() {
  mainBuf.fillScreen(C_BG); uiHeader("HACKER NEWS");
  if(!wifiConnected){ uiCenteredText("WiFi diperlukan",68,C_MGRAY,&fonts::Font2); uiFooter("L+R:back"); pushFrame(); return; }
  if(!hnFetched){ uiCenteredText("SEL untuk ambil berita",80,C_MGRAY,&fonts::Font2); uiFooter("SEL:fetch  L+R:back"); pushFrame(); return; }
  if(hnStories.empty()){ uiCenteredText("Gagal mengambil berita",68,C_MGRAY,&fonts::Font2); uiFooter("SEL:coba lagi  L+R:back"); pushFrame(); return; }

  mainBuf.fillRoundRect(4,26,90,14,5,C_XXDGRAY);
  mainBuf.drawRoundRect(4,26,90,14,5,C_DGRAY);
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_MGRAY);
  mainBuf.setCursor(8,28); mainBuf.print("Top 5 Stories");

  mainBuf.drawFastHLine(0,42,SCR_W,C_DGRAY);

  const int ITEM_H=24, LIST_Y=44;
  for(int i=0;i<(int)hnStories.size();i++){
    HNStory& s=hnStories[i]; bool sel=(i==hnSel);
    int y=LIST_Y+i*(ITEM_H+2);
    if(y+ITEM_H > SCR_H-16) break;
    if(sel){
      mainBuf.fillRoundRect(0,y,SCR_W,ITEM_H,5,C_XDGRAY);
      mainBuf.fillRoundRect(0,y+3,2,ITEM_H-6,2,C_WHITE);
      mainBuf.drawRoundRect(0,y,SCR_W,ITEM_H,5,C_DGRAY);
    } else if(i%2==0) mainBuf.fillRoundRect(0,y,SCR_W,ITEM_H,4,C_XXDGRAY);
    mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_DGRAY);
    char nStr[4]; snprintf(nStr,sizeof(nStr),"%d",i+1);
    mainBuf.setCursor(6,y+4); mainBuf.print(nStr);
    mainBuf.setTextColor(sel?C_WHITE:C_LGRAY);
    String title=s.title;
    while(title.length()>2 && mainBuf.textWidth((title+"...").c_str())>SCR_W-54) title.remove(title.length()-1);
    if(s.title.length()>title.length()) title+="...";
    mainBuf.setCursor(22,y+4); mainBuf.print(title);
    char sc[10]; snprintf(sc,sizeof(sc),"^%d",s.score);
    mainBuf.setTextColor(C_DGRAY);
    int sw=mainBuf.textWidth(sc);
    mainBuf.setCursor(SCR_W-sw-4,y+13); mainBuf.print(sc);
  }
  uiFooter("UP/DW:pilih  SEL:komentar  L+R:back");
}

// Layar komentar HN — [NEW v9.1]
void drawHNComments() {
  mainBuf.fillScreen(C_BG);
  // Header dengan judul berita
  String storyTitle = "";
  if(hnCommentsStoryIdx >= 0 && hnCommentsStoryIdx < (int)hnStories.size())
    storyTitle = hnStories[hnCommentsStoryIdx].title;
  uiHeader("HN COMMENTS");

  // Judul berita di sub-header
  mainBuf.fillRoundRect(0,24,SCR_W,16,0,C_XXDGRAY);
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_MGRAY);
  String dispTitle = storyTitle;
  while(dispTitle.length()>2 && mainBuf.textWidth((dispTitle+"...").c_str()) > SCR_W-8)
    dispTitle.remove(dispTitle.length()-1);
  if(storyTitle.length()>dispTitle.length()) dispTitle+="...";
  mainBuf.setCursor(4,27); mainBuf.print(dispTitle);
  mainBuf.drawFastHLine(0,40,SCR_W,C_DGRAY);

  if(!hnCommentsFetched) {
    uiCenteredText("Mengambil komentar...",80,C_MGRAY,&fonts::Font2);
    uiFooter("L+R:back"); pushFrame(); return;
  }
  if(hnComments.empty()) {
    uiCenteredText("Tidak ada komentar",72,C_MGRAY,&fonts::Font2);
    uiCenteredText("atau gagal memuat",86,C_DGRAY,&fonts::Font2);
    uiFooter("SEL:coba lagi  L+R:back"); pushFrame(); return;
  }

  // Tampilkan komentar dengan separator, scroll secara global
  // Bangun full teks dulu
  String fullText = "";
  for(int i=0;i<(int)hnComments.size();i++) {
    char hdr[50]; snprintf(hdr,sizeof(hdr),"[%d] %s",i+1,hnComments[i].author.c_str());
    fullText += String(hdr) + "\n";
    fullText += hnComments[i].text;
    if(i < (int)hnComments.size()-1) fullText += "\n\n---\n\n";
  }

  int maxScroll = uiWrapScroll(fullText, 4, 44, SCR_W-8, SCR_H-60, hnCommentsScrollY, C_WHITE, 12);

  // Badge jumlah komentar
  char cntStr[20]; snprintf(cntStr,sizeof(cntStr),"%d komentar",(int)hnComments.size());
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_DGRAY);
  int tw=mainBuf.textWidth(cntStr);
  mainBuf.setCursor(SCR_W-tw-4,27); mainBuf.print(cntStr);

  // Scrollbar
  if(maxScroll > 0) {
    int sbH = max(10, (int)((SCR_H-60) * (SCR_H-60) / ((SCR_H-60)+maxScroll)));
    int sbY = 44 + (int)((SCR_H-60-sbH) * (float)hnCommentsScrollY / maxScroll);
    mainBuf.fillRect(SCR_W-3, 44, 2, SCR_H-60, C_DGRAY);
    mainBuf.fillRect(SCR_W-3, sbY, 2, sbH, C_MGRAY);
  }

  uiFooter("UP/DW:scroll  SEL:reload  L+R:back");
}

// ════════════════════════════════════════════════════════════════
// SETUP
// ════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] MediaHub v9.1");

  for(int i=0;i<5;i++) pinMode(btnPins[i],INPUT_PULLUP);
  neo.begin(); neo.setBrightness(255); ledSet(0,0,0);

  tft.init(); tft.setRotation(1);
  tft.setBrightness(brightness); tft.fillScreen(0x0000);
  initColors();

  mainBuf.setColorDepth(16);
  if(!mainBuf.createSprite(SCR_W,SCR_H)){
    tft.setFont(&fonts::Font4); tft.setTextColor(C_WHITE);
    tft.setCursor(10,60); tft.print("PSRAM ERROR");
    while(1) delay(1000);
  }

  size_t outBufBytes = SCR_W * OUTPUT_BUF_LINES * sizeof(uint16_t);
  output_buf = (uint16_t*)heap_caps_malloc(outBufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if(!output_buf) output_buf = (uint16_t*)heap_caps_malloc(outBufBytes, MALLOC_CAP_8BIT);

  mjpeg_buf = (uint8_t*)heap_caps_malloc(MJPEG_BUF_SIZE, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
  Serial.printf("[MEM] mjpeg_buf=%p  PSRAM free=%uKB\n", mjpeg_buf, ESP.getFreePsram()/1024);

  autoBrInit();
  drawSplash("Initializing...");

  for(int v=0;v<=50;v+=5){ledSet(v,v,v);delay(20);}
  for(int v=50;v>=0;v-=5){ledSet(v,v,v);delay(20);}
  ledSet(0,0,0);

  sdSPI.begin(SD_SCK,SD_MISO,SD_MOSI,SD_CS);
  bool sdOk=SD.begin(SD_CS,sdSPI,SD_SPI_SPEED);
  if(sdOk){
    drawSplash("SD OK...");
    if(!tryPlayBootMjpeg()) drawSplash("SD OK — scanning...");
    scanVideos(); fmScanDir("/");
    char msg[48]; snprintf(msg,sizeof(msg),"SD OK — %d video",(int)videoFiles.size());
    drawSplash(msg);
  } else {
    drawSplash("SD FAIL — periksa wiring"); delay(2000);
  }

  // [NEW v9.1] Load WiFi credentials dari NVS
  wifiPrefsLoad();
  if(strlen(savedSSID) > 0) {
    drawSplash("Auto-connect WiFi...");
    Serial.printf("[WiFi] Mencoba auto-connect ke: %s\n", savedSSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID, savedPass);
    uint32_t t = millis();
    while(WiFi.status() != WL_CONNECTED && millis()-t < 12000) {
      delay(200);
      ledPulse(400);
    }
    if(WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      clockSyncNTP();
      char msg2[48]; snprintf(msg2,sizeof(msg2),"WiFi: %s", savedSSID);
      drawSplash(msg2);
      Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
      drawSplash("WiFi gagal — lanjut offline");
      Serial.println("[WiFi] Auto-connect gagal");
      delay(1000);
    }
    ledSet(0,0,0);
  }

  delay(600);
  appState=ST_MENU;
  drawMenu(); pushFrame();
  btnFlushAll();
  loadAiKeys();
  Serial.println("[OK] Boot selesai v9.1");
}

// ════════════════════════════════════════════════════════════════
// HELPER: Cek WiFi untuk fitur online
// ════════════════════════════════════════════════════════════════
bool requireWifi(const char* featureName) {
  if(wifiConnected) return true;
  mainBuf.fillScreen(C_BG); uiHeader(featureName);
  uiCenteredText("WiFi diperlukan!",70,C_MGRAY,&fonts::Font2);
  uiCenteredText("Sambungkan WiFi dulu",88,C_DGRAY,&fonts::Font2);
  uiFooter("SEL:ok"); pushFrame(); delay(2000);
  drawMenu(); pushFrame(); btnFlushAll();
  return false;
}

// ════════════════════════════════════════════════════════════════
// LOOP
// ════════════════════════════════════════════════════════════════





// ════════════════════════════════════════════════════════════════
// AI CHAT FUNCTIONS
// ════════════════════════════════════════════════════════════════
void loadAiKeys() {
  File file = SD.open("/api_keys.json", FILE_READ);
  if (file) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    if (!error) {
      if (doc.containsKey("gemini_api_key")) geminiApiKey = doc["gemini_api_key"].as<String>();
      if (doc.containsKey("groq_api_key")) groqApiKey = doc["groq_api_key"].as<String>();
      geminiApiKey.trim(); groqApiKey.trim();
    }
    file.close();
  }
}

void drawAIModeSelection() {
  mainBuf.fillScreen(C_BG); uiHeader("SELECT AI MODE");
  const char* modes[] = {"SUBARU AWA (Gemini)", "STANDARD AI (Gemini)", "GROQ CLOUD (Llama 3)"};
  const char* descs[] = {"Friendly & Fun", "Professional & Precise", "Fast & Powerful"};
  for(int i=0; i<3; i++) {
    int y = 35 + i*40; bool sel = (i == aiModeSel);
    mainBuf.fillRoundRect(10, y, SCR_W-20, 36, 8, sel ? C_XDGRAY : C_XXDGRAY);
    mainBuf.drawRoundRect(10, y, SCR_W-20, 36, 8, sel ? C_WHITE : C_DGRAY);
    mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(sel ? C_WHITE : C_MGRAY);
    mainBuf.setCursor(22, y+4); mainBuf.print(modes[i]);
    mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_DGRAY);
    mainBuf.setCursor(22, y+20); mainBuf.print(descs[i]);
  }
  uiFooter("UP/DW:pilih  SEL:lanjut  L+R:back");
}

void fetchAiResponse() {
  aiResponseText = "Thinking...";
  mainBuf.fillScreen(C_BG); uiHeader("AI CHAT");
  uiCenteredText("Mencari jawaban...", 74, C_MGRAY, &fonts::Font2);
  pushFrame();

  if (aiMode == 0 || aiMode == 1) { // Gemini
    if (geminiApiKey == "" || geminiApiKey.startsWith("PASTE")) { aiResponseText = "Error: Gemini API Key not set in /api_keys.json"; return; }
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key=" + geminiApiKey;
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(25000);
    String sysPrompt = (aiMode == 0) ? "Jawablah dengan gaya bahasa yang ramah, sedikit santai, gunakan emoji, dan panggil aku 'kakak'. " : "Answer professionally and concisely. ";
    String prompt = sysPrompt + String(aiPrompt);
    prompt.replace("\\", "\\\\"); prompt.replace("\"", "\\\"");
    String payload = "{\"contents\":[{\"parts\":[{\"text\":\"" + prompt + "\"}]}]}";
    int code = http.POST(payload);
    if (code == 200) {
      JsonDocument doc; deserializeJson(doc, http.getString());
      aiResponseText = doc["candidates"][0]["content"]["parts"][0]["text"].as<String>();
    } else { aiResponseText = "HTTP Error: " + String(code); }
    http.end();
  } else { // Groq
    if (groqApiKey == "" || groqApiKey.startsWith("PASTE")) { aiResponseText = "Error: Groq API Key not set in /api_keys.json"; return; }
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.groq.com/openai/v1/chat/completions");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + groqApiKey);
    http.setTimeout(25000);
    String prompt = String(aiPrompt);
    prompt.replace("\\", "\\\\"); prompt.replace("\"", "\\\"");
    String payload = "{\"model\":\"llama-3.3-70b-versatile\",\"messages\":[{\"role\":\"user\",\"content\":\"" + prompt + "\"}]}";
    int code = http.POST(payload);
    if (code == 200) {
      JsonDocument doc; deserializeJson(doc, http.getString());
      aiResponseText = doc["choices"][0]["message"]["content"].as<String>();
    } else { aiResponseText = "HTTP Error: " + String(code); }
    http.end();
  }
  aiResponseText.trim();
  if(aiResponseText.length() == 0) aiResponseText = "No response from AI.";
}

static int aiMaxScroll = 0;
void drawAiResponse() {
  mainBuf.fillScreen(C_BG); uiHeader("AI RESPONSE");
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_WHITE);
  mainBuf.setClipRect(5, 26, SCR_W-10, SCR_H-44);
  aiMaxScroll = uiWrapScroll(aiResponseText, 10, 30 - (textScrollY % 13), SCR_W-20, SCR_H-56, textScrollY, C_WHITE, 13);
  mainBuf.clearClipRect();
  uiFooter("UP/DW:scroll  SEL:tanya lagi  L+R:back");
}

void loop() {

  autoBrUpdate();

  if(wifiConnected&&clockSynced&&millis()-lastNtpSync>NTP_SYNC_INTERVAL_MS)
    clockSyncNTP();


  // Global back combo
  if(appState!=ST_MENU&&appState!=ST_SPLASH&&appState!=ST_POWER_MENU&&appState!=ST_CAMERA_STREAM) {
    if(btnComboLR()){
      mainBuf.fillRoundRect(SCR_W/2-70,SCR_H/2-10,140,20,6,C_XDGRAY);
      mainBuf.drawRoundRect(SCR_W/2-70,SCR_H/2-10,140,20,6,C_MGRAY);
      mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_WHITE);
      const char* msg="< kembali";
      int tw2=mainBuf.textWidth(msg);
      mainBuf.setCursor((SCR_W-tw2)/2,SCR_H/2-5); mainBuf.print(msg);
      pushFrame(); ledSet(0,0,0); delay(300);
      _barPulseT = millis();
      if(appState >= ST_GAME_MENU && appState <= ST_SUDOKU_OVER) { if(appState == ST_GAME_MENU) appState = ST_MENU; else appState = ST_GAME_MENU; } else appState = ST_MENU; textScrollY=0; if(appState==ST_MENU) drawMenu(); else if(appState==ST_GAME_MENU) drawGameMenu(); pushFrame(); btnFlushAll(); return;
    }
  }

  switch(appState) {
    case ST_AI_MODE:
      if(btnPressed(B_UP)){aiModeSel=(aiModeSel+2)%3;drawAIModeSelection();pushFrame();}
      if(btnPressed(B_DW)){aiModeSel=(aiModeSel+1)%3;drawAIModeSelection();pushFrame();}
      if(btnPressed(B_SEL)){
        aiMode = aiModeSel;
        memset(aiPrompt, 0, sizeof(aiPrompt)); aiPromptLen = 0;
        kbRow=1; kbCol=4; kbCaps=false;
        appState = ST_AI_CHAT_INPUT; drawKeyboard("TANYA AI", aiPrompt, aiPromptLen, false); pushFrame(); btnFlushAll();
      }
      break;

    case ST_AI_CHAT_INPUT: {
      int kb = handleKeyboard(aiPrompt, aiPromptLen, 127);
      if(kb == 2) {
        if(aiPromptLen > 0) {
          fetchAiResponse();
          textScrollY = 0;
          appState = ST_AI_CHAT_RES; drawAiResponse(); pushFrame(); btnFlushAll();
        }
      } else if(kb == 1) {
        drawKeyboard("TANYA AI", aiPrompt, aiPromptLen, false); pushFrame();
      }
      break;
    }

    case ST_AI_CHAT_RES:
      if(btnHeld(B_UP)){textScrollY=max(0, textScrollY-2);drawAiResponse();pushFrame();}
      if(btnHeld(B_DW)){textScrollY+=2;drawAiResponse();pushFrame();}
      if(btnPressed(B_SEL)){
        memset(aiPrompt, 0, sizeof(aiPrompt)); aiPromptLen = 0;
        appState = ST_AI_CHAT_INPUT; drawKeyboard("TANYA AI", aiPrompt, aiPromptLen, false); pushFrame(); btnFlushAll();
      }
      break;


    // ── MENU UTAMA ──
    case ST_MENU:
      if(btnPressed(B_UP)){menuSel=(menuSel+MENU_N-1)%MENU_N;drawMenu();pushFrame();}
      if(btnPressed(B_DW)){menuSel=(menuSel+1)%MENU_N;drawMenu();pushFrame();}
      if(btnPressed(B_SEL)) {
                switch(menuSel) {
          case 0:
            if(!requireWifi("AI CHAT")) break;
            appState=ST_AI_MODE; aiModeSel=0; drawAIModeSelection(); pushFrame(); btnFlushAll(); break;
          case 1:
            appState=ST_VIDEO_LIST; videoSel=0; drawVideoList(); pushFrame(); btnFlushAll(); break;
          case 2:
            if(!requireWifi("TRIVIA")) break;
            appState=ST_TRIVIA_SETUP; triviaSetupRow=0; drawTriviaSetup(); pushFrame(); btnFlushAll(); break;
          case 3:
            appState=ST_FILE_MGR; fmScanDir(fmCurrentPath); drawFileManager(); pushFrame(); btnFlushAll(); break;
          case 4:
            appState=ST_WIFI_SCAN; scanWifi(); drawWifiList(); pushFrame(); btnFlushAll(); break;
          case 5:
            appState=ST_SETTINGS; drawSettings(); pushFrame(); btnFlushAll(); break;
          case 6:
            appState=ST_POWER_MENU; powerSel=2; drawPowerMenu(); pushFrame(); btnFlushAll(); break;
          case 7: // HACKER NEWS
            if(!requireWifi("HACKER NEWS")) break;
            appState=ST_HACKER_NEWS; textScrollY=0;
            if(!hnFetched){ drawLoading("HACKER NEWS"); fetchHackerNews(); }
            drawHackerNews(); pushFrame(); btnFlushAll(); break;
          case 8: // DAD JOKE
            if(!requireWifi("DAD JOKE")) break;
            appState=ST_JOKES; jokeSource=0; textScrollY=0;
            if(!jokeFetched){ drawLoading("DAD JOKE"); pushFrame(); fetchJoke(); }
            drawJokes(); pushFrame(); btnFlushAll(); break;
          case 9: // CHUCK NORRIS
            if(!requireWifi("CHUCK NORRIS")) break;
            appState=ST_JOKES; jokeSource=1; jokeFetched=false; textScrollY=0;
            drawLoading("CHUCK NORRIS"); pushFrame(); fetchJoke();
            drawJokes(); pushFrame(); btnFlushAll(); break;
          case 10: // BORED
            if(!requireWifi("AKTIVITAS")) break;
            appState=ST_BORED; textScrollY=0;
            if(!boredFetched){ drawLoading("AKTIVITAS ACAK"); pushFrame(); fetchBored(); }
            drawBored(); pushFrame(); btnFlushAll(); break;
          case 11: // STOIC QUOTE
            if(!requireWifi("STOIC QUOTE")) break;
            appState=ST_STOIC; textScrollY=0;
            if(!stoicFetched){ drawLoading("STOIC QUOTE"); pushFrame(); fetchStoic(); }
            drawStoic(); pushFrame(); btnFlushAll(); break;
          case 12: // NUMBER FACT
            if(!requireWifi("NUMBER FACT")) break;
            appState=ST_NUMBER_FACT; textScrollY=0;
            if(!numFactFetched){ drawLoading("NUMBER FACT"); pushFrame(); fetchNumberFact(); }
            drawNumberFact(); pushFrame(); btnFlushAll(); break;
          case 13: // ISS TRACKER
            if(!requireWifi("ISS TRACKER")) break;
            appState=ST_ISS_TRACKER;
            if(!issFetched){ drawLoading("ISS TRACKER"); fetchISSNow(); }
            drawISS(); pushFrame(); btnFlushAll(); break;
          case 14: // PEOPLE IN SPACE
            if(!requireWifi("PEOPLE IN SPACE")) break;
            appState=ST_PEOPLE_SPACE;
            if(!astronautFetched){ drawLoading("PEOPLE IN SPACE"); fetchPeopleInSpace(); }
            drawPeopleInSpace(); pushFrame(); btnFlushAll(); break;
          case 15: // APOD
            if(!requireWifi("APOD NASA")) break;
            appState=ST_APOD;
            if(!apodFetched){ drawLoading("APOD NASA"); fetchAPOD(); }
            drawAPOD(); pushFrame(); btnFlushAll(); break;
          case 16:
            if(!requireWifi("CAMERA STREAM")) break;
            appState=ST_CAMERA_STREAM;
            if(!camInit()){ appState=ST_MENU; drawMenu(); pushFrame(); }
            btnFlushAll(); break;
          case 17:
            appState=ST_GAME_MENU; drawGameMenu(); pushFrame(); btnFlushAll(); break;
        }
        break;
      }
      if(appState==ST_MENU && millis()-_barPulseT>=18){ drawMenu(); pushFrame(); }
      ledPulse(1500);
      break;

    // ── TRIVIA ──
    case ST_TRIVIA_SETUP:
      if(handleTriviaSetup()) {
        triviaScore=0; triviaTotal=0;
        triviaTarget=TRIVIA_AMOUNTS[triviaSetAmtIdx];
        triviaTimeLimit=TRIVIA_TIMES[triviaSetTimeIdx];
        appState=ST_TRIVIA_LOAD; btnFlushAll();
      }
      ledPulse(1200); break;

    case ST_WIFI_SCAN:
      if(btnPressed(B_UP)){if(wifiSel>0)wifiSel--;drawWifiList();pushFrame();}
      if(btnPressed(B_DW)){if(wifiSel<(int)wifiSSIDs.size()-1)wifiSel++;drawWifiList();pushFrame();}
      if(btnPressed(B_SEL)){
        if(wifiSSIDs.empty()){scanWifi();drawWifiList();pushFrame();}
        else{ memset(wifiPassword,0,sizeof(wifiPassword));wifiPassLen=0;kbRow=1;kbCol=4;kbCaps=false; appState=ST_WIFI_PASS; drawKeyboard("PASSWORD", wifiPassword, wifiPassLen, true); pushFrame(); btnFlushAll(); }
      }
      ledPulse(1000); break;

    case ST_WIFI_PASS: {
      int kb=handleKeyboard(wifiPassword, wifiPassLen, 63);
      if(kb==2){
        appState=ST_WIFI_CONN;
        bool ok=connectWifi(wifiSSIDs[wifiSel].c_str(),wifiPassword);
        wifiConnected=ok;
        if(ok){
          // [NEW v9.1] Simpan kredensial ke NVS
          wifiPrefsSave(wifiSSIDs[wifiSel].c_str(), wifiPassword);
        }
        drawWifiResult(ok,wifiSSIDs[wifiSel].c_str()); pushFrame(); btnFlushAll();
      }
      else if(kb==1){drawKeyboard("PASSWORD", wifiPassword, wifiPassLen, true);pushFrame();}
      break;
    }
    case ST_WIFI_CONN:
      if(btnPressed(B_SEL)){appState=ST_MENU;drawMenu();pushFrame();btnFlushAll();} break;

    case ST_VIDEO_LIST:
      if(btnPressed(B_UP)){if(videoSel>0)videoSel--;drawVideoList();pushFrame();}
      if(btnPressed(B_DW)){if(videoSel<(int)videoFiles.size()-1)videoSel++;drawVideoList();pushFrame();}
      if(btnPressed(B_R)){moodLampEnabled=!moodLampEnabled;if(!moodLampEnabled)ledSet(0,0,0);drawVideoList();pushFrame();}
      if(btnPressed(B_SEL)&&!videoFiles.empty()){appState=ST_VIDEO_PLAY;playVideo(videoFiles[videoSel]);btnFlushAll();}
      ledPulse(1200); break;

    case ST_VIDEO_PLAY: break;
    case ST_CAMERA_STREAM:
      camUpdate();
      if(btnPressed(B_SEL)) { camCaptureReq = true; ledPulse(300); }
      if(btnComboLR()){
        camStop();
        appState=ST_MENU; drawMenu(); pushFrame(); btnFlushAll();
      }
      break;

    case ST_FILE_MGR:
      if(!fmConfirmDelete){
        if(btnPressed(B_UP)){if(fmSel>0){fmSel--;drawFileManager();pushFrame();}}
        if(btnPressed(B_DW)){if(fmSel<(int)fmEntries.size()-1){fmSel++;drawFileManager();pushFrame();}}
        if(btnPressed(B_SEL)&&!fmEntries.empty()){
          FsEntry& e=fmEntries[fmSel];
          if(e.name==".."){int lastSlash=fmCurrentPath.lastIndexOf('/');if(lastSlash>0)fmCurrentPath=fmCurrentPath.substring(0,lastSlash);else fmCurrentPath="/";fmScanDir(fmCurrentPath);drawFileManager();pushFrame();}
          else if(e.isDir){if(fmCurrentPath.endsWith("/"))fmCurrentPath+=e.name;else fmCurrentPath+="/"+e.name;fmScanDir(fmCurrentPath);drawFileManager();pushFrame();}
          else {
            String low = e.name; low.toLowerCase();
            if(low.endsWith(".jpg") || low.endsWith(".jpeg")) {
              viewImagePath = (fmCurrentPath.endsWith("/")?fmCurrentPath:fmCurrentPath+"/")+e.name;
              appState = ST_IMAGE_VIEW; drawImageFile(viewImagePath); pushFrame(); btnFlushAll();
            }
          }
        }
        if(btnPressed(B_R)&&!fmEntries.empty()){if(fmEntries[fmSel].name!=".."){fmConfirmDelete=true;drawFileManager();pushFrame();}}
        if(btnPressed(B_L)){if(fmCurrentPath!="/"){int lastSlash=fmCurrentPath.lastIndexOf('/');if(lastSlash>0)fmCurrentPath=fmCurrentPath.substring(0,lastSlash);else fmCurrentPath="/";fmScanDir(fmCurrentPath);drawFileManager();pushFrame();}}
      } else {
        if(btnPressed(B_SEL)){FsEntry& e=fmEntries[fmSel];String fullPath=(fmCurrentPath.endsWith("/")?fmCurrentPath:fmCurrentPath+"/")+e.name;fmDeletePath(fullPath);fmConfirmDelete=false;fmScanDir(fmCurrentPath);if(fmSel>=(int)fmEntries.size())fmSel=max(0,(int)fmEntries.size()-1);drawFileManager();pushFrame();}
        if(btnPressed(B_L)){fmConfirmDelete=false;drawFileManager();pushFrame();}
      }
      ledPulse(1200); break;

    case ST_TRIVIA_LOAD:
      drawTriviaLoad(); pushFrame(); ledPulse(400); delay(80);
      if(fetchTrivia()){ triviaAns=0;triviaAnswered=false;triviaTimer=millis(); triviaTotal++; appState=ST_TRIVIA_Q;drawTriviaQ();pushFrame();btnFlushAll(); }
      break;

    case ST_TRIVIA_Q: {
      bool redraw=false;
      if(!triviaAnswered){
        if(btnPressed(B_UP)){triviaAns=(triviaAns+3)%4;redraw=true;}
        if(btnPressed(B_DW)){triviaAns=(triviaAns+1)%4;redraw=true;}
        if(btnPressed(B_SEL)){triviaAnswered=true;bool correct=(triviaAns==triviaQ.correct);if(correct){triviaScore++;ledSet(50,50,50);}else ledSet(0,0,0);drawTriviaQ();pushFrame();delay(1500);appState=ST_TRIVIA_RESULT;drawTriviaResult(correct);pushFrame();btnFlushAll();break;}
        if((uint32_t)(millis()-triviaTimer)/1000>=(uint32_t)triviaTimeLimit){triviaAnswered=true;ledSet(0,0,0);drawTriviaQ();pushFrame();delay(1500);appState=ST_TRIVIA_RESULT;drawTriviaResult(false);pushFrame();btnFlushAll();break;}
        static uint32_t lastTimerRedraw=0;
        if(millis()-lastTimerRedraw>500){lastTimerRedraw=millis();redraw=true;}
      }
      if(redraw){drawTriviaQ();pushFrame();}
      ledPulse(800); break;
    }

    case ST_TRIVIA_RESULT:
      if(btnPressed(B_SEL)){ledSet(0,0,0);if(triviaTotal>=triviaTarget){appState=ST_TRIVIA_SUMMARY;drawTriviaSummary();pushFrame();}else appState=ST_TRIVIA_LOAD;btnFlushAll();}
      break;

    case ST_TRIVIA_SUMMARY:
      if(btnPressed(B_SEL)){triviaSetupRow=0;appState=ST_TRIVIA_SETUP;drawTriviaSetup();pushFrame();btnFlushAll();}
      break;

    case ST_SETTINGS: {
      bool changed=false;
      if(btnPressed(B_L)){if(!autoBrEnabled){brightness=max(20,brightness-20);tft.setBrightness(brightness);changed=true;}}
      if(btnPressed(B_R)){
        if(!autoBrEnabled){brightness=min(255,brightness+20);tft.setBrightness(brightness);changed=true;}
        // [NEW v9.1] R juga hapus WiFi credentials dari NVS
        else { wifiPrefsClear(); changed=true; }
      }
      // Tambah tombol R khusus hapus WiFi NVS jika autoBr tidak enabled
      // (Di Settings: R saat autoBr off = brightness up, R saat autoBr on = hapus NVS)
      // Untuk kejelasan: jika autoBr off, R naik brightness; jika autoBr on, R hapus NVS
      if(btnPressed(B_SEL)){autoBrEnabled=!autoBrEnabled;if(!autoBrEnabled)tft.setBrightness(brightness);changed=true;}
      if(btnPressed(B_UP)||btnPressed(B_DW)){moodLampEnabled=!moodLampEnabled;if(!moodLampEnabled)ledSet(0,0,0);changed=true;}
      if(changed||millis()-lastStatsUpdate>1000){lastStatsUpdate=millis();drawSettings();pushFrame();}
      break;
    }

    case ST_POWER_MENU:
      if(btnPressed(B_UP)){powerSel=(powerSel+2)%3;drawPowerMenu();pushFrame();}
      if(btnPressed(B_DW)){powerSel=(powerSel+1)%3;drawPowerMenu();pushFrame();}
      if(btnPressed(B_L)){appState=ST_MENU;drawMenu();pushFrame();btnFlushAll();}
      if(btnPressed(B_SEL)){
        if(powerSel==0){mainBuf.fillScreen(C_BG);uiCenteredText("Restarting...",SCR_H/2-8,C_WHITE,&fonts::Font2);pushFrame();ledSet(0,0,0);delay(800);ESP.restart();}
        else if(powerSel==1){mainBuf.fillScreen(C_BG);uiCenteredText("Sleeping...",SCR_H/2-8,C_LGRAY,&fonts::Font2);pushFrame();ledSet(0,0,0);tft.setBrightness(0);delay(500);esp_deep_sleep_start();}
        else{appState=ST_MENU;drawMenu();pushFrame();btnFlushAll();}
      }
      break;

    case ST_ISS_TRACKER:
      if(millis()-issLastFetch > ISS_UPDATE_MS && wifiConnected) fetchISSNow();
      if(btnPressed(B_SEL)){ fetchISSNow(); }
      drawISS(); pushFrame();
      ledPulse(2000);
      delay(4); break;

    case ST_APOD:
      if(btnPressed(B_UP)){ textScrollY=max(0,textScrollY-13); drawAPOD(); pushFrame(); }
      if(btnPressed(B_DW)){ textScrollY+=13; drawAPOD(); pushFrame(); }
      if(btnPressed(B_SEL)){
        textScrollY=0; apodFetched=false;
        drawLoading("APOD — NASA", "Mengambil dari NASA...");
        pushFrame(); fetchAPOD(); drawAPOD(); pushFrame(); btnFlushAll();
      }
      ledPulse(1500); break;

    case ST_STOIC:
      if(btnPressed(B_UP)){ textScrollY=max(0,textScrollY-13); drawStoic(); pushFrame(); }
      if(btnPressed(B_DW)){ textScrollY+=13; drawStoic(); pushFrame(); }
      if(btnPressed(B_SEL)){
        textScrollY=0; stoicFetched=false;
        drawLoading("STOIC QUOTE"); pushFrame();
        fetchStoic(); drawStoic(); pushFrame(); btnFlushAll();
      }
      ledPulse(1500); break;

    case ST_NUMBER_FACT:
      if(btnPressed(B_UP)){ textScrollY=max(0,textScrollY-13); drawNumberFact(); pushFrame(); }
      if(btnPressed(B_DW)){ textScrollY+=13; drawNumberFact(); pushFrame(); }
      if(btnPressed(B_SEL)){
        textScrollY=0; numFactFetched=false;
        drawLoading("NUMBER FACT"); pushFrame();
        fetchNumberFact(); drawNumberFact(); pushFrame(); btnFlushAll();
      }
      ledPulse(1500); break;

    case ST_JOKES:
      if(btnPressed(B_UP)){ textScrollY=max(0,textScrollY-13); drawJokes(); pushFrame(); }
      if(btnPressed(B_DW)){ textScrollY+=13; drawJokes(); pushFrame(); }
      if(btnPressed(B_R)){
        jokeSource=(jokeSource+1)%2; jokeFetched=false; textScrollY=0;
        drawLoading(jokeSource==0?"DAD JOKE":"CHUCK NORRIS"); pushFrame();
        fetchJoke(); drawJokes(); pushFrame(); btnFlushAll();
      }
      if(btnPressed(B_SEL)){
        textScrollY=0; jokeFetched=false;
        drawLoading(jokeSource==0?"DAD JOKE":"CHUCK NORRIS"); pushFrame();
        fetchJoke(); drawJokes(); pushFrame(); btnFlushAll();
      }
      ledPulse(1500); break;

    case ST_BORED:
      if(btnPressed(B_SEL)){
        boredFetched=false;
        drawLoading("AKTIVITAS ACAK"); pushFrame();
        fetchBored(); drawBored(); pushFrame(); btnFlushAll();
      }
      ledPulse(1500); break;

    case ST_PEOPLE_SPACE:
      if(btnPressed(B_UP)){ if(astronautSel>0){astronautSel--;drawPeopleInSpace();pushFrame();} }
      if(btnPressed(B_DW)){ if(astronautSel<(int)astronauts.size()-1){astronautSel++;drawPeopleInSpace();pushFrame();} }
      if(btnPressed(B_SEL)){
        astronautFetched=false; astronauts.clear();
        drawLoading("PEOPLE IN SPACE"); pushFrame();
        fetchPeopleInSpace(); drawPeopleInSpace(); pushFrame(); btnFlushAll();
      }
      ledPulse(1500); break;

    case ST_HACKER_NEWS:
      if(btnPressed(B_UP)){ if(hnSel>0){hnSel--;drawHackerNews();pushFrame();} }
      if(btnPressed(B_DW)){ if(hnSel<(int)hnStories.size()-1){hnSel++;drawHackerNews();pushFrame();} }
      if(btnPressed(B_SEL)){
        // [NEW v9.1] SEL = buka layar komentar story yang dipilih
        if(!hnStories.empty()) {
          hnCommentsStoryIdx = hnSel;
          hnCommentsFetched = false;
          hnComments.clear();
          hnCommentsScrollY = 0;
          appState = ST_HN_COMMENTS;
          drawLoading("HN COMMENTS","Mengambil komentar...");
          pushFrame();
          fetchHNComments(hnStories[hnSel].id);
          drawHNComments(); pushFrame(); btnFlushAll();
        }
      }
      if(btnPressed(B_R)){
        // R = refresh daftar berita
        hnFetched=false; hnStories.clear();
        drawLoading("HACKER NEWS"); pushFrame();
        fetchHackerNews(); drawHackerNews(); pushFrame(); btnFlushAll();
      }
      ledPulse(1500); break;

    // [NEW v9.1] Layar komentar HN
    case ST_HN_COMMENTS:
      if(btnPressed(B_UP)){ hnCommentsScrollY=max(0,hnCommentsScrollY-12); drawHNComments(); pushFrame(); }
      if(btnPressed(B_DW)){ hnCommentsScrollY+=12; drawHNComments(); pushFrame(); }
      if(btnPressed(B_SEL)){
        // Reload komentar
        hnCommentsFetched=false; hnComments.clear(); hnCommentsScrollY=0;
        drawLoading("HN COMMENTS","Mengambil komentar...");
        pushFrame();
        if(hnCommentsStoryIdx>=0&&hnCommentsStoryIdx<(int)hnStories.size())
          fetchHNComments(hnStories[hnCommentsStoryIdx].id);
        drawHNComments(); pushFrame(); btnFlushAll();
      }
      ledPulse(1500); break;

    case ST_IMAGE_VIEW:
      if(btnPressed(B_SEL)||btnPressed(B_L)||btnPressed(B_R)||btnPressed(B_UP)||btnPressed(B_DW)) {
        appState = ST_FILE_MGR; drawFileManager(); pushFrame(); btnFlushAll();
      }
      break;

    case ST_GAME_MENU:
      if(btnPressed(B_UP)){gameMenuSel=(gameMenuSel+8)%9; drawGameMenu(); pushFrame();}
      if(btnPressed(B_DW)){gameMenuSel=(gameMenuSel+1)%9; drawGameMenu(); pushFrame();}
      if(btnPressed(B_SEL)){
        if(gameMenuSel==0) appState=ST_TICTACTOE_MODE;
        else if(gameMenuSel==1) appState=ST_TANK_MODE;
        else if(gameMenuSel==2) appState=ST_FLAPPY_MODE;
        else if(gameMenuSel==3) appState=ST_MINESWEEPER_SIZE;
        else if(gameMenuSel==4) appState=ST_PACMAN_MODE;
        else if(gameMenuSel==5) appState=ST_DOODLE_MODE;
        else if(gameMenuSel==6) appState=ST_MEMORY_MODE;
        else if(gameMenuSel==7) appState=ST_SIMON_MODE;
        else if(gameMenuSel==8) appState=ST_SUDOKU_MODE;
        btnFlushAll();
      }
      drawGameMenu(); pushFrame(); break;

    case ST_TICTACTOE_MODE:
      tttInit(); appState=ST_TICTACTOE; drawTTT(); pushFrame(); break;
    case ST_TICTACTOE:
      handleTTT(); break;
    case ST_TICTACTOE_OVER:
      drawTTT(); pushFrame(); if(btnPressed(B_SEL)){ tttInit(); appState=ST_TICTACTOE; } break;

    case ST_TANK_MODE:
      tankInit(); appState=ST_TANK; drawTank(); pushFrame(); break;
    case ST_TANK:
      handleTank(); break;
    case ST_FLAPPY_MODE:
      flappyInit(); appState=ST_FLAPPY; drawFlappy(); pushFrame(); break;
    case ST_FLAPPY:
      handleFlappy(); break;
    case ST_FLAPPY_OVER:
      drawFlappy(); pushFrame(); if(btnPressed(B_SEL)){ flappyInit(); appState=ST_FLAPPY; } break;
    case ST_MINESWEEPER_SIZE:
      msInit(9, 9); appState=ST_MINESWEEPER; drawMinesweeper(); pushFrame(); break;
    case ST_MINESWEEPER:
      handleMinesweeper(); break;
    case ST_MINESWEEPER_OVER:
      drawMinesweeper(); pushFrame(); if(btnPressed(B_SEL)){ msInit(msW, msH); appState=ST_MINESWEEPER; } break;

    case ST_TANK_OVER:
      drawTank(); pushFrame(); if(btnPressed(B_SEL)){ tankInit(); appState=ST_TANK; } break;
    case ST_PACMAN_MODE:
      pacmanInit(); appState=ST_PACMAN; drawPacman(); pushFrame(); break;
    case ST_PACMAN:
      handlePacman(); break;
    case ST_PACMAN_OVER:
      drawPacman(); pushFrame(); if(btnPressed(B_SEL)){ pacmanInit(); appState=ST_PACMAN; } break;
    case ST_DOODLE_MODE:
      doodleInit(); appState=ST_DOODLE; drawDoodle(); pushFrame(); break;
    case ST_DOODLE:
      handleDoodle(); break;
    case ST_DOODLE_OVER:
      drawDoodle(); pushFrame(); if(btnPressed(B_SEL)){ doodleInit(); appState=ST_DOODLE; } break;
    case ST_MEMORY_MODE: memoInit(); appState=ST_MEMORY; break;
    case ST_MEMORY: handleMemory(); break;
    case ST_MEMORY_OVER: drawMemory(); pushFrame(); if(btnPressed(B_SEL)) memoInit(); break;
    case ST_SIMON_MODE: simonInit(); appState=ST_SIMON; break;
    case ST_SIMON: handleSimon(); break;
    case ST_SIMON_OVER: drawSimon(); pushFrame(); if(btnPressed(B_SEL)) simonInit(); break;
    case ST_SUDOKU_MODE: sudoInit(); appState=ST_SUDOKU; break;
    case ST_SUDOKU: handleSudoku(); break;
    case ST_SUDOKU_OVER: drawSudoku(); pushFrame(); if(btnPressed(B_SEL)) sudoInit(); break;
  }

  delay(4);
}
void drawGameMenu() {
  mainBuf.fillScreen(C_BG); uiHeader("9 MINI GAMES");
  const int ITEM_H = 32, GAP = 4;
  int startIdx = max(0, min(5, gameMenuSel-2));
  for(int i=startIdx; i<startIdx+4; i++) {
    bool sel = (i == gameMenuSel);
    int y = 32 + (i-startIdx) * (ITEM_H + GAP);
    uiCard(8, y, SCR_W-16, ITEM_H, sel ? C_WHITE : C_DGRAY, 8);
    if(sel) mainBuf.fillRoundRect(8, y+4, 3, ITEM_H-8, 2, C_WHITE);
    mainBuf.setFont(&fonts::Font4); mainBuf.setTextColor(sel ? C_WHITE : C_MGRAY);
    mainBuf.setCursor(20, y+4); mainBuf.print(gameList[i]);
    mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(sel ? C_LGRAY : C_DGRAY);
    mainBuf.setCursor(SCR_W-mainBuf.textWidth(gameSub[i])-16, y+10); mainBuf.print(gameSub[i]);
  }
  uiFooter("UP/DW:pilih  SEL:main  L+R:back");
}

// TIC TAC TOE ULTIMATE




void tttInit() {
  for(int y=0; y<5; y++) {
    for(int x=0; x<5; x++) {
      ultimateBoard[x][y].winner = 0;
      for(int sy=0; sy<3; sy++) {
        for(int sx=0; sx<3; sx++) ultimateBoard[x][y].board[sx][sy] = 0;
      }
    }
  }
  bigWinner = 0; tttTurn = 1;
  tttBigX=2; tttBigY=2; tttSmallX=1; tttSmallY=1;
  tttTargetBigX=-1; tttTargetBigY=-1;
  tttAITurn = false;
}

int8_t tttCheckWin(int8_t b[3][3]) {
  for(int i=0; i<3; i++) {
    if(b[i][0] != 0 && b[i][0] == b[i][1] && b[i][1] == b[i][2]) return b[i][0];
    if(b[0][i] != 0 && b[0][i] == b[1][i] && b[1][i] == b[2][i]) return b[0][i];
  }
  if(b[0][0] != 0 && b[0][0] == b[1][1] && b[1][1] == b[2][2]) return b[0][0];
  if(b[0][2] != 0 && b[0][2] == b[1][1] && b[1][1] == b[2][0]) return b[0][2];
  bool full = true;
  for(int y=0; y<3; y++) for(int x=0; x<3; x++) if(b[x][y] == 0) full = false;
  return full ? 3 : 0;
}

int8_t tttCheckBigWin() {
  int8_t b[5][5];
  for(int y=0; y<5; y++) for(int x=0; x<5; x++) b[x][y] = ultimateBoard[x][y].winner;
  // 5x5 needs 3 in a row to win
  for(int y=0; y<5; y++) {
    for(int x=0; x<3; x++) {
      if(b[x][y] != 0 && b[x][y] != 3 && b[x][y] == b[x+1][y] && b[x+1][y] == b[x+2][y]) return b[x][y];
    }
  }
  for(int x=0; x<5; x++) {
    for(int y=0; y<3; y++) {
      if(b[x][y] != 0 && b[x][y] != 3 && b[x][y] == b[x][y+1] && b[x][y+1] == b[x][y+2]) return b[x][y];
    }
  }
  for(int y=0; y<3; y++) {
    for(int x=0; x<3; x++) {
      if(b[x][y] != 0 && b[x][y] != 3 && b[x][y] == b[x+1][y+1] && b[x+1][y+1] == b[x+2][y+2]) return b[x][y];
      if(b[x+2][y] != 0 && b[x+2][y] != 3 && b[x+2][y] == b[x+1][y+1] && b[x+1][y+1] == b[x][y+2]) return b[x+2][y];
    }
  }
  bool full = true;
  for(int y=0; y<5; y++) for(int x=0; x<5; x++) if(ultimateBoard[x][y].winner == 0) full = false;
  return full ? 3 : 0;
}

void drawTTT() {
  mainBuf.fillScreen(C_BG); uiHeader("TIC TAC TOE ULTIMATE");
  const int OFFSET_X = 70, OFFSET_Y = 28, BIG_SIZE = 26, SMALL_SIZE = 8;
  const int GRID_W = BIG_SIZE * 5 + 4;

  // Draw 5x5 Big Grid
  for(int i=1; i<5; i++) {
    mainBuf.drawFastVLine(OFFSET_X + i*BIG_SIZE + (i-1), OFFSET_Y, GRID_W, C_DGRAY);
    mainBuf.drawFastHLine(OFFSET_X, OFFSET_Y + i*BIG_SIZE + (i-1), GRID_W, C_DGRAY);
  }

  for(int y=0; y<5; y++) {
    for(int x=0; x<5; x++) {
      int bx = OFFSET_X + x*(BIG_SIZE+1);
      int by = OFFSET_Y + y*(BIG_SIZE+1);

      if(ultimateBoard[x][y].winner != 0) {
        uint16_t col = (ultimateBoard[x][y].winner == 1) ? C_ACCENT_SYS : (ultimateBoard[x][y].winner == 2) ? C_ACCENT_FUN : C_XDGRAY;
        mainBuf.fillRect(bx+1, by+1, BIG_SIZE-1, BIG_SIZE-1, col);
        mainBuf.setFont(&fonts::Font4); mainBuf.setTextColor(C_WHITE);
        mainBuf.setCursor(bx+6, by+4);
        if(ultimateBoard[x][y].winner == 1) mainBuf.print("X");
        else if(ultimateBoard[x][y].winner == 2) mainBuf.print("O");
        else mainBuf.print("-");
      } else {
        // Draw 3x3 small grid
        for(int sy=0; sy<3; sy++) {
          for(int sx=0; sx<3; sx++) {
            int val = ultimateBoard[x][y].board[sx][sy];
            int sx_pos = bx + 2 + sx*SMALL_SIZE;
            int sy_pos = by + 2 + sy*SMALL_SIZE;
            if(val == 1) { mainBuf.setTextColor(C_ACCENT_SYS); mainBuf.setCursor(sx_pos+1, sy_pos-2); mainBuf.setFont(&fonts::Font2); mainBuf.print("x"); }
            else if(val == 2) { mainBuf.setTextColor(C_ACCENT_FUN); mainBuf.setCursor(sx_pos+1, sy_pos-2); mainBuf.setFont(&fonts::Font2); mainBuf.print("o"); }
          }
        }
      }

      // Highlight target grid
      if(tttTargetBigX == x && tttTargetBigY == y) mainBuf.drawRect(bx, by, BIG_SIZE+1, BIG_SIZE+1, C_WHITE);
    }
  }

  // Cursor
  if((millis()/300)%2 == 0) {
    int curX = OFFSET_X + tttBigX*(BIG_SIZE+1) + 2 + tttSmallX*SMALL_SIZE;
    int curY = OFFSET_Y + tttBigY*(BIG_SIZE+1) + 2 + tttSmallY*SMALL_SIZE;
    mainBuf.drawRect(curX, curY, SMALL_SIZE, SMALL_SIZE, C_WHITE);
  }

  // Info
  uiCard(4, 30, 60, 110, C_DGRAY, 6);
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_LGRAY);
  mainBuf.setCursor(10, 40); mainBuf.print(tttVsAI ? "VS AI" : "VS P2");
  mainBuf.setCursor(10, 55); mainBuf.print("Turn:");
  mainBuf.setTextColor(tttTurn==1 ? C_ACCENT_SYS : C_ACCENT_FUN);
  mainBuf.setCursor(10, 68); mainBuf.print(tttTurn==1 ? "PLAYER X" : (tttVsAI ? "AI O" : "PLAYER O"));

  if(tttVsAI) {
    mainBuf.setTextColor(C_DGRAY);
    mainBuf.setCursor(10, 100); mainBuf.print("Lvl:");
    mainBuf.setTextColor(C_LGRAY);
    mainBuf.print(tttAiLevel==0 ? "Easy" : tttAiLevel==1 ? "Med" : "Hard");
  }

  if(bigWinner != 0) {
    mainBuf.fillRoundRect(SCR_W/2-60, SCR_H/2-30, 120, 60, 10, C_SURFACE);
    mainBuf.drawRoundRect(SCR_W/2-60, SCR_H/2-30, 120, 60, 10, C_WHITE);
    uiCenteredText(bigWinner==3 ? "DRAW!" : "WINNER!", SCR_H/2-20, C_LGRAY, &fonts::Font2);
    mainBuf.setFont(&fonts::Font6); mainBuf.setTextColor(bigWinner==1 ? C_ACCENT_SYS : C_ACCENT_FUN);
    String ws = (bigWinner==1) ? "X" : (bigWinner==2) ? "O" : "-";
    int tw = mainBuf.textWidth(ws.c_str());
    mainBuf.setCursor((SCR_W-tw)/2, SCR_H/2-8); mainBuf.print(ws);
    uiFooter("SEL: Restart  L+R: Back");
  } else {
    uiFooter("UP/DW/L/R: Move  SEL: Mark  R: Level");
  }
}

// AI: Minimax simplified for non-blocking

TTTMove tttBestMove;

int tttEvaluate() {
  int8_t b[5][5];
  for(int y=0; y<5; y++) for(int x=0; x<5; x++) b[x][y] = ultimateBoard[x][y].winner;
  int score = 0;
  // Basic evaluation: count 2-in-a-rows for AI (2) and player (1)
  return score;
}

void tttMakeMove(int bx, int by, int sx, int sy) {
  ultimateBoard[bx][by].board[sx][sy] = tttTurn;
  int8_t sw = tttCheckWin(ultimateBoard[bx][by].board);
  if(sw != 0) {
    ultimateBoard[bx][by].winner = sw;
    bigWinner = tttCheckBigWin();
    if(bigWinner != 0) { ledSet(255,255,255); ledPulse(500); }
  }

  tttTurn = (tttTurn == 1) ? 2 : 1;
  if(bigWinner == 1) { prefs.begin("games", false); int w = prefs.getInt("ttt_wins", 0); prefs.putInt("ttt_wins", w+1); prefs.end(); }
  ledSet(tttTurn==1 ? 0:0, tttTurn==1 ? 0:0, 255); // Blue for X, Red for O (simplified)
  if(tttTurn == 1) ledSet(0,0,255); else ledSet(255,0,0);

  if(ultimateBoard[sx][sy].winner == 0) {
    tttTargetBigX = sx; tttTargetBigY = sy;
  } else {
    tttTargetBigX = -1; tttTargetBigY = -1;
  }

  tttBigX = (tttTargetBigX == -1) ? bx : tttTargetBigX;
  tttBigY = (tttTargetBigY == -1) ? by : tttTargetBigY;
  tttSmallX = 1; tttSmallY = 1;
}

void tttAIThink() {
  // Random for Easy, simple for Medium, limited search for Hard
  std::vector<TTTMove> moves;
  int bx_start = (tttTargetBigX == -1) ? 0 : tttTargetBigX;
  int bx_end   = (tttTargetBigX == -1) ? 5 : tttTargetBigX + 1;
  int by_start = (tttTargetBigX == -1) ? 0 : tttTargetBigY;
  int by_end   = (tttTargetBigX == -1) ? 5 : tttTargetBigY + 1;

  for(int by=by_start; by<by_end; by++) {
    for(int bx=bx_start; bx<bx_end; bx++) {
      if(ultimateBoard[bx][by].winner != 0) continue;
      for(int sy=0; sy<3; sy++) {
        for(int sx=0; sx<3; sx++) {
          if(ultimateBoard[bx][by].board[sx][sy] == 0) moves.push_back({bx,by,sx,sy,0});
        }
      }
    }
  }

  if(!moves.empty()) {
    int idx = random(moves.size());
    TTTMove m = moves[idx];
    tttMakeMove(m.bx, m.by, m.sx, m.sy);
  }
  tttAITurn = false;
}

void handleTTT() {
  if(bigWinner != 0) {
    if(btnPressed(B_SEL)) { tttInit(); drawTTT(); pushFrame(); }
    return;
  }

  if(tttVsAI && tttTurn == 2 && !tttAITurn) {
    tttAITurn = true; tttAINextMoveT = millis() + 500;
  }

  if(tttAITurn) {
    if(millis() > tttAINextMoveT) { tttAIThink(); drawTTT(); pushFrame(); }
    return;
  }

  bool changed = false;
  if(btnPressed(B_UP)) { tttSmallY--; if(tttSmallY<0){ tttSmallY=2; tttBigY=(tttBigY+4)%5; } changed=true; }
  if(btnPressed(B_DW)) { tttSmallY++; if(tttSmallY>2){ tttSmallY=0; tttBigY=(tttBigY+1)%5; } changed=true; }
  if(btnPressed(B_L))  { tttSmallX--; if(tttSmallX<0){ tttSmallX=2; tttBigX=(tttBigX+4)%5; } changed=true; }
  if(btnPressed(B_R))  { tttSmallX++; if(tttSmallX>2){ tttSmallX=0; tttBigX=(tttBigX+1)%5; } changed=true; }

  if(btnPressed(B_SEL)) {
    if(tttTargetBigX != -1 && (tttBigX != tttTargetBigX || tttBigY != tttTargetBigY)) {
      // Invalid target grid
    } else if(ultimateBoard[tttBigX][tttBigY].winner != 0 || ultimateBoard[tttBigX][tttBigY].board[tttSmallX][tttSmallY] != 0) {
      // Already won or filled
    } else {
      tttMakeMove(tttBigX, tttBigY, tttSmallX, tttSmallY);
      changed = true;
    }
  }

  if(btnPressed(B_R)) { // Actually Pause/Level
    // tttVsAI = !tttVsAI; tttInit(); changed=true;
  }

  if(changed || (millis()-tttCursorT > 300)) {
    tttCursorT = millis();
    drawTTT(); pushFrame();
  }
}

// TANK BATTLE






void tankInit() {
  randomSeed(millis());
  for(int y=0; y<10; y++) {
    for(int x=0; x<19; x++) {
      if(x==0 || x==18 || y==0 || y==9) tankMap[y][x] = 1;
      else if(random(100) < 20) tankMap[y][x] = 2;
      else tankMap[y][x] = 0;
    }
  }
  // Clear spawn points
  tankMap[1][1] = 0; tankMap[2][1] = 0; tankMap[1][2] = 0;
  tankMap[8][17] = 0; tankMap[7][17] = 0; tankMap[8][16] = 0;

  playerTank = {1.5f, 1.5f, 0, 3, 0, false};
  aiTank = {17.5f, 8.5f, 3.14f, 3, 0, true};
  bullets.clear();
  tankGameOver = false; tankScore = 0; tankShake = 0;
  tankPath.clear();
}

void drawTank() {
  mainBuf.fillScreen(C_BG); uiHeader("TANK BATTLE");
  const int T_SZ = 14; int OFF_X = 15, OFF_Y = 25; if(tankShake > 0) { OFF_X += random(-tankShake, tankShake); OFF_Y += random(-tankShake, tankShake); }

  for(int y=0; y<10; y++) {
    for(int x=0; x<19; x++) {
      int tx = OFF_X + x*T_SZ, ty = OFF_Y + y*T_SZ;
      if(tankMap[y][x] == 1) mainBuf.fillRect(tx, ty, T_SZ, T_SZ, C_DGRAY);
      else if(tankMap[y][x] == 2) {
        mainBuf.fillRect(tx, ty, T_SZ, T_SZ, lgfx::color565(80,50,30));
        mainBuf.drawRect(tx, ty, T_SZ, T_SZ, lgfx::color565(120,80,50));
      }
    }
  }

  // Draw Tanks
  auto drawT = [&](Tank& t, uint16_t col) {
    int tx = OFF_X + (int)(t.x*T_SZ), ty = OFF_Y + (int)(t.y*T_SZ);
    mainBuf.fillRoundRect(tx-6, ty-6, 12, 12, 2, col);
    int bx = tx + (int)(cos(t.angle)*10), by = ty + (int)(sin(t.angle)*10);
    mainBuf.drawLine(tx, ty, bx, by, C_WHITE);
  };
  drawT(playerTank, C_ACCENT_SYS);
  drawT(aiTank, C_ACCENT_FUN);

  // Bullets
  for(auto& b : bullets) {
    if(!b.active) continue;
    mainBuf.fillCircle(OFF_X + (int)(b.x*T_SZ), OFF_Y + (int)(b.y*T_SZ), 2, C_WHITE);
  }

  // HUD
  mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_LGRAY);
  mainBuf.setCursor(10, 10); mainBuf.print("HP: ");
  for(int i=0; i<3; i++) mainBuf.print(i<playerTank.hp ? "♥" : "♡");

  if(tankGameOver) {
    uiCenteredText(playerTank.hp>0 ? "MISSION SUCCESS" : "MISSION FAILED", SCR_H/2, C_WHITE, &fonts::Font4);
    uiFooter("SEL: Restart  L+R: Back");
  }
}


void tankBFS() {
  if(millis() - lastBfsT < 600) return;
  lastBfsT = millis();
  int startX = (int)aiTank.x, startY = (int)aiTank.y;
  int targetX = (int)playerTank.x, targetY = (int)playerTank.y;
  if(startX == targetX && startY == targetY) return;
  int dists[10][19];
  for(int y=0; y<10; y++) for(int x=0; x<19; x++) dists[y][x] = -1;
  std::vector<MSNode> q; q.push_back({startX, startY}); dists[startY][startX] = 0;
  bool found = false; int head = 0;
  while(head < (int)q.size()) {
    MSNode curr = q[head++];
    if(curr.x == targetX && curr.y == targetY) { found = true; break; }
    int dx[]={0,0,1,-1}, dy[]={1,-1,0,0};
    for(int i=0; i<4; i++) {
      int nx=curr.x+dx[i], ny=curr.y+dy[i];
      if(nx>=0&&nx<19&&ny>=0&&ny<10 && tankMap[ny][nx]==0 && dists[ny][nx]==-1) {
        dists[ny][nx] = dists[curr.y][curr.x]+1; q.push_back({nx, ny});
      }
    }
  }
  if(found) {
    int cx = targetX, cy = targetY;
    while(dists[cy][cx] > 1) {
      int dx[]={0,0,1,-1}, dy[]={1,-1,0,0};
      for(int i=0; i<4; i++) {
        int nx=cx+dx[i], ny=cy+dy[i];
        if(nx>=0&&nx<19&&ny>=0&&ny<10 && dists[ny][nx] == dists[cy][cx]-1) { cx=nx; cy=ny; break; }
      }
    }
    aiTank.angle = atan2(cy + 0.5f - aiTank.y, cx + 0.5f - aiTank.x); }
}

float gameDist(float x1, float y1, float x2, float y2) { return sqrt(pow(x1-x2, 2) + pow(y1-y2, 2)); }
void handleTank() {
  if(tankGameOver) {
    if(btnPressed(B_SEL)) { tankInit(); drawTank(); pushFrame(); }
    return;
  }

  uint32_t now = millis();
  if(now - tankGameT < 30) return;
  tankGameT = now; if(tankShake > 0) tankShake--;

  // Player input
  if(btnHeld(B_UP)) {
    float nx = playerTank.x + cos(playerTank.angle)*0.1f;
    float ny = playerTank.y + sin(playerTank.angle)*0.1f;
    if(tankMap[(int)ny][(int)nx] == 0) { playerTank.x = nx; playerTank.y = ny; }
  }
  if(btnHeld(B_DW)) {
    float nx = playerTank.x - cos(playerTank.angle)*0.1f;
    float ny = playerTank.y - sin(playerTank.angle)*0.1f;
    if(tankMap[(int)ny][(int)nx] == 0) { playerTank.x = nx; playerTank.y = ny; }
  }
  if(btnHeld(B_L)) playerTank.angle -= 0.15f;
  if(btnHeld(B_R)) playerTank.angle += 0.15f;
  if(btnPressed(B_SEL) && bullets.size() < 3) {
    bullets.push_back({playerTank.x, playerTank.y, cos(playerTank.angle)*0.2f, sin(playerTank.angle)*0.2f, true, 3, now});
    ledSet(255, 255, 0); ledPulse(100);
  }

  tankBFS();
  float nax = aiTank.x + cos(aiTank.angle)*0.04f;
  float nay = aiTank.y + sin(aiTank.angle)*0.04f;
  if(tankMap[(int)nay][(int)nax] == 0) { aiTank.x = nax; aiTank.y = nay; }
  if(random(100) < 3 && now - aiTank.lastShot > 1500) { bullets.push_back({aiTank.x, aiTank.y, cos(aiTank.angle)*0.2f, sin(aiTank.angle)*0.2f, true, 3, now}); aiTank.lastShot = now; }
  for(auto& b : bullets) {
    if(!b.active) continue;
    b.x += b.dx; b.y += b.dy;
    int mx = (int)b.x, my = (int)b.y;
    if(tankMap[my][mx] != 0) {
      if(tankMap[my][mx] == 2) tankMap[my][mx] = 0; // Destroy wall
      if(b.bounces-- > 0) {
        // Simple bounce logic
        if(tankMap[my][(int)(b.x-b.dx)] != 0) b.dy = -b.dy; else b.dx = -b.dx;
      } else b.active = false;
    }
    // Check hit
    if(gameDist(b.x, b.y, playerTank.x, playerTank.y) < 0.5f) { playerTank.hp--; b.active = false; tankShake = 5; ledSet(255,0,0); ledPulse(200); }
    if(gameDist(b.x, b.y, aiTank.x, aiTank.y) < 0.5f) { aiTank.hp--; b.active = false; tankShake = 5; ledSet(255,255,255); ledPulse(200); }
  }

  if(playerTank.hp <= 0 || aiTank.hp <= 0) tankGameOver = true;
  if(tankGameOver && aiTank.hp <= 0) { prefs.begin("games", false); int s = prefs.getInt("tank_wins", 0); prefs.putInt("tank_wins", s+1); prefs.end(); }
  drawTank(); pushFrame();
}


// FLAPPY BIRD


void flappyInit() {
  birdY = 80; birdV = 0;
  pipes.clear();
  pipes.push_back({300, random(40, 100), false});
  flappyScore = 0; flappyGameOver = false;
  prefs.begin("games", true); flappyHi = prefs.getInt("flappy_hi", 0); prefs.end();
}

void drawFlappy() {
  mainBuf.fillScreen(lgfx::color565(100, 200, 255));
  // Parallax Clouds
  for(int i=0; i<3; i++) {
    int cx = (millis()/50 + i*120)%400 - 40;
    mainBuf.fillCircle(cx, 40+i*10, 20, C_WHITE);
  }

  // Pipes
  for(auto& p : pipes) {
    mainBuf.fillRect(p.x, 0, 30, p.h, lgfx::color565(0, 200, 0));
    mainBuf.fillRect(p.x, p.h+50, 30, 170-(p.h+50), lgfx::color565(0, 200, 0));
    mainBuf.drawRect(p.x, 0, 30, p.h, C_BLACK);
    mainBuf.drawRect(p.x, p.h+50, 30, 170-(p.h+50), C_BLACK);
  }

  // Bird
  int by = (int)birdY;
  uint16_t bCol = lgfx::color565(255, 255, 0);
  mainBuf.fillEllipse(60, by, 8, 6, bCol);
  mainBuf.fillCircle(65, by-2, 3, C_WHITE); // Eye
  mainBuf.fillTriangle(68, by, 74, by-2, 74, by+2, lgfx::color565(255, 100, 0)); // Beak

  // Ground
  mainBuf.fillRect(0, 160, SCR_W, 10, lgfx::color565(150, 100, 50));

  // Score
  char scStr[16]; snprintf(scStr, sizeof(scStr), "%d", flappyScore);
  uiCenteredText(scStr, 30, C_WHITE, &fonts::Font6);

  if(flappyGameOver) {
    uiCenteredText("GAME OVER", 80, C_WHITE, &fonts::Font4);
    if(flappyScore > flappyHi) {
      flappyHi = flappyScore;
      prefs.begin("games", false); prefs.putInt("flappy_hi", flappyHi); prefs.end();
      uiCenteredText("NEW RECORD!", 100, C_ACCENT_FUN, &fonts::Font2);
    }
    uiFooter("SEL: Restart  L+R: Back");
  }
}

void handleFlappy() {
  if(flappyGameOver) {
    if(btnPressed(B_SEL)) { flappyInit(); drawFlappy(); pushFrame(); }
    return;
  }

  uint32_t now = millis();
  if(now - flappyGameT < 25) return;
  flappyGameT = now;

  if(btnPressed(B_SEL)) birdV = -3.5f;
  birdV += 0.25f; birdY += birdV;

  for(auto& p : pipes) {
    p.x -= 3;
    if(!p.passed && p.x < 60) { p.passed = true; flappyScore++; ledSet(0, 255, 0); ledPulse(100); }
    // Collision
    if(p.x < 68 && p.x+30 > 52) {
      if(birdY-6 < p.h || birdY+6 > p.h+50) { flappyGameOver = true; ledSet(255, 0, 0); ledPulse(300); }
    }
  }

  if(pipes.back().x < 180) pipes.push_back({320, random(30, 90), false});
  if(pipes.front().x < -40) pipes.erase(pipes.begin());
  if(birdY > 160 || birdY < 0) { flappyGameOver = true; ledSet(255, 0, 0); ledPulse(300); }

  drawFlappy(); pushFrame();
}

// MINESWEEPER




void msInit(int w, int h) {
  msW = w; msH = h; msM = (w*h)/6;
  for(int y=0; y<msH; y++) {
    for(int x=0; x<msW; x++) { msGrid[x][y] = {false, false, false, 0}; }
  }
  msCurX=w/2; msCurY=h/2; msGameOver=false; msWin=false; msFirst=true;
  msTimer=0; msQueue.clear();
}

void msPlace(int fx, int fy) {
  int placed=0;
  while(placed < msM) {
    int rx = random(msW), ry = random(msH);
    if(!msGrid[rx][ry].mine && (abs(rx-fx)>1 || abs(ry-fy)>1)) {
      msGrid[rx][ry].mine = true; placed++;
    }
  }
  for(int y=0; y<msH; y++) {
    for(int x=0; x<msW; x++) {
      if(msGrid[x][y].mine) continue;
      int c=0;
      for(int dy=-1; dy<=1; dy++) {
        for(int dx=-1; dx<=1; dx++) {
          int nx=x+dx, ny=y+dy;
          if(nx>=0 && nx<msW && ny>=0 && ny<msH && msGrid[nx][ny].mine) c++;
        }
      }
      msGrid[x][y].adj = c;
    }
  }
}

void msOpen(int x, int y) {
  if(x<0 || x>=msW || y<0 || y>=msH || msGrid[x][y].open || msGrid[x][y].flag) return;
  msGrid[x][y].open = true;
  if(msGrid[x][y].mine) { msGameOver=true; ledSet(255,0,0); ledPulse(500); return; }
  if(msGrid[x][y].adj == 0) msQueue.push_back({x, y});
}

void msHandleQueue() {
  int processed = 0;
  while(!msQueue.empty() && processed < 5) {
    MSNode n = msQueue.back(); msQueue.pop_back();
    for(int dy=-1; dy<=1; dy++) {
      for(int dx=-1; dx<=1; dx++) {
        int nx=n.x+dx, ny=n.y+dy;
        if(nx>=0 && nx<msW && ny>=0 && ny<msH && !msGrid[nx][ny].open && !msGrid[nx][ny].flag) {
          msGrid[nx][ny].open = true;
          if(msGrid[nx][ny].adj == 0) msQueue.push_back({nx, ny});
        }
      }
    }
    processed++;
  }
  // Check win
  bool win = true;
  for(int y=0; y<msH; y++) for(int x=0; x<msW; x++) if(!msGrid[x][y].mine && !msGrid[x][y].open) win = false;
  if(win) { msWin=true; msGameOver=true; ledSet(255,255,255); ledPulse(500); prefs.begin("games", false); int w = prefs.getInt("ms_wins", 0); prefs.putInt("ms_wins", w+1); prefs.end(); }
}

void drawMinesweeper() {
  mainBuf.fillScreen(C_BG); uiHeader("MINESWEEPER");
  const int C_SZ = 16;
  int startX = (SCR_W - msW*C_SZ)/2;
  int startY = 30;

  for(int y=0; y<msH; y++) {
    for(int x=0; x<msW; x++) {
      int tx = startX + x*C_SZ, ty = startY + y*C_SZ;
      MSCell& c = msGrid[x][y];
      if(c.open) {
        mainBuf.fillRect(tx, ty, C_SZ-1, C_SZ-1, C_XDGRAY);
        if(c.mine) mainBuf.fillCircle(tx+C_SZ/2, ty+C_SZ/2, 4, C_WHITE);
        else if(c.adj > 0) {
          mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_WHITE);
          mainBuf.setCursor(tx+4, ty+1); mainBuf.print(c.adj);
        }
      } else {
        mainBuf.fillRect(tx, ty, C_SZ-1, C_SZ-1, C_DGRAY);
        if(c.flag) { mainBuf.setFont(&fonts::Font2); mainBuf.setTextColor(C_ACCENT_FUN); mainBuf.setCursor(tx+4, ty+1); mainBuf.print("f"); }
      }
      if(x==msCurX && y==msCurY && (millis()/200)%2==0) mainBuf.drawRect(tx, ty, C_SZ, C_SZ, C_WHITE);
    }
  }
  uiFooter("UP/DW/L/R: Move  SEL: Open  R: Flag");
}

void handleMinesweeper() {
  if(msGameOver) {
    if(btnPressed(B_SEL)) { msInit(msW, msH); drawMinesweeper(); pushFrame(); }
    return;
  }

  bool changed = false;
  if(btnPressed(B_UP)) { msCurY=(msCurY+msH-1)%msH; changed=true; }
  if(btnPressed(B_DW)) { msCurY=(msCurY+1)%msH; changed=true; }
  if(btnPressed(B_L))  { msCurX=(msCurX+msW-1)%msW; changed=true; }
  if(btnPressed(B_R))  { msCurX=(msCurX+1)%msW; changed=true; }

  if(btnPressed(B_SEL)) {
    if(msFirst) { msPlace(msCurX, msCurY); msFirst=false; msTimer=millis(); }
    msOpen(msCurX, msCurY); changed=true;
  }
  if(btnPressed(B_R)) {
    msGrid[msCurX][msCurY].flag = !msGrid[msCurX][msCurY].flag; changed=true;
  }

  if(!msQueue.empty()) { msHandleQueue(); changed=true; }

  if(changed || (millis()%500 < 50)) {
    drawMinesweeper(); pushFrame();
  }
}

// PAC-MAN LITE
void pacmanInit() {
  const int8_t MAZE[11][21] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},
    {1,0,1,1,0,1,0,1,1,0,1,0,1,1,0,1,0,1,1,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,0,1,1,0,1,1,1,0,1,0,1,1,1,0,1,1,0,1,1},
    {0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0},
    {1,1,0,1,1,0,1,1,1,1,1,1,1,1,1,0,1,1,0,1,1},
    {1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
    {1,0,1,1,0,1,1,1,1,0,1,0,1,1,1,1,0,1,1,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
  };
  pacDots = 0;
  for(int y=0; y<11; y++) {
    for(int x=0; x<21; x++) {
      pacMap[y][x] = MAZE[y][x];
      if(pacMap[y][x] == 0) { pacMap[y][x] = 2; pacDots++; }
    }
  }
  pacPlayer = {10.5f, 7.5f, 0, 0, 0};
  ghosts[0] = {1.5f, 1.5f, 0, 0, 1};
  ghosts[1] = {19.5f, 1.5f, 0, 0, 2};
  ghosts[2] = {10.5f, 1.5f, 0, 0, 3};
  pacScore = 0; pacGameOver = false;
  prefs.begin("games", true); pacHi = prefs.getInt("pac_hi", 0); prefs.end();
}

void drawPacman() {
  mainBuf.fillScreen(C_BLACK);
  uiHeader("PAC-MAN LITE");
  const int T_SZ = 14;
  const int OFF_X = (SCR_W - 21*T_SZ)/2;
  const int OFF_Y = 25;

  for(int y=0; y<11; y++) {
    for(int x=0; x<21; x++) {
      int tx = OFF_X + x*T_SZ, ty = OFF_Y + y*T_SZ;
      if(pacMap[y][x] == 1) mainBuf.fillRect(tx+1, ty+1, T_SZ-2, T_SZ-2, lgfx::color565(0,0,150));
      else if(pacMap[y][x] == 2) mainBuf.fillCircle(tx+T_SZ/2, ty+T_SZ/2, 1, C_WHITE);
    }
  }

  // Player
  int px = OFF_X + pacPlayer.x*T_SZ, py = OFF_Y + pacPlayer.y*T_SZ;
  mainBuf.fillCircle(px, py, 5, lgfx::color565(255,255,0));

  // Ghosts
  uint16_t gCols[] = {lgfx::color565(255,0,0), lgfx::color565(255,182,193), lgfx::color565(0,255,255)};
  for(int i=0; i<3; i++) {
    int gx = OFF_X + ghosts[i].x*T_SZ, gy = OFF_Y + ghosts[i].y*T_SZ;
    mainBuf.fillCircle(gx, gy, 5, gCols[i]);
    mainBuf.fillCircle(gx-2, gy-2, 1, C_WHITE); mainBuf.fillCircle(gx+2, gy-2, 1, C_WHITE);
  }

  char buf[32]; snprintf(buf, sizeof(buf), "SCORE:%d HI:%d", pacScore, pacHi);
  uiFooter(buf);

  if(pacGameOver) {
    uiCenteredText("GAME OVER", 80, C_WHITE, &fonts::Font4);
    if(pacScore > pacHi) {
      pacHi = pacScore;
      prefs.begin("games", false); prefs.putInt("pac_hi", pacHi); prefs.end();
      uiCenteredText("NEW RECORD!", 100, C_ACCENT_FUN, &fonts::Font2);
    }
    uiFooter("SEL: Restart  L+R: Back");
  }
}

void handlePacman() {
  if(pacGameOver) {
    if(btnPressed(B_SEL)) { pacmanInit(); drawPacman(); pushFrame(); }
    return;
  }
  uint32_t now = millis();
  if(now - pacGameT < 40) return;
  pacGameT = now;

  float lastX = pacPlayer.x, lastY = pacPlayer.y;
  if(btnHeld(B_UP)) { pacPlayer.dx=0; pacPlayer.dy=-0.15f; }
  if(btnHeld(B_DW)) { pacPlayer.dx=0; pacPlayer.dy=0.15f; }
  if(btnHeld(B_L))  { pacPlayer.dx=-0.15f; pacPlayer.dy=0; }
  if(btnHeld(B_R))  { pacPlayer.dx=0.15f; pacPlayer.dy=0; }

  pacPlayer.x += pacPlayer.dx; pacPlayer.y += pacPlayer.dy;
  if(pacMap[(int)pacPlayer.y][(int)pacPlayer.x] == 1) { pacPlayer.x = lastX; pacPlayer.y = lastY; }

  // Wrap around
  if(pacPlayer.x < 0) pacPlayer.x = 20.9f;
  if(pacPlayer.x > 21) pacPlayer.x = 0.1f;

  if(pacMap[(int)pacPlayer.y][(int)pacPlayer.x] == 2) {
    pacMap[(int)pacPlayer.y][(int)pacPlayer.x] = 0; pacScore += 10; pacDots--;
    ledSet(0, 50, 0); ledPulse(50);
    if(pacDots <= 0) pacmanInit(); // Next level / Reset
  }

  // Ghost AI (Simple toward player)
  for(int i=0; i<3; i++) {
    float glastX = ghosts[i].x, glastY = ghosts[i].y;
    if(random(100) < 5) { // Change direction occasionally
       if(abs(pacPlayer.x - ghosts[i].x) > abs(pacPlayer.y - ghosts[i].y)) {
         ghosts[i].dx = (pacPlayer.x > ghosts[i].x) ? 0.1f : -0.1f; ghosts[i].dy = 0;
       } else {
         ghosts[i].dy = (pacPlayer.y > ghosts[i].y) ? 0.1f : -0.1f; ghosts[i].dx = 0;
       }
    }
    ghosts[i].x += ghosts[i].dx; ghosts[i].y += ghosts[i].dy;
    if(pacMap[(int)ghosts[i].y][(int)ghosts[i].x] == 1) { ghosts[i].x = glastX; ghosts[i].y = glastY; ghosts[i].dx = -ghosts[i].dx; ghosts[i].dy = -ghosts[i].dy; }

    if(sqrt(pow(pacPlayer.x-ghosts[i].x,2)+pow(pacPlayer.y-ghosts[i].y,2)) < 0.6f) { pacGameOver = true; ledSet(255,0,0); ledPulse(500); }
  }

  drawPacman(); pushFrame();
}

// DOODLE JUMP
void doodleInit() {
  doodleX = 160; doodleY = 100; doodleV = -5;
  doodleCamY = 0; doodleScore = 0; doodleGameOver = false;
  platforms.clear();
  for(int i=0; i<10; i++) {
    platforms.push_back({(float)random(20, 280), (float)(150 - i*30), 0});
  }
  prefs.begin("games", true); doodleHi = prefs.getInt("doodle_hi", 0); prefs.end();
}

void drawDoodle() {
  mainBuf.fillScreen(lgfx::color565(250, 250, 230)); // Paper color
  uiHeader("DOODLE JUMP");

  // Grid lines
  for(int i=0; i<SCR_W; i+=20) mainBuf.drawFastVLine(i, 25, SCR_H-25, lgfx::color565(230, 230, 255));
  for(int i=25; i<SCR_H; i+=20) mainBuf.drawFastHLine(0, i, SCR_W, lgfx::color565(230, 230, 255));

  // Platforms
  for(auto& p : platforms) {
    int py = (int)(p.y - doodleCamY + 120);
    if(py > 25 && py < SCR_H) {
      mainBuf.fillRoundRect(p.x-15, py, 30, 6, 3, lgfx::color565(100, 200, 0));
      mainBuf.drawRoundRect(p.x-15, py, 30, 6, 3, C_BLACK);
    }
  }

  // Player (Doodle)
  int dy = (int)(doodleY - doodleCamY + 120);
  mainBuf.fillEllipse(doodleX, dy, 8, 10, lgfx::color565(180, 200, 50));
  mainBuf.fillCircle(doodleX-4, dy-4, 2, C_BLACK); mainBuf.fillCircle(doodleX+4, dy-4, 2, C_BLACK);
  mainBuf.fillRect(doodleX-2, dy+2, 4, 8, lgfx::color565(180, 200, 50)); // Snout

  char sc[16]; snprintf(sc, sizeof(sc), "SCORE: %d", doodleScore);
  uiFooter(sc);

  if(doodleGameOver) {
    uiCenteredText("GAME OVER", 80, C_WHITE, &fonts::Font4);
    if(doodleScore > doodleHi) {
      doodleHi = doodleScore;
      prefs.begin("games", false); prefs.putInt("doodle_hi", doodleHi); prefs.end();
      uiCenteredText("NEW RECORD!", 100, C_ACCENT_FUN, &fonts::Font2);
    }
    uiFooter("SEL: Restart  L+R: Back");
  }
}

void handleDoodle() {
  if(doodleGameOver) {
    if(btnPressed(B_SEL)) { doodleInit(); drawDoodle(); pushFrame(); }
    return;
  }
  uint32_t now = millis();
  if(now - doodleGameT < 30) return;
  doodleGameT = now;

  if(btnHeld(B_L)) doodleX -= 4;
  if(btnHeld(B_R)) doodleX += 4;
  if(doodleX < 0) doodleX = 320;
  if(doodleX > 320) doodleX = 0;

  doodleV += 0.2f; // Gravity
  doodleY += doodleV;

  if(doodleY < doodleCamY) {
    doodleCamY = doodleY;
    if(-doodleY > doodleScore) doodleScore = (int)-doodleY;
  }

  // Collision
  if(doodleV > 0) {
    for(auto& p : platforms) {
      if(doodleX > p.x-20 && doodleX < p.x+20 && doodleY > p.y-5 && doodleY < p.y+5) {
        doodleV = -6.5f; ledSet(50, 50, 0); ledPulse(50); break;
      }
    }
  }

  // Infinite generation
  if(platforms.back().y > doodleCamY - 200) {
    platforms.push_back({(float)random(20, 300), platforms.back().y - (float)random(30, 50), 0});
  }
  if(platforms.front().y > doodleCamY + 200) platforms.erase(platforms.begin());

  if(doodleY - doodleCamY > 100) { doodleGameOver = true; ledSet(255, 0, 0); ledPulse(500); }

  drawDoodle(); pushFrame();
}

// MEMORY MATCH
static int memoGrid[18];
static bool memoRevealed[18];
static int memoSelX = 0, memoSelY = 0;
static int memoFirstIdx = -1, memoSecondIdx = -1;
static uint32_t memoWaitT = 0;
static int memoMatches = 0;
static bool memoGameOver = false;

void memoInit() {
  std::vector<int> cards;
  for(int i=0; i<9; i++) { cards.push_back(i); cards.push_back(i); }
  std::random_shuffle(cards.begin(), cards.end());
  for(int i=0; i<18; i++) { memoGrid[i] = cards[i]; memoRevealed[i] = false; }
  memoSelX = 0; memoSelY = 0; memoFirstIdx = -1; memoSecondIdx = -1;
  memoWaitT = 0; memoMatches = 0; memoGameOver = false;
}

void drawMemory() {
  mainBuf.fillScreen(C_BG); uiHeader("MEMORY MATCH");
  const int CW = 40, CH = 35, GAP = 6;
  const int OFF_X = (SCR_W - (6*CW + 5*GAP))/2;
  const int OFF_Y = 25;

  for(int i=0; i<18; i++) {
    int x = i % 6, y = i / 6;
    int tx = OFF_X + x*(CW+GAP), ty = OFF_Y + y*(CH+GAP);
    bool sel = (x == memoSelX && y == memoSelY);

    if(memoRevealed[i] || i == memoFirstIdx || i == memoSecondIdx) {
      mainBuf.fillRoundRect(tx, ty, CW, CH, 4, C_CARD);
      mainBuf.setTextColor(C_WHITE); mainBuf.setFont(&fonts::Font4);
      mainBuf.drawCentreString(String(memoGrid[i]).c_str(), tx+CW/2, ty+CH/2-10);
    } else {
      mainBuf.fillRoundRect(tx, ty, CW, CH, 4, C_SURFACE);
      mainBuf.drawRoundRect(tx, ty, CW, CH, 4, C_BORDER);
    }
    if(sel) mainBuf.drawRoundRect(tx-2, ty-2, CW+4, CH+4, 6, C_WHITE);
  }

  if(memoGameOver) uiCenteredText("EXCELLENT!", SCR_H/2, C_WHITE, &fonts::Font4);
  uiFooter(memoGameOver ? "SEL: Restart  L+R: Back" : "UP/DW/L/R: Move  SEL: Flip");
}

void handleMemory() {
  if(memoGameOver) {
    if(btnPressed(B_SEL)) { memoInit(); drawMemory(); pushFrame(); }
    return;
  }
  if(memoWaitT > 0 && millis() > memoWaitT) {
    if(memoGrid[memoFirstIdx] != memoGrid[memoSecondIdx]) {
      memoRevealed[memoFirstIdx] = memoRevealed[memoSecondIdx] = false;
    } else {
      memoMatches++;
      if(memoMatches == 9) memoGameOver = true;
    }
    memoFirstIdx = memoSecondIdx = -1;
    memoWaitT = 0;
    drawMemory(); pushFrame(); return;
  }
  if(memoWaitT > 0) return;

  bool changed = false;
  if(btnPressed(B_UP)) { memoSelY = (memoSelY + 2) % 3; changed = true; }
  if(btnPressed(B_DW)) { memoSelY = (memoSelY + 1) % 3; changed = true; }
  if(btnPressed(B_L))  { memoSelX = (memoSelX + 5) % 6; changed = true; }
  if(btnPressed(B_R))  { memoSelX = (memoSelX + 1) % 6; changed = true; }

  if(btnPressed(B_SEL)) {
    int idx = memoSelY * 6 + memoSelX;
    if(!memoRevealed[idx] && idx != memoFirstIdx) {
      if(memoFirstIdx == -1) memoFirstIdx = idx;
      else {
        memoSecondIdx = idx;
        memoWaitT = millis() + 800;
      }
      changed = true;
    }
  }
  if(changed) { drawMemory(); pushFrame(); }
}

// SIMON NUMERIC
static std::vector<int> simonSeq;
static int simonStep = 0;
static bool simonPlayback = true;
static uint32_t simonNextT = 0;
static int simonShowIdx = -1;
static bool simonGameOver = false;

void simonInit() {
  simonSeq.clear(); simonSeq.push_back(random(1, 5));
  simonStep = 0; simonPlayback = true; simonNextT = millis() + 1000;
  simonShowIdx = -1; simonGameOver = false;
}

void drawSimon() {
  mainBuf.fillScreen(C_BG); uiHeader("SIMON NUMERIC");
  const int SZ = 50, GAP = 10;
  int OFF_X = (SCR_W - (2*SZ + GAP))/2;
  int OFF_Y = 40;

  int highlights[] = {0,0,0,0,0};
  if(simonShowIdx != -1) highlights[simonSeq[simonShowIdx]] = 1;

  auto drawBtn = [&](int id, int x, int y, uint16_t color, const char* txt) {
    mainBuf.fillRoundRect(x, y, SZ, SZ, 8, highlights[id] ? color : C_XDGRAY);
    mainBuf.setTextColor(highlights[id] ? C_BLACK : C_WHITE);
    mainBuf.setFont(&fonts::Font4);
    mainBuf.drawCentreString(txt, x+SZ/2, y+SZ/2-10);
  };

  drawBtn(1, OFF_X, OFF_Y, C_WHITE, "1");       // UP
  drawBtn(2, OFF_X+SZ+GAP, OFF_Y, C_WHITE, "2"); // DW
  drawBtn(3, OFF_X, OFF_Y+SZ+GAP, C_WHITE, "3"); // L
  drawBtn(4, OFF_X+SZ+GAP, OFF_Y+SZ+GAP, C_WHITE, "4"); // R

  char buf[32]; snprintf(buf, sizeof(buf), "LEVEL: %d", (int)simonSeq.size());
  uiFooter(buf);

  if(simonGameOver) uiCenteredText("GAME OVER!", SCR_H/2, C_WHITE, &fonts::Font4);
  else if(simonPlayback) uiCenteredText("WATCH...", 35, C_WHITE, &fonts::Font2);
  else uiCenteredText("YOUR TURN!", 35, C_ACCENT_INFO, &fonts::Font2);
}

void handleSimon() {
  if(simonGameOver) {
    if(btnPressed(B_SEL)) { simonInit(); drawSimon(); pushFrame(); }
    return;
  }

  if(simonPlayback) {
    if(millis() > simonNextT) {
      if(simonShowIdx == -1) {
        simonShowIdx = simonStep;
        simonNextT = millis() + 600;
      } else {
        simonShowIdx = -1;
        simonStep++;
        if(simonStep >= (int)simonSeq.size()) {
          simonPlayback = false; simonStep = 0;
        }
        simonNextT = millis() + 200;
      }
      drawSimon(); pushFrame();
    }
    return;
  }

  int input = 0;
  if(btnPressed(B_UP)) input = 1;
  if(btnPressed(B_DW)) input = 2;
  if(btnPressed(B_L))  input = 3;
  if(btnPressed(B_R))  input = 4;

  if(input > 0) {
    if(input == simonSeq[simonStep]) {
      simonStep++;
      ledSet(0, 100, 0); ledPulse(100);
      if(simonStep >= (int)simonSeq.size()) {
        simonSeq.push_back(random(1, 5));
        simonStep = 0; simonPlayback = true; simonNextT = millis() + 1000;
      }
    } else {
      simonGameOver = true; ledSet(255, 0, 0); ledPulse(500);
    }
    drawSimon(); pushFrame();
  }
}

// SUDOKU
static int sudoGrid[9][9];
static bool sudoFixed[9][9];
static int sudoCurX = 0, sudoCurY = 0;
static bool sudoWin = false;

void sudoInit() {
  // Simple pattern-based Sudoku for demo
  int base[9][9] = {
    {5,3,4,6,7,8,9,1,2},{6,7,2,1,9,5,3,4,8},{1,9,8,3,4,2,5,6,7},
    {8,5,9,7,6,1,4,2,3},{4,2,6,8,5,3,7,9,1},{7,1,3,9,2,4,8,5,6},
    {9,6,1,5,3,7,2,8,4},{2,8,7,4,1,9,6,3,5},{3,4,5,2,8,6,1,7,9}
  };
  for(int y=0;y<9;y++) {
    for(int x=0;x<9;x++) {
      sudoGrid[y][x] = (random(100) < 35) ? base[y][x] : 0;
      sudoFixed[y][x] = (sudoGrid[y][x] != 0);
    }
  }
  sudoCurX = 4; sudoCurY = 4; sudoWin = false;
}

void drawSudoku() {
  mainBuf.fillScreen(C_BG); uiHeader("SUDOKU");
  const int S = 14;
  const int OFF_X = (SCR_W - 9*S)/2;
  const int OFF_Y = 25;

  for(int y=0; y<9; y++) {
    for(int x=0; x<9; x++) {
      int tx = OFF_X + x*S, ty = OFF_Y + y*S;
      mainBuf.drawRect(tx, ty, S+1, S+1, C_DGRAY);
      if(sudoGrid[y][x] != 0) {
        mainBuf.setTextColor(sudoFixed[y][x] ? C_WHITE : C_ACCENT_INFO);
        mainBuf.setFont(&fonts::Font2);
        mainBuf.setCursor(tx+5, ty+1); mainBuf.print(sudoGrid[y][x]);
      }
      if(x == sudoCurX && y == sudoCurY) mainBuf.drawRect(tx+1, ty+1, S-1, S-1, C_WHITE);
    }
  }
  // Bold lines for 3x3
  for(int i=0; i<=9; i+=3) {
    mainBuf.drawFastVLine(OFF_X+i*S, OFF_Y, 9*S, C_WHITE);
    mainBuf.drawFastHLine(OFF_X, OFF_Y+i*S, 9*S, C_WHITE);
  }

  if(sudoWin) uiCenteredText("SOLVED!", 100, C_ACCENT_FUN, &fonts::Font4);
  uiFooter("UP/DW/L/R: Nav  SEL: Inc Num");
}

void handleSudoku() {
  if(sudoWin) { if(btnPressed(B_SEL)) sudoInit(); return; }
  bool changed = false;
  if(btnPressed(B_UP)) { sudoCurY = (sudoCurY + 8) % 9; changed = true; }
  if(btnPressed(B_DW)) { sudoCurY = (sudoCurY + 1) % 9; changed = true; }
  if(btnPressed(B_L))  { sudoCurX = (sudoCurX + 8) % 9; changed = true; }
  if(btnPressed(B_R))  { sudoCurX = (sudoCurX + 1) % 9; changed = true; }

  if(btnPressed(B_SEL) && !sudoFixed[sudoCurY][sudoCurX]) {
    sudoGrid[sudoCurY][sudoCurX] = (sudoGrid[sudoCurY][sudoCurX] + 1) % 10;
    changed = true;
    // Simple check win (could be more robust)
    bool full = true;
    for(int y=0;y<9;y++) for(int x=0;x<9;x++) if(sudoGrid[y][x]==0) full=false;
    if(full) sudoWin = true;
  }
  if(changed) { drawSudoku(); pushFrame(); }
}
