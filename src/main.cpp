#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <esp_task_wdt.h>
#include <esp_random.h>
#include "soc/soc.h"
#include "soc/gpio_reg.h"

// ─── Config ─────────────────────────────────────────────────────
const char* MQTT_SERVER = "od51161c.ala.asia-southeast1.emqxsl.com";
const int   MQTT_PORT   = 8883;
const char* MQTT_USER   = "imodel_1";
const char* MQTT_PASS   = "goku@2025";

// ─── Pins ───────────────────────────────────────────────────────
#define LATCH_PIN      5
#define CLK_PIN        18
#define MOSI_PIN       23
#define HEARTBEAT_PIN  27
#define FACTORY_PIN    26  // active LOW

// DIP Switch: active LOW — GPIO34,35 input-only (cần pullup ngoại)
#define DIP_BIT0  34
#define DIP_BIT1  35
#define DIP_BIT2  32
#define DIP_BIT3  33
#define DIP_BIT4  25

// ─── Constants ──────────────────────────────────────────────────
// Mỗi board xuất 40 bit (5 byte) qua thanh ghi dịch. 1 bit = 1 nhà.
//   Board addr 0 : nhà 1..40            -> bit 0..39
//   Board addr 1 : nhà 41..58 (bit 0..17) + cảnh quan (bit 18)
#define BITS_PER_HOUSE      40
#define LANDSCAPE_BIT       18       // chỉ tồn tại trên board 1
#define HEARTBEAT_MS        500
#define MQTT_RETRY_MS       5000
#define MQTT_KEEPALIVE_S    60
#define WIFI_RESTART_MS     30000
#define MQTT_LOST_MS        300000
#define WDT_TIMEOUT_S       30
#define ALL_BITS_ON         0xFFFFFFFFFFULL

// PWM mềm (so sánh tuyến tính) để đèn sáng/tắt mượt bằng thanh ghi dịch.
//   - Bật/tắt thường: duty = 0 hoặc PWM_LEVELS (giữ nguyên hành vi cũ y hệt).
//   - Chế độ random: mỗi nhà tự fade in/out độc lập.
#define PWM_LEVELS          64                    // số mức sáng (duty 0..64) — PHẢI là luỹ thừa của 2
#define PWM_FRAME_HZ        120                   // tần số quét khung hình
static_assert((PWM_LEVELS & (PWM_LEVELS - 1)) == 0, "PWM_LEVELS phai la luy thua cua 2 (mask trong ISR)");
#define PWM_TICK_US         (1000000UL / ((unsigned long)PWM_LEVELS * PWM_FRAME_HZ))
#define ANIM_STEP_MS        33                    // nhịp cập nhật hiệu ứng (~30fps)

const char* RANDOM_TOPIC = "diorama/imodel/random";
// Topic trạng thái online/offline của board: retained "1" khi kết nối,
// broker tự phát LWT "0" khi board rớt (mất điện/mất mạng) để app hiển thị.

// ─── State ──────────────────────────────────────────────────────
WiFiClientSecure espClient;
PubSubClient     client(espClient);

uint64_t      houseState      = 0;
bool          heartbeatState  = false;
unsigned long lastHeartbeat   = 0;
unsigned long wifiLostSince   = 0;
unsigned long mqttLostSince   = 0;
int           boardAddr       = 0;
int           houseBitCount   = BITS_PER_HOUSE;   // số bit là "nhà" trên board này
char          myTopic[40]     = {0};
char          statusTopic[40] = {0};

// PWM engine
volatile uint8_t bri[BITS_PER_HOUSE] = {0};       // duty mỗi output: 0..PWM_LEVELS
volatile uint8_t pwmCounter          = 0;
hw_timer_t*      pwmTimer            = nullptr;
uint8_t          gammaLUT[256]       = {0};        // mức cảm nhận 0..255 -> duty 0..PWM_LEVELS

