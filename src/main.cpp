#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#include <DHT.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "tinyml_model.h"

// ================== CẤU HÌNH PHẦN CỨNG ==================

// LED đơn cho Task 1 – dùng chân R của module RGB
#define LED_PIN   18

// DHT22 (cảm biến T/H)
#define DHTPIN    15
#define DHTTYPE   DHT22
DHT dht(DHTPIN, DHTTYPE);

// NeoPixel (WS2812) cho Task 2
#define NEO_PIN    21        // DIN của dải 10 led
#define NEO_COUNT  10
Adafruit_NeoPixel strip(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);

// OLED I2C cho Task 3
#define OLED_SDA   8
#define OLED_SCL   9
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Thiết bị điều khiển qua Web (Task 4)
#define DEVICE1_PIN 2   // ví dụ: LED 1
#define DEVICE2_PIN 4   // ví dụ: LED 2

bool device1State = false;
bool device2State = false;

// ================== WIFI AP + WEBSERVER (TASK 4) ==================

const char* AP_SSID = "ESP32_S3_AP";
const char* AP_PASS = "12345678";

WebServer server(80);

// ================== RTOS HANDLE & BIẾN DÙNG CHUNG ==================

TaskHandle_t      taskHandleSensor    = NULL;
TaskHandle_t      taskHandleLED       = NULL;
TaskHandle_t      taskHandleNeoPixel  = NULL;
TaskHandle_t      taskHandleOLED      = NULL;

SemaphoreHandle_t xTempSemaphore      = NULL;   // Task 1
SemaphoreHandle_t xHumSemaphore       = NULL;   // Task 2
SemaphoreHandle_t xDisplaySemaphore   = NULL;   // Task 3

volatile float g_temperature = 0.0f;
volatile float g_humidity    = 0.0f;

// Trạng thái hệ thống cho Task 3
enum SystemState {
  STATE_NORMAL = 0,
  STATE_WARNING,
  STATE_CRITICAL
};

volatile SystemState g_state = STATE_NORMAL;

// ================== TIỆN ÍCH CHO LED ĐƠN (TASK 1) ==================

// Nếu LED là common cathode (chân chung GND) thì ON = HIGH.
const bool LED_ACTIVE_HIGH = true;

void ledOn()
{
  digitalWrite(LED_PIN, LED_ACTIVE_HIGH ? HIGH : LOW);
}

void ledOff()
{
  digitalWrite(LED_PIN, LED_ACTIVE_HIGH ? LOW : HIGH);
}

// ---- 3 PATTERN NHẤP NHÁY TƯƠNG ỨNG 3 VÙNG NHIỆT ĐỘ ----

// COOL: T < 25°C  → nháy chậm, 2 lần
void patternCool()
{
  // Serial.println("[LED] Mode: COOL (slow blink, 2 times)");
  for (int i = 0; i < 2; ++i)
  {
    ledOn();
    vTaskDelay(pdMS_TO_TICKS(800));
    ledOff();
    vTaskDelay(pdMS_TO_TICKS(800));
  }
}

// NORMAL: 25°C ≤ T < 30°C → nháy trung bình, 4 lần
void patternNormal()
{
  // Serial.println("[LED] Mode: NORMAL (medium blink, 4 times)");
  for (int i = 0; i < 4; ++i)
  {
    ledOn();
    vTaskDelay(pdMS_TO_TICKS(300));
    ledOff();
    vTaskDelay(pdMS_TO_TICKS(300));
  }
}

// HOT: T ≥ 30°C → nháy nhanh, 6 lần
void patternHot()
{
  // Serial.println("[LED] Mode: HOT (fast blink, 6 times)");
  for (int i = 0; i < 6; ++i)
  {
    ledOn();
    vTaskDelay(pdMS_TO_TICKS(120));
    ledOff();
    vTaskDelay(pdMS_TO_TICKS(120));
  }
}

// ================== HÀM VẼ THANH ĐỘ ẨM 10 MỨC (TASK 2) ==================

