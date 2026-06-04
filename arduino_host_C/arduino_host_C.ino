/*
 * =====================================================================
 *  雙人節奏音律遊戲 — 主機 (Arduino C)
 *
 *  ┌─────────────────────────────────────────────────────────────────┐
 *  │  腳位配置 (Arduino Uno)                                          │
 *  │                                                                 │
 *  │  ── TFT ILI9341 (SPI) ──────────────────────────────────────   │
 *  │    VCC  → 3.3V 或 5V (依模組規格)                               │
 *  │    GND  → GND                                                   │
 *  │    CS   → D8                                                    │
 *  │    RST  → D9                                                    │
 *  │    DC   → D10                                                   │
 *  │    MOSI → D11  (硬體 SPI)                                       │
 *  │    SCK  → D13  (硬體 SPI)                                       │
 *  │    LED  → 3.3V 或透過限流電阻接 5V                              │
 *  │                                                                 │
 *  │  ── 無源蜂鳴器 ─────────────────────────────────────────────   │
 *  │    正極  → D3  (PWM, tone())                                    │
 *  │    負極  → GND                                                  │
 *  │                                                                 │
 *  │  ── 玩家 A (Arduino A TX) ──────────────────────────────────   │
 *  │    A.TX → 主機 D0  (HardwareSerial RX)                         │
 *  │    注意：上傳程式時請先拔掉此連線，避免衝突                      │
 *  │                                                                 │
 *  │  ── 玩家 B (Arduino B TX) ──────────────────────────────────   │
 *  │    B.TX → 主機 D4  (SoftwareSerial RX)                         │
 *  │    D5   → (SoftwareSerial TX, 不使用，可空接)                   │
 *  └─────────────────────────────────────────────────────────────────┘
 *
 *  通訊協定：玩家端按左鍵送 'L'，按右鍵送 'R'（9600 baud）
 * =====================================================================
 */

// ════════════════════════════════════════════════════════════════════
//  函式庫
// ════════════════════════════════════════════════════════════════════
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <SoftwareSerial.h>

// ════════════════════════════════════════════════════════════════════
//  【必須最先定義】所有自訂 struct — 避免編譯器前向宣告找不到型別
// ════════════════════════════════════════════════════════════════════

// 蜂鳴器音符
struct MusicNote {
  uint16_t freq;
  uint16_t duration;
};

// 音符時間表（存在 PROGMEM）
struct NoteSchedule {
  uint32_t spawnMs;
  bool     isYellow;
  bool     isPlayerA;
};

// 畫面上移動的節奏音符
struct RhythmNote {
  bool    active;
  bool    isYellow;   // true=黃(左鍵) / false=藍(右鍵)
  bool    isPlayerA;  // true=P1 lane / false=P2 lane
  int16_t x;          // 當前圓心 X
  int16_t prevX;      // 上一幀圓心 X（局部抹除用）
  bool    judged;
};

// ════════════════════════════════════════════════════════════════════
//  腳位 & 物件
// ════════════════════════════════════════════════════════════════════
#define TFT_CS   8
#define TFT_RST  9
#define TFT_DC  10
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

#define BUZZER_PIN 3          // PWM 腳位

// 軟體序列埠：D4=RX(接B的TX), D5=TX(空接)
SoftwareSerial serialB(4, 5);

// ════════════════════════════════════════════════════════════════════
//  螢幕 & 顏色常數
// ════════════════════════════════════════════════════════════════════
#define SCREEN_W 320
#define SCREEN_H 240

#define COL_BG         ILI9341_BLACK
#define COL_LANE_BG    0x0841
#define COL_DIVIDER    0x4208
#define COL_JUDGE_LINE 0xF800
#define COL_YELLOW     ILI9341_YELLOW
#define COL_BLUE       0x001F
#define COL_WHITE      ILI9341_WHITE
#define COL_GREEN      ILI9341_GREEN
#define COL_RED        ILI9341_RED
#define COL_CYAN       ILI9341_CYAN
#define COL_MAGENTA    ILI9341_MAGENTA

