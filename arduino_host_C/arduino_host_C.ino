/*
 * =====================================================================
 *  雙人節奏音律遊戲 — 主機 (Arduino C)
 *
 *  ┌─────────────────────────────────────────────────────────────────┐
 *  │  腳位配置 (Arduino Uno)                                          │
 *  │                                                                 │
 *  │  ── TFT ILI9341 2.8" SPI 240×320 ──────────────────────────   │
 *  │    VCC  → 3.3V 或 5V (依模組規格)                               │
 *  │    GND  → GND                                                   │
 *  │    CS   → D8                                                    │
 *  │    RST  → D9                                                    │
 *  │    DC   → D10                                                   │
 *  │    MOSI → D11  (硬體 SPI)                                       │
 *  │    SCK  → D13  (硬體 SPI)                                       │
 *  │    LED  → 3.3V 或透過 100Ω 電阻接 5V                           │
 *  │                                                               │                                           │
 *  │                                                                │
 *  │  ── 玩家 A (Arduino A) ─────────────────────────────────────   │
 *  │    A.TX → 主機 D0  (HardwareSerial RX)                         ｜
 *  |    A.RX → 主機 D1  (HardwareSerial TX)                          │
 *  │    ⚠ 上傳程式時請先拔掉此連線，避免衝突                           │
 *  │                                                                 │
 *  │  ── 玩家 B (Arduino B) ─────────────────────────────────────    │
 *  │    B.TX → 主機 D4  (SoftwareSerial RX)                          |
 *  |    B.RX → 主機 D5  (SoftwareSerial TX)                          │
 *  └─────────────────────────────────────────────────────────────────┘
 *
 *  通訊協定：玩家端按左鍵送 'L'，按右鍵送 'R'（9600 baud）
 *
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
//  Struct 定義（必須在所有函式之前）
// ════════════════════════════════════════════════════════════════════
struct MusicNote {
  uint32_t spawnTime;   // 距離歌曲開始的絕對 ms
  bool     isYellow;    // 對應音符顏色：true=黃(左鍵) false=藍(右鍵)
  bool     isPlayerA;   // 對應玩家：true=P1 false=P2
};

struct RhythmNote {
  bool    active;
  bool    isYellow;
  bool    isPlayerA;
  int16_t x;
  int16_t prevX;
  bool    judged;
};

// ════════════════════════════════════════════════════════════════════
//  腳位 & 硬體物件
// ════════════════════════════════════════════════════════════════════
#define TFT_CS   8
#define TFT_RST  9
#define TFT_DC  10
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// #define BUZZER_PIN 3

SoftwareSerial serialB(4, 5);   // D4=RX(接B.TX), D5=TX(接B.RX)

// ════════════════════════════════════════════════════════════════════
//  螢幕尺寸 & 顏色
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
//  版面 & 速度參數
// ════════════════════════════════════════════════════════════════════
#define LANE_H      108    // 每個 lane 高度
#define LANE_A_Y      4    // P1 lane 起始 Y
#define LANE_B_Y    128    // P2 lane 起始 Y
#define DIVIDER_Y   116    // 分隔線 Y
#define NOTE_R       13    // 音符半徑 (px)
#define JUDGE_X      45    // 判定線 X

// ── 速度調整區 ──────────────────────────────────────────────────────
// NOTE_SPEED：每幀移動幾像素，越大越快
// FRAME_MS  ：每幀間隔，越小越順（建議 8~16）
#define NOTE_SPEED   5     // px / frame  ← 調快（原本 2）
#define FRAME_MS     8     // ms / frame  ← 刷新加倍（原本 16）

// 飛行距離 = 螢幕右緣到判定線
// 飛行時間(ms) = (SCREEN_W - JUDGE_X) / NOTE_SPEED * FRAME_MS
// = (320-45)/5*8 = 440ms  → 音符生成後 440ms 恰好到達判定線
#define TRAVEL_PX   (SCREEN_W - JUDGE_X + NOTE_R)   // 生成點到判定線距離

// ════════════════════════════════════════════════════════════════════
//  有限狀態機
// ════════════════════════════════════════════════════════════════════
enum GameState { LOBBY, PLAYING, RESULT };
GameState gameState = LOBBY;

// ════════════════════════════════════════════════════════════════════
//  旋律資料（PROGMEM）
//  每個音符同時記錄：音高、音長、對應玩家、按鍵顏色
//  休止符（freq=0）不生成圖形，只停頓
// ════════════════════════════════════════════════════════════════════
#define N_C4  262
#define N_D4  294
#define N_E4  330
#define N_F4  349
#define N_G4  392
#define N_A4  440
#define N_C5  523
#define REST    0

// isYellow / isPlayerA 交替排列，讓兩個玩家輪流出現音符
// Y=黃色(左鍵)  B=藍色(右鍵)   A=玩家A  b=玩家B
//                    freq  dur   Y/B    A/b
/*
PROGMEM：AVR 的巨集，指示編譯器把這份資料放進 Flash（程式記憶體） 而非 SRAM
必須使用pgm_read_word存取。例如：pgm_read_word(&SONG[i].freq);
*/
const MusicNote SONG[] PROGMEM = {
  {0,    true,  true },   // 黃 A 
  {0,    false, false},   // 藍 B，與上個音符在同個位置（0ms）生成
  {400,  false, true },   // 藍 A，再400ms生成
};
#define SONG_LEN (sizeof(SONG)/sizeof(SONG[0]))