// Hiệu ứng random ("thành phố về đêm")
bool          randomMode      = false;
unsigned long lastAnim        = 0;
uint8_t       animPhase[BITS_PER_HOUSE] = {0};    // 0 OFF_HOLD,1 FADE_IN,2 ON_HOLD,3 FADE_OUT
uint8_t       animLevel[BITS_PER_HOUSE] = {0};    // mức cảm nhận hiện tại 0..255
uint8_t       animPeak [BITS_PER_HOUSE] = {0};    // mức sáng đỉnh ngẫu nhiên
uint8_t       animSpeed[BITS_PER_HOUSE] = {0};    // tốc độ fade
unsigned long animHold [BITS_PER_HOUSE] = {0};    // mốc thời gian hết giữ

// ─── Shift-register output (bit-bang, an toàn trong ISR) ─────────
// Ghi trực tiếp thanh ghi GPIO (atomic set/clear) -> dùng được cả trong ISR.

static inline void IRAM_ATTR pinHigh(uint8_t p) { REG_WRITE(GPIO_OUT_W1TS_REG, 1u << p); }
static inline void IRAM_ATTR pinLow (uint8_t p) { REG_WRITE(GPIO_OUT_W1TC_REG, 1u << p); }
#define PWM_SETTLE() __asm__ __volatile__("nop;nop;nop;nop;")   // đảm bảo setup/hold cho 74HC595

// Nạp 40 bit ra chuỗi thanh ghi — GIỮ ĐÚNG thứ tự SPI cũ (bit 39 -> bit 0, MSB first).
void IRAM_ATTR shiftOut40(uint64_t mask) {
  pinLow(LATCH_PIN);
  for (int i = BITS_PER_HOUSE - 1; i >= 0; i--) {
    if ((mask >> i) & 1ULL) pinHigh(MOSI_PIN); else pinLow(MOSI_PIN);
    PWM_SETTLE();
    pinHigh(CLK_PIN);
    PWM_SETTLE();
    pinLow(CLK_PIN);
  }
  pinHigh(LATCH_PIN);
}

// ISR quét PWM: mỗi tick so sánh duty với bộ đếm rồi nạp 1 khung ra thanh ghi.
void IRAM_ATTR onPwmTick() {
  uint8_t c = pwmCounter;
  uint64_t mask = 0;
  for (int i = 0; i < BITS_PER_HOUSE; i++) {
    // Lệch pha mỗi kênh (i*23 mod 64) để cạnh đóng/cắt rải đều trong khung thay vì
    // 40 kênh cùng bật tại tick 0 -> giảm bước dòng đột ngột trên nguồn LED khi PWM
    // (chỉ ảnh hưởng duty lửng của random mode; duty 0/64 bật tắt tĩnh không đổi).
    uint8_t cc = (uint8_t)(c + ((i * 23) & (PWM_LEVELS - 1))) & (PWM_LEVELS - 1);
    if (bri[i] > cc) mask |= (1ULL << i);
  }
  shiftOut40(mask);
  if (++c >= PWM_LEVELS) c = 0;
  pwmCounter = c;
}

void startPwmEngine() {
  pwmTimer = timerBegin(0, 80, true);            // timer0, chia 80 => 1 tick = 1µs, đếm lên
  timerAttachInterrupt(pwmTimer, &onPwmTick, true);
  timerAlarmWrite(pwmTimer, PWM_TICK_US, true);  // tự nạp lại
  timerAlarmEnable(pwmTimer);
}

void buildGamma() {
  for (int i = 0; i < 256; i++) {
    float f = (float)i / 255.0f;
    gammaLUT[i] = (uint8_t)(f * f * PWM_LEVELS + 0.5f);   // gamma ~2.0: dịu ở vùng tối
  }
}

// Đặt độ sáng theo bitmask on/off (0 hoặc sáng tối đa).
void setBriFromMask(uint64_t m) {
  for (int i = 0; i < BITS_PER_HOUSE; i++)
    bri[i] = ((m >> i) & 1ULL) ? PWM_LEVELS : 0;
}

