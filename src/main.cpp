#include <Arduino.h>

// Chân RGB
#define PIN_R 18
#define PIN_G 17
#define PIN_B 16

// Nếu module là common cathode (chân chung GND) thì ACTIVE_HIGH = true
// Nếu module là common anode (chân chung 3V3) thì ACTIVE_HIGH = false (ngược logic)
const bool ACTIVE_HIGH = true;  // thử với true trước, nếu bị ngược thì đổi thành false

void setColor(bool r, bool g, bool b)
{
  // Chuyển bool thành HIGH/LOW theo kiểu đấu dây
  if (ACTIVE_HIGH)
  {
    digitalWrite(PIN_R, r ? HIGH : LOW);
    digitalWrite(PIN_G, g ? HIGH : LOW);
    digitalWrite(PIN_B, b ? HIGH : LOW);
  }
  else
  {
    // Common anode: bật LED = kéo chân xuống LOW
    digitalWrite(PIN_R, r ? LOW : HIGH);
    digitalWrite(PIN_G, g ? LOW : HIGH);
    digitalWrite(PIN_B, b ? LOW : HIGH);
  }
}

void setup()
{
  Serial.begin(115200);
  delay(2000);

  Serial.println("=== Test RGB LED on pins 18 (R), 19 (G), 20 (B) ===");

  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);

  // Tắt hết ban đầu
  setColor(false, false, false);
}

void loop()
{
  Serial.println("Red");
  setColor(true, false, false);   // Đỏ
  delay(1000);

  Serial.println("Green");
  setColor(false, true, false);   // Xanh lá
  delay(1000);

  Serial.println("Blue");
  setColor(false, false, true);   // Xanh dương
  delay(1000);

  Serial.println("White");
  setColor(true, true, true);     // Trắng (RGB đều bật)
  delay(1000);

  Serial.println("Off");
  setColor(false, false, false);  // Tắt
  delay(1000);
}
