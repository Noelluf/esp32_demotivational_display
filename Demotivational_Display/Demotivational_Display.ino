#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <driver/adc.h>
#include <Fonts/FreeMonoBoldOblique12pt7b.h>
#include <Fonts/FreeMonoBoldOblique9pt7b.h>
//Custom GFX Font
#include "Komika_display_kaps12pt7b.h"
#include "Komika_display_kaps10pt7b.h"
#include "icon_bitmaps.h"

//Board specific
#define LILYGO_T5_V213
#include <boards.h>
#include <GxEPD.h>
//#include <SD.h>
//#include <FS.h>
#include <GxDEPG0213BN/GxDEPG0213BN.h>
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <GxIO/GxIO.h>
#include <SPI.h>

// WiFi Credentials
const char* ssid = "---------";
const char* password = "----------";

// JSON file URL (GitHub Pages)
const char* jsonURL = "https://Noelluf.github.io/esp32_demotivational_display/my_own_quotes.json";

// NTP Time Server
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

//Initialise SPI and Display
GxIO_Class io(SPI, EPD_CS, EPD_DC, EPD_RSET);
GxEPD_Class display(io, EPD_RSET, EPD_BUSY);

#define BATTERY_ADC_PIN 35
#define BATTERY_ADC_CHANNEL ADC1_CHANNEL_7
#define MAX_BATTERY_VOLTAGE 4.2
#define MIN_BATTERY_VOLTAGE 3.35
#define BATTERY_HIGH 4.0
#define BATTERY_MEDIUM 3.85
#define BATTERY_LOW 3.7
#define BATTERY_CRITICAL 3.5
#define ADC_SAMPLES 10
#define CALIBRATION_FACTOR 1.4
#define UPDATE_HOUR 8

float getBatteryVoltage() {
  int sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sum += adc1_get_raw(BATTERY_ADC_CHANNEL);  // Read ADC directly
    delay(5);
  }
  int raw_average = sum / ADC_SAMPLES;

  // Convert ADC value to actual voltage at IO35 (0 - 2.45V range)
  float adcVoltage = (raw_average / 4095.0) * 2.45;

  //Scale up to actual battery voltage (due to voltage divider)
  return adcVoltage * 2 * CALIBRATION_FACTOR;
}

int calculateBatteryPercentage(float voltage) {
  if (voltage > MAX_BATTERY_VOLTAGE) voltage = MAX_BATTERY_VOLTAGE;
  if (voltage < MIN_BATTERY_VOLTAGE) voltage = MIN_BATTERY_VOLTAGE;
  float percentage = ((voltage - MIN_BATTERY_VOLTAGE) / (MAX_BATTERY_VOLTAGE - MIN_BATTERY_VOLTAGE)) * 100;

  return round(percentage);
}

void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");

  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED && retryCount < 100) {
    delay(1000);
    Serial.print(".");
    retryCount++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
  } else {
    Serial.println("\nWiFi connection failed. Sleeping for 60 minutes...");
    esp_sleep_enable_timer_wakeup(60 * 60 * 1000000ULL);  // 60 minutes in microseconds
    esp_deep_sleep_start();
  }
}

String getCurrentDate() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;

  int maxRetries = 5;      // Number of retry attempts
  int retryDelay = 30000;  // 30 seconds per retry

  for (int i = 0; i < maxRetries; i++) {
    if (getLocalTime(&timeinfo)) {
      char dateStr[11];
      strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo);
      return String(dateStr);  //Return valid date if successful
    }
    Serial.println("Failed to obtain time. Retrying...");
    delay(retryDelay);
  }

  // If all retries fail, deep sleep for 10 minutes
  Serial.println("NTP sync failed after multiple attempts. Sleeping for 10 minutes...");
  display.powerDown();
  esp_sleep_enable_timer_wakeup(10 * 60 * 1000000ULL);  // 10 minutes in microseconds
  esp_deep_sleep_start();
}

int getCurrentHour() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to get time.");
    delay(30000);

    if (!getLocalTime(&timeinfo)) {
      Serial.println("Failed again.");
      return -1;  // Return an invalid hour
    }
  }
  return timeinfo.tm_hour;
}