// ════════════════════════════════════════════════════════════════════
//  版面配置（橫置 320×240）
// ════════════════════════════════════════════════════════════════════
#define LANE_H     108   // 每個 lane 高度
#define LANE_A_Y     4   // P1 lane 起始 Y
#define LANE_B_Y   128   // P2 lane 起始 Y
#define DIVIDER_Y  116   // 分隔線 Y
#define NOTE_R      14   // 音符半徑 (px)
#define JUDGE_X     40   // 判定線 X
#define NOTE_SPEED   2   // px / frame
#define FRAME_MS    16   // ~60 fps

// ════════════════════════════════════════════════════════════════════
//  有限狀態機
// ════════════════════════════════════════════════════════════════════
enum GameState { LOBBY, PLAYING, RESULT };
GameState gameState = LOBBY;

// ════════════════════════════════════════════════════════════════════
//  旋律資料（存 PROGMEM 節省 SRAM）
// ════════════════════════════════════════════════════════════════════
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_C5  523

// 小星星 × 2 段（示範；可自行替換）
const MusicNote SONG[] PROGMEM = {
  {NOTE_C4,400},{NOTE_C4,400},{NOTE_G4,400},{NOTE_G4,400},
  {NOTE_A4,400},{NOTE_A4,400},{NOTE_G4,800},
  {NOTE_F4,400},{NOTE_F4,400},{NOTE_E4,400},{NOTE_E4,400},
  {NOTE_D4,400},{NOTE_D4,400},{NOTE_C4,800},
  {NOTE_G4,400},{NOTE_G4,400},{NOTE_F4,400},{NOTE_F4,400},
  {NOTE_E4,400},{NOTE_E4,400},{NOTE_D4,800},
  {NOTE_G4,400},{NOTE_G4,400},{NOTE_F4,400},{NOTE_F4,400},
  {NOTE_E4,400},{NOTE_E4,400},{NOTE_D4,800},
  {NOTE_C4,400},{NOTE_C4,400},{NOTE_G4,400},{NOTE_G4,400},
  {NOTE_A4,400},{NOTE_A4,400},{NOTE_G4,800},
  {NOTE_F4,400},{NOTE_F4,400},{NOTE_E4,400},{NOTE_E4,400},
  {NOTE_D4,400},{NOTE_D4,400},{NOTE_C4,800},
  // 第二段（稍快 300ms）
  {NOTE_C5,300},{NOTE_C5,300},{NOTE_G4,300},{NOTE_G4,300},
  {NOTE_A4,300},{NOTE_A4,300},{NOTE_G4,600},
  {NOTE_F4,300},{NOTE_F4,300},{NOTE_E4,300},{NOTE_E4,300},
  {NOTE_D4,300},{NOTE_D4,300},{NOTE_C4,600},
  {NOTE_G4,300},{NOTE_G4,300},{NOTE_F4,300},{NOTE_F4,300},
  {NOTE_E4,300},{NOTE_E4,300},{NOTE_D4,600},
  {NOTE_G4,300},{NOTE_G4,300},{NOTE_F4,300},{NOTE_F4,300},
  {NOTE_E4,300},{NOTE_E4,300},{NOTE_D4,600},
  {NOTE_C4,300},{NOTE_C4,300},{NOTE_G4,300},{NOTE_G4,300},
  {NOTE_A4,300},{NOTE_A4,300},{NOTE_G4,600},
  {NOTE_F4,300},{NOTE_F4,300},{NOTE_E4,300},{NOTE_E4,300},
  {NOTE_D4,300},{NOTE_D4,300},{NOTE_C4,1200},
};
#define SONG_LEN (sizeof(SONG)/sizeof(SONG[0]))

