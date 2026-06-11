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
 *  │              Arduino A → 主機 D0 (SoftwareSerial RX)       │
 *  │              Arduino B → 主機 D4 (SoftwareSerial RX)       │
 *  │                                                             │
 *  │   注意：Uno 硬體中斷只有 D2(INT0) 與 D3(INT1)                 │
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

#include <SoftwareSerial.h>

// ─── 腳位定義 ────────────────────────────────────────────────────────
// Uno 硬體中斷腳位：D2 = INT0, D3 = INT1
#define BTN_LEFT_PIN   2   // INT0 — 左鍵（黃色音符）
#define BTN_RIGHT_PIN  3   // INT1 — 右鍵（藍色音符）
// 七段顯示器
#define dataPin 9
#define clockPin 8
#define strobePin 7
byte digitPatterns[10] = {
  0b00111111, // 0
  0b00000110, // 1
  0b01011011, // 2
  0b01001111, // 3
  0b01100110, // 4
  0b01101101, // 5
  0b01111101, // 6
  0b00000111, // 7
  0b01111111, // 8
  0b01101111  // 9
};

// ─── 防彈跳時間 ───────────────────────────────────────────────────────
#define DEBOUNCE_MS 50

// ─── 中斷共享變數（volatile 確保主程式即時讀到最新值）────────────────
volatile bool     leftPressed  = false;
volatile bool     rightPressed = false;
volatile uint32_t lastLeftMs   = 0;
volatile uint32_t lastRightMs  = 0;

// 軟體UART
const byte rxPin = 4;
const byte txPin = 5;
SoftwareSerial mySerial (rxPin, txPin);

// 函式定義
void recvScore(); // 接收uart分數資料

void sendByte(byte data); // 顯示分數函式
void displayNumber(long number);
void initTM1638();

// 變數定義
int score = 0;

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
  mySerial.begin(9600);

  // 設定按鈕腳位：INPUT_PULLUP（不需外接電阻，按下 → LOW）
  pinMode(BTN_LEFT_PIN,  INPUT_PULLUP);
  pinMode(BTN_RIGHT_PIN, INPUT_PULLUP);

  // 設定七段顯示器腳位
  pinMode(dataPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(strobePin, OUTPUT);
  digitalWrite(strobePin, HIGH);
  initTM1638();
  // 綁定中斷：FALLING = 下降沿（按下瞬間 HIGH→LOW）
  attachInterrupt(digitalPinToInterrupt(BTN_LEFT_PIN),  ISR_LeftBtn,  FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_RIGHT_PIN), ISR_RightBtn, FALLING);
}

// ─── 主迴圈：只負責把旗標轉成序列訊號 ───────────────────────────────
void loop() {
  // 左鍵旗標被 ISR 設起
  if (leftPressed) {
    leftPressed = false;   // 先清旗標，再送資料
    mySerial.write('L');
  }

  // 右鍵旗標被 ISR 設起
  if (rightPressed) {
    rightPressed = false;
    mySerial.write('R');
  }

  // 接收分數
  recvScore();
}

/*
=================
|  其他函式定義  |
=================
*/

/*
  新增接收分數資料後，在七段顯示器上顯示分數的功能。
  1代表+50
  2代表+100
  3代表+150
  4代表重置
*/
void recvScore() {
  if (mySerial.available() > 0) {
    byte rc = mySerial.read();
    if (rc == '1')      score += 50;
    else if (rc == '2') score += 100;
    else if (rc == '3') score += 150;
    else if (rc == '4') score = 0; // 重新開始遊戲

    if (rc >= '1' && rc <= '4') {
      displayNumber(score);
    }

    Serial.println(rc, BIN);
  }
}

/*
  分數顯示功能 >>>
*/

void sendByte(byte data) {
  for (int i = 0; i < 8; i++) {
    digitalWrite(clockPin, LOW);
    digitalWrite(dataPin, (data & 1) ? HIGH : LOW);
    data >>= 1;
    digitalWrite(clockPin, HIGH);
  }
}

void displayNumber(long number) {
  for (int pos = 7; pos >=0; pos--) {
    int d = number % 10;   // lấy chữ số cuối
    number /= 10;

    digitalWrite(strobePin, LOW);
    sendByte(0xC0 + pos * 2);       // địa chỉ digit
    sendByte(digitPatterns[d]);     // dữ liệu hiển thị
    digitalWrite(strobePin, HIGH);
  }
}


void initTM1638() {
  digitalWrite(strobePin, LOW);
  sendByte(0x8F); // bật hiển thị, độ sáng max
  digitalWrite(strobePin, HIGH);

  digitalWrite(strobePin, LOW);
  sendByte(0x40); // chế độ ghi dữ liệu
  digitalWrite(strobePin, HIGH);
}