void battery_protection() {
  float batteryVoltage = getBatteryVoltage();

  if (batteryVoltage <= MIN_BATTERY_VOLTAGE || batteryVoltage > MAX_BATTERY_VOLTAGE) {
    SPI.begin(EPD_SCLK, EPD_MISO, EPD_MOSI);
    display.init();
    display.setRotation(1);  // Adjust if text is upside-down
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    int displayWidth = display.width();    // Get display width
    int displayHeight = display.height();  // Get display height

    int bitmapWidth = 48;
    int bitmapHeight = 48;

    // Calculate centered position
    int xPos = (displayWidth - bitmapWidth) / 2;
    int yPos = (displayHeight - bitmapHeight) / 2;

    display.drawBitmap(xPos, yPos, Battery_Warning, bitmapWidth, bitmapHeight, GxEPD_BLACK);
    display.update();
    display.powerDown();
    esp_sleep_enable_timer_wakeup(6 * 60 * 60 * 1000000ULL);  // 6 hours
    esp_deep_sleep_start();
  }
  return;
}

void battery_monitor() {
  float batteryVoltage = getBatteryVoltage();
  const uint8_t* batteryIcon;  // Pointer to selected icon

  // Determine which battery icon to use
  if (batteryVoltage >= BATTERY_HIGH) {
    batteryIcon = Battery_80;
  } else if (batteryVoltage >= BATTERY_MEDIUM) {
    batteryIcon = Battery_60;
  } else if (batteryVoltage >= BATTERY_LOW) {
    batteryIcon = Battery_40;
  } else {
    batteryIcon = Battery_20;
  }

  // Display the selected battery icon
  display.drawBitmap(23, 102, batteryIcon, 20, 20, GxEPD_BLACK);
}

void displayWrappedText(int x, int y, int maxWidth, const char* text) {
  display.setFont(&Komika_display_kaps10pt7b);

  const int lineHeight = 16;
  const int maxLines = 6;
  String lines[maxLines];
  int lineCount = 0;

  String line = "";
  while (*text != '\0') {
    String word = "";

    // Wort aufbauen
    while (*text != '\0' && *text != ' ') {
      word += *text;
      text++;
    }
    if (*text == ' ') text++;  // Leerzeichen überspringen

    String testLine = line + (line.length() > 0 ? " " : "") + word;

    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(testLine.c_str(), 0, 0, &x1, &y1, &w, &h);

    if (w > maxWidth - 15 && line.length() > 0) {
      if (lineCount < maxLines - 1) {
        lines[lineCount++] = line;
        line = word;
      } else {
        // Letzte erlaubte Zeile -> "..." anhängen
        line += ". . .";

        // Kürzen falls zu lang
        while (true) {
          display.getTextBounds(line.c_str(), 0, 0, &x1, &y1, &w, &h);
          if (w <= maxWidth - 15 || line.length() == 3) break;  // nur noch "..." übrig?
          line.remove(line.length() - 4, 1);                   // Letztes Zeichen vor "..." entfernen
        }

        lines[lineCount++] = line;
        break;
      }
    } else {
      if (line.length() > 0) line += " ";
      line += word;
    }
  }

  // Letzte Zeile speichern
  if (line.length() > 0 && lineCount < maxLines) {
    lines[lineCount++] = line;
  }

  // Vertikal zentriert zwischen 0 und 100
  const int usableHeight = 100;
  const int verticalOffset = 11;  // Start des nutzbaren Bereichs

  int totalTextHeight = lineCount * lineHeight;
  int topY = verticalOffset + (usableHeight - totalTextHeight) / 2;


  // Zeilen zentriert ausgeben
  for (int i = 0; i < lineCount; i++) {
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(lines[i].c_str(), 0, 0, &x1, &y1, &w, &h);
    int centeredX = x + (maxWidth - w) / 2;
    display.setCursor(centeredX, topY + i * lineHeight);
    display.print(lines[i]);
  }
}

