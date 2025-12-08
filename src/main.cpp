#include <Arduino.h>
#include <DHT.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

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

// ================== HÀM TIỆN ÍCH CHO LED ĐƠN (TASK 1) ==================

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
  Serial.println("[LED] Mode: COOL (slow blink, 2 times)");
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
  Serial.println("[LED] Mode: NORMAL (medium blink, 4 times)");
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
  Serial.println("[LED] Mode: HOT (fast blink, 6 times)");
  for (int i = 0; i < 6; ++i)
  {
    ledOn();
    vTaskDelay(pdMS_TO_TICKS(120));
    ledOff();
    vTaskDelay(pdMS_TO_TICKS(120));
  }
}

// ================== HÀM VẼ THANH ĐỘ ẨM 10 MỨC (TASK 2) ==================
//
// LED 0..2  (mức 1–3):  xanh lá
// LED 3..6  (mức 4–7):  vàng
// LED 7..9  (mức 8–10): đỏ
//
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

  Serial.print("[NeoPixel] hum = ");
  Serial.print(hum);
  Serial.print(" %, level = ");
  Serial.println(level);
}

// ================== TASK ĐỌC DHT22 (SENSOR TASK – cho cả 3 Task) ==================

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
      Serial.println("[Sensor] Failed to read from DHT22!");
    }
    else
    {
      g_temperature = t;
      g_humidity    = h;

      Serial.print("[Sensor] T = ");
      Serial.print(g_temperature);
      Serial.print(" *C, H = ");
      Serial.print(g_humidity);
      Serial.println(" %");

      // ---- Task 1 & Task 2: give semaphore mỗi lần có dữ liệu hợp lệ ----
      xSemaphoreGive(xTempSemaphore);
      xSemaphoreGive(xHumSemaphore);

      // ---- Task 3: xác định state và chỉ give semaphore khi state thay đổi ----
      SystemState newState;

      if (t < 30.0f && h < 70.0f)
        newState = STATE_NORMAL;
      else if ((t < 35.0f && h < 85.0f))
        newState = STATE_WARNING;
      else
        newState = STATE_CRITICAL;

      if (newState != lastState)
      {
        g_state = newState;
        xSemaphoreGive(xDisplaySemaphore);  // chỉ release khi điều kiện đo thay đổi
        lastState = newState;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(2000));   // chu kỳ đọc ~2s
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

      Serial.print("[LED] New T = ");
      Serial.print(temp);
      Serial.println(" *C");

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
      Serial.print("[NeoPixel] New H = ");
      Serial.print(hum);
      Serial.println(" %");

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
      display.println("COOL");
      display.setCursor(0, 28);
      display.print("GREAT");
      break;

    case STATE_WARNING:
      display.println("NORMAL");
      display.setCursor(0, 28);
      display.print("OK");
      break;

    case STATE_CRITICAL:
      display.println("CRITICAL!");
      display.setCursor(0, 28);
      display.print("TAKE ACTION!");
      // tô 1 khung invert để dễ nhận biết
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

  // I2C & OLED init
  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println("SSD1306 allocation failed");
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
    // Chỉ update khi state thay đổi (semaphore được give từ SensorTask)
    if (xSemaphoreTake(xDisplaySemaphore, portMAX_DELAY) == pdTRUE)
    {
      float t  = g_temperature;
      float h  = g_humidity;
      SystemState st = g_state;

      Serial.print("[OLED] Update: T=");
      Serial.print(t);
      Serial.print("C, H=");
      Serial.print(h);
      Serial.print("%, State=");
      Serial.println((int)st);

      drawStateOnOLED(t, h, st);
    }
  }
}

// ================== SETUP & LOOP ==================

void setup()
{
  Serial.begin(115200);
  delay(2000);

  Serial.println("=== Tasks 1-3: LED (Temp) + NeoPixel (Hum) + OLED (State) ===");

  dht.begin();

  // Tạo binary semaphore
  xTempSemaphore    = xSemaphoreCreateBinary();
  xHumSemaphore     = xSemaphoreCreateBinary();
  xDisplaySemaphore = xSemaphoreCreateBinary();

  if (!xTempSemaphore || !xHumSemaphore || !xDisplaySemaphore)
  {
    Serial.println("Failed to create semaphores!");
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
  if (result != pdPASS) Serial.println("Failed to create SensorTask");

  // Task LED theo nhiệt độ
  result = xTaskCreate(
    vTaskLED,
    "LEDTask",
    4096,
    NULL,
    2,
    &taskHandleLED
  );
  if (result != pdPASS) Serial.println("Failed to create LEDTask");

  // Task NeoPixel theo độ ẩm
  result = xTaskCreate(
    vTaskNeoPixel,
    "NeoTask",
    4096,
    NULL,
    1,
    &taskHandleNeoPixel
  );
  if (result != pdPASS) Serial.println("Failed to create NeoTask");

  // Task OLED hiển thị trạng thái
  result = xTaskCreate(
    vTaskOLED,
    "OLEDTask",
    4096,
    NULL,
    1,
    &taskHandleOLED
  );
  if (result != pdPASS) Serial.println("Failed to create OLEDTask");
}

void loop()
{
  // Không dùng loop; mọi việc chạy trong các task
  vTaskDelay(pdMS_TO_TICKS(1000));
}