// ════════════════════════════════════════════════════════════════════
//  音符時間表（存 PROGMEM）
// ════════════════════════════════════════════════════════════════════
const NoteSchedule NOTE_SCHEDULE[] PROGMEM = {
  {  200, true,  true  },
  {  600, false, false },
  { 1000, true,  true  },
  { 1400, false, false },
  { 1800, true,  true  },
  { 2200, false, false },
  { 2600, true,  false },
  { 3400, false, true  },
  { 3800, true,  true  },
  { 4200, false, false },
  { 4600, true,  false },
  { 5000, false, true  },
  { 5400, true,  true  },
  { 5800, false, false },
  { 6600, true,  false },
  { 7000, false, true  },
  { 7400, true,  true  },
  { 7800, false, false },
  { 8200, true,  true  },
  { 8600, false, false },
  { 9000, true,  false },
  { 9400, false, true  },
  { 9800, true,  true  },
  {10200, false, false },
  {10600, true,  true  },
  {11000, false, false },
  {11400, true,  false },
  {11800, false, true  },
  {12600, true,  true  },
  {12900, false, false },
  {13200, true,  true  },
  {13500, false, false },
  {13800, true,  false },
  {14100, false, true  },
  {14400, true,  true  },
  {14700, false, false },
  {15000, true,  true  },
  {15300, false, false },
  {15600, true,  false },
  {15900, false, true  },
  {16200, true,  true  },
  {16500, false, false },
  {16800, true,  false },
  {17100, false, true  },
  {17400, true,  true  },
  {17700, false, false },
  {18000, true,  true  },
  {18300, false, false },
  {18600, true,  false },
  {18900, false, true  },
  {19200, true,  true  },
  {19500, false, false },
  {19800, true,  false },
  {20100, false, true  },
};
#define SCHEDULE_LEN (sizeof(NOTE_SCHEDULE)/sizeof(NOTE_SCHEDULE[0]))

// ════════════════════════════════════════════════════════════════════
//  執行期變數
// ════════════════════════════════════════════════════════════════════
#define MAX_NOTES 32
RhythmNote notes[MAX_NOTES];

int16_t  scoreA = 0, scoreB = 0;
int16_t  prevScoreA = -1, prevScoreB = -1;

uint8_t  musicIdx     = 0;
uint32_t musicTimer   = 0;
bool     musicPlaying = false;
uint32_t songStartMs  = 0;

uint32_t lastFrameMs     = 0;
uint32_t lastNoteSpawnMs = 0;
uint8_t  scheduleIdx     = 0;

bool lobbyPressA = false, lobbyPressB = false;

uint32_t gameOverTimer   = 0;
bool     gameOverPending = false;

// ════════════════════════════════════════════════════════════════════
//  圖形輔助函式
// ════════════════════════════════════════════════════════════════════
int16_t laneNoteY(bool isPlayerA) {
  return isPlayerA ? (LANE_A_Y + LANE_H / 2)
                   : (LANE_B_Y + LANE_H / 2);
}

void eraseNote(const RhythmNote& n) {
  int16_t y = laneNoteY(n.isPlayerA);
  tft.fillRect(n.prevX - NOTE_R - 1, y - NOTE_R - 1,
               (NOTE_R + 1) * 2 + 2, (NOTE_R + 1) * 2 + 2,
               COL_LANE_BG);
  // 判定線若被音符蓋過，重畫
  if (n.prevX - NOTE_R <= JUDGE_X + 1 && n.prevX + NOTE_R >= JUDGE_X) {
    int16_t ly = n.isPlayerA ? LANE_A_Y : LANE_B_Y;
    tft.drawFastVLine(JUDGE_X,     ly, LANE_H, COL_JUDGE_LINE);
    tft.drawFastVLine(JUDGE_X + 1, ly, LANE_H, COL_JUDGE_LINE);
  }
}

void drawNote(const RhythmNote& n) {
  int16_t  y   = laneNoteY(n.isPlayerA);
  uint16_t col = n.isYellow ? COL_YELLOW : COL_BLUE;
  tft.fillCircle(n.x, y, NOTE_R, col);
  tft.drawCircle(n.x, y, NOTE_R, COL_WHITE);
  tft.fillCircle(n.x, y, NOTE_R / 3, n.isYellow ? 0xFC00 : 0x001F);
}

