/*
 * MatrixPortal S3 - PRODUCTION VERSION
 * WiFi Captive Portal Setup (Customer-Friendly!)
 * Clock, Weather & NFL Scores
 */

#include <Adafruit_Protomatter.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Preferences.h>
#include <Update.h>
#include <HTTPUpdate.h>

// ===== Setup Mode Settings =====
const char* AP_SSID = "MatrixClock-Setup";
const char* AP_PASSWORD = "";
WebServer server(80);
DNSServer dnsServer;
bool setupMode = false;

// ===== OTA Update Settings =====
const char* FIRMWARE_VERSION = "1.3.0";  // Increment this with each release
const char* GITHUB_FIRMWARE_URL = "https://github.com/markstamp/MatrixPortal-NFL-Clock/releases/latest/download/firmware.bin";
unsigned long lastUpdateCheck = 0;
const unsigned long updateCheckInterval = 21600000; // Check every 6 hours (6 * 60 * 60 * 1000)
bool updateInProgress = false;

// Optional: Manual update button
const int UPDATE_BUTTON_PIN = 3; // GPIO 3 for manual update trigger
// ===== Setup Button Pin =====
const int SETUP_BUTTON_PIN = 0; // Change this to your button pin

// ===== Saved Settings =====
String saved_ssid = "";
String saved_password = "";
String saved_apiKey = "";
String saved_city = "New York";
String saved_country = "US";
int saved_timezone = -5;

// ===== MatrixPortal S3 Pins =====
uint8_t rgbPins[]  = {42, 41, 40, 38, 39, 37};
uint8_t addrPins[] = {45, 36, 48, 35, 21};
uint8_t clockPin   = 2, latchPin = 47, oePin = 14;

Adafruit_Protomatter matrix(64, 4, 1, rgbPins, 4, addrPins, clockPin, latchPin, oePin, false);

// ===== Display Modes =====
enum DisplayMode { CLOCK_WEATHER, NFL_SCORES };
DisplayMode currentMode = CLOCK_WEATHER;
unsigned long modeChangeTime = 0;
const unsigned long MODE_DURATION = 15000;

// ===== Data Variables =====
String weatherDescription = "";
float temperature = 0.0;
unsigned long lastWeatherUpdate = 0;
const unsigned long weatherUpdateInterval = 600000;
String lastWeatherDescription = "";  // ADD THIS LINE
float lastTemperature = 0.0;         // ADD THIS LINE
bool weatherChanged = false;          // ADD THIS LINE

struct NFLGame {
  String awayTeam, homeTeam, status;
  int awayScore, homeScore;
  bool isLive;
  bool isFinal;
  bool isUpcoming;
  String gameTime; // For upcoming games
};
NFLGame games[16];
int gameCount = 0, currentGameIndex = 0;
unsigned long lastNFLUpdate = 0;
const unsigned long nflUpdateInterval = 60000;

// ===== Colors =====
uint16_t COLOR_CYAN, COLOR_YELLOW, COLOR_WHITE, COLOR_GREEN, COLOR_RED, COLOR_BLUE, COLOR_ORANGE, COLOR_PURPLE;

