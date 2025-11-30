#include <Arduino.h>
#include <DHT.h>

// define 
#define LED_PIN   18
#define DHTPIN    15
#define DHTTYPE   DHT22

DHT dht(DHTPIN, DHTTYPE);

// ================== RTOS HANDLE & BIẾN DÙNG CHUNG ==================

TaskHandle_t      taskHandleSensor = NULL;
TaskHandle_t      taskHandleLED    = NULL;
SemaphoreHandle_t xTempSemaphore   = NULL;

// Nhiệt độ đọc được từ DHT11 (°C)
volatile float g_temperature = 0.0f;

// ================== HÀM TIỆN ÍCH CHO LED ==================

// Nếu LED của bạn là common cathode (chân chung GND) thì ON = HIGH.
// Nếu common anode (chân chung 3V3) thì đổi LED_ACTIVE_HIGH = false.
const bool LED_ACTIVE_HIGH = true;

void ledOn()
{
  digitalWrite(LED_PIN, LED_ACTIVE_HIGH ? HIGH : LOW);
}

void ledOff()
{
  digitalWrite(LED_PIN, LED_ACTIVE_HIGH ? LOW : HIGH);
}

// ==== 3 PATTERN NHẤP NHÁY TƯƠNG ỨNG 3 VÙNG NHIỆT ĐỘ ====

// COOL: T < 25°C  → nháy chậm, 2 lần
void patternCool()
{
  Serial.println("[LED] Mode: COOL (slow blink, 2 times)");
  for (int i = 0; i < 2; ++i)
  {
    ledOn();
    vTaskDelay(pdMS_TO_TICKS(800));   // 0.8s ON
    ledOff();
    vTaskDelay(pdMS_TO_TICKS(800));   // 0.8s OFF
  }
}

// NORMAL: 25°C ≤ T < 30°C → nháy trung bình, 4 lần
void patternNormal()
{
  Serial.println("[LED] Mode: NORMAL (medium blink, 4 times)");
  for (int i = 0; i < 4; ++i)
  {
    ledOn();
    vTaskDelay(pdMS_TO_TICKS(300));   // 0.3s ON
    ledOff();
    vTaskDelay(pdMS_TO_TICKS(300));   // 0.3s OFF
  }
}

// HOT: T ≥ 30°C → nháy nhanh, 6 lần
void patternHot()
{
  Serial.println("[LED] Mode: HOT (fast blink, 6 times)");
  for (int i = 0; i < 6; ++i)
  {
    ledOn();
    vTaskDelay(pdMS_TO_TICKS(120));   // 0.12s ON
    ledOff();
    vTaskDelay(pdMS_TO_TICKS(120));   // 0.12s OFF
  }
}

// ================== TASK ĐỌC DHT11 ==================

void vTaskSensor(void *pvParameters)
{
  (void)pvParameters;

  for (;;)
  {
    float t = dht.readTemperature();  // đọc °C

    if (isnan(t))
    {
      Serial.println("[Sensor] Failed to read from DHT11!");
      // Không give semaphore nếu đọc lỗi, tránh dùng dữ liệu rác
    }
    else
    {
      g_temperature = t;

      Serial.print("[Sensor] Temperature = ");
      Serial.print(g_temperature);
      Serial.println(" *C");

      // Báo cho task LED biết là có nhiệt độ mới
      xSemaphoreGive(xTempSemaphore);
    }

    // Chu kỳ đọc DHT11 ~ 2 giây (DHT11 không cần đọc quá nhanh)
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// ================== TASK ĐIỀU KHIỂN LED ==================

void vTaskLED(void *pvParameters)
{
  (void)pvParameters;

  pinMode(LED_PIN, OUTPUT);
  ledOff();

  for (;;)
  {
    // Chờ đến khi TaskSensor "give" semaphore (có dữ liệu mới)
    if (xSemaphoreTake(xTempSemaphore, portMAX_DELAY) == pdTRUE)
    {
      // Lấy snapshot nhiệt độ để quyết định pattern
      float temp = g_temperature;

      Serial.print("[LED] Received new temperature = ");
      Serial.print(temp);
      Serial.println(" *C");

      if (temp < 25.0f)
      {
        patternCool();
      }
      else if (temp < 30.0f)
      {
        patternNormal();
      }
      else
      {
        patternHot();
      }

      // Sau khi nháy xong, tắt LED chờ lần cập nhật kế tiếp
      ledOff();
    }
  }
}

// ================== SETUP & LOOP ==================

void setup()
{
  Serial.begin(115200);
  delay(2000);

  Serial.println("=== Task 1: Single LED Blink with Temperature Conditions (ESP32-S3 + DHT11) ===");

  dht.begin();

  // Tạo binary semaphore
  xTempSemaphore = xSemaphoreCreateBinary();
  if (xTempSemaphore == NULL)
  {
    Serial.println("Failed to create xTempSemaphore!");
    while (1) { delay(1000); }
  }

  BaseType_t result;

  // Tạo Task đọc DHT11
  result = xTaskCreate(vTaskSensor,"SensorTask",4096,NULL,2,&taskHandleSensor);
  if (result != pdPASS)
  {
    Serial.println("Failed to create SensorTask");
  }

  // Tạo Task điều khiển LED
  result = xTaskCreate(vTaskLED,"LEDTask",4096,NULL,1,&taskHandleLED);
  if (result != pdPASS)
  {
    Serial.println("Failed to create LEDTask");
  }
}

void loop()
{
  vTaskDelay(pdMS_TO_TICKS(1000));
}