void drawScores() {
  if (scoreA == prevScoreA && scoreB == prevScoreB) return;
  tft.fillRect(200, LANE_A_Y + 2, 118, 20, COL_LANE_BG);
  tft.fillRect(200, LANE_B_Y + 2, 118, 20, COL_LANE_BG);
  tft.setTextSize(2);
  tft.setTextColor(COL_WHITE);
  tft.setCursor(202, LANE_A_Y + 4);
  tft.print(scoreA);
  tft.setCursor(202, LANE_B_Y + 4);
  tft.print(scoreB);
  prevScoreA = scoreA;
  prevScoreB = scoreB;
}

void drawStaticUI() {
  tft.fillScreen(COL_BG);
  tft.fillRect(0, LANE_A_Y, SCREEN_W, LANE_H, COL_LANE_BG);
  tft.fillRect(0, LANE_B_Y, SCREEN_W, LANE_H, COL_LANE_BG);
  tft.drawFastHLine(0, DIVIDER_Y,     SCREEN_W, COL_DIVIDER);
  tft.drawFastHLine(0, DIVIDER_Y + 1, SCREEN_W, COL_DIVIDER);
  tft.drawFastVLine(JUDGE_X,     LANE_A_Y, LANE_H, COL_JUDGE_LINE);
  tft.drawFastVLine(JUDGE_X,     LANE_B_Y, LANE_H, COL_JUDGE_LINE);
  tft.drawFastVLine(JUDGE_X + 1, LANE_A_Y, LANE_H, COL_JUDGE_LINE);
  tft.drawFastVLine(JUDGE_X + 1, LANE_B_Y, LANE_H, COL_JUDGE_LINE);
  tft.setTextSize(1);
  tft.setTextColor(0xA514);
  tft.setCursor(JUDGE_X + 5, LANE_A_Y + 3); tft.print("P1");
  tft.setCursor(JUDGE_X + 5, LANE_B_Y + 3); tft.print("P2");
  prevScoreA = prevScoreB = -1;
  drawScores();
}

// ════════════════════════════════════════════════════════════════════
//  判定輔助函式
// ════════════════════════════════════════════════════════════════════
int16_t calcScore(int16_t dist) {
  if (dist <= 15) return 150;
  if (dist <= 25) return 100;
  if (dist <= 35) return  50;
  return 0;
}

void showJudgeText(bool isPlayerA, int16_t score) {
  int16_t y = laneNoteY(isPlayerA) + NOTE_R + 2;
  tft.fillRect(60, y - 1, 100, 12, COL_LANE_BG);
  tft.setTextSize(1);
  if (score == 150)     { tft.setTextColor(COL_YELLOW);  tft.setCursor(62, y); tft.print("PERFECT!"); }
  else if (score == 100){ tft.setTextColor(COL_GREEN);   tft.setCursor(62, y); tft.print("GREAT");    }
  else if (score ==  50){ tft.setTextColor(COL_CYAN);    tft.setCursor(62, y); tft.print("GOOD");     }
  else                  { tft.setTextColor(COL_RED);     tft.setCursor(62, y); tft.print("MISS");     }
}

void processButtonEvent(bool isPlayerA, bool isLeft) {
  int16_t bestDist = 999;
  int8_t  bestIdx  = -1;

  for (uint8_t i = 0; i < MAX_NOTES; i++) {
    if (!notes[i].active || notes[i].judged) continue;
    if (notes[i].isPlayerA != isPlayerA)     continue;
    int16_t d = abs(notes[i].x - JUDGE_X);
    if (d < bestDist) { bestDist = d; bestIdx = i; }
  }

  if (bestIdx < 0) { showJudgeText(isPlayerA, 0); return; }

  RhythmNote& n = notes[bestIdx];
  bool correctBtn = (n.isYellow == isLeft);
  int16_t pts = correctBtn ? calcScore(bestDist) : 0;

  if (isPlayerA) scoreA += pts; else scoreB += pts;
  showJudgeText(isPlayerA, pts);
  eraseNote(n);
  n.active = false;
  n.judged = true;
  drawScores();
}

