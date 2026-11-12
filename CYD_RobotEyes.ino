/*
 * ดวงตาหุ่นยนต์มีอารมณ์ สำหรับ ESP32 CYD (ESP32-2432S028R) จอสี 2.8" 320x240 + จอสัมผัส
 * -------------------------------------------------------------------------------------
 * ติดตั้งก่อนใช้:
 *   1) Board: ติดตั้ง "esp32 by Espressif" แล้วเลือกบอร์ด "ESP32 Dev Module"
 *   2) Library: "TFT_eSPI" (Bodmer) และ "XPT2046_Touchscreen" (Paul Stoffregen)
 *   3) คัดลอก TFT_eSPI_Setup/Setup_CYD.h ไปไว้ใน libraries\TFT_eSPI\User_Setups\
 *      แล้วแก้ libraries\TFT_eSPI\User_Setup_Select.h ให้ #include <User_Setups/Setup_CYD.h>
 *      (รายละเอียดอยู่ในหัวไฟล์ Setup_CYD.h)
 *
 * ความสามารถ:
 *   - 7 อารมณ์: ปกติ, ดีใจ, โกรธ, เศร้า, ง่วง, ตกใจ, ตาหัวใจ  (ตาขาวบนพื้นดำ ไม่มีตัวหนังสือ)
 *   - เปลี่ยนรูปตาและสีแบบนุ่มนวล, กะพริบตาและกลอกตาเองแบบสุ่ม
 *   - แตะ/ลากนิ้วบนจอ  -> ตามองตามนิ้ว
 *   - แตะค้าง ~1 วินาที -> เปลี่ยนอารมณ์
 *   - ไม่แตะจอ          -> เปลี่ยนอารมณ์เองทุก 6 วินาที
 *   - วาดลง Sprite (หน่วยความจำ) ก่อนส่งขึ้นจอทีเดียว ภาพจึงไม่กระพริบ
 */

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ตรวจว่า TFT_eSPI ใช้ไฟล์ตั้งค่าของ CYD จริง (ถ้ายังเป็นค่าเริ่มต้น จอจะไม่ติด)
#if !defined(TFT_BL) || !defined(TFT_MOSI) || (TFT_MOSI != 13)
#error "TFT_eSPI is not configured for CYD: copy TFT_eSPI_Setup/Setup_CYD.h to libraries/TFT_eSPI/User_Setups/ and #include <User_Setups/Setup_CYD.h> in User_Setup_Select.h"
#endif

// ---------------- ขาระบบสัมผัสของ CYD (แยก SPI กับจอ) ----------------
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

// ค่าดิบของ touch -> พิกัดจอ  (ถ้าแตะแล้วตามองผิดทาง/ไม่ตรง ให้ปรับ 4 ค่านี้
// ดูค่าดิบได้จาก Serial Monitor 115200 ตอนแตะจอ)
#define TS_MINX  200
#define TS_MAXX 3700
#define TS_MINY  240
#define TS_MAXY 3800

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft;
TFT_eSprite spr(&tft);

const int SW = 320, SH = 240;
const int LEFT_CX = 100, RIGHT_CX = 220;
const int EYE_CY = 120;                // กลางจอ
const uint16_t BG    = TFT_BLACK;      // พื้นดำ
const uint16_t EYE_C = TFT_WHITE;      // ตาขาว

// ---------------- อารมณ์ ----------------
enum Mood { MOOD_NORMAL, MOOD_HAPPY, MOOD_ANGRY, MOOD_SAD, MOOD_SLEEPY,
            MOOD_SURPRISED, MOOD_LOVE, MOOD_COUNT };

struct EyeShape {
  float w, h, r;        // กว้าง สูง ความโค้งมุม
  float offY;           // เลื่อนตาขึ้น/ลง
  float angry, sad;     // 0..1 เปลือกตาเฉียง (ด้านใน / ด้านนอก)
  float happy;          // 0..1 ตัดขอบล่างเป็นตายิ้ม
  float love;           // 0..1 (>0.5 = วาดเป็นหัวใจ)
};

EyeShape cur, tgt;
Mood mood = MOOD_NORMAL;