// ════════════════════════════════════════════════════════════════════
//  執行期變數
// ════════════════════════════════════════════════════════════════════
#define MAX_NOTES 8
RhythmNote notes[MAX_NOTES];

int16_t  scoreA = 0, scoreB = 0;
int16_t  prevScoreA = -1, prevScoreB = -1;

uint8_t  musicIdx     = 0;
bool     musicPlaying = false;
uint32_t songStartMs  = 0;

uint32_t lastFrameMs  = 0;

bool lobbyPressA = false, lobbyPressB = false;
uint32_t gameOverTimer   = 0;
bool     gameOverPending = false;
bool gameRestarting = false;

// ════════════════════════════════════════════════════════════════════
//  圖形輔助
// ════════════════════════════════════════════════════════════════════
int16_t laneNoteY(bool isPlayerA) {
  return isPlayerA ? (LANE_A_Y + LANE_H / 2)
                   : (LANE_B_Y + LANE_H / 2);
}

void eraseNote(const RhythmNote& n) {
  int16_t y  = laneNoteY(n.isPlayerA);
  int16_t lx = n.prevX - NOTE_R - 1;
  int16_t ly = y - NOTE_R - 1;
  int16_t lw = (NOTE_R + 1) * 2 + 2;
  int16_t lh = (NOTE_R + 1) * 2 + 2;
  tft.fillRect(lx, ly, lw, lh, COL_LANE_BG);
  // 補畫判定線（若音符曾壓過）
  if (n.prevX - NOTE_R <= JUDGE_X + 1 && n.prevX + NOTE_R >= JUDGE_X) {
    int16_t base = n.isPlayerA ? LANE_A_Y : LANE_B_Y;
    tft.drawFastVLine(JUDGE_X,     base, LANE_H, COL_JUDGE_LINE);
    tft.drawFastVLine(JUDGE_X + 1, base, LANE_H, COL_JUDGE_LINE);
  }
}

void drawNote(const RhythmNote& n) {
  int16_t  y   = laneNoteY(n.isPlayerA);
  uint16_t col = n.isYellow ? COL_YELLOW : COL_BLUE;
  tft.fillCircle(n.x, y, NOTE_R, col);
  tft.drawCircle(n.x, y, NOTE_R,     COL_WHITE);
  tft.fillCircle(n.x, y, NOTE_R / 3, n.isYellow ? 0xFC00 : 0x031F);
}

void drawScores() {
  if (scoreA == prevScoreA && scoreB == prevScoreB) return;
  tft.fillRect(220, LANE_A_Y + 2, 98, 20, COL_LANE_BG);
  tft.fillRect(220, LANE_B_Y + 2, 98, 20, COL_LANE_BG);
  tft.setTextSize(2); tft.setTextColor(COL_WHITE);
  tft.setCursor(222, LANE_A_Y + 4); tft.print(scoreA);
  tft.setCursor(222, LANE_B_Y + 4); tft.print(scoreB);
  prevScoreA = scoreA; prevScoreB = scoreB;
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
  tft.setTextSize(1); tft.setTextColor(0xA514);
  tft.setCursor(JUDGE_X + 5, LANE_A_Y + 3); tft.print("P1");
  tft.setCursor(JUDGE_X + 5, LANE_B_Y + 3); tft.print("P2");
  prevScoreA = prevScoreB = -1;
  drawScores();
}

