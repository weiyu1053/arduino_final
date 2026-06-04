/*
 * =====================================================================
 *  雙人節奏遊戲 — 玩家端 (Arduino A 與 B 通用，中斷版)
 *
 *  ┌─────────────────────────────────────────────────────────────┐
 *  │  腳位接線圖 (Arduino Uno)                                    │
 *  │                                                             │
 *  │   [左鍵]  ── D2 ── (INT0) ── 內部上拉至 VCC               │
 *  │             │                                               │
 *  │            GND                                              │
 *  │                                                             │
 *  │   [右鍵]  ── D3 ── (INT1) ── 內部上拉至 VCC               │
 *  │             │                                               │
 *  │            GND                                              │
 *  │                                                             │
 *  │   TX(D1) ──→ 主機C 對應 RX 腳位                            │
 *  │              Arduino A → 主機 D0 (HardwareSerial RX)       │
 *  │              Arduino B → 主機 D4 (SoftwareSerial RX)       │
 *  │                                                             │
 *  │   注意：Uno 硬體中斷只有 D2(INT0) 與 D3(INT1)              │
 *  └─────────────────────────────────────────────────────────────┘
 *
 *  通訊協定：
 *    按下左鍵 → 送出 'L'
 *    按下右鍵 → 送出 'R'
 *    Baud rate: 9600
 *
 *  中斷觸發條件：FALLING（高電位 → 低電位，即按下按鈕瞬間）
 *  防彈跳：中斷內用 millis() 計時，< 50ms 忽略重複觸發
 * =====================================================================
 */

// ─── 腳位定義 ────────────────────────────────────────────────────────
// Uno 硬體中斷腳位：D2 = INT0, D3 = INT1
#define BTN_LEFT_PIN   2   // INT0 — 左鍵（黃色音符）
#define BTN_RIGHT_PIN  3   // INT1 — 右鍵（藍色音符）

// ─── 防彈跳時間 ───────────────────────────────────────────────────────
#define DEBOUNCE_MS 50

// ─── 中斷共享變數（volatile 確保主程式即時讀到最新值）────────────────
volatile bool     leftPressed  = false;
volatile bool     rightPressed = false;
volatile uint32_t lastLeftMs   = 0;
volatile uint32_t lastRightMs  = 0;

// ─── 中斷服務函式 (ISR) ───────────────────────────────────────────────
// ISR 內不能用 Serial，只設旗標，由 loop() 實際送出
void ISR_LeftBtn() {
  uint32_t now = millis();
  if (now - lastLeftMs > DEBOUNCE_MS) {
    leftPressed = true;
    lastLeftMs  = now;
  }
}

void ISR_RightBtn() {
  uint32_t now = millis();
  if (now - lastRightMs > DEBOUNCE_MS) {
    rightPressed = true;
    lastRightMs  = now;
  }
}

// ─── 初始化 ───────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);

  // 設定按鈕腳位：INPUT_PULLUP（不需外接電阻，按下 → LOW）
  pinMode(BTN_LEFT_PIN,  INPUT_PULLUP);
  pinMode(BTN_RIGHT_PIN, INPUT_PULLUP);

  // 綁定中斷：FALLING = 下降沿（按下瞬間 HIGH→LOW）
  attachInterrupt(digitalPinToInterrupt(BTN_LEFT_PIN),  ISR_LeftBtn,  FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_RIGHT_PIN), ISR_RightBtn, FALLING);
}

// ─── 主迴圈：只負責把旗標轉成序列訊號 ───────────────────────────────
void loop() {
  // 左鍵旗標被 ISR 設起
  if (leftPressed) {
    leftPressed = false;   // 先清旗標，再送資料
    Serial.write('L');
  }

  // 右鍵旗標被 ISR 設起
  if (rightPressed) {
    rightPressed = false;
    Serial.write('R');
  }
}