// ปรับรูปตาของแต่ละอารมณ์ได้ตรงนี้
void setMood(Mood m) {
  mood = m;
  tgt.w = 80;  tgt.h = 90;  tgt.r = 24;  tgt.offY = 0;
  tgt.angry = 0; tgt.sad = 0; tgt.happy = 0; tgt.love = 0;

  switch (m) {
    case MOOD_HAPPY:     tgt.happy = 1;                                break;
    case MOOD_ANGRY:     tgt.angry = 0.5;  tgt.h = 80;                 break;
    case MOOD_SAD:       tgt.sad = 0.45;   tgt.offY = 10;              break;
    case MOOD_SLEEPY:    tgt.h = 24;       tgt.offY = 22; tgt.r = 10;  break;
    case MOOD_SURPRISED: tgt.w = 96; tgt.h = 110; tgt.r = 40;          break;
    case MOOD_LOVE:      tgt.love = 1;                                 break;
    default: break;
  }
}

// ---------------- tween ----------------
inline void ease(float &c, float t, float k = 0.2f) { c += (t - c) * k; }

void updateShape() {
  ease(cur.w, tgt.w); ease(cur.h, tgt.h); ease(cur.r, tgt.r); ease(cur.offY, tgt.offY);
  ease(cur.angry, tgt.angry); ease(cur.sad, tgt.sad);
  ease(cur.happy, tgt.happy); ease(cur.love, tgt.love);
}

// ---------------- หัวใจ ----------------
void drawHeart(int cx, int cy, float s, uint16_t col) {
  int r = (int)(s / 2);
  cy -= r / 4;                                    // จัดให้อยู่กลาง
  spr.fillCircle(cx - r, cy - r / 2, r, col);
  spr.fillCircle(cx + r, cy - r / 2, r, col);
  int tipY = cy + (int)(1.914f * r);
  int sideX = (int)(1.707f * r), sideY = cy + (int)(0.207f * r);
  spr.fillTriangle(cx - sideX, sideY, cx + sideX, sideY, cx, tipY, col);
  spr.fillTriangle(cx - r, cy - r / 2, cx + r, cy - r / 2, cx, tipY, col);
}

// ---------------- วาดตา 1 ข้าง ----------------
void drawEye(int cx, int cy, float blink, bool isLeft, uint16_t col, unsigned long now) {
  if (cur.love > 0.5f) {
    float pulse = 1.0f + 0.08f * sin(now / 150.0f);
    drawHeart(cx, cy, cur.w * 0.55f * pulse, col);
    return;
  }

  int w = (int)cur.w;
  int h = max(4, (int)(cur.h * blink));
  int r = min((int)cur.r, h / 2);
  int l = cx - w / 2, t = cy - h / 2, rr = l + w - 1, b = t + h - 1;

  spr.fillRoundRect(l, t, w, h, r, col);

  // เปลือกตาโกรธ: ตัดมุมบนด้านใน
  int lid = (int)(cur.angry * h);
  if (lid > 0) {
    if (isLeft) spr.fillTriangle(l - 2, t - 2, rr + 2, t - 2, rr + 2, t + lid, BG);
    else        spr.fillTriangle(l - 2, t - 2, rr + 2, t - 2, l - 2,  t + lid, BG);
  }

  // เปลือกตาเศร้า: ตัดมุมบนด้านนอก
  lid = (int)(cur.sad * h);
  if (lid > 0) {
    if (isLeft) spr.fillTriangle(l - 2, t - 2, rr + 2, t - 2, l - 2,  t + lid, BG);
    else        spr.fillTriangle(l - 2, t - 2, rr + 2, t - 2, rr + 2, t + lid, BG);
  }

  // ตายิ้ม: วงกลมสีพื้นดันขึ้นจากด้านล่าง
  if (cur.happy > 0.02f) {
    int R = (int)(w * 0.75f);
    int ccy = b + R - (int)(cur.happy * R * 0.65f);
    spr.fillCircle(cx, ccy, R, BG);
  }
}

// ---------------- Zzz ตอนง่วง ----------------
void drawZzz(unsigned long now, uint16_t col) {
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(col);
  for (int i = 0; i < 3; i++) {
    float p = fmod(now / 1800.0f + i / 3.0f, 1.0f);   // 0..1 ลอยขึ้น
    int x = 262 + (int)(p * 34);
    int y = 122 - (int)(p * 70);
    spr.drawString("z", x, y, p < 0.5f ? 2 : 4);
  }
}

// ---------------- กะพริบตา ----------------
unsigned long blinkStart = 0, nextBlink = 2000;