// ════════════════════════════════════════════════════════════════════
//  生成新音符（在螢幕右側）
// ════════════════════════════════════════════════════════════════════
void spawnNote(bool isYellow, bool isPlayerA) {
  for (uint8_t i = 0; i < MAX_NOTES; i++) {
    if (!notes[i].active) {
      notes[i].active    = true;
      notes[i].judged    = false;
      notes[i].isYellow  = isYellow;
      notes[i].isPlayerA = isPlayerA;
      notes[i].x         = SCREEN_W + NOTE_R;
      notes[i].prevX     = notes[i].x;
      return;
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  判定
// ════════════════════════════════════════════════════════════════════
int16_t calcScore(int16_t dist) {
  if (dist <= 15) return 150;
  if (dist <= 25) return 100;
  if (dist <= 35) return  50;
  return 0;
}

void showJudgeText(bool isPlayerA, int16_t score) {
  int16_t y = laneNoteY(isPlayerA) + NOTE_R + 4;
  if (y + 10 > (isPlayerA ? LANE_A_Y + LANE_H : LANE_B_Y + LANE_H))
    y = laneNoteY(isPlayerA) - NOTE_R - 12;
  tft.fillRect(JUDGE_X + 4, y - 1, 72, 12, COL_LANE_BG);
  tft.setTextSize(1);
  if      (score == 150) { tft.setTextColor(COL_YELLOW);  tft.setCursor(JUDGE_X+5, y); tft.print("PERFECT!"); }
  else if (score == 100) { tft.setTextColor(COL_GREEN);   tft.setCursor(JUDGE_X+5, y); tft.print("GREAT");    }
  else if (score ==  50) { tft.setTextColor(COL_CYAN);    tft.setCursor(JUDGE_X+5, y); tft.print("GOOD");     }
  else                   { tft.setTextColor(COL_RED);     tft.setCursor(JUDGE_X+5, y); tft.print("MISS");     }
}

// ════════════════════════════════════════════════════════════════════
//  新增：回傳判定資料給玩家端
// ════════════════════════════════════════════════════════════════════
void sendFeedback(bool isPlayerA, int16_t score) {
  byte feedback = 0x00; // 預設 0 代表沒事或 MISS
  if      (score == 150) feedback = 0x03; // Perfect
  else if (score == 100) feedback = 0x02; // Great
  else if (score ==  50) feedback = 0x01; // Good
  if (gameRestarting) feedback = 0x04;
  //else                   feedback = '0'; // Miss

  if (isPlayerA) {
    Serial.print(feedback);    // 透過硬體 Serial 送給 P1 (D1)
  } else {
    serialB.print(feedback);   // 透過 SoftwareSerial 送給 P2 (D5)
  }
}

// ════════════════════════════════════════════════════════════════════
//  修改後的判定函式
// ════════════════════════════════════════════════════════════════════
void processButtonEvent(bool isPlayerA, bool isLeft) {
  int16_t bestDist = 999;
  int8_t  bestIdx  = -1;
  for (uint8_t i = 0; i < MAX_NOTES; i++) {
    if (!notes[i].active || notes[i].judged)    continue;
    if (notes[i].isPlayerA != isPlayerA)        continue;
    int16_t d = abs(notes[i].x - JUDGE_X);
    if (d < bestDist) { bestDist = d; bestIdx = i; }
  }
  
  // 若沒按到音符，送 '0' 回饋
  if (bestIdx < 0) { 
    showJudgeText(isPlayerA, 0); 
    //sendFeedback(isPlayerA, 0); 
    return; 
  }

  RhythmNote& n = notes[bestIdx];
  bool    correct = (n.isYellow == isLeft);
  int16_t pts     = correct ? calcScore(bestDist) : 0;
  
  if (isPlayerA) scoreA += pts; else scoreB += pts;
  
  showJudgeText(isPlayerA, pts);
  sendFeedback(isPlayerA, pts); //新增：送出判定結果給玩家
  
  eraseNote(n);
  n.active = false;
  n.judged = true;
  drawScores();
}

/*void processButtonEvent(bool isPlayerA, bool isLeft) {
  int16_t bestDist = 999;
  int8_t  bestIdx  = -1;
  for (uint8_t i = 0; i < MAX_NOTES; i++) {
    if (!notes[i].active || notes[i].judged)   continue;
    if (notes[i].isPlayerA != isPlayerA)        continue;
    int16_t d = abs(notes[i].x - JUDGE_X);
    if (d < bestDist) { bestDist = d; bestIdx = i; }
  }
  if (bestIdx < 0) { showJudgeText(isPlayerA, 0); return; }

  RhythmNote& n = notes[bestIdx];
  bool    correct = (n.isYellow == isLeft);
  int16_t pts     = correct ? calcScore(bestDist) : 0;
  if (isPlayerA) scoreA += pts; else scoreB += pts;
  showJudgeText(isPlayerA, pts);
  eraseNote(n);
  n.active = false;
  n.judged = true;
  drawScores();
}*/

// ════════════════════════════════════════════════════════════════════
//  序列埠
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
//  畫面
// ════════════════════════════════════════════════════════════════════
void drawLobby() {
  tft.fillScreen(COL_BG);
  tft.setTextSize(3); tft.setTextColor(COL_YELLOW);
  tft.setCursor(30, 30);  tft.print("RHYTHM");
  tft.setCursor(30, 63);  tft.print("MASTER");
  tft.setTextSize(1); tft.setTextColor(COL_CYAN);
  tft.setCursor(20, 108); tft.print("2-Player Music Game");
  tft.setTextColor(COL_WHITE);
  tft.setCursor(10, 135); tft.print("P1(A): Yellow=Left  Blue=Right");
  tft.setCursor(10, 147); tft.print("P2(B): Yellow=Left  Blue=Right");
  tft.setTextColor(COL_GREEN);
  tft.setCursor(20, 190); tft.print("Both press any button to START");
}

void drawResult() {
  tft.fillScreen(COL_BG);
  tft.setTextSize(2); tft.setTextColor(COL_YELLOW);
  tft.setCursor(90, 18); tft.print("RESULT");
  tft.setTextColor(COL_CYAN);  tft.setCursor(20, 62);
  tft.print("P1: "); tft.setTextColor(COL_WHITE); tft.print(scoreA);
  tft.setTextColor(COL_MAGENTA); tft.setCursor(20, 95);
  tft.print("P2: "); tft.setTextColor(COL_WHITE); tft.print(scoreB);
  tft.setTextSize(3); tft.setCursor(35, 140);
  if      (scoreA > scoreB) { tft.setTextColor(COL_CYAN);    tft.print("P1 Wins!"); }
  else if (scoreB > scoreA) { tft.setTextColor(COL_MAGENTA); tft.print("P2 Wins!"); }
  else                      { tft.setTextColor(COL_YELLOW);  tft.print("  Draw!");  }
  tft.setTextSize(1); tft.setTextColor(COL_GREEN);
  tft.setCursor(20, 212); tft.print("Both press any button to LOBBY");
}

// ════════════════════════════════════════════════════════════════════
//  遊戲重置
// ════════════════════════════════════════════════════════════════════
void resetGame() {
  scoreA = scoreB = 0;
  prevScoreA = prevScoreB = -1;
  musicIdx     = 0;
  musicPlaying = false;
  lobbyPressA  = lobbyPressB  = false;
  gameOverPending = false;
  for (uint8_t i = 0; i < MAX_NOTES; i++) notes[i].active = false;

  // 觸發重置通知
  gameRestarting = true;
  sendFeedback(true, 0);  // 通知 P1
  sendFeedback(false, 0); // 通知 P2
  gameRestarting = false; // 發送完立刻關閉，避免影響後續判定
}

bool isGameOver() {
  if (musicPlaying || musicIdx < SONG_LEN) return false;
  for (uint8_t i = 0; i < MAX_NOTES; i++)
    if (notes[i].active) return false;
  return true;
}

// ════════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(9600);
  serialB.begin(9600);
  tft.begin();
  tft.setRotation(1);
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
        musicPlaying = true;
        lastFrameMs  = songStartMs;
        gameState    = PLAYING;
      }
      break;

    // ── PLAYING ────────────────────────────────────────────────────
    case PLAYING: {

      // ── 1. 音符生成（依絕對時間掃描）─────────────────────────────
      if (musicPlaying) {
        uint32_t elapsed = now - songStartMs;

        while (musicIdx < SONG_LEN) {
          uint32_t spawnTime = pgm_read_dword(&SONG[musicIdx].spawnTime);
          if (spawnTime > elapsed) break;  // 後面的都還沒到，不用繼續掃

          bool iy = pgm_read_byte(&SONG[musicIdx].isYellow);
          bool ia = pgm_read_byte(&SONG[musicIdx].isPlayerA);
          spawnNote(iy, ia);
          musicIdx++;
        }

        if (musicIdx >= SONG_LEN) {
          musicPlaying = false;
        }
      }

      // ── 2. 每幀移動 & 重繪音符 ─────────────────────────────────
      if (now - lastFrameMs >= FRAME_MS) {
        lastFrameMs = now;

        for (uint8_t i = 0; i < MAX_NOTES; i++) {
          if (!notes[i].active) continue;

          eraseNote(notes[i]);
          notes[i].prevX = notes[i].x;
          notes[i].x    -= NOTE_SPEED;

          // 超過判定線左側太多 → 自動 MISS
          if (notes[i].x < JUDGE_X - 50) {
            showJudgeText(notes[i].isPlayerA, 0);
            notes[i].active = false;
            continue;
          }

          drawNote(notes[i]);
        }

        drawScores();
      }

      // ── 3. 遊戲結束 ────────────────────────────────────────────
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