//Function to Display the Daily Quote and Icons on the Display
void display_current_quote(const char* today) {
  int16_t x1, y1;
  uint16_t dateWidth, dateHeight;
  int datePosition;

  bool wifiConnected;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected.");
    wifiConnected = false;
  } else {
    wifiConnected = true;
  }

  HTTPClient http;
  http.begin(jsonURL);
  int httpCode = http.GET();

  if (httpCode != 200) {
    Serial.print("HTTP Error Code: ");
    Serial.println(httpCode);
    Serial.println("Sleeping for 5 minutes...");
    display.powerDown();
    esp_sleep_enable_timer_wakeup(5 * 60 * 1000000ULL);  // 5 minutes in microseconds
    esp_deep_sleep_start();
  }


  if (httpCode == 200) {  //HTTP OK
    String payload = http.getString();
    Serial.println("JSON Fetched Successfully.");

    // Parse JSON
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("JSON Parse Error: ");
      Serial.println(error.f_str());
      display.powerDown();
      esp_sleep_enable_timer_wakeup(5 * 60 * 1000000ULL);
      esp_deep_sleep_start();
    }

    //Extract Quote
    const char* quote = nullptr;

    if (doc.containsKey(today) && doc[today].is<const char*>()) {//damit kein leerer Pointer zurückgegeben wird
      quote = doc[today].as<const char*>();
    }

    // Fallback, wenn kein Zitat vorhanden
    if (!quote || strlen(quote) == 0) {
      quote = "Keine Motivation in der Datenbank gefunden.";
    }

    Serial.println("Today's Quote:");
    Serial.println(quote);

    float batteryVoltage = getBatteryVoltage();
    int batteryPercentage = calculateBatteryPercentage(batteryVoltage);

    //set display parameters
    display.setRotation(1); 
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    displayWrappedText(2, 12, display.width() - 4, quote);

    /*
    display.setCursor(47, 118);  // Adjust position as needed
    display.setFont(&FreeMonoBoldOblique9pt7b);
    display.println(batteryVoltage);
    display.setCursor(95, 118);  // Adjust position as needed
    display.setFont(&FreeMonoBoldOblique9pt7b);
    display.println(batteryPercentage);
    */

    //draw separation line
    display.drawLine(0, 100, 250, 100, GxEPD_BLACK);

    //show wifi status and battery icon
    if (wifiConnected) {
      display.drawBitmap(1, 102, WiFi_Icon, 20, 20, GxEPD_BLACK);
    } else {
      display.drawBitmap(1, 102, WiFi_Icon_Off, 20, 20, GxEPD_BLACK);
    }
    battery_monitor();

    //Show current date in bottom right corner
    display.getTextBounds(today, 0, 0, &x1, &y1, &dateWidth, &dateHeight);
    datePosition = display.width() - dateWidth - 8;  // Subtract margin (1px)
    display.setCursor(datePosition, 120);            // Set new right-aligned position
    display.setFont(&FreeMonoBoldOblique9pt7b);
    display.print(today);

    display.update();
  } else {
    Serial.print("HTTP Error Code: ");
    Serial.println(httpCode);
  }
  http.end();
}

void deepSleepUntilNextUpdate() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  int currentHour = timeinfo.tm_hour;
  int sleepSeconds;

  if (currentHour < UPDATE_HOUR) {
    //If before 08:00, sleep until 08:00 today
    sleepSeconds = (UPDATE_HOUR - currentHour) * 3600;
    Serial.print("Too early, sleeping until 10:00 in ");
    Serial.print(sleepSeconds / 3600);
    Serial.println(" hours.");
  } else {
    //If at or after 08:00, update quote first, then sleep until 08:00 tomorrow
    sleepSeconds = ((24 - currentHour) + UPDATE_HOUR) * 3600;
    Serial.print("Already updated, sleeping until 08:00 tomorrow in ");
    Serial.print(sleepSeconds / 3600);
    Serial.println(" hours.");
  }

  Serial.print("Going to deep sleep for ");
  Serial.print(sleepSeconds / 3600);
  Serial.println(" hours.");

  esp_sleep_enable_timer_wakeup(sleepSeconds * 1000000ULL);
  esp_deep_sleep_start();
}


void setup() {
  delay(1000);
  Serial.begin(115200);


  adc1_config_width(ADC_WIDTH_BIT_12);  // 12-bit resolution (0 - 4095)
  adc1_config_channel_atten(BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_12);
  delay(1000);
  battery_protection();
  connectToWiFi();

  delay(2000);
  // Get the current date
  String today = getCurrentDate();
  Serial.print("Today's Date: ");
  Serial.println(today);

  delay(1000);
  int currentHour = getCurrentHour();
  Serial.print("Current Hour: ");
  Serial.println(currentHour);

  delay(1000);
  SPI.begin(EPD_SCLK, EPD_MISO, EPD_MOSI);
  display.init();

  if (currentHour >= UPDATE_HOUR) {
    display_current_quote(today.c_str());
  }
  display.powerDown();
  deepSleepUntilNextUpdate();
}

void loop() {
}