// ===== HTML Setup Page =====
const char SETUP_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MatrixClock Setup</title>
  <style>
    body { font-family: Arial; max-width: 500px; margin: 50px auto; padding: 20px; background: #1a1a1a; color: #fff; }
    h1 { color: #00ffff; text-align: center; }
    input, select { width: 100%; padding: 12px; margin: 8px 0; box-sizing: border-box; background: #333; color: #fff; border: 2px solid #00ffff; border-radius: 5px; font-size: 16px; }
    button { width: 100%; padding: 15px; background: #00ffff; color: #000; border: none; border-radius: 5px; font-size: 18px; font-weight: bold; cursor: pointer; margin-top: 20px; }
    button:hover { background: #00cccc; }
    label { display: block; margin-top: 15px; color: #00ffff; font-weight: bold; }
    .info { background: #2a2a2a; padding: 15px; border-radius: 5px; margin: 15px 0; border-left: 4px solid #00ffff; }
  </style>
</head>
<body>
  <h1>🏈 MatrixClock Setup 🏈</h1>
  
  <div class="info">
    <p><strong>Welcome!</strong> Configure your MatrixClock display!</p>
  </div>
  
  <form action="/save" method="POST">
    <label>WiFi Network Name</label>
    <input type="text" name="ssid" placeholder="Your WiFi Name" required>
    
    <label>WiFi Password</label>
    <input type="password" name="password" placeholder="WiFi Password" required>
    
    <label>City</label>
    <input type="text" name="city" placeholder="e.g., New York" required>
    
    <label>Country Code</label>
    <select name="country">
      <option value="US">United States</option>
      <option value="CA">Canada</option>
      <option value="GB">United Kingdom</option>
    </select>
    
    <label>Time Zone</label>
    <select name="timezone">
      <option value="-8">Pacific (PST)</option>
      <option value="-7">Mountain (MST)</option>
      <option value="-6">Central (CST)</option>
      <option value="-5" selected>Eastern (EST)</option>
    </select>
    
    <label>Weather API Key (Optional)</label>
    <input type="text" name="apikey" placeholder="OpenWeatherMap API Key">
    
    <button type="submit">💾 Save & Connect</button>
  </form>
</body>
</html>
)rawliteral";

const char SUCCESS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Setup Complete</title>
  <style>
    body { font-family: Arial; max-width: 500px; margin: 50px auto; padding: 20px; background: #1a1a1a; color: #fff; text-align: center; }
    h1 { color: #00ff00; }
    .success { background: #004400; padding: 30px; border-radius: 10px; border: 3px solid #00ff00; }
  </style>
</head>
<body>
  <div class="success">
    <h1>✅ Setup Complete!</h1>
    <p style="font-size: 18px;">Your MatrixClock is restarting...</p>
  </div>
</body>
</html>
)rawliteral";

// ===== FUNCTION DECLARATIONS =====
void enterSetupMode();
void handleRoot();
void handleSave();
bool connectToWiFi();
void showWiFiError();
void displayClockAndWeather();
void displayNFLScores();
void drawWeatherIcon();
void getWeather();
void getNFLScores();
void sortGamesByPriority();

// ===== OTA UPDATE FUNCTIONS =====

void checkForOTAUpdate() {
  if (updateInProgress) return;
  
  Serial.println("Checking for firmware updates...");
  
  HTTPClient http;
  http.begin("https://api.github.com/repos/markstamp/MatrixPortal-NFL-Clock/releases/latest");
  http.addHeader("User-Agent", "MatrixPortal-NFL-Clock");
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, http.getString());
    
    if (!error) {
      String latestVersion = doc["tag_name"].as<String>();
      latestVersion.replace("v", ""); // Remove 'v' prefix if present
      
      Serial.print("Current version: ");
      Serial.println(FIRMWARE_VERSION);
      Serial.print("Latest version: ");
      Serial.println(latestVersion);
      
      if (latestVersion != FIRMWARE_VERSION && latestVersion.length() > 0) {
        Serial.println("New version available! Starting update...");
        performOTAUpdate();
      } else {
        Serial.println("Firmware is up to date.");
      }
    } else {
      Serial.println("Failed to parse release info");
    }
  } else {
    Serial.print("Failed to check for updates. HTTP code: ");
    Serial.println(httpCode);
  }
  
  http.end();
  lastUpdateCheck = millis();
}

void performOTAUpdate() {
  updateInProgress = true;
  
  // Show update message on display
  matrix.fillScreen(0);
  matrix.setTextSize(1);
  matrix.setTextColor(COLOR_YELLOW);
  matrix.setCursor(2, 2);
  matrix.print("UPDATE");
  matrix.setCursor(2, 12);
  matrix.print("Loading");
  matrix.setCursor(2, 22);
  matrix.print("Please");
  matrix.setCursor(2, 32);
  matrix.print("Wait...");
  matrix.show();
  
  Serial.println("Starting OTA update from: " + String(GITHUB_FIRMWARE_URL));
  
  WiFiClient client;
  httpUpdate.setLedPin(LED_BUILTIN, LOW);
  
  // Register callback for progress
  httpUpdate.onProgress([](int current, int total) {
    Serial.printf("Progress: %d%%\n", (current * 100) / total);
    
    // Update progress bar on display
    int barWidth = (current * 60) / total;
    matrix.fillRect(2, 28, barWidth, 3, COLOR_GREEN);
    matrix.show();
  });
  
  t_httpUpdate_return ret = httpUpdate.update(client, GITHUB_FIRMWARE_URL);
  
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("Update failed. Error (%d): %s\n", 
                    httpUpdate.getLastError(), 
                    httpUpdate.getLastErrorString().c_str());
      
      matrix.fillScreen(0);
      matrix.setTextColor(COLOR_RED);
      matrix.setCursor(2, 12);
      matrix.print("Update");
      matrix.setCursor(2, 22);
      matrix.print("Failed!");
      matrix.show();
      delay(3000);
      
      updateInProgress = false;
      break;
      
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("No update available");
      updateInProgress = false;
      break;
      
    case HTTP_UPDATE_OK:
      Serial.println("Update successful! Rebooting...");
      matrix.fillScreen(0);
      matrix.setTextColor(COLOR_GREEN);
      matrix.setCursor(2, 12);
      matrix.print("Update");
      matrix.setCursor(2, 22);
      matrix.print("Success!");
      matrix.show();
      delay(2000);
      ESP.restart();
      break;
  }
}

void displayFirmwareVersion() {
  // Show version on display during startup
  matrix.fillScreen(0);
  matrix.setTextSize(1);
  matrix.setTextColor(COLOR_CYAN);
  matrix.setCursor(2, 2);
  matrix.print("Version");
  matrix.setTextColor(COLOR_WHITE);
  matrix.setCursor(2, 12);
  matrix.print(FIRMWARE_VERSION);
  matrix.show();
  delay(2000);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Check if setup button is pressed on boot to force setup mode
  pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);
  if (digitalRead(SETUP_BUTTON_PIN) == LOW) {
    Serial.println("Setup button pressed - clearing all settings!");
    preferences.begin("settings", false);
    preferences.clear();
    preferences.end();
    
    if(matrix.begin() == PROTOMATTER_OK) {
      matrix.fillScreen(0);
      matrix.setTextSize(1);
      matrix.setTextColor(matrix.color565(255, 0, 0));
      matrix.setCursor(2, 2);
      matrix.print("Settings");
      matrix.setCursor(2, 12);
      matrix.print("Cleared!");
      matrix.setCursor(2, 22);
      matrix.print("Reboot...");
      matrix.show();
    }
    
    delay(2000);
    ESP.restart();
  }
  
  // ADD THIS: Setup manual update button
  pinMode(UPDATE_BUTTON_PIN, INPUT_PULLUP);
  
  if(matrix.begin() != PROTOMATTER_OK) {
    Serial.println("Matrix init FAILED!");
    while(1);
  }
  
  COLOR_CYAN = matrix.color565(0, 255, 255);
  COLOR_YELLOW = matrix.color565(255, 255, 0);
  COLOR_WHITE = matrix.color565(255, 255, 255);
  COLOR_GREEN = matrix.color565(0, 255, 0);
  COLOR_RED = matrix.color565(255, 0, 0);
  COLOR_BLUE = matrix.color565(0, 100, 255);
  COLOR_ORANGE = matrix.color565(255, 128, 0);
  COLOR_PURPLE = matrix.color565(200, 0, 255);
  
  preferences.begin("settings", false);
  saved_ssid = preferences.getString("ssid", "");
  saved_password = preferences.getString("password", "");
  saved_apiKey = preferences.getString("apikey", "");
  saved_city = preferences.getString("city", "New York");
  saved_country = preferences.getString("country", "US");
  saved_timezone = preferences.getInt("timezone", -5);
  
  if (saved_ssid.length() == 0) {
    enterSetupMode();
  } else {
    if (!connectToWiFi()) {
      enterSetupMode();
    } else {
      // ADD THIS: Show version after WiFi connects
      displayFirmwareVersion();
      
      configTime(saved_timezone * 3600, 0, "pool.ntp.org");
      delay(2000);
      getWeather();
      getNFLScores();
      
      // ADD THIS: Check for updates on startup
      Serial.println("Checking for firmware updates on startup...");
      checkForOTAUpdate();
    }
  }
}

void loop() {
  if (setupMode) {
    dnsServer.processNextRequest();
    server.handleClient();
    
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 1000) {
      matrix.fillScreen(0);
      matrix.setTextSize(1);
      matrix.setTextColor(COLOR_PURPLE);
      matrix.setCursor(2, 2);
      matrix.print("SETUP");
      matrix.setTextColor(COLOR_WHITE);
      matrix.setCursor(2, 12);
      matrix.print("Connect:");
      matrix.setTextColor(COLOR_CYAN);
      matrix.setCursor(2, 22);
      matrix.print(AP_SSID);
      matrix.show();
      lastBlink = millis();
    }
    return;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    showWiFiError();
    delay(5000);
    return;
  }
    // ADD THIS: Periodic update check (every 6 hours)
  if (millis() - lastUpdateCheck > updateCheckInterval) {
    checkForOTAUpdate();
  }
  
  if (millis() - lastWeatherUpdate > weatherUpdateInterval) {
    getWeather();
  }
  if (millis() - lastWeatherUpdate > weatherUpdateInterval) {
    getWeather();
  }
  
  // Update NFL scores - more frequently during game days
  unsigned long nflCheckInterval = nflUpdateInterval;
  
  // Count game types and check for live games
  bool hasLiveGames = false;
  int liveGameCount = 0;
  int upcomingGameCount = 0;
  int finalGameCount = 0;
  
  for (int i = 0; i < gameCount; i++) {
    if (games[i].isLive) {
      hasLiveGames = true;
      liveGameCount++;
    } else if (games[i].isUpcoming) {
      upcomingGameCount++;
    } else if (games[i].isFinal) {
      finalGameCount++;
    }
  }
  
  if (hasLiveGames) {
    nflCheckInterval = 30000; // 30 seconds for live games
  }
  
  if (millis() - lastNFLUpdate > nflCheckInterval) {
    getNFLScores();
  }
  
  // HEAVY FOCUS ON LIVE GAMES
  unsigned long currentModeDuration = MODE_DURATION;
  
  if (hasLiveGames) {
    if (currentMode == NFL_SCORES) {
      // Calculate cycle time with heavy live game focus
      // Live games: 55 sec each (most time)
      // Next upcoming: 6 sec (only show 1)
      // Recent finals: 5 sec each (only show 2)
      
      int upcomingToShow = min(upcomingGameCount, 1);  // ONLY 1 upcoming
      int finalsToShow = min(finalGameCount, 2);        // ONLY 2 finals
      
      unsigned long cycleTime = (liveGameCount * 55000) +   // 55 sec per live game
                                (upcomingToShow * 6000) +    // 6 sec for 1 upcoming
                                (finalsToShow * 5000);       // 5 sec per final (max 2)
      
      currentModeDuration = cycleTime;
      
      Serial.print("NFL cycle time: ");
      Serial.print(cycleTime / 1000);
      Serial.print(" seconds (");
      Serial.print(liveGameCount);
      Serial.print(" live [55s each], ");
      Serial.print(upcomingToShow);
      Serial.print(" upcoming [6s], ");
      Serial.print(finalsToShow);
      Serial.println(" final [5s each])");
      
    } else {
      // CLOCK/WEATHER mode during live games
      if (weatherChanged) {
        currentModeDuration = 5000; // 5 seconds if weather changed
      } else {
        currentModeDuration = 0; // Skip entirely if no change
      }
    }
  } else {
    // No live games - normal rotation (show everything)
    currentModeDuration = 15000; // 15 seconds each
  }
  
  if (millis() - modeChangeTime > currentModeDuration) {
    // Switch modes
    if (currentMode == CLOCK_WEATHER) {
      currentMode = NFL_SCORES;
      currentGameIndex = 0;
      weatherChanged = false;
      Serial.println("=== Switching to NFL Scores ===");
    } else {
      // Only switch back to clock/weather if weather changed OR no live games
      if (weatherChanged || !hasLiveGames) {
        currentMode = CLOCK_WEATHER;
        Serial.println("=== Switching to Clock/Weather ===");
      } else {
        // Stay on NFL scores, restart the cycle
        currentGameIndex = 0;
        Serial.println("=== Restarting NFL cycle ===");
      }
    }
    
    modeChangeTime = millis();
  }
  
  if (currentMode == CLOCK_WEATHER) {
    displayClockAndWeather();
  } else {
    displayNFLScores();
  }
  
  delay(1000);
}

void enterSetupMode() {
  setupMode = true;
  Serial.println("Entering Setup Mode");
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  
  Serial.print("Setup WiFi: ");
  Serial.println(AP_SSID);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  
  dnsServer.start(53, "*", WiFi.softAPIP());
  
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleRoot);
  
  server.begin();
  Serial.println("Web server started");
}

void handleRoot() {
  server.send(200, "text/html", SETUP_HTML);
}

void handleSave() {
  saved_ssid = server.arg("ssid");
  saved_password = server.arg("password");
  saved_city = server.arg("city");
  saved_country = server.arg("country");
  saved_timezone = server.arg("timezone").toInt();
  saved_apiKey = server.arg("apikey");
  
  preferences.putString("ssid", saved_ssid);
  preferences.putString("password", saved_password);
  preferences.putString("city", saved_city);
  preferences.putString("country", saved_country);
  preferences.putInt("timezone", saved_timezone);
  preferences.putString("apikey", saved_apiKey);
  
  server.send(200, "text/html", SUCCESS_HTML);
  
  Serial.println("Settings saved! Restarting...");
  delay(3000);
  ESP.restart();
}

bool connectToWiFi() {
  matrix.fillScreen(0);
  matrix.setTextSize(1);
  matrix.setTextColor(COLOR_CYAN);
  matrix.setCursor(2, 12);
  matrix.print("WiFi...");
  matrix.show();
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(saved_ssid.c_str(), saved_password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected!");
    matrix.fillScreen(0);
    matrix.setTextColor(COLOR_GREEN);
    matrix.setCursor(2, 12);
    matrix.print("Connected!");
    matrix.show();
    delay(1500);
    return true;
  }
  
  Serial.println("WiFi Failed");
  return false;
}

void showWiFiError() {
  matrix.fillScreen(0);
  matrix.setTextColor(COLOR_RED);
  matrix.setCursor(2, 12);
  matrix.print("WiFi Lost");
  matrix.show();
}

void displayClockAndWeather() {
  matrix.fillScreen(0);
  
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return;
  
  int hours = timeinfo.tm_hour;
  int minutes = timeinfo.tm_min;
  String ampm = (hours >= 12) ? "PM" : "AM";
  if (hours > 12) hours -= 12;
  if (hours == 0) hours = 12;
  
  String timeStr = "";
  if (hours < 10) timeStr += " ";
  timeStr += String(hours) + ":";
  if (minutes < 10) timeStr += "0";
  timeStr += String(minutes) + ampm;
  
  matrix.setTextSize(1);
  matrix.setTextColor(COLOR_CYAN);
  matrix.setCursor(2, 2);
  matrix.print(timeStr);
  
  if (temperature != 0.0) {
    matrix.setTextColor(COLOR_YELLOW);
    matrix.setCursor(2, 12);
    matrix.print(String((int)temperature) + "F");
  }
  
  if (weatherDescription.length() > 0) {
    matrix.setTextColor(COLOR_WHITE);
    matrix.setCursor(2, 22);
    
    // Abbreviate common weather terms
    String shortWeather = weatherDescription;
    shortWeather.replace("clear sky", "clear");
    shortWeather.replace("few clouds", "clouds");
    shortWeather.replace("scattered clouds", "cloudy");
    shortWeather.replace("broken clouds", "cloudy");
    shortWeather.replace("overcast clouds", "overcast");
    shortWeather.replace("light rain", "rain");
    shortWeather.replace("moderate rain", "rain");
    shortWeather.replace("heavy rain", "hvyrain");
    shortWeather.replace("shower rain", "showers");
    shortWeather.replace("thunderstorm", "tstorm");
    
    // Limit to 8 characters max
    shortWeather = shortWeather.substring(0, min(8, (int)shortWeather.length()));
    matrix.print(shortWeather);
  }
  
  drawWeatherIcon();
  matrix.show();
}

void displayNFLScores() {
  matrix.fillScreen(0);
  
  if (gameCount == 0) {
    matrix.setTextSize(1);
    matrix.setTextColor(COLOR_WHITE);
    matrix.setCursor(8, 12);
    matrix.print("No Games");
    matrix.show();
    return;
  }
  
  NFLGame game = games[currentGameIndex];
  matrix.setTextSize(1);
  
  if (game.isLive) {
    // LIVE GAME - Top bar with status
    matrix.fillRect(0, 0, 64, 8, COLOR_RED);
    matrix.setTextColor(COLOR_WHITE);
    matrix.setCursor(20, 1);
    matrix.print("LIVE");
    
    // Away team and score - centered layout
    matrix.setTextColor(COLOR_WHITE);
    matrix.setCursor(4, 12);
    matrix.print(game.awayTeam);
    
    matrix.setTextColor(COLOR_YELLOW);
    matrix.setCursor(48, 12);
    matrix.print(game.awayScore);
    
    // Home team and score
    matrix.setTextColor(COLOR_WHITE);
    matrix.setCursor(4, 22);
    matrix.print(game.homeTeam);
    
    matrix.setTextColor(COLOR_YELLOW);
    matrix.setCursor(48, 22);
    matrix.print(game.homeScore);
    
  } else if (game.isFinal) {
    // FINAL GAME - Top bar
    matrix.fillRect(0, 0, 64, 8, COLOR_GREEN);
    matrix.setTextColor(COLOR_WHITE);
    matrix.setCursor(18, 1);
    matrix.print("FINAL");
    
    // Away team and score
    matrix.setTextColor(COLOR_WHITE);
    matrix.setCursor(4, 12);
    matrix.print(game.awayTeam);
    
    matrix.setTextColor(COLOR_YELLOW);
    matrix.setCursor(48, 12);
    matrix.print(game.awayScore);
    
    // Home team and score
    matrix.setTextColor(COLOR_WHITE);
    matrix.setCursor(4, 22);
    matrix.print(game.homeTeam);
    
    matrix.setTextColor(COLOR_YELLOW);
    matrix.setCursor(48, 22);
    matrix.print(game.homeScore);
    
  } else if (game.isUpcoming) {
    // UPCOMING GAME - Top bar
    matrix.fillRect(0, 0, 64, 8, COLOR_CYAN);
    matrix.setTextColor(COLOR_WHITE);
    matrix.setCursor(12, 1);
    matrix.print("UPCOMING");
    
    // Away team
    matrix.setTextColor(COLOR_WHITE);
    matrix.setCursor(4, 12);
    matrix.print(game.awayTeam);
    
    matrix.setTextColor(COLOR_YELLOW);
    matrix.setCursor(28, 12);
    matrix.print("at");
    
    // Home team
    matrix.setTextColor(COLOR_WHITE);
    matrix.setCursor(4, 22);
    matrix.print(game.homeTeam);
    
    // Game time - clean format
    if (game.gameTime.length() > 0) {
      matrix.setTextColor(COLOR_YELLOW);
      String displayTime = game.gameTime;
      
      // Simplify time: remove :00 for cleaner look
      if (displayTime.indexOf(":00") > 0) {
        displayTime.replace(":00", "");
      }
      
      // Right-align the time
      int timeWidth = displayTime.length() * 6;
      int xPos = 62 - timeWidth;
      if (xPos < 32) {
        // If too long, truncate date and just show time
        int spacePos = displayTime.lastIndexOf(' ');
        if (spacePos > 0) {
          displayTime = displayTime.substring(spacePos + 1);
          timeWidth = displayTime.length() * 6;
          xPos = 62 - timeWidth;
        }
      }
      
      matrix.setCursor(xPos, 22);
      matrix.print(displayTime);
    }
  }
  
  matrix.show();
  
  // HEAVY FOCUS TIMING: Live games get most time
  static unsigned long lastGameSwitch = 0;
  unsigned long switchInterval;
  
  if (game.isLive) {
    switchInterval = 55000; // 55 seconds for LIVE games (MAXIMUM FOCUS)
  } else if (game.isUpcoming) {
    switchInterval = 6000;  // 6 seconds for UPCOMING games (brief)
  } else if (game.isFinal) {
    switchInterval = 5000;  // 5 seconds for FINAL games (very brief)
  } else {
    switchInterval = 5000;  // 5 seconds default
  }
  
  if (millis() - lastGameSwitch > switchInterval) {
    currentGameIndex = (currentGameIndex + 1) % gameCount;
    lastGameSwitch = millis();
  }
}

void drawWeatherIcon() {
  int x = 52, y = 2;
  if (weatherDescription.indexOf("clear") >= 0) {
    matrix.drawCircle(x + 4, y + 4, 3, COLOR_YELLOW);
  } 
  else if (weatherDescription.indexOf("cloud") >= 0) {
    matrix.fillCircle(x + 2, y + 4, 2, COLOR_WHITE);
    matrix.fillCircle(x + 5, y + 3, 2, COLOR_WHITE);
    matrix.fillCircle(x + 8, y + 4, 2, COLOR_WHITE);
  }
  else if (weatherDescription.indexOf("rain") >= 0) {
    matrix.drawLine(x + 2, y + 6, x + 2, y + 8, COLOR_BLUE);
    matrix.drawLine(x + 5, y + 5, x + 5, y + 7, COLOR_BLUE);
  }
}

void getWeather() {
  if (saved_apiKey.length() == 0) return;
  
  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + saved_city + "," + 
               saved_country + "&units=imperial&appid=" + saved_apiKey;
  
  http.begin(url);
  int httpCode = http.GET();
  
  if (httpCode > 0) {
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, http.getString());
    
    if (!error) {
      float newTemp = doc["main"]["temp"] | 0.0;
      String newDesc = doc["weather"][0]["description"].as<String>();
      
      // Check if weather changed significantly
      if (abs(newTemp - lastTemperature) >= 2.0 || newDesc != lastWeatherDescription) {
        weatherChanged = true;
        Serial.println("Weather changed!");
        Serial.print("Old: ");
        Serial.print(lastTemperature);
        Serial.print("F, ");
        Serial.println(lastWeatherDescription);
        Serial.print("New: ");
        Serial.print(newTemp);
        Serial.print("F, ");
        Serial.println(newDesc);
      }
      
      // Update values
      lastTemperature = temperature;
      lastWeatherDescription = weatherDescription;
      temperature = newTemp;
      weatherDescription = newDesc;
      lastWeatherUpdate = millis();
      
      Serial.print("Weather: ");
      Serial.print(temperature);
      Serial.print("F, ");
      Serial.println(weatherDescription);
    } else {
      Serial.print("Weather JSON Error: ");
      Serial.println(error.c_str());
    }
  } else {
    Serial.print("Weather HTTP Error: ");
    Serial.println(httpCode);
  }
  
  http.end();
}

void sortGamesByPriority() {
  // Simple bubble sort to prioritize: Live > Upcoming (by actual time) > Final
  for (int i = 0; i < gameCount - 1; i++) {
    for (int j = 0; j < gameCount - i - 1; j++) {
      bool shouldSwap = false;
      
      // Priority 1: Live games first
      if (!games[j].isLive && games[j+1].isLive) {
        shouldSwap = true;
      }
      // Priority 2: Then upcoming games
      else if (!games[j].isLive && !games[j].isUpcoming && 
               !games[j+1].isLive && games[j+1].isUpcoming) {
        shouldSwap = true;
      }
      // For upcoming games, sort by actual game time (earliest first)
      else if (games[j].isUpcoming && games[j+1].isUpcoming) {
        // Parse the game times to compare properly
        // Format: "11/27 1:30PM" or "11/27 4:30PM"
        
        // Extract date and time components
        int slash1_j = games[j].gameTime.indexOf('/');
        int space_j = games[j].gameTime.indexOf(' ');
        int colon_j = games[j].gameTime.indexOf(':');
        
        int slash1_k = games[j+1].gameTime.indexOf('/');
        int space_k = games[j+1].gameTime.indexOf(' ');
        int colon_k = games[j+1].gameTime.indexOf(':');
        
        if (slash1_j > 0 && space_j > 0 && slash1_k > 0 && space_k > 0) {
          // Get month, day, hour for both games
          int month_j = games[j].gameTime.substring(0, slash1_j).toInt();
          int day_j = games[j].gameTime.substring(slash1_j + 1, space_j).toInt();
          
          int month_k = games[j+1].gameTime.substring(0, slash1_k).toInt();
          int day_k = games[j+1].gameTime.substring(slash1_k + 1, space_k).toInt();
          
          // Compare dates first
          if (month_j != month_k) {
            shouldSwap = (month_j > month_k);
          } else if (day_j != day_k) {
            shouldSwap = (day_j > day_k);
          } else {
            // Same date, compare times
            if (colon_j > 0 && colon_k > 0) {
              int hour_j = games[j].gameTime.substring(space_j + 1, colon_j).toInt();
              int hour_k = games[j+1].gameTime.substring(space_k + 1, colon_k).toInt();
              
              // Check for PM/AM
              bool isPM_j = (games[j].gameTime.indexOf("PM") > 0);
              bool isPM_k = (games[j+1].gameTime.indexOf("PM") > 0);
              
              // Convert to 24-hour for comparison
              if (isPM_j && hour_j != 12) hour_j += 12;
              if (!isPM_j && hour_j == 12) hour_j = 0;
              if (isPM_k && hour_k != 12) hour_k += 12;
              if (!isPM_k && hour_k == 12) hour_k = 0;
              
              if (hour_j != hour_k) {
                shouldSwap = (hour_j > hour_k);
              } else {
                // Same hour, compare minutes
                int minute_j = games[j].gameTime.substring(colon_j + 1, colon_j + 3).toInt();
                int minute_k = games[j+1].gameTime.substring(colon_k + 1, colon_k + 3).toInt();
                shouldSwap = (minute_j > minute_k);
              }
            }
          }
        }
      }
      
      if (shouldSwap) {
        NFLGame temp = games[j];
        games[j] = games[j+1];
        games[j+1] = temp;
      }
    }
  }
}

void getNFLScores() {
  HTTPClient http;
  
  http.begin("http://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard");
  http.setTimeout(10000);
  
  int httpCode = http.GET();
  
  if (httpCode > 0) {
    WiFiClient* stream = http.getStreamPtr();
    String payload = stream->readString();
    
    Serial.print("Payload size: ");
    Serial.println(payload.length());
    Serial.println("Parsing NFL scores...");
    
    gameCount = 0;
    
    // Find the events array
    int eventsStart = payload.indexOf("\"events\":[");
    if (eventsStart == -1) {
      Serial.println("No events array found");
      http.end();
      return;
    }
    
    Serial.println("Found events array");
    
    // Find each event by looking for the pattern: {"id":"401...
    int searchPos = eventsStart;
    
    while (gameCount < 16) {
      // Look for event ID pattern (more unique than shortName)
      int eventIdPos = payload.indexOf("{\"id\":\"401", searchPos);
      if (eventIdPos == -1 || eventIdPos > payload.length() - 500) {
        Serial.println("No more events");
        break;
      }
      
      // Make sure we're still in the events array (before next major section)
      int nextSection = payload.indexOf("\"leagues\":[", searchPos);
      if (nextSection != -1 && eventIdPos > nextSection) {
        Serial.println("Reached end of events array");
        break;
      }
      
      Serial.print("Processing event at position: ");
      Serial.println(eventIdPos);
      
      // Find shortName for this specific event (within next 800 chars)
      int shortNamePos = payload.indexOf("\"shortName\":\"", eventIdPos);
      if (shortNamePos == -1 || shortNamePos > eventIdPos + 800) {
        searchPos = eventIdPos + 100;
        Serial.println("  No shortName found nearby");
        continue;
      }
      
      shortNamePos += 13;
      int shortNameEnd = payload.indexOf("\"", shortNamePos);
      String matchup = payload.substring(shortNamePos, shortNameEnd);
      
      // Verify this looks like a matchup (has " @ ")
      if (matchup.indexOf(" @ ") == -1) {
        searchPos = shortNameEnd;
        Serial.print("  Not a matchup: ");
        Serial.println(matchup);
        continue;
      }
      
      Serial.print("  Found game: ");
      Serial.println(matchup);
      
      // Parse away @ home format
      int atPos = matchup.indexOf(" @ ");
      String awayTeam = matchup.substring(0, atPos);
      String homeTeam = matchup.substring(atPos + 3);
      
      // Find the competitions array (status is inside it)
      int competitionsPos = payload.indexOf("\"competitions\":[", eventIdPos);
      if (competitionsPos == -1 || competitionsPos > eventIdPos + 1000) {
        searchPos = shortNameEnd;
        Serial.println("  No competitions array found");
        continue;
      }
      
      // Find status within the competition (much larger search window)
      int statusPos = payload.indexOf("\"status\":{", competitionsPos);
      if (statusPos == -1 || statusPos > competitionsPos + 20000) {
        searchPos = shortNameEnd;
        Serial.print("  No status found (searched from ");
        Serial.print(competitionsPos);
        Serial.println(")");
        continue;
      }
      
      int statePos = payload.indexOf("\"state\":\"", statusPos);
      String state = "";
      if (statePos != -1 && statePos < statusPos + 1000) {
        statePos += 9;
        int stateEnd = payload.indexOf("\"", statePos);
        state = payload.substring(statePos, stateEnd);
      }
      
      Serial.print("  State: ");
      Serial.println(state);
      
      // Find competitors array for scores
      int competitorsPos = payload.indexOf("\"competitors\":[", competitionsPos);
      if (competitorsPos == -1 || competitorsPos > competitionsPos + 15000) {
        searchPos = shortNameEnd;
        Serial.println("  No competitors found");
        continue;
      }
      
      // Find first score (home team - order:0)
      int score1Pos = payload.indexOf("\"score\":", competitorsPos);
      int homeScore = 0;
      if (score1Pos != -1 && score1Pos < competitorsPos + 3000) {
        score1Pos += 8;
        if (payload.charAt(score1Pos) == '\"') score1Pos++;
        String scoreStr = "";
        for (int i = 0; i < 3; i++) {
          char c = payload.charAt(score1Pos + i);
          if (c >= '0' && c <= '9') scoreStr += c;
          else break;
        }
        homeScore = scoreStr.toInt();
      }
      
      // Find second score (away team - order:1)
      int score2Pos = payload.indexOf("\"score\":", score1Pos + 10);
      int awayScore = 0;
      if (score2Pos != -1 && score2Pos < competitorsPos + 3000) {
        score2Pos += 8;
        if (payload.charAt(score2Pos) == '\"') score2Pos++;
        String scoreStr = "";
        for (int i = 0; i < 3; i++) {
          char c = payload.charAt(score2Pos + i);
          if (c >= '0' && c <= '9') scoreStr += c;
          else break;
        }
        awayScore = scoreStr.toInt();
      }
      
      // Find game date
      int datePos = payload.lastIndexOf("\"date\":\"", shortNamePos);
      String gameTime = "";
      
      if (state == "pre" && datePos != -1 && datePos > eventIdPos) {
        datePos += 8;
        String dateStr = payload.substring(datePos, datePos + 20);
        
        if (dateStr.length() >= 16) {
          int month = dateStr.substring(5, 7).toInt();
          int day = dateStr.substring(8, 10).toInt();
          int hour = dateStr.substring(11, 13).toInt();
          int minute = dateStr.substring(14, 16).toInt();
          
          // Adjust for timezone
          hour += saved_timezone;
          if (hour < 0) hour += 24;
          if (hour >= 24) hour -= 24;
          
          String ampm = (hour >= 12) ? "PM" : "AM";
          if (hour > 12) hour -= 12;
          if (hour == 0) hour = 12;
          
          gameTime = String(month) + "/" + String(day) + " " + 
                    String(hour) + ":";
          if (minute < 10) gameTime += "0";
          gameTime += String(minute) + ampm;
        }
      }
      
      // Store the game
      games[gameCount].awayTeam = awayTeam;
      games[gameCount].homeTeam = homeTeam;
      games[gameCount].awayScore = awayScore;
      games[gameCount].homeScore = homeScore;
      games[gameCount].isLive = (state == "in");
      games[gameCount].isFinal = (state == "post");
      games[gameCount].isUpcoming = (state == "pre");
      games[gameCount].gameTime = gameTime;
      
      Serial.print("  ✓ Stored: ");
      Serial.print(awayTeam);
      Serial.print(" @ ");
      Serial.print(homeTeam);
      Serial.print(" - ");
      Serial.print(awayScore);
      Serial.print("-");
      Serial.print(homeScore);
      Serial.print(" [");
      Serial.print(state);
      Serial.println("]");
      
      gameCount++;
      searchPos = statusPos + 1000; // Move well past this event
    }
    
    Serial.print("\nTotal games found: ");
    Serial.println(gameCount);
    
    if (gameCount > 0) {
      // Sort by priority
      sortGamesByPriority();
      
      Serial.println("\n--- Games sorted by priority ---");
      for (int i = 0; i < gameCount; i++) {
        Serial.print(i + 1);
        Serial.print(". ");
        Serial.print(games[i].awayTeam);
        Serial.print(" @ ");
        Serial.print(games[i].homeTeam);
        if (games[i].isLive) Serial.print(" [LIVE]");
        else if (games[i].isUpcoming) Serial.print(" [UPCOMING]");
        else if (games[i].isFinal) Serial.print(" [FINAL]");
        Serial.println();
      }
    }
    
    lastNFLUpdate = millis();
  } else {
    Serial.print("NFL HTTP Error: ");
    Serial.println(httpCode);
    gameCount = 0;
  }
  
  http.end();
}