void showHumidityBar(float hum)
{
  int level = (int)round((hum / 100.0f) * NEO_COUNT);
  level = constrain(level, 0, NEO_COUNT);

  strip.clear();

  for (int i = 0; i < level; ++i)
  {
    uint8_t r, g, b;

    if (i < 3) {
      r = 0;   g = 255; b = 0;      // xanh lá
    } else if (i < 7) {
      r = 255; g = 255; b = 0;      // vàng
    } else {
      r = 255; g = 0;   b = 0;      // đỏ
    }

    strip.setPixelColor(i, strip.Color(r, g, b));
  }

  strip.show();

  // Serial.print("[NeoPixel] hum = ");
  // Serial.print(hum);
  // Serial.print(" %, level = ");
  // Serial.println(level);
}

// ================== TASK ĐỌC DHT22 (SENSOR – dùng cho 3 Task) ==================

void vTaskSensor(void *pvParameters)
{
  (void)pvParameters;

  SystemState lastState = STATE_NORMAL;

  for (;;)
  {
    float t = dht.readTemperature();  // °C
    float h = dht.readHumidity();     // %

    if (isnan(t) || isnan(h))
    {
      // Nếu muốn im lặng hoàn toàn thì comment dòng này
      // Serial.println("[Sensor] Failed to read from DHT22!");
    }
    else
    {
      g_temperature = t;
      g_humidity    = h;

      // ==== LOG DỮ LIỆU CHO DATASET (Task 5) ====
      // Format: DATA,<temp>,<hum>
      Serial.print("DATA,");
      Serial.print(t, 2);   // 2 chữ số sau dấu phẩy
      Serial.print(",");
      Serial.println(h, 2);
      // ==========================================

      // ---- Task 1 & 2: báo cho LED và NeoPixel ----
      xSemaphoreGive(xTempSemaphore);
      xSemaphoreGive(xHumSemaphore);

      // ---- Task 3: xác định state & chỉ give khi state đổi ----

      int label = tinyml_predict(t, h);   // nhãn gốc từ model (0,1,2)

      SystemState newState = STATE_NORMAL;
      if (label == 0)      newState = STATE_NORMAL;
      else if (label == 1) newState = STATE_WARNING;
      else if (label == 2) newState = STATE_CRITICAL;
      else                 newState = STATE_NORMAL; // fallback

      if (newState != lastState)
      {
        g_state = newState;
        xSemaphoreGive(xDisplaySemaphore);
        lastState = newState;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(2000));   // đọc DHT mỗi 2 giây
  }
}

// ================== TASK LED ĐƠN (TASK 1) ==================

void vTaskLED(void *pvParameters)
{
  (void)pvParameters;

  pinMode(LED_PIN, OUTPUT);
  ledOff();

  for (;;)
  {
    if (xSemaphoreTake(xTempSemaphore, portMAX_DELAY) == pdTRUE)
    {
      float temp = g_temperature;

      // Serial.print("[LED] New T = ");
      // Serial.print(temp);
      // Serial.println(" *C");

      if (temp < 25.0f)
        patternCool();
      else if (temp < 30.0f)
        patternNormal();
      else
        patternHot();

      ledOff();
    }
  }
}

// ================== TASK NEOPIXEL (TASK 2) ==================

void vTaskNeoPixel(void *pvParameters)
{
  (void)pvParameters;

  strip.begin();
  strip.setBrightness(60);
  strip.clear();
  strip.show();

  for (;;)
  {
    if (xSemaphoreTake(xHumSemaphore, portMAX_DELAY) == pdTRUE)
    {
      float hum = g_humidity;
      // Serial.print("[NeoPixel] New H = ");
      // Serial.print(hum);
      // Serial.println(" %");

      showHumidityBar(hum);
    }
  }
}

// ================== TASK OLED (TASK 3) ==================

void drawStateOnOLED(float t, float h, SystemState st)
{
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Dòng 1: T & H
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("T: ");
  display.print(t, 1);
  display.print("C  H: ");
  display.print(h, 0);
  display.println("%");

  // Dòng 2 & 3: trạng thái
  display.setCursor(0, 16);
  display.print("State: ");

  switch (st)
  {
    case STATE_NORMAL:
      display.println("NORMAL");
      display.setCursor(0, 28);
      display.print("System OK");
      break;

    case STATE_WARNING:
      display.println("WARNING");
      display.setCursor(0, 28);
      display.print("Check env!");
      break;

    case STATE_CRITICAL:
      display.println("CRITICAL!");
      display.setCursor(0, 28);
      display.print("TAKE ACTION!");
      display.fillRect(0, 40, SCREEN_WIDTH, 16, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(10, 44);
      display.print("ALERT");
      break;
  }

  display.display();
}

void vTaskOLED(void *pvParameters)
{
  (void)pvParameters;

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    // Serial.println("SSD1306 allocation failed");
    while (1) { delay(1000); }
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("OLED ready...");
  display.display();

  for (;;)
  {
    if (xSemaphoreTake(xDisplaySemaphore, portMAX_DELAY) == pdTRUE)
    {
      float t  = g_temperature;
      float h  = g_humidity;
      SystemState st = g_state;

      // Serial.print("[OLED] Update: T=");
      // Serial.print(t);
      // Serial.print("C, H=");
      // Serial.print(h);
      // Serial.print("%, State=");
      // Serial.println((int)st);

      drawStateOnOLED(t, h, st);
    }
  }
}

// ================== HTML UI (TASK 4) ==================

String htmlPage()
{
  String html = F(
    "<!DOCTYPE html><html><head><meta charset='utf-8'/>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
    "<title>Control Panel</title>"
    "<style>"
    "body{font-family:system-ui,Arial;background:#0f172a;color:#e5e7eb;margin:0;padding:0;}"
    ".wrap{max-width:420px;margin:24px auto;padding:16px;border-radius:16px;"
      "background:linear-gradient(135deg,#020617,#0b1220);box-shadow:0 18px 40px rgba(15,23,42,0.6);}"
    "h1{font-size:1.4rem;margin:0 0 8px;color:#f9fafb;text-align:center;}"
    ".sub{font-size:.8rem;color:#9ca3af;text-align:center;margin-bottom:16px;}"
    ".card{border-radius:14px;padding:12px 14px;margin-bottom:12px;background:#020617;"
      "border:1px solid #1f2937;display:flex;justify-content:space-between;align-items:center;}"
    ".devname{font-weight:600;font-size:.9rem;color:#e5e7eb;}"
    ".state{font-size:.8rem;padding:4px 10px;border-radius:999px;border:1px solid #374151;}"
    ".on{background:rgba(22,163,74,.15);color:#bbf7d0;border-color:#15803d;}"
    ".off{background:rgba(248,113,113,.08);color:#fecaca;border-color:#b91c1c;}"
    ".btnrow{margin-top:8px;display:flex;gap:8px;}"
    "a.btn{flex:1;text-align:center;text-decoration:none;font-size:.85rem;font-weight:500;"
      "border-radius:999px;padding:8px 0;border:1px solid #4b5563;transition:.15s;background:#111827;color:#e5e7eb;}"
    "a.btn.onb{border-color:#16a34a;background:linear-gradient(135deg,#15803d,#22c55e);color:#ecfdf5;}"
    "a.btn.offb{border-color:#b91c1c;background:linear-gradient(135deg,#7f1d1d,#ef4444);color:#fef2f2;}"
    "a.btn:hover{filter:brightness(1.06);}"
    ".footer{margin-top:12px;font-size:.7rem;color:#6b7280;text-align:center;}"
    "</style></head><body><div class='wrap'>"
    "<div class='sub'>Control devices via ESP32-S3 Access Point</div>"
  );

  // Card Device 1
  html += F("<div class='card'><div>");
  html += F("<div class='devname'>Device 1 - LED1</div>");
  html += F("<div style='font-size:.75rem;color:#9ca3af;'>Green LED</div>");
  html += F("</div><div class='state ");
  html += device1State ? F("on'>ON") : F("off'>OFF");
  html += F("</div></div><div class='btnrow'>");
  html += F("<a class='btn onb' href='/device1/on'>Turn ON</a>");
  html += F("<a class='btn offb' href='/device1/off'>Turn OFF</a></div>");

  // Card Device 2
  html += F("<div class='card'><div>");
  html += F("<div class='devname'>Device 2 - LED2</div>");
  html += F("<div style='font-size:.75rem;color:#9ca3af;'>Blue LED</div>");
  html += F("</div><div class='state ");
  html += device2State ? F("on'>ON") : F("off'>OFF");
  html += F("</div></div><div class='btnrow'>");
  html += F("<a class='btn onb' href='/device2/on'>Turn ON</a>");
  html += F("<a class='btn offb' href='/device2/off'>Turn OFF</a></div>");

  html += F(
    "<div class='footer'>ESP32_S3_AP password: 12345678. Refresh page to update state.</div>"
    "</div></body></html>"
  );

  return html;
}

// Cập nhật chân GPIO theo state
void applyDeviceState()
{
  digitalWrite(DEVICE1_PIN, device1State ? HIGH : LOW);
  digitalWrite(DEVICE2_PIN, device2State ? HIGH : LOW);
}

// Handlers HTTP
void handleRoot()
{
  server.send(200, "text/html", htmlPage());
}

void handleDevice1On()
{
  device1State = true;
  applyDeviceState();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleDevice1Off()
{
  device1State = false;
  applyDeviceState();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleDevice2On()
{
  device2State = true;
  applyDeviceState();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleDevice2Off()
{
  device2State = false;
  applyDeviceState();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleNotFound()
{
  server.send(404, "text/plain", "Not found");
}

// ================== SETUP & LOOP ==================

void setup()
{
  Serial.begin(115200);
  delay(2000);

  // Serial.println("=== Tasks 1-4: LED (Temp) + NeoPixel (Hum) + OLED (State) + WebServer AP ===");

  // --- I/O ---
  pinMode(DEVICE1_PIN, OUTPUT);
  pinMode(DEVICE2_PIN, OUTPUT);
  device1State = false;
  device2State = false;
  applyDeviceState();

  dht.begin();

  // --- WiFi AP ---
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  // IPAddress ip = WiFi.softAPIP();
  // Serial.print("AP started. SSID: ");
  // Serial.print(AP_SSID);
  // Serial.print("  Password: ");
  // Serial.print(AP_PASS);
  // Serial.print("  IP: ");
  // Serial.println(ip);

  // --- Web server routes ---
  server.on("/",            HTTP_GET, handleRoot);
  server.on("/device1/on",  HTTP_GET, handleDevice1On);
  server.on("/device1/off", HTTP_GET, handleDevice1Off);
  server.on("/device2/on",  HTTP_GET, handleDevice2On);
  server.on("/device2/off", HTTP_GET, handleDevice2Off);
  server.onNotFound(handleNotFound);
  server.begin();
  // Serial.println("HTTP server started.");

  // --- Semaphores ---
  xTempSemaphore    = xSemaphoreCreateBinary();
  xHumSemaphore     = xSemaphoreCreateBinary();
  xDisplaySemaphore = xSemaphoreCreateBinary();

  if (!xTempSemaphore || !xHumSemaphore || !xDisplaySemaphore)
  {
    // Serial.println("Failed to create semaphores!");
    while (1) { delay(1000); }
  }

  BaseType_t result;

  // Task đọc sensor
  result = xTaskCreate(
    vTaskSensor,
    "SensorTask",
    4096,
    NULL,
    3,
    &taskHandleSensor
  );
  // if (result != pdPASS) Serial.println("Failed to create SensorTask");

  // Task LED theo nhiệt độ
  result = xTaskCreate(
    vTaskLED,
    "LEDTask",
    4096,
    NULL,
    2,
    &taskHandleLED
  );
  // if (result != pdPASS) Serial.println("Failed to create LEDTask");

  // Task NeoPixel theo độ ẩm
  result = xTaskCreate(
    vTaskNeoPixel,
    "NeoTask",
    4096,
    NULL,
    1,
    &taskHandleNeoPixel
  );
  // if (result != pdPASS) Serial.println("Failed to create NeoTask");

  // Task OLED hiển thị trạng thái
  result = xTaskCreate(
    vTaskOLED,
    "OLEDTask",
    4096,
    NULL,
    1,
    &taskHandleOLED
  );
  // if (result != pdPASS) Serial.println("Failed to create OLEDTask");
}

void loop()
{
  // Task loop dành cho WebServer (Task 4)
  server.handleClient();
  vTaskDelay(pdMS_TO_TICKS(10));   // nhường CPU cho các task RTOS khác
}
