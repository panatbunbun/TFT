/*
 * ตัวอย่างแอนิเมชันบนจอ OLED 0.96" 128x64 I2C (SSD1306)
 * การต่อสายเหมือน OLED_Test (UNO: SDA=A4, SCL=A5)
 * ไลบรารี: Adafruit SSD1306 + Adafruit GFX
 *
 * มี 3 เทคนิค สลับกันโชว์ ทีละ 6 วินาที:
 *   1) วาดด้วยโค้ด (Procedural)  -> ดวงตาหุ่นยนต์ กะพริบ/กลอกตา
 *   2) ภาพ Bitmap หลายเฟรม (Sprite) -> หัวใจเต้น + เด้งไปมา
 *   3) คำนวณตำแหน่งวัตถุหลายชิ้น  -> ดาววิ่งเข้าหาจอ (Starfield)
 *
 * หลักการทุกแบบเหมือนกัน: ล้างจอ -> วาดเฟรมใหม่ -> display() -> วนซ้ำ
 * ใช้ millis() คุมจังหวะแทน delay() เพื่อให้ความเร็วคงที่และไม่บล็อกงานอื่น
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define W 128
#define H 64
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(W, H, &Wire, -1);

// =====================================================================
// Bitmap หัวใจ 16x16 (2 เฟรม) เก็บใน Flash ด้วย PROGMEM เพื่อประหยัด RAM
// 1 แถว = 2 ไบต์ (16 บิต), บิต 1 = พิกเซลติด
// สร้างจากรูปจริงได้ที่เว็บ image2cpp (เลือก Arduino code, Horizontal)
// =====================================================================
const unsigned char PROGMEM heartBig[] = {
  0x00,0x00, 0x1C,0x38, 0x3E,0x7C, 0x7F,0xFE,
  0x7F,0xFE, 0x7F,0xFE, 0x3F,0xFC, 0x1F,0xF8,
  0x0F,0xF0, 0x07,0xE0, 0x03,0xC0, 0x01,0x80,
  0x00,0x00, 0x00,0x00, 0x00,0x00, 0x00,0x00
};
const unsigned char PROGMEM heartSmall[] = {
  0x00,0x00, 0x00,0x00, 0x00,0x00, 0x0E,0x70,
  0x1F,0xF8, 0x1F,0xF8, 0x0F,0xF0, 0x07,0xE0,
  0x03,0xC0, 0x01,0x80, 0x00,0x00, 0x00,0x00,
  0x00,0x00, 0x00,0x00, 0x00,0x00, 0x00,0x00
};
const unsigned char* const heartFrames[] = { heartBig, heartSmall };

// =====================================================================
// 1) ดวงตาหุ่นยนต์ — วาดด้วย fillRoundRect ล้วนๆ ไม่ใช้รูป
// =====================================================================
void animEyes(unsigned long t) {
  // กลอกตาซ้าย-ขวา ตามเวลา (sin ให้การเคลื่อนไหวนุ่ม)
  int lookX = (int)(sin(t / 700.0) * 12);
  int lookY = (int)(cos(t / 1100.0) * 4);

  // กะพริบทุก ~2.5 วินาที นาน 150 ms
  int eyeH = 30;
  unsigned long phase = t % 2500;
  if (phase < 150) eyeH = map(abs((int)phase - 75), 0, 75, 2, 30);

  int y = 34 - eyeH / 2 + lookY;
  display.fillRoundRect(24 + lookX, y, 28, eyeH, 8, SSD1306_WHITE);
  display.fillRoundRect(76 + lookX, y, 28, eyeH, 8, SSD1306_WHITE);
}

// =====================================================================
// 2) Sprite หัวใจ — สลับเฟรม + เคลื่อนที่เด้งขอบจอ
// =====================================================================
float hx = 10, hy = 20, hdx = 1.6, hdy = 1.1;

void animHeart(unsigned long t) {
  byte frame = (t / 250) % 2;            // เปลี่ยนเฟรมทุก 250 ms
  hx += hdx; hy += hdy;
  if (hx <= 0 || hx >= W - 16) hdx = -hdx;
  if (hy <= 16 || hy >= H - 16) hdy = -hdy;   // ไม่ให้ลงไปทับแถบเหลือง

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(30, 4);
  display.print(F("I <3 Arduino"));
  display.drawBitmap((int)hx, (int)hy, heartFrames[frame], 16, 16, SSD1306_WHITE);
}

// =====================================================================
// 3) Starfield — ดาว 40 ดวง พุ่งจากกลางจอออกไป (จำลอง 3D อย่างง่าย)
// =====================================================================
#define NSTAR 40
int16_t sx[NSTAR], sy[NSTAR], sz[NSTAR];

void resetStar(byte i) {
  sx[i] = random(-W, W);
  sy[i] = random(-H, H);
  sz[i] = random(20, 255);
}

void animStars(unsigned long) {
  for (byte i = 0; i < NSTAR; i++) {
    sz[i] -= 4;                            // ดาวเข้าใกล้ขึ้น
    if (sz[i] <= 1) { resetStar(i); continue; }
    int px = W / 2 + (long)sx[i] * 64 / sz[i];   // perspective projection
    int py = H / 2 + (long)sy[i] * 64 / sz[i];
    if (px < 0 || px >= W || py < 0 || py >= H) { resetStar(i); continue; }
    if (sz[i] < 60) display.fillRect(px, py, 2, 2, SSD1306_WHITE);  // ใกล้ = ใหญ่
    else            display.drawPixel(px, py, SSD1306_WHITE);
  }
}

// =====================================================================
typedef void (*AnimFn)(unsigned long);
AnimFn anims[] = { animEyes, animHeart, animStars };
const byte NUM_ANIMS = sizeof(anims) / sizeof(anims[0]);

const unsigned long FRAME_MS = 33;      // ~30 FPS
const unsigned long SCENE_MS = 6000;    // แต่ละฉาก 6 วินาที
unsigned long lastFrame = 0, sceneStart = 0;
byte scene = 0;

void setup() {
  Serial.begin(9600);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("SSD1306 init FAILED"));
    while (true) delay(1000);
  }
  randomSeed(analogRead(A0));
  for (byte i = 0; i < NSTAR; i++) resetStar(i);
}

void loop() {
  unsigned long now = millis();

  if (now - sceneStart >= SCENE_MS) {     // เปลี่ยนฉาก
    scene = (scene + 1) % NUM_ANIMS;
    sceneStart = now;
  }

  if (now - lastFrame >= FRAME_MS) {      // ถึงเวลาวาดเฟรมใหม่
    lastFrame = now;
    display.clearDisplay();
    anims[scene](now - sceneStart);
    display.display();
  }

  // ตรงนี้ทำงานอื่นได้ เช่น อ่านปุ่ม/เซนเซอร์ เพราะไม่มี delay() บล็อก
}