// ════════════════════════════════════════════════════════════════════
//  序列埠讀取
// ════════════════════════════════════════════════════════════════════
void readSerialEvents() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c != 'L' && c != 'R') continue;
    if      (gameState == LOBBY)   lobbyPressA = true;
    else if (gameState == PLAYING) processButtonEvent(true,  c == 'L');
    else if (gameState == RESULT)  lobbyPressA = true;
  }
  while (serialB.available()) {
    char c = serialB.read();
    if (c != 'L' && c != 'R') continue;
    if      (gameState == LOBBY)   lobbyPressB = true;
    else if (gameState == PLAYING) processButtonEvent(false, c == 'L');
    else if (gameState == RESULT)  lobbyPressB = true;
  }
}

// ════════════════════════════════════════════════════════════════════
//  畫面：大廳
// ════════════════════════════════════════════════════════════════════
void drawLobby() {
  tft.fillScreen(COL_BG);
  tft.setTextSize(3); tft.setTextColor(COL_YELLOW);
  tft.setCursor(30, 35);  tft.print("RHYTHM");
  tft.setCursor(30, 68);  tft.print("MASTER");
  tft.setTextSize(1); tft.setTextColor(COL_CYAN);
  tft.setCursor(20, 115); tft.print("2-Player Music Game");
  tft.setTextColor(COL_WHITE);
  tft.setCursor(10, 145); tft.print("P1(A): Yellow=Left  Blue=Right");
  tft.setCursor(10, 157); tft.print("P2(B): Yellow=Left  Blue=Right");
  tft.setTextColor(COL_GREEN);
  tft.setCursor(25, 195); tft.print("Both press any button to START");
}

// ════════════════════════════════════════════════════════════════════
//  畫面：結算
// ════════════════════════════════════════════════════════════════════
void drawResult() {
  tft.fillScreen(COL_BG);
  tft.setTextSize(2); tft.setTextColor(COL_YELLOW);
  tft.setCursor(90, 20); tft.print("RESULT");

  tft.setTextColor(COL_CYAN);  tft.setCursor(20, 65);
  tft.print("P1: "); tft.setTextColor(COL_WHITE); tft.print(scoreA);

  tft.setTextColor(COL_MAGENTA); tft.setCursor(20, 100);
  tft.print("P2: "); tft.setTextColor(COL_WHITE); tft.print(scoreB);

  tft.setTextSize(3); tft.setCursor(40, 145);
  if      (scoreA > scoreB) { tft.setTextColor(COL_CYAN);    tft.print("P1 Wins!"); }
  else if (scoreB > scoreA) { tft.setTextColor(COL_MAGENTA); tft.print("P2 Wins!"); }
  else                      { tft.setTextColor(COL_YELLOW);  tft.print("  Draw!");  }

  tft.setTextSize(1); tft.setTextColor(COL_GREEN);
  tft.setCursor(20, 215); tft.print("Both press any button to LOBBY");
}

// ════════════════════════════════════════════════════════════════════
//  遊戲重置
// ════════════════════════════════════════════════════════════════════
void resetGame() {
  scoreA = scoreB = 0;
  prevScoreA = prevScoreB = -1;
  musicIdx    = 0;
  musicTimer  = 0;
  musicPlaying = false;
  scheduleIdx = 0;
  lobbyPressA = lobbyPressB = false;
  gameOverPending = false;
  noTone(BUZZER_PIN);
  for (uint8_t i = 0; i < MAX_NOTES; i++) notes[i].active = false;
}

