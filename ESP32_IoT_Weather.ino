/**
 * @file ESP32_IoT_Station.ino
 * @brief ESP32 Based IoT Weather Station with SoftAP & Web Dashboard
 * * This project creates a standalone Access Point (WiFi) to serve a real-time
 * weather dashboard. It utilizes BME280 for sensing and SSD1306 OLED for local display.
 * The web interface includes a custom JS-based charting engine (Canvas API).
 * * @hardware
 * - ESP32 Development Board
 * - BME280 (I2C: 0x76)
 * - SSD1306 OLED 0.96" (I2C: 0x3C)
 * - Buzzer & LED (Alarm indicators)
 * * @author mVefa
 * @date 2025
 */

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// ==========================================
// HARDWARE CONFIGURATION
// ==========================================
#define LED_PIN    2    // Status/Alarm LED (Onboard or External)
#define BUZZER_PIN 15   // Active Buzzer for Alarm

// Alarm Thresholds
const float TEMP_ALARM_LIMIT = 30.0; 

// ==========================================
// WIFI ACCESS POINT SETTINGS
// ==========================================
// Note: ESP32 acts as a Router (SoftAP).
const char* ssid     = "IoT_Weather_Station"; // Updated for professional look
const char* password = "12345678";            // WPA2 Password

WiFiServer server(80);
Adafruit_BME280 bme;

// ==========================================
// OLED DISPLAY SETTINGS
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Global Variables
float temperature = 0;
float humidity    = 0;
float pressure    = 0;
bool alarmState   = false;

// Timing & Multitasking
unsigned long lastReadMillis = 0;
const unsigned long readInterval = 1000; // Sensor polling rate (1s)

unsigned long lastBuzzerMillis = 0;
bool buzzerLedState = false;

// Error Handling
int errorCount = 0;
const int maxErrorsBeforeReinit = 5;

// ==========================================
// SETUP ROUTINE
// ==========================================
void setup() {
  Serial.begin(115200);
  
  // Pin Configuration
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  delay(1000);
  
  // I2C Initialization (SDA:21, SCL:22 by default on ESP32)
  Wire.begin(21, 22);

  initDisplay();
  initSensor();

  // Start WiFi Access Point
  Serial.println("Starting Access Point Mode...");
  WiFi.softAP(ssid, password);
  server.begin();
  
  Serial.print("AP Started. IP Address: ");
  Serial.println(WiFi.softAPIP());
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  unsigned long now = millis();

  // 1. Sensor Polling (Non-blocking)
  if (now - lastReadMillis >= readInterval) {
    lastReadMillis = now;
    readSensorSafe();
    checkAlarm(); 
  }

  // 2. Handle Alarm Outputs
  handleAlarmEffects(now);

  // 3. Handle Web Server Requests
  WiFiClient client = server.available();
  if (client) {
    String req = client.readStringUntil('\r');
    client.readStringUntil('\n'); // Clear buffer

    // Route handling
    if (req.indexOf("GET /data") >= 0) {
      sendJson(client); // AJAX Endpoint
    } else {
      sendHtml(client); // Main Dashboard
    }
    client.stop();
  }
}

// ==========================================
// LOGIC FUNCTIONS
// ==========================================
void checkAlarm() {
  if (temperature >= TEMP_ALARM_LIMIT) {
    alarmState = true;
  } else {
    alarmState = false;
    // Reset outputs immediately when alarm clears
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    buzzerLedState = false;
  }
}

void handleAlarmEffects(unsigned long currentMillis) {
  if (alarmState) {
    // Toggle Buzzer/LED every 500ms
    if (currentMillis - lastBuzzerMillis >= 500) {
      lastBuzzerMillis = currentMillis;
      buzzerLedState = !buzzerLedState;
      digitalWrite(BUZZER_PIN, buzzerLedState ? HIGH : LOW);
      digitalWrite(LED_PIN, buzzerLedState ? HIGH : LOW);
    }
  }
}

void initDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    return;
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("Booting System...");
  display.display();
}

void initSensor() {
  // Default BME280 address is usually 0x76 or 0x77
  if (!bme.begin(0x76)) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    return;
  }
  errorCount = 0;
}