float blinkFactor(unsigned long now) {
  if (now >= nextBlink) {
    blinkStart = now;
    nextBlink = now + random(2000, 5000);
    if (random(5) == 0) nextBlink = now + 300;      // บางทีกะพริบ 2 ครั้ง
  }
  unsigned long dt = now - blinkStart;
  const unsigned long CLOSE = 80, OPEN = 110;
  if (dt < CLOSE)        return 1.0f - (float)dt / CLOSE;
  if (dt < CLOSE + OPEN) return (float)(dt - CLOSE) / OPEN;
  return 1.0f;
}

// ---------------- กลอกตา ----------------
float lookX = 0, lookY = 0, lookTX = 0, lookTY = 0;
unsigned long nextLook = 0;

void updateLook(unsigned long now) {
  if (now >= nextLook) {
    nextLook = now + random(800, 3000);
    if (random(3) == 0) { lookTX = 0; lookTY = 0; }
    else { lookTX = random(-35, 36); lookTY = random(-15, 16); }
  }
  ease(lookX, lookTX, 0.25f);
  ease(lookY, lookTY, 0.25f);
}

// ---------------- จอสัมผัส ----------------
bool wasTouched = false, longFired = false;
unsigned long touchStart = 0, lastMoodChange = 0;
const unsigned long AUTO_MOOD_MS = 6000;     // 0 = ปิดเปลี่ยนอารมณ์อัตโนมัติ
const unsigned long LONG_PRESS_MS = 900;

void nextMood() {
  setMood((Mood)((mood + 1) % MOOD_COUNT));
  lastMoodChange = millis();
}

void handleTouch(unsigned long now) {
  bool touched = ts.tirqTouched() && ts.touched();
  if (touched) {
    TS_Point p = ts.getPoint();
    int x = constrain(map(p.x, TS_MINX, TS_MAXX, 0, SW), 0, SW - 1);
    int y = constrain(map(p.y, TS_MINY, TS_MAXY, 0, SH), 0, SH - 1);

    // ตามองตามนิ้ว
    lookTX = constrain((x - SW / 2) * 35 / (SW / 2), -35, 35);
    lookTY = constrain((y - EYE_CY) * 15 / 100, -15, 15);
    nextLook = now + 2000;                    // ปล่อยนิ้วแล้วยังมองค้างอีก 2 วิ
    lastMoodChange = now;                     // กำลังเล่นอยู่ อย่าเปลี่ยนอารมณ์เอง

    if (!wasTouched) {
      touchStart = now; longFired = false;
      Serial.printf("touch raw x=%d y=%d -> %d,%d\n", p.x, p.y, x, y);
    } else if (!longFired && now - touchStart >= LONG_PRESS_MS) {
      nextMood(); longFired = true;           // แตะค้าง = เปลี่ยนอารมณ์
    }
  }
  wasTouched = touched;
}

// =====================================================================
const unsigned long FRAME_MS = 33;   // ~30 FPS
unsigned long lastFrame = 0;

void setup() {
  Serial.begin(115200);

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  tft.init();
  tft.setRotation(1);                 // แนวนอน 320x240
  tft.fillScreen(BG);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);

  spr.setColorDepth(8);               // 8-bit = 76.8KB พอดี RAM ของ ESP32
  if (spr.createSprite(SW, SH) == nullptr) {
    Serial.println("Sprite alloc FAILED");
    tft.setTextColor(TFT_RED);
    tft.drawString("Sprite alloc FAILED", 10, 10, 4);
    while (true) delay(1000);
  }

  randomSeed(esp_random());
  setMood(MOOD_NORMAL);
  cur = tgt;
}

void loop() {
  unsigned long now = millis();

  handleTouch(now);
  if (AUTO_MOOD_MS && now - lastMoodChange >= AUTO_MOOD_MS) nextMood();

  if (now - lastFrame < FRAME_MS) return;
  lastFrame = now;

  updateShape();
  updateLook(now);
  bool noBlink = (mood == MOOD_SLEEPY || mood == MOOD_LOVE);
  float bf = noBlink ? 1.0f : blinkFactor(now);
  const uint16_t col = EYE_C;

  spr.fillSprite(BG);

  int cy = EYE_CY + (int)(cur.offY + lookY);
  int dx = (int)lookX;
  drawEye(LEFT_CX + dx,  cy, bf, true,  col, now);
  drawEye(RIGHT_CX + dx, cy, bf, false, col, now);

  if (mood == MOOD_SLEEPY) drawZzz(now, col);

  spr.pushSprite(0, 0);
}