// ════════════════════════════════════════════════════════════════════
//  遊戲結束判斷
// ════════════════════════════════════════════════════════════════════
bool isGameOver() {
  if (musicPlaying || musicIdx < SONG_LEN) return false;
  if (scheduleIdx  < SCHEDULE_LEN)         return false;
  for (uint8_t i = 0; i < MAX_NOTES; i++)
    if (notes[i].active) return false;
  return true;
}

// ════════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(9600);    // 玩家 A（D0 RX）
  serialB.begin(9600);   // 玩家 B（D4 RX）

  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  tft.begin();
  tft.setRotation(1);    // 橫置 320×240

  resetGame();
  drawLobby();
}

// ════════════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════════════
void loop() {
  uint32_t now = millis();
  readSerialEvents();

  switch (gameState) {

    // ── LOBBY ──────────────────────────────────────────────────────
    case LOBBY:
      if (lobbyPressA && lobbyPressB) {
        resetGame();
        drawStaticUI();
        songStartMs  = millis();
        musicTimer   = songStartMs;
        musicPlaying = true;
        lastFrameMs  = songStartMs;
        // 播放第一個音
        uint16_t f0 = pgm_read_word(&SONG[0].freq);
        if (f0 > 0) tone(BUZZER_PIN, f0);
        gameState = PLAYING;
      }
      break;

    // ── PLAYING ────────────────────────────────────────────────────
    case PLAYING: {

      // 1) 蜂鳴器播放
      if (musicIdx < SONG_LEN && musicPlaying) {
        uint16_t dur = pgm_read_word(&SONG[musicIdx].duration);
        if (now - musicTimer >= dur) {
          musicTimer += dur;
          musicIdx++;
          if (musicIdx < SONG_LEN) {
            uint16_t f = pgm_read_word(&SONG[musicIdx].freq);
            if (f > 0) tone(BUZZER_PIN, f); else noTone(BUZZER_PIN);
          } else {
            noTone(BUZZER_PIN);
            musicPlaying = false;
          }
        }
      }

      // 2) 按時間表生成音符
      uint32_t elapsed = now - songStartMs;
      while (scheduleIdx < SCHEDULE_LEN) {
        uint32_t spawnMs = pgm_read_dword(&NOTE_SCHEDULE[scheduleIdx].spawnMs);
        if (elapsed < spawnMs) break;
        for (uint8_t i = 0; i < MAX_NOTES; i++) {
          if (!notes[i].active) {
            notes[i].active    = true;
            notes[i].judged    = false;
            notes[i].isYellow  = pgm_read_byte(&NOTE_SCHEDULE[scheduleIdx].isYellow);
            notes[i].isPlayerA = pgm_read_byte(&NOTE_SCHEDULE[scheduleIdx].isPlayerA);
            notes[i].x         = SCREEN_W + NOTE_R;
            notes[i].prevX     = notes[i].x;
            break;
          }
        }
        scheduleIdx++;
      }

      // 3) 每幀更新畫面
      if (now - lastFrameMs >= FRAME_MS) {
        lastFrameMs = now;
        for (uint8_t i = 0; i < MAX_NOTES; i++) {
          if (!notes[i].active) continue;
          eraseNote(notes[i]);
          notes[i].prevX = notes[i].x;
          notes[i].x    -= NOTE_SPEED;
          if (notes[i].x < JUDGE_X - 40) {
            showJudgeText(notes[i].isPlayerA, 0);
            notes[i].active = false;
            continue;
          }
          drawNote(notes[i]);
        }
        drawScores();
      }

      // 4) 遊戲結束
      if (!gameOverPending && isGameOver()) {
        gameOverTimer   = now;
        gameOverPending = true;
      }
      if (gameOverPending && (now - gameOverTimer >= 1500)) {
        drawResult();
        lobbyPressA = lobbyPressB = false;
        gameState   = RESULT;
        gameOverPending = false;
      }
      break;
    }

    // ── RESULT ─────────────────────────────────────────────────────
    case RESULT:
      if (lobbyPressA && lobbyPressB) {
        resetGame();
        drawLobby();
        gameState = LOBBY;
      }
      break;
  }
}