// Chỉ áp lại bit cảnh quan (board 1) — dùng khi đang chạy random.
void applyLandscape() {
  if (boardAddr == 1)
    bri[LANDSCAPE_BIT] = (houseState & (1ULL << LANDSCAPE_BIT)) ? PWM_LEVELS : 0;
}

// ─── Hiệu ứng random ────────────────────────────────────────────

void animInit() {
  unsigned long now = millis();
  for (int i = 0; i < houseBitCount; i++) {
    animPhase[i] = 0;                       // OFF_HOLD
    animLevel[i] = 0;
    bri[i]       = 0;
    animHold[i]  = now + random(0, 1600);   // lệch pha để "thành phố" sáng dần
    animPeak[i]  = random(150, 256);
    animSpeed[i] = random(3, 9);
  }
  for (int i = houseBitCount; i < BITS_PER_HOUSE; i++) bri[i] = 0; // bit không phải nhà: tắt
  applyLandscape();                         // cảnh quan giữ nguyên trạng thái
}

void animStep() {
  unsigned long now = millis();
  for (int i = 0; i < houseBitCount; i++) {
    switch (animPhase[i]) {
      case 0: // OFF_HOLD
        if ((long)(now - animHold[i]) >= 0) {
          animPhase[i] = 1;
          animPeak[i]  = random(150, 256);
          animSpeed[i] = random(3, 9);
        }
        break;
      case 1: { // FADE_IN
        int v = animLevel[i] + animSpeed[i];
        if (v >= animPeak[i]) { v = animPeak[i]; animPhase[i] = 2; animHold[i] = now + random(600, 3500); }
        animLevel[i] = v;
        break;
      }
      case 2: // ON_HOLD
        if ((long)(now - animHold[i]) >= 0) { animPhase[i] = 3; animSpeed[i] = random(2, 7); }
        break;
      case 3: { // FADE_OUT
        int v = animLevel[i] - animSpeed[i];
        if (v <= 0) { v = 0; animPhase[i] = 0; animHold[i] = now + random(500, 4500); }
        animLevel[i] = v;
        break;
      }
    }
    bri[i] = gammaLUT[animLevel[i]];
  }
}

void setRandomMode(bool on) {
  // Retained message được gửi lại mỗi lần MQTT reconnect — bỏ qua nếu trạng thái
  // không đổi để hiệu ứng không bị animInit() reset (đèn tắt hết rồi sáng lại).
  if (on == randomMode) return;
  randomMode = on;
  if (on) {
    Serial.println(">> RANDOM mode ON");
    animInit();
  } else {
    Serial.println(">> RANDOM mode OFF");
    setBriFromMask(houseState);
  }
}

// ─── Debug ──────────────────────────────────────────────────────

void printBuffer() {
  Serial.printf(">> DATA [H%d]: ", boardAddr);
  for (int b = BITS_PER_HOUSE - 1; b >= 0; b--) {
    Serial.print((int)((houseState >> b) & 1));
    if (b > 0 && b % 8 == 0) Serial.print(".");
  }
  Serial.println();
}

// ─── MQTT ───────────────────────────────────────────────────────

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  // Lệnh bật/tắt chế độ random (topic riêng, không phải bitmask).
  if (strcmp(topic, RANDOM_TOPIC) == 0) {
    bool on = (length >= 1 && payload[0] == '1');
    setRandomMode(on);
    return;
  }

  char buf[16] = {0};
  if (length == 0 || length >= sizeof(buf)) return;
  memcpy(buf, payload, length);

  char* endPtr = NULL;
  uint64_t state = strtoull(buf, &endPtr, 10);
  if (endPtr == buf) {
    Serial.printf(">> WARN: invalid payload: %s\n", buf);
    return;
  }
  if (state > ALL_BITS_ON) {
    Serial.printf(">> WARN: out of range: %s\n", buf);
    return;
  }
  if (strcmp(topic, myTopic) != 0) return;

  Serial.printf(">> [house_%d] = %llu\n", boardAddr, state);
  houseState = state;
  if (randomMode) applyLandscape();      // đang random: chỉ giữ cảnh quan, nhà vẫn tự chạy
  else            setBriFromMask(houseState);
  printBuffer();
}