void readSensorSafe() {
  float t = bme.readTemperature();
  float h = bme.readHumidity();
  float p = bme.readPressure() / 100.0F; // Convert to hPa

  // Validate Sensor Data
  if (isnan(t) || isnan(h) || t < -40 || t > 85) {
    handleError();
    return;
  }
  temperature = t;
  humidity    = h;
  pressure    = p;
  errorCount  = 0;
  updateDisplay();
}

void handleError() {
  errorCount++;
  Serial.print("Sensor Error Count: ");
  Serial.println(errorCount);
  if (errorCount >= maxErrorsBeforeReinit) {
    Serial.println("Re-initializing sensor...");
    initSensor();
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  
  if(alarmState) display.print("!! ALARM: CRITICAL !!");
  else display.print("IoT Monitor Active");

  display.setTextSize(2);
  display.setCursor(0, 15);
  display.print(temperature, 1);
  display.setTextSize(1);
  display.print(" C");

  display.setCursor(0, 40);
  display.print("Hum: %"); display.println(humidity, 1);
  display.print("Prs: "); display.print(pressure, 0); display.print(" hPa");
  display.display();
}

// ==========================================
// WEB SERVER FUNCTIONS
// ==========================================

// Send Sensor Data as JSON
void sendJson(WiFiClient &client) {
  String json = "{";
  json += "\"temperature\":" + String(temperature, 2) + ",";
  json += "\"humidity\":" + String(humidity, 2) + ",";
  json += "\"pressure\":" + String(pressure, 2) + ",";
  json += "\"alarm\":" + String(alarmState ? "true" : "false");
  json += "}";

  client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n");
  client.print(json);
}

// Send Main Dashboard HTML/CSS/JS
void sendHtml(WiFiClient &client) {
  String html = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
  
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>IoT Monitor</title>";
  
  // CSS STYLES
  html += "<style>";
  html += "body { background-color: #1a1a2e; color: #fff; font-family: 'Segoe UI', sans-serif; text-align: center; margin: 0; padding: 20px; transition: background 0.5s; }";
  html += "h2 { color: #e94560; margin-bottom: 30px; letter-spacing: 2px; }";
  
  html += ".container { display: flex; flex-wrap: wrap; justify-content: center; gap: 20px; }";
  html += ".card { background: #16213e; padding: 20px; border-radius: 12px; box-shadow: 0 10px 20px rgba(0,0,0,0.3); width: 100%; max-width: 280px; position: relative; overflow: hidden; }";
  html += ".card::before { content:''; position: absolute; top:0; left:0; width: 5px; height: 100%; background: #0f3460; }";
  html += ".val { font-size: 3em; font-weight: 600; margin-top: 10px; }";
  html += ".unit { font-size: 0.4em; color: #888; margin-left: 5px; }";
  html += ".label { color: #aaa; text-transform: uppercase; letter-spacing: 1px; font-size: 0.8em; }";

  html += ".alarm-box { display:none; background: #e94560; color: white; padding: 15px; margin-bottom: 20px; border-radius: 8px; font-weight:bold; box-shadow: 0 0 15px #e94560; animation: pulse 1s infinite; }";
  html += "@keyframes pulse { 0% { opacity: 1; transform: scale(1); } 50% { opacity: 0.8; transform: scale(0.98); } 100% { opacity: 1; transform: scale(1); } }";

  html += "canvas { background: #16213e; border-radius: 12px; margin-top: 30px; width: 100%; max-width: 700px; height: 300px; box-shadow: 0 10px 25px rgba(0,0,0,0.4); }";
  html += "</style></head><body>";

  // HTML BODY
  html += "<div id='alarmMsg' class='alarm-box'>!!! CRITICAL TEMP ALARM !!!</div>";
  html += "<h2>IOT WEATHER STATION</h2>";
  
  html += "<div class='container'>";
  html += "<div class='card' style='border-left-color: #ff2e63;'><span class='label'>Temperature</span><div class='val' style='color:#ff2e63' id='t'>--<span class='unit'>&deg;C</span></div></div>";
  html += "<div class='card' style='border-left-color: #08d9d6;'><span class='label'>Humidity</span><div class='val' style='color:#08d9d6' id='h'>--<span class='unit'>%</span></div></div>";
  html += "<div class='card' style='border-left-color: #eaeaea;'><span class='label'>Pressure</span><div class='val' style='color:#eaeaea' id='p'>--<span class='unit'>hPa</span></div></div>";
  html += "</div>";

  html += "<canvas id='chart' width='700' height='300'></canvas>";

  // JAVASCRIPT LOGIC
  html += "<script>";
  html += "const cvs = document.getElementById('chart');";
  html += "const ctx = cvs.getContext('2d');";
  html += "let data = [];";
  html += "const maxLen = 60;"; 

  html += "async function loop() {";
  html += "  try {";
  html += "    const r = await fetch('/data');";
  html += "    const j = await r.json();";
  
  html += "    document.getElementById('t').innerText = j.temperature.toFixed(1);";
  html += "    document.getElementById('h').innerText = j.humidity.toFixed(1);";
  html += "    document.getElementById('p').innerText = j.pressure.toFixed(0);";
  
  html += "    const alarm = document.getElementById('alarmMsg');";
  html += "    if(j.alarm) { ";
  html += "      alarm.style.display='block'; document.body.style.backgroundColor='#2b1016';";
  html += "    } else { ";
  html += "      alarm.style.display='none'; document.body.style.backgroundColor='#1a1a2e';";
  html += "    }";

  html += "    data.push(j.temperature);";
  html += "    if (data.length > maxLen) data.shift();";
  html += "    draw();";
  html += "  } catch(e) {}";
  html += "}";

  // Chart Drawing Function (Canvas API)
  html += "function draw() {";
  html += "  const w = cvs.width; const h = cvs.height;";
  html += "  ctx.clearRect(0,0,w,h);";
  
  // Grid
  html += "  ctx.strokeStyle = 'rgba(255,255,255,0.05)'; ctx.lineWidth = 1;";
  html += "  for(let i=1; i<5; i++) {";
  html += "    let y = (h/5)*i; ctx.beginPath(); ctx.moveTo(0,y); ctx.lineTo(w,y); ctx.stroke();";
  html += "  }";

  // Scaling
  html += "  let min = Math.min(...data) - 1; let max = Math.max(...data) + 1;";
  html += "  let range = max - min; if(range<1) range=1;";
  html += "  const step = w / (maxLen - 1);";

  // Gradient Fill
  html += "  let grad = ctx.createLinearGradient(0,0,0,h);";
  html += "  grad.addColorStop(0, 'rgba(255, 46, 99, 0.4)');";
  html += "  grad.addColorStop(1, 'rgba(255, 46, 99, 0)');"; 

  // Fill Path
  html += "  ctx.beginPath();";
  html += "  data.forEach((v, i) => {";
  html += "    let x = i * step;";
  html += "    let y = h - ((v - min) / range * (h - 40)) - 20;";
  html += "    if(i===0) ctx.moveTo(x, y); else ctx.lineTo(x, y);";
  html += "  });";
  html += "  ctx.lineTo((data.length-1)*step, h);"; 
  html += "  ctx.lineTo(0, h);"; 
  html += "  ctx.fillStyle = grad; ctx.fill();";

  // Stroke Line
  html += "  ctx.beginPath();";
  html += "  ctx.strokeStyle = '#ff2e63'; ctx.lineWidth = 3;";
  html += "  ctx.shadowBlur = 10; ctx.shadowColor = '#ff2e63';"; 
  
  html += "  data.forEach((v, i) => {";
  html += "    let x = i * step;";
  html += "    let y = h - ((v - min) / range * (h - 40)) - 20;";
  html += "    if(i===0) ctx.moveTo(x, y); else ctx.lineTo(x, y);";
  html += "  });";
  html += "  ctx.stroke(); ctx.shadowBlur = 0;"; 

  // Points
  html += "  ctx.fillStyle = '#fff';";
  html += "  data.forEach((v, i) => {";
  html += "    let x = i * step;";
  html += "    let y = h - ((v - min) / range * (h - 40)) - 20;";
  html += "    ctx.beginPath(); ctx.arc(x, y, 3, 0, Math.PI*2); ctx.fill();";
  html += "  });";

  // Min/Max Labels
  html += "  ctx.fillStyle = '#aaa'; ctx.font = '14px Arial';";
  html += "  ctx.fillText('Max: ' + max.toFixed(1), 10, 20);";
  html += "  ctx.fillText('Min: ' + min.toFixed(1), 10, h - 10);";
  html += "}";

  html += "setInterval(loop, 1000); loop();";
  html += "</script></body></html>";

  client.print(html);
}