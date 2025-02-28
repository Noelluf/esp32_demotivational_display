#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeMonoBoldOblique12pt7b.h>  

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
const char* ssid = "Masthof";
const char* password = "12A.Uw3dz.7s";

// JSON file URL (GitHub Pages)
const char* jsonURL = "https://Noelluf.github.io/esp32_demotivational_display/test_quotes.json";

// NTP Time Server
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600; // Adjust if needed
const int daylightOffset_sec = 3600; // Adjust for daylight saving

//Initialise SPI and Display
GxIO_Class io(SPI,  EPD_CS, EPD_DC,  EPD_RSET);
GxEPD_Class display(io, EPD_RSET, EPD_BUSY);

#define BATTERY_ADC_PIN 35
#define MAX_BATTERY_VOLTAGE 4.2
#define MIN_BATTERY_VOLTAGE 3.4  
#define ADC_SAMPLES 10  
#define CALIBRATION_FACTOR 1.1

float getBatteryVoltage() {//Multisampling
    int sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        sum += analogRead(BATTERY_ADC_PIN);
        delay(5); 
    }
    int raw_average = sum / ADC_SAMPLES;
    float voltage = (raw_average / 4095.0) * 2 * 3.3; // ADC reads 0-3.3V, voltage divider scales it
    return voltage * CALIBRATION_FACTOR; // Apply calibration factor
}

int calculateBatteryPercentage(float voltage) {
    if (voltage > MAX_BATTERY_VOLTAGE) voltage = MAX_BATTERY_VOLTAGE;
    if (voltage < MIN_BATTERY_VOLTAGE) voltage = MIN_BATTERY_VOLTAGE;

    return (int)(((voltage - MIN_BATTERY_VOLTAGE) / (MAX_BATTERY_VOLTAGE - MIN_BATTERY_VOLTAGE)) * 100);
}

// Array of Quotes (Modify as Needed)
const char* quotes[] = {
    "Das Leben ist eine lange Kette verpasster Chancen.",
    "Jeder Tag ist eine neue Möglichkeit, nichts zu ändern.",
    "Der frühe Vogel kann mich mal.",
    "Arbeit macht das Leben nicht leichter, nur kürzer.",
    "Es ist nie zu spät, um aufzugeben."
};

// Number of Quotes
const int numQuotes = sizeof(quotes) / sizeof(quotes[0]);
int currentQuoteIndex = 0; // Track which quote to show



void setup() {
    Serial.begin(115200);
    
    // Connect to WiFi
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected!");

    // Get the current date
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return;
    }

    char dateStr[11]; // Format: YYYY-MM-DD
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo);
    Serial.print("Today's Date: ");
    Serial.println(dateStr);

    // Fetch and display the quote in serial
    fetchAndDisplayQuote(dateStr);

    //start display
    SPI.begin(EPD_SCLK, EPD_MISO, EPD_MOSI);
    display.init();
    display_tester(0);
}



void fetchAndDisplayQuote(const char* today) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(jsonURL);
        int httpCode = http.GET();

        if (httpCode == 200) { // HTTP OK
            String payload = http.getString();
            Serial.println("JSON Fetched Successfully.");

            // Parse JSON
            DynamicJsonDocument doc(4096);
            DeserializationError error = deserializeJson(doc, payload);

            if (error) {
                Serial.print("JSON Parse Error: ");
                Serial.println(error.f_str());
                return;
            }

            // Extract quote for today
            const char* quote = doc[today];
            if (quote) {
                Serial.println("Today's Quote:");
                Serial.println(quote);
            } else {
                Serial.println("No quote found for today.");
            }
        } else {
            Serial.print("HTTP Error Code: ");
            Serial.println(httpCode);
        }
        http.end();
    } else {
        Serial.println("WiFi not connected.");
    }
}

void display_tester(int index) {
    display.setRotation(1); // Adjust if text is upside-down
    display.fillScreen(GxEPD_WHITE); // Clear display
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMonoBoldOblique12pt7b); // Set font
    display.setCursor(10, 50); // Position (X, Y)
    display.println(quotes[index]); // Print the quote
    display.update(); // Refresh the display

    Serial.print("Displayed Quote: ");
    Serial.println(quotes[index]);
}

void loop() {
    float batteryVoltage = getBatteryVoltage();
    int batteryPercentage = calculateBatteryPercentage(batteryVoltage);
    
    Serial.print("Battery Voltage: ");
    Serial.print(batteryVoltage);
    Serial.print("V | Estimated Charge: ");
    Serial.print(batteryPercentage);
    Serial.println("%");
     // Show the next quote
    // Update display with the next quote
    currentQuoteIndex = (currentQuoteIndex + 1) % numQuotes;
    
    display_tester(currentQuoteIndex);
    delay(10000);  // Check every 5 seconds
}