bool mqtt_reconnect() {
  static unsigned long lastAttempt = 0;
  if (millis() - lastAttempt < MQTT_RETRY_MS) return false;
  lastAttempt = millis();

  char id[30];
  snprintf(id, sizeof(id), "ESP32-%06X-H%d", (uint32_t)ESP.getEfuseMac(), boardAddr);
  Serial.printf("MQTT connecting as %s...\n", id);

  // LWT: broker giữ retained "0" trên statusTopic khi board rớt đột ngột;
  // board tự publish retained "1" ngay sau khi kết nối -> app biết board sống/chết.
  if (client.connect(id, MQTT_USER, MQTT_PASS, statusTopic, 1, true, "0")) {
    Serial.printf("MQTT connected! Subscribing: %s + %s\n", myTopic, RANDOM_TOPIC);
    client.publish(statusTopic, "1", true);
    client.subscribe(myTopic);
    client.subscribe(RANDOM_TOPIC);
    return true;
  }

  Serial.printf("MQTT failed, rc=%d\n", client.state());
  return false;
}

// ─── WiFi ───────────────────────────────────────────────────────

void setup_wifi() {
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);

  char apName[30];
  snprintf(apName, sizeof(apName), "Imodel_Controller_%d", boardAddr);

  if (!wm.autoConnect(apName, "68686868")) {
    Serial.println("WiFi failed! Restarting...");
    delay(1000);
    ESP.restart();
  }

  IPAddress dns1(8, 8, 8, 8), dns2(8, 8, 4, 4);
  WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns1, dns2);
  delay(500);

  Serial.printf("WiFi OK | IP: %s\n", WiFi.localIP().toString().c_str());
}

void wifi_check() {
  if (WiFi.status() == WL_CONNECTED) { wifiLostSince = 0; return; }

  if (wifiLostSince == 0) {
    wifiLostSince = millis();
    Serial.println("WiFi lost! Waiting...");
  }

  if (millis() - wifiLostSince > WIFI_RESTART_MS) {
    Serial.println("WiFi timeout! Restarting...");
    delay(1000);
    ESP.restart();
  }
}

// ─── Heartbeat ──────────────────────────────────────────────────

void heartbeat_update() {
  if (millis() - lastHeartbeat >= HEARTBEAT_MS) {
    lastHeartbeat = millis();
    heartbeatState = !heartbeatState;
    digitalWrite(HEARTBEAT_PIN, heartbeatState);
  }
}

// ─── DIP Switch ─────────────────────────────────────────────────

int readBoardAddress() {
  int b0 = !digitalRead(DIP_BIT0);
  int b1 = !digitalRead(DIP_BIT1);
  int b2 = !digitalRead(DIP_BIT2);
  int b3 = !digitalRead(DIP_BIT3);
  int b4 = !digitalRead(DIP_BIT4);
  return b0 | (b1 << 1) | (b2 << 2) | (b3 << 3) | (b4 << 4);
}

// ─── Factory Test ───────────────────────────────────────────────

bool factory_wait(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    heartbeat_update();
    esp_task_wdt_reset();
    if (digitalRead(FACTORY_PIN) != LOW) return false;
    delay(10);
  }
  return true;
}

