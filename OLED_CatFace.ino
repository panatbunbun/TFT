/*
 * หน้าน้องแมวตาโต (ตาวิ้งมีประกายรูปหัวใจ ขนตางอน แก้มแดง ปากแมว ω)
 * จอ OLED 0.96" 128x64 I2C (SSD1306) แบบ 2 สี เหลือง/ฟ้า
 * ---------------------------------------------------------------
 * ต่อสาย (UNO/Nano): VCC->5V, GND->GND, SDA->A4, SCL->A5
 * ปุ่ม (ไม่ต่อก็ได้): ขา D2 -> ปุ่ม -> GND
 * ไลบรารี: Adafruit SSD1306 + Adafruit GFX
 *
 * ออกแบบสำหรับจอกลับหัว (FLIP_SCREEN 1):
 *   ตาอยู่โซนสีฟ้า (ด้านบน) / จมูก+ปากอยู่แถบสีเหลือง (ด้านล่าง)
 *
 * ลูกเล่น:
 *   - กะพริบตา, ขยิบตา, กะพริบ 2 ที, มองไปมา (ประกายในตาเลื่อนตาม), หายใจ, กระโดดดึ๋ง
 *   - 6 อารมณ์: ปกติ, ดีใจ (ตายิ้ม ^ ^ ปากอ้า), ตกหลุมรัก (หัวใจในตาเต้นตุบๆ),
 *               ตกใจ (ตาโต ปาก o), ง่วง (เปลือกตาตก), เศร้า (หางตาตก น้ำตาไหล)
 *   - ปุ่ม D2: กดสั้น = ลูบหัว -> ดีใจ, กดรัวๆ 4 ที = ตกหลุมรัก,
 *              กดค้าง 1 วิ = เปลี่ยนอารมณ์เอง (ค้าง 15 วิ), ถ้าหลับอยู่จะสะดุ้งตื่น
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define W 128
#define H 64
#define OLED_ADDR   0x3C
#define BUTTON_PIN  2
#define FLIP_SCREEN 1      // 1 = กลับหัวจอ (แนะนำสำหรับหน้านี้), 0 = ปกติ

Adafruit_SSD1306 display(W, H, &Wire, -1);
const uint16_t C_ON = SSD1306_WHITE, C_OFF = SSD1306_BLACK;

// ตำแหน่งบนหน้า
const int LEFT_CX = 34, RIGHT_CX = 94, EYE_CY = 22;
const int NOSE_Y = 48, MOUTH_Y = 53;

// ===================== อารมณ์ =====================
enum Face : uint8_t { F_NORMAL, F_HAPPY, F_LOVE, F_SURPRISED, F_SLEEPY, F_SAD,
                      F_COUNT, F_NONE = 255 };
const uint8_t faceWeight[F_COUNT] = { 45, 18, 15, 5, 9, 8 };   // โอกาสสุ่มเจอ

// ===================== สปริง =====================
struct Spring { float v, vel, t; };
struct Ell { float A, B, C; int ym; };   // วงรีเอียง (ต้องประกาศก่อนฟังก์ชันแรก)
inline void stepSpring(Spring &s, float k, float d) {
  s.vel = (s.vel + (s.t - s.v) * k) * d;
  s.v += s.vel;
}
Spring sA, sB, sTilt, sOff, sLX, sLY, sLid, sHeart;

struct Base { float a, b, tilt, lid, heart, lookY; } base;

Face face = F_NORMAL;
Face pendingFace = F_NONE;
unsigned long pendingDur = 0;
unsigned long now, faceStart, faceEnd;

unsigned long giggleUntil = 0, nextGiggle = 0, nextAction = 0;
unsigned long glanceAt = 0;  int8_t glanceTo = 0;

struct { bool on; float x, y, vy; } tear = { false, 0, 0, 0 };
unsigned long nextTear = 0;

void setFace(Face f, unsigned long dur) {
  face = f;
  faceStart = now;
  faceEnd = now + dur;
  base = { 21, 14, 0.25, 0, 6, 0 };                    // หางตาชี้ขึ้นแบบตาแมว
  switch (f) {
    case F_HAPPY:
      giggleUntil = now + 700; nextGiggle = now + random(1500, 3000);
      sOff.vel -= 3;
      break;
    case F_LOVE:
      base.b = 16; base.heart = 10;
      sHeart.vel += 4;
      break;
    case F_SURPRISED:
      base.a = 19; base.b = 18; base.tilt = 0.1; base.heart = 5;
      sB.vel += 4; sLX.t = 0; sLY.t = 0;
      break;
    case F_SLEEPY:
      base.lid = 0.55; base.tilt = 0.1;
      sLX.t = 0; sLY.t = 2;
      pendingFace = F_SURPRISED; pendingDur = 600;      // หลับเสร็จสะดุ้งตื่น
      break;
    case F_SAD:
      base.b = 13; base.tilt = -0.12; base.lookY = 3;
      nextTear = now + 900;
      break;
    default: break;
  }
}

void pickNextFace() {
  if (pendingFace != F_NONE) {
    Face f = pendingFace;
    pendingFace = F_NONE;
    setFace(f, pendingDur);
    return;
  }
  int sum = 0;
  for (uint8_t i = 0; i < F_COUNT; i++) sum += faceWeight[i];
  int r = random(sum);
  uint8_t f = 0;
  while (r >= faceWeight[f]) { r -= faceWeight[f]; f++; }
  setFace((Face)f, f == F_NORMAL ? random(5000, 10000) : random(3500, 7000));
}

// ===================== กะพริบตา =====================
unsigned long blinkStart = 0, nextBlink = 1500, secondBlinkAt = 0;
uint8_t blinkMask = 0;                 // bit0 = ซ้าย, bit1 = ขวา

void startBlink(uint8_t mask) { blinkStart = now; blinkMask = mask; }

float blinkClosed() {
  unsigned long dt = now - blinkStart;
  if (dt < 60)  return dt / 60.0;
  if (dt < 90)  return 1.0;
  if (dt < 180) return 1.0 - (dt - 90) / 90.0;
  return 0;
}

void autoBlink() {
  if (face == F_SLEEPY || face == F_HAPPY) return;
  if (secondBlinkAt && now >= secondBlinkAt) { startBlink(3); secondBlinkAt = 0; }
  if (now >= nextBlink) {
    startBlink(3);
    nextBlink = now + random(1800, 5000);
    if (random(5) == 0) secondBlinkAt = now + 280;
  }
}

// ===================== ท่าทางสุ่ม =====================
void idleActions() {
  if (glanceAt && now >= glanceAt) {
    sLX.t = glanceTo;
    glanceAt = (glanceTo != 0) ? now + 450 : 0;
    glanceTo = 0;
  }
  if (!(face == F_NORMAL || face == F_LOVE || face == F_SAD)) return;
  if (now < nextAction) return;
  nextAction = now + random(800, 2600);

  int r = random(100);
  if (r < 50) {                                    // มองไปทางสุ่ม
    if (random(3) == 0) { sLX.t = 0; sLY.t = 0; }
    else { sLX.t = random(-5, 6); sLY.t = random(-3, 4); }
  } else if (r < 64) {                             // เหลือบซ้าย-ขวา
    sLX.t = -5; glanceTo = 5; glanceAt = now + 450;
  } else if (r < 76) {                             // ขยิบตา
    startBlink(random(2) ? 1 : 2);
  } else if (r < 90) {                             // กะพริบ 2 ที
    startBlink(3); secondBlinkAt = now + 260;
  } else {                                         // กระโดดดึ๋ง
    sOff.vel -= 3;
  }
}

void faceBehaviors() {
  if (face == F_HAPPY && now >= nextGiggle) {
    giggleUntil = now + 600; nextGiggle = now + random(1500, 3500);
  }
  if (face == F_SAD && !tear.on && now >= nextTear) {
    tear.on = true;
    tear.x = LEFT_CX + 9;
    tear.y = EYE_CY + sOff.v + 12;
    tear.vy = 0;
    nextTear = now + random(1500, 3500);
  }
  if (tear.on) {
    tear.vy += 0.2;
    tear.y += tear.vy;
    if (tear.y > H + 4) tear.on = false;
  }
}

void updateSprings() {
  sA.t = base.a; sB.t = base.b; sTilt.t = base.tilt; sLid.t = base.lid; sHeart.t = base.heart;
  sOff.t = 0;
  if (face == F_SAD) sLY.t = base.lookY;
  stepSpring(sA, 0.2, 0.65);   stepSpring(sB, 0.2, 0.65);   stepSpring(sTilt, 0.2, 0.65);
  stepSpring(sOff, 0.2, 0.65); stepSpring(sLid, 0.15, 0.7); stepSpring(sHeart, 0.2, 0.6);
  stepSpring(sLX, 0.3, 0.6);   stepSpring(sLY, 0.3, 0.6);
}

// ===================== วงรีเอียง =====================
// วาดวงรีที่หมุนได้ ทีละบรรทัด (ใช้ทำตาแมวหางตาชี้ขึ้น)

Ell makeEll(float a, float b, float phi) {
  float c = cos(phi), s = sin(phi), ia = 1 / (a * a), ib = 1 / (b * b);
  Ell e;
  e.A = c * c * ia + s * s * ib;
  e.B = 2 * s * c * (ia - ib);
  e.C = s * s * ia + c * c * ib;
  e.ym = (int)sqrt(4 * e.A / (4 * e.A * e.C - e.B * e.B));
  return e;
}

bool ellSpan(const Ell &e, int y, int &x1, int &x2) {
  float d = e.B * e.B * y * y - 4 * e.A * (e.C * y * y - 1);
  if (d < 0) return false;
  float s = sqrt(d);
  x1 = (int)lround((-e.B * y - s) / (2 * e.A));
  x2 = (int)lround((-e.B * y + s) / (2 * e.A));
  return true;
}

void fillEll(int cx, int cy, const Ell &e, uint16_t col) {
  int x1, x2;
  for (int y = -e.ym; y <= e.ym; y++)
    if (ellSpan(e, y, x1, x2)) display.drawFastHLine(cx + x1, cy + y, x2 - x1 + 1, col);
}

// ===================== ส่วนประกอบหน้า =====================
void drawHeart(int cx, int cy, float s, uint16_t col) {
  int r = max(1, (int)(s / 2));
  cy -= r / 4;
  display.fillCircle(cx - r, cy - r / 2, r, col);
  display.fillCircle(cx + r, cy - r / 2, r, col);
  int tipY = cy + (int)(1.9 * r), sx = (int)(1.7 * r), sy = cy + r / 4;
  display.fillTriangle(cx - sx, sy, cx + sx, sy, cx, tipY, col);
  display.fillTriangle(cx - r, cy - r / 2, cx + r, cy - r / 2, cx, tipY, col);
}

// มุมหางตา (ด้านนอก)
void outerCorner(int cx, int cy, float a, float tilt, int m, int &ox, int &oy) {
  ox = cx + (int)(m * a * cos(tilt));
  oy = cy - (int)(a * sin(tilt));
}

void lashesUp(int ox, int oy, int m) {
  display.drawLine(ox, oy, ox + m * 6, oy - 3, C_ON);
  display.drawLine(ox - m * 3, oy - 2, ox + m * 2, oy - 7, C_ON);
}

void lashesDown(int ox, int oy, int m) {
  display.drawLine(ox, oy, ox + m * 5, oy + 3, C_ON);
  display.drawLine(ox - m * 2, oy + 1, ox + m * 1, oy + 5, C_ON);
}

// ตาลืม: ขอบตาหนา (ด้านบนหนากว่า) ข้างในดำ มีประกายหัวใจ + จุดวิ้ง
void drawOpenEye(int cx, int cy, bool isLeft, float a, float b, float heart) {
  int m = isLeft ? -1 : 1;
  float tilt = sTilt.v, phi = isLeft ? tilt : -tilt;
  Ell outer = makeEll(a, b, phi);
  fillEll(cx, cy, outer, C_ON);
  if (b > 5) fillEll(cx, cy + 1, makeEll(a - 2, b - 2.5, phi), C_OFF);

  // ประกาย (เลื่อนตามทิศที่มอง)
  if (b > 7) {
    int lx = (int)sLX.v, ly = (int)sLY.v;
    int hx = (face == F_LOVE) ? cx - 2 : cx - 6;        // หัวใจใหญ่ขยับเข้ากลางตา
    drawHeart(hx + lx, cy - 3 + ly, heart * min(1.0f, b / 15), C_ON);
    display.fillCircle(cx + 8 + lx / 2, cy + 5 + ly / 2, b > 11 ? 2 : 1, C_ON);
  }

  int ox, oy;
  outerCorner(cx, cy, a, tilt, m, ox, oy);

  // เปลือกตาตก (ง่วง)
  float lid = constrain(sLid.v, 0, 0.9);
  if (lid > 0.03) {
    int top = cy - outer.ym - 1;
    int lidY = top + (int)(lid * 2 * outer.ym);
    display.fillRect(cx - (int)a - 4, top - 1, (int)(2 * a) + 8, lidY - top + 1, C_OFF);
    int x1, x2;
    if (ellSpan(outer, lidY - cy, x1, x2)) {
      display.drawFastHLine(cx + x1 - 1, lidY, x2 - x1 + 3, C_ON);
      display.drawFastHLine(cx + x1 - 1, lidY + 1, x2 - x1 + 3, C_ON);
      lashesDown(m < 0 ? cx + x1 - 1 : cx + x2 + 1, lidY + 1, m);
    }
  } else {
    lashesUp(ox, oy, m);
  }
}

// ตาปิด (กะพริบ): เส้นโค้ง ‿ ขนตาชี้ลง
void drawClosedEye(int cx, int cy, bool isLeft, float a) {
  int m = isLeft ? -1 : 1;
  float tilt = sTilt.v, phi = isLeft ? tilt : -tilt;
  Ell e = makeEll(a, 5, phi);
  fillEll(cx, cy, e, C_ON);
  fillEll(cx, cy - 2, e, C_OFF);
  int ox, oy;
  outerCorner(cx, cy, a, tilt, m, ox, oy);
  lashesDown(ox, oy, m);
}

// ตายิ้ม: เส้นโค้ง ^ ขนตาชี้ขึ้น
void drawHappyEye(int cx, int cy, bool isLeft, float a) {
  int m = isLeft ? -1 : 1;
  float ha = a * 0.75;                           // โค้งแคบกว่าตาลืม ดูเป็น ^ ชัดๆ
  cy += 4;
  fillEll(cx, cy, makeEll(ha, 10, 0), C_ON);
  fillEll(cx, cy + 4, makeEll(ha + 2, 10, 0), C_OFF);
  lashesUp(cx + m * (int)(ha - 1), cy + 1, m);
}

void drawEye(int cx, int cy, bool isLeft, float open) {
  float a = sA.v, b = sB.v * open;
  if (face == F_HAPPY)  drawHappyEye(cx, cy, isLeft, a);
  else if (b < 3.5)     drawClosedEye(cx, cy, isLeft, a);
  else {
    float heart = sHeart.v;
    if (face == F_LOVE) {                       // หัวใจเต้นตุบ-ตุบ
      unsigned long ph = now % 1000;
      float beat = 0;
      if (ph < 120) beat = sin(ph / 120.0 * PI);
      else if (ph > 200 && ph < 320) beat = 0.6 * sin((ph - 200) / 120.0 * PI);
      heart *= 1 + 0.2 * beat;
    }
    drawOpenEye(cx, cy, isLeft, a, b, heart);
  }
}

void drawBlush(int dy) {
  int n = (face == F_LOVE || face == F_HAPPY) ? 4 : 3;
  for (int i = 0; i < n; i++) {
    display.drawLine(9 + i * 4,   45 + dy, 12 + i * 4,  41 + dy, C_ON);
    display.drawLine(103 + i * 4, 45 + dy, 106 + i * 4, 41 + dy, C_ON);
  }
}

void drawMouth(int dy) {
  int my = MOUTH_Y + dy;
  switch (face) {
    case F_HAPPY:                               // ปากอ้ายิ้ม
      display.fillCircle(64, my, 5, C_ON);
      display.fillRect(58, my - 6, 13, 6, C_OFF);
      break;
    case F_SURPRISED:                           // ปาก o
      display.drawCircle(64, my + 3, 3, C_ON);
      break;
    case F_SLEEPY:
      display.drawFastHLine(62, my + 2, 5, C_ON);
      break;
    case F_SAD:                                 // ปากคว่ำ
      display.drawCircleHelper(64, my + 5, 4, 0x1 | 0x2, C_ON);
      break;
    default:                                    // ปากแมว ω
      display.drawCircleHelper(61, NOSE_Y + 2 + dy, 3, 0x4 | 0x8, C_ON);   // ติดใต้จมูก
      display.drawCircleHelper(67, NOSE_Y + 2 + dy, 3, 0x4 | 0x8, C_ON);
      break;
  }
  // จมูกสามเหลี่ยม + เส้นเชื่อมปาก
  display.fillTriangle(62, NOSE_Y + dy, 66, NOSE_Y + dy, 64, NOSE_Y + 2 + dy, C_ON);
}

void drawTear() {
  if (!tear.on) return;
  int x = (int)tear.x, y = (int)tear.y;
  display.fillCircle(x, y, 2, C_ON);
  display.fillTriangle(x - 2, y - 1, x + 2, y - 1, x, y - 5, C_ON);
}

void render() {
  float closed = blinkClosed();
  float openL = (blinkMask & 1) ? 1 - closed : 1;
  float openR = (blinkMask & 2) ? 1 - closed : 1;

  int dy = (int)(sOff.v + sin(now / 700.0) * 0.8);      // หายใจ + กระโดด
  if (now < giggleUntil) dy -= (int)(fabs(sin(now / 80.0)) * 3);
  int ex = (int)(sLX.v * 0.4);
  int ey = EYE_CY + dy + (int)(sLY.v * 0.4);

  display.clearDisplay();
  drawEye(LEFT_CX + ex,  ey, true,  openL);
  drawEye(RIGHT_CX + ex, ey, false, openR);
  drawBlush(dy);
  drawMouth(dy);
  drawTear();
  display.display();
}

// ===================== ปุ่ม =====================
bool btnDown = false, holdFired = false;
unsigned long btnDownAt = 0, lastBtnChange = 0, firstPokeAt = 0;
uint8_t pokeCount = 0;

void poke() {
  if (now - firstPokeAt < 2000) pokeCount++;
  else { pokeCount = 1; firstPokeAt = now; }

  pendingFace = F_NONE;
  if (pokeCount >= 4) {                               // ลูบรัวๆ = ตกหลุมรัก
    pokeCount = 0;
    setFace(F_LOVE, 5000);
  } else if (face == F_SLEEPY) {                      // ปลุก
    setFace(F_SURPRISED, 700);
    pendingFace = F_HAPPY; pendingDur = 3000;
  } else {                                            // ลูบหัว = ดีใจ
    setFace(F_HAPPY, 2500);
  }
}

void handleButton() {
  bool down = digitalRead(BUTTON_PIN) == LOW;
  if (down != btnDown && now - lastBtnChange > 30) {
    lastBtnChange = now;
    btnDown = down;
    if (down) { btnDownAt = now; holdFired = false; }
    else if (!holdFired) poke();
  }
  if (btnDown && !holdFired && now - btnDownAt >= 1000) {
    holdFired = true;
    pendingFace = F_NONE;
    setFace((Face)((face + 1) % F_COUNT), 15000);
  }
}

// ===================== main =====================
const unsigned long FRAME_MS = 30;
unsigned long lastFrame = 0;

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    while (true) {                                    // จอไม่ติด: ไฟ LED บนบอร์ดกะพริบเร็ว
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(150);
    }
  }
  display.setRotation(FLIP_SCREEN ? 2 : 0);
  randomSeed(analogRead(A0));

  now = millis();
  setFace(F_NORMAL, 4000);
  sA.v = base.a; sTilt.v = base.tilt; sHeart.v = base.heart;
  sB.v = 1;                                          // เริ่มจากหลับตา แล้วลืมตาเด้งขึ้นมา
}

void loop() {
  now = millis();
  handleButton();
  if ((long)(now - faceEnd) >= 0) pickNextFace();

  if (now - lastFrame < FRAME_MS) return;
  lastFrame = now;

  faceBehaviors();
  idleActions();
  autoBlink();
  updateSprings();
  render();
}