void factory_test() {
  Serial.println("=== FACTORY TEST ===");

  while (digitalRead(FACTORY_PIN) == LOW) {
    // Phase 1: Nhấp nháy tất cả (2 lần)
    Serial.println(">> Phase 1: Blink ALL");
    for (int i = 0; i < 2; i++) {
      setBriFromMask(ALL_BITS_ON);
      if (!factory_wait(800)) goto done;

      setBriFromMask(0);
      if (!factory_wait(800)) goto done;
    }

    // Phase 2: Chạy tuần tự từng đèn
    Serial.println(">> Phase 2: Sequential scan");
    for (int bit = 0; bit < BITS_PER_HOUSE; bit++) {
      setBriFromMask(1ULL << bit);
      if (!factory_wait(100)) goto done;
    }

    setBriFromMask(0);
    if (!factory_wait(300)) break;
  }

done:
  setBriFromMask(0);
  Serial.println("=== FACTORY EXIT ===");
}

// ─── Setup & Loop ───────────────────────────────────────────────

void setup() {
  delay(1000); // Chờ 1 giây ổn định nguồn điện sau khi khởi động
  Serial.begin(115200);
  Serial.printf("\n=== Diorama Controller | %d bits/board (addr 0..1) ===\n",
                BITS_PER_HOUSE);

  pinMode(LATCH_PIN, OUTPUT); digitalWrite(LATCH_PIN, HIGH);
  pinMode(CLK_PIN,  OUTPUT);  digitalWrite(CLK_PIN,  LOW);
  pinMode(MOSI_PIN, OUTPUT);  digitalWrite(MOSI_PIN, LOW);
  pinMode(HEARTBEAT_PIN, OUTPUT);
  pinMode(FACTORY_PIN, INPUT_PULLUP);

  pinMode(DIP_BIT0, INPUT);
  pinMode(DIP_BIT1, INPUT);
  pinMode(DIP_BIT2, INPUT_PULLUP);
  pinMode(DIP_BIT3, INPUT_PULLUP);
  pinMode(DIP_BIT4, INPUT_PULLUP);

  randomSeed(esp_random());
  buildGamma();
  for (int i = 0; i < BITS_PER_HOUSE; i++) bri[i] = 0;
  startPwmEngine();               // bắt đầu quét PWM (hiện all-off)

  delay(100);
  if (digitalRead(FACTORY_PIN) == LOW) {
    delay(100);
    if (digitalRead(FACTORY_PIN) == LOW) {
      factory_test();
    }
  }

  boardAddr = readBoardAddress();
  Serial.printf("DIP address: %d\n", boardAddr);

  if (boardAddr < 0 || boardAddr > 1) {
    Serial.printf(">> FATAL: Invalid DIP address %d (must be 0-1). HALTED.\n", boardAddr);
    for (int i = 0; i < BITS_PER_HOUSE; i++) bri[i] = 0;  // tắt hết
    // Nháy nhanh 100ms (khác hẳn nhịp 500ms bình thường) để nhìn LED là biết board lỗi DIP.
    while (true) {
      digitalWrite(HEARTBEAT_PIN, (millis() / 100) & 1);
      esp_task_wdt_reset();
      delay(10);
    }
  }
  houseBitCount = (boardAddr == 0) ? 40 : 18;

  snprintf(myTopic, sizeof(myTopic), "diorama/imodel/house_%d", boardAddr);
  snprintf(statusTopic, sizeof(statusTopic), "diorama/imodel/status_%d", boardAddr);
  Serial.printf("Board topic: %s\n", myTopic);

  setup_wifi();
  espClient.setInsecure();
  client.setKeepAlive(MQTT_KEEPALIVE_S);
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(mqtt_callback);
  client.setBufferSize(512);

  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);

  Serial.println("Setup complete!\n");
}

void loop() {
  esp_task_wdt_reset();
  heartbeat_update();
  wifi_check();

  if (randomMode && (millis() - lastAnim >= ANIM_STEP_MS)) {
    lastAnim = millis();
    animStep();
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      mqtt_reconnect();
      if (mqttLostSince == 0) mqttLostSince = millis();
      if (millis() - mqttLostSince > MQTT_LOST_MS) {
        Serial.println("MQTT timeout! Restarting...");
        delay(1000);
        ESP.restart();
      }
    } else {
      mqttLostSince = 0;
      client.loop();
    }
  }
}
