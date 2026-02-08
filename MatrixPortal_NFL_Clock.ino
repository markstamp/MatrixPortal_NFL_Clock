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
Preferences preferences;  // Must be at global scope before setup()

// ===== Setup Mode Settings =====
const char* AP_SSID = "MatrixClock-Setup";
const char* AP_PASSWORD = "";
WebServer server(80);
DNSServer dnsServer;
bool setupMode = false;

// ===== OTA Update Settings =====
const char* FIRMWARE_VERSION = "1.5.1";  // Increment this with each release
const char* GITHUB_FIRMWARE_URL = "https://github.com/markstamp/MatrixPortal_NFL_Clock/releases/latest/download/firmware.bin";
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
enum DisplayMode { CLOCK_WEATHER, OLYMPICS_MEDALS, NFL_SCORES };
DisplayMode currentMode = CLOCK_WEATHER;
unsigned long modeChangeTime = 0;
const unsigned long MODE_DURATION = 15000;

// ===== Data Variables =====
String weatherDescription = "";
float temperature = 0.0;
unsigned long lastWeatherUpdate = 0;
const unsigned long weatherUpdateInterval = 600000;
String lastWeatherDescription = "";
float lastTemperature = 0.0;
bool weatherChanged = false;

// ===== Touchdown Detection =====
struct TouchdownEvent {
  bool active;
  bool initialized;
  String team;
  unsigned long startTime;
  int awayScoreBefore;
  int homeScoreBefore;
};

TouchdownEvent touchdown = {false, false, "", 0, 0, 0};
const unsigned long TOUCHDOWN_ANIMATION_DURATION = 5000; // 5 seconds

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

// ===== Olympics Data =====
int usaGold = 0, usaSilver = 0, usaBronze = 0;
unsigned long lastOlympicsUpdate = 0;
const unsigned long olympicsUpdateInterval = 600000; // 10 minutes

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
void getOlympicsMedals();
void displayOlympicsMedals();

// ===== OTA UPDATE FUNCTIONS =====

void checkForOTAUpdate() {
  if (updateInProgress) return;
  
  Serial.println("Checking for firmware updates...");
  
  HTTPClient http;
  http.begin("https://api.github.com/repos/markstamp/MatrixPortal_NFL_Clock/releases/latest");
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
  
  // Initialize matrix first
  if(matrix.begin() != PROTOMATTER_OK) {
    Serial.println("Matrix init FAILED!");
    while(1);
  }
  
  // Initialize colors
  COLOR_CYAN = matrix.color565(0, 255, 255);
  COLOR_YELLOW = matrix.color565(255, 255, 0);
  COLOR_WHITE = matrix.color565(255, 255, 255);
  COLOR_GREEN = matrix.color565(0, 255, 0);
  COLOR_RED = matrix.color565(255, 0, 0);
  COLOR_BLUE = matrix.color565(0, 100, 255);
  COLOR_ORANGE = matrix.color565(255, 128, 0);
  COLOR_PURPLE = matrix.color565(200, 0, 255);
  
  // Setup buttons BEFORE using preferences
  pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(UPDATE_BUTTON_PIN, INPUT_PULLUP);
  
  // NOW initialize preferences
  preferences.begin("settings", false);
  
  // Check if setup button is pressed on boot to force setup mode
  if (digitalRead(SETUP_BUTTON_PIN) == LOW) {
    Serial.println("Setup button pressed - clearing all settings!");
    preferences.clear();
    preferences.end();
    
    matrix.fillScreen(0);
    matrix.setTextSize(1);
    matrix.setTextColor(COLOR_RED);
    matrix.setCursor(2, 2);
    matrix.print("Settings");
    matrix.setCursor(2, 12);
    matrix.print("Cleared!");
    matrix.setCursor(2, 22);
    matrix.print("Reboot...");
    matrix.show();
    
    delay(2000);
    ESP.restart();
  }
  
  // Load saved settings
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
      // Show version after WiFi connects
      displayFirmwareVersion();
      
      configTime(saved_timezone * 3600, 0, "pool.ntp.org");
      delay(2000);
      getWeather();
      getOlympicsMedals();
      getNFLScores();

      // Check for updates on startup
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
  
  // Check for manual update button press
  static unsigned long lastUpdateButtonPress = 0;
  if (digitalRead(UPDATE_BUTTON_PIN) == LOW) {
    if (millis() - lastUpdateButtonPress > 1000) { // Debounce
      Serial.println("Manual update triggered!");
      checkForOTAUpdate();
      lastUpdateButtonPress = millis();
    }
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    showWiFiError();
    delay(2000);
    Serial.println("Attempting WiFi reconnection...");
    WiFi.disconnect();
    WiFi.begin(saved_ssid.c_str(), saved_password.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
      delay(500);
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi reconnected!");
    }
    return;
  }
  
  // Periodic update check (every 6 hours)
  if (millis() - lastUpdateCheck > updateCheckInterval) {
    checkForOTAUpdate();
  }
  
  if (millis() - lastWeatherUpdate > weatherUpdateInterval) {
    getWeather();
  }

  if (millis() - lastOlympicsUpdate > olympicsUpdateInterval) {
    getOlympicsMedals();
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
  
  // TIMING LOGIC - Show ALL games, but with different durations
  unsigned long currentModeDuration = MODE_DURATION;
  
  if (hasLiveGames) {
    if (currentMode == NFL_SCORES) {
      // Calculate total cycle time for ALL games
      // Live games get 55 seconds each
      // Upcoming games get 15 seconds each
      // Final games get 5 seconds each
      
      unsigned long cycleTime = (liveGameCount * 55000) +      // 55 sec per live game
                                (upcomingGameCount * 15000) +   // 15 sec per upcoming game
                                (finalGameCount * 5000);        // 5 sec per final game
      
      currentModeDuration = cycleTime;
      
      Serial.print("NFL cycle time: ");
      Serial.print(cycleTime / 1000);
      Serial.print(" seconds (");
      Serial.print(liveGameCount);
      Serial.print(" live [55s], ");
      Serial.print(upcomingGameCount);
      Serial.print(" upcoming [15s], ");
      Serial.print(finalGameCount);
      Serial.println(" final [5s])");
      
    } else if (currentMode == OLYMPICS_MEDALS) {
      currentModeDuration = 10000; // 10 seconds for Olympics
    } else {
      // CLOCK/WEATHER mode during live games
      if (weatherChanged) {
        currentModeDuration = 5000; // 5 seconds if weather changed
      } else {
        currentModeDuration = 0; // Skip entirely if no change
      }
    }
  } else {
    // No live games - normal rotation
    if (currentMode == OLYMPICS_MEDALS) {
      currentModeDuration = 10000; // 10 seconds for Olympics
    } else {
      currentModeDuration = 15000; // 15 seconds each
    }
  }

  if (millis() - modeChangeTime > currentModeDuration) {
    // Switch modes: CLOCK_WEATHER -> OLYMPICS_MEDALS -> NFL_SCORES -> CLOCK_WEATHER
    if (currentMode == CLOCK_WEATHER) {
      currentMode = OLYMPICS_MEDALS;
      weatherChanged = false;
      Serial.println("=== Switching to Olympics Medals ===");
    } else if (currentMode == OLYMPICS_MEDALS) {
      currentMode = NFL_SCORES;
      currentGameIndex = 0;
      Serial.println("=== Switching to NFL Scores ===");
      Serial.print("Will display ");
      Serial.print(gameCount);
      Serial.println(" total games");
    } else {
      // From NFL_SCORES, switch back to clock/weather or continue
      if (weatherChanged || !hasLiveGames) {
        currentMode = CLOCK_WEATHER;
        Serial.println("=== Switching to Clock/Weather ===");
      } else {
        // Stay on NFL scores - continue cycling through ALL games
        Serial.println("=== Continuing NFL cycle (no weather change) ===");
      }
    }

    modeChangeTime = millis();
  }

  if (currentMode == CLOCK_WEATHER) {
    displayClockAndWeather();
  } else if (currentMode == OLYMPICS_MEDALS) {
    displayOlympicsMedals();
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
// ===== TOUCHDOWN DETECTION =====

void checkForTouchdown() {
  // Only check during live games
  for (int i = 0; i < gameCount; i++) {
    if (games[i].isLive) {
      // Check if this is the game we're tracking
      if (i == currentGameIndex) {
        int awayScore = games[i].awayScore;
        int homeScore = games[i].homeScore;
        
        // First time seeing this game - initialize tracking
        if (!touchdown.initialized) {
          touchdown.awayScoreBefore = awayScore;
          touchdown.homeScoreBefore = homeScore;
          touchdown.initialized = true;
          return;
        }
        
        // Check for away team touchdown (6+ point increase)
        if (awayScore >= touchdown.awayScoreBefore + 6) {
          touchdown.active = true;
          touchdown.team = games[i].awayTeam;
          touchdown.startTime = millis();
          touchdown.awayScoreBefore = awayScore;
          touchdown.homeScoreBefore = homeScore;
          
          Serial.print("🏈 TOUCHDOWN! ");
          Serial.print(touchdown.team);
          Serial.print(" scored! New score: ");
          Serial.print(awayScore);
          Serial.print("-");
          Serial.println(homeScore);
          return;
        }
        
        // Check for home team touchdown (6+ point increase)
        if (homeScore >= touchdown.homeScoreBefore + 6) {
          touchdown.active = true;
          touchdown.team = games[i].homeTeam;
          touchdown.startTime = millis();
          touchdown.awayScoreBefore = awayScore;
          touchdown.homeScoreBefore = homeScore;
          
          Serial.print("🏈 TOUCHDOWN! ");
          Serial.print(touchdown.team);
          Serial.print(" scored! New score: ");
          Serial.print(awayScore);
          Serial.print("-");
          Serial.println(homeScore);
          return;
        }
        
        // Update tracking scores (for field goals, safeties, etc.)
        if (awayScore != touchdown.awayScoreBefore || homeScore != touchdown.homeScoreBefore) {
          touchdown.awayScoreBefore = awayScore;
          touchdown.homeScoreBefore = homeScore;
        }
      }
    }
  }
}

// ===== TOUCHDOWN ANIMATION =====

void displayTouchdownAnimation() {
  // Calculate elapsed time
  unsigned long elapsed = millis() - touchdown.startTime;
  
  // Animation is done after 5 seconds
  if (elapsed > TOUCHDOWN_ANIMATION_DURATION) {
    touchdown.active = false;
    return;
  }
  
  // Blink effect: on for 500ms, off for 500ms
  bool blinkOn = (elapsed % 1000) < 500;
  
  matrix.fillScreen(0);
  
  if (blinkOn) {
    // Get team colors
    uint16_t teamColor = getTeamPrimaryColor(touchdown.team);
    uint16_t accentColor = getTeamSecondaryColor(touchdown.team);
    
    // Flash background with team color
    matrix.fillScreen(teamColor);
    
    // Draw "TOUCHDOWN!" text
    matrix.setTextSize(1);
    matrix.setTextColor(accentColor);
    
    // Line 1: Team abbreviation
    matrix.setCursor(16, 2);
    matrix.print(touchdown.team);
    
    // Line 2: "TOUCH"
    matrix.setCursor(4, 12);
    matrix.print("TOUCH");
    
    // Line 3: "DOWN!"
    matrix.setCursor(4, 22);
    matrix.print("DOWN!");
  } else {
    // Blink off - show black screen
    matrix.fillScreen(0);
  }
  
  matrix.show();
}
void displayNFLScores() {
  // CHECK FOR TOUCHDOWN ANIMATION FIRST
  if (touchdown.active) {
    displayTouchdownAnimation();
    return; // Don't show normal score display during animation
  }
  
  // Check for new touchdowns in current game
  checkForTouchdown();
  
  matrix.fillScreen(0);
  
  if (gameCount == 0) {
    matrix.setTextSize(1);
    matrix.setTextColor(COLOR_WHITE);
    matrix.setCursor(8, 12);
    matrix.print("No Games");
    matrix.show();
    return;
  }
  
  NFLGame& game = games[currentGameIndex];
  matrix.setTextSize(1);
  
  if (game.isLive) {
    // LIVE GAME - Red status bar
    matrix.fillRect(0, 0, 64, 8, COLOR_RED);
    matrix.setTextColor(COLOR_WHITE);
    matrix.setCursor(20, 1);
    matrix.print("LIVE");
    
    // Away team - colored box with team abbreviation
    drawTeamBox(game.awayTeam, 2, 10, 22, 9);
    
    // Away score
    matrix.setTextColor(COLOR_YELLOW);
    matrix.setCursor(26, 11);
    matrix.print(game.awayScore);
    
    // Home team - colored box with team abbreviation
    drawTeamBox(game.homeTeam, 2, 21, 22, 9);
    
    // Home score
    matrix.setTextColor(COLOR_YELLOW);
    matrix.setCursor(26, 22);
    matrix.print(game.homeScore);
    
  } else if (game.isFinal) {
    // FINAL GAME - Green status bar
    matrix.fillRect(0, 0, 64, 8, COLOR_GREEN);
    matrix.setTextColor(COLOR_WHITE);
    matrix.setCursor(18, 1);
    matrix.print("FINAL");
    
    // Away team - colored box
    drawTeamBox(game.awayTeam, 2, 10, 22, 9);
    
    // Away score
    matrix.setTextColor(COLOR_WHITE);
    matrix.setCursor(26, 11);
    matrix.print(game.awayScore);
    
    // Home team - colored box
    drawTeamBox(game.homeTeam, 2, 21, 22, 9);
    
    // Home score
    matrix.setTextColor(COLOR_WHITE);
    matrix.setCursor(26, 22);
    matrix.print(game.homeScore);
    
  } else if (game.isUpcoming) {
    // UPCOMING GAME - Cyan status bar
    matrix.fillRect(0, 0, 64, 8, COLOR_CYAN);
    matrix.setTextColor(COLOR_WHITE);
    matrix.setCursor(12, 1);
    matrix.print("UPCOMING");
    
    // Away team - colored box
    drawTeamBox(game.awayTeam, 2, 10, 22, 9);
    
    // "at" text
    matrix.setTextColor(COLOR_WHITE);
    matrix.setCursor(26, 11);
    matrix.print("at");
    
    // Home team - colored box
    drawTeamBox(game.homeTeam, 2, 21, 22, 9);
    
    // Game time - right aligned
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
      if (xPos < 26) {
        // If too long, just show time
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
  
  // CRITICAL: Use separate static timer for game switching
  static unsigned long lastGameSwitch = 0;
  static int lastGameIndex = -1;
  
  // Reset timer if we just switched modes or game index changed externally
  if (lastGameIndex != currentGameIndex) {
    lastGameSwitch = millis();
    lastGameIndex = currentGameIndex;
    
    // RESET TOUCHDOWN TRACKING when switching games
    touchdown.awayScoreBefore = 0;
    touchdown.homeScoreBefore = 0;
    touchdown.initialized = false;
    
    // Debug output
    Serial.print("Displaying game ");
    Serial.print(currentGameIndex + 1);
    Serial.print("/");
    Serial.print(gameCount);
    Serial.print(": ");
    Serial.print(game.awayTeam);
    Serial.print(" @ ");
    Serial.print(game.homeTeam);
    if (game.isLive) Serial.println(" [LIVE - will show for 55 seconds]");
    else if (game.isUpcoming) Serial.println(" [UPCOMING - will show for 15 seconds]");
    else if (game.isFinal) Serial.println(" [FINAL - will show for 5 seconds]");
  }
  
  // Determine how long to show this game
  unsigned long switchInterval;
  
  if (game.isLive) {
    switchInterval = 55000; // 55 seconds for LIVE games
  } else if (game.isUpcoming) {
    switchInterval = 15000; // 15 seconds for UPCOMING games
  } else if (game.isFinal) {
    switchInterval = 5000;  // 5 seconds for FINAL games
  } else {
    switchInterval = 5000;  // 5 seconds default
  }
  
  // Only switch to next game after the interval has passed
  // UNLESS touchdown animation is active (then pause game switching)
  if (!touchdown.active && millis() - lastGameSwitch >= switchInterval) {
    Serial.print("Switching from game ");
    Serial.print(currentGameIndex + 1);
    Serial.print(" to ");
    
    currentGameIndex = (currentGameIndex + 1) % gameCount;
    lastGameSwitch = millis();
    lastGameIndex = currentGameIndex;
    
    Serial.println(currentGameIndex + 1);
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
  String url = "https://api.openweathermap.org/data/2.5/weather?q=" + saved_city + "," +
               saved_country + "&units=imperial&appid=" + saved_apiKey;
  
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == 200) {
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
      temperature = newTemp;
      weatherDescription = newDesc;
      lastTemperature = newTemp;
      lastWeatherDescription = newDesc;
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
// ===== TEAM COLOR FUNCTIONS =====

// Get primary team color
uint16_t getTeamPrimaryColor(const String& team) {
  // AFC East
  if (team == "BUF") return matrix.color565(0, 51, 141);      // Bills Blue
  if (team == "MIA") return matrix.color565(0, 142, 151);     // Dolphins Aqua
  if (team == "NE")  return matrix.color565(0, 34, 68);       // Patriots Navy
  if (team == "NYJ") return matrix.color565(18, 87, 64);      // Jets Green
  
  // AFC North
  if (team == "BAL") return matrix.color565(26, 25, 95);      // Ravens Purple
  if (team == "CIN") return matrix.color565(251, 79, 20);     // Bengals Orange
  if (team == "CLE") return matrix.color565(49, 29, 0);       // Browns Brown
  if (team == "PIT") return matrix.color565(255, 182, 18);    // Steelers Gold
  
  // AFC South
  if (team == "HOU") return matrix.color565(3, 32, 47);       // Texans Navy
  if (team == "IND") return matrix.color565(0, 44, 95);       // Colts Blue
  if (team == "JAX") return matrix.color565(0, 103, 120);     // Jaguars Teal
  if (team == "TEN") return matrix.color565(12, 35, 64);      // Titans Navy
  
  // AFC West
  if (team == "DEN") return matrix.color565(251, 79, 20);     // Broncos Orange
  if (team == "KC")  return matrix.color565(227, 24, 55);     // Chiefs Red
  if (team == "LV")  return matrix.color565(0, 0, 0);         // Raiders Black
  if (team == "LAC") return matrix.color565(0, 128, 198);     // Chargers Blue
  
  // NFC East
  if (team == "DAL") return matrix.color565(0, 34, 68);       // Cowboys Blue
  if (team == "NYG") return matrix.color565(1, 35, 82);       // Giants Blue
  if (team == "PHI") return matrix.color565(0, 76, 84);       // Eagles Green
  if (team == "WAS") return matrix.color565(90, 20, 20);      // Commanders Burgundy
  
  // NFC North
  if (team == "CHI") return matrix.color565(11, 22, 42);      // Bears Navy
  if (team == "DET") return matrix.color565(0, 118, 182);     // Lions Blue
  if (team == "GB")  return matrix.color565(24, 48, 40);      // Packers Green
  if (team == "MIN") return matrix.color565(79, 38, 131);     // Vikings Purple
  
  // NFC South
  if (team == "ATL") return matrix.color565(167, 25, 48);     // Falcons Red
  if (team == "CAR") return matrix.color565(0, 133, 202);     // Panthers Blue
  if (team == "NO")  return matrix.color565(211, 188, 141);   // Saints Gold
  if (team == "TB")  return matrix.color565(213, 10, 10);     // Buccaneers Red
  
  // NFC West
  if (team == "ARI") return matrix.color565(151, 35, 63);     // Cardinals Red
  if (team == "LAR") return matrix.color565(0, 53, 148);      // Rams Blue
  if (team == "SF")  return matrix.color565(170, 0, 0);       // 49ers Red
  if (team == "SEA") return matrix.color565(0, 34, 68);       // Seahawks Navy
  
  return COLOR_WHITE; // Default if team not found
}

// Get secondary/accent team color
uint16_t getTeamSecondaryColor(const String& team) {
  // AFC East
  if (team == "BUF") return matrix.color565(198, 12, 48);     // Bills Red
  if (team == "MIA") return matrix.color565(252, 76, 2);      // Dolphins Orange
  if (team == "NE")  return matrix.color565(198, 12, 48);     // Patriots Red
  if (team == "NYJ") return matrix.color565(255, 255, 255);   // Jets White
  
  // AFC North
  if (team == "BAL") return matrix.color565(158, 124, 12);    // Ravens Gold
  if (team == "CIN") return matrix.color565(0, 0, 0);         // Bengals Black
  if (team == "CLE") return matrix.color565(255, 60, 0);      // Browns Orange
  if (team == "PIT") return matrix.color565(0, 0, 0);         // Steelers Black
  
  // AFC South
  if (team == "HOU") return matrix.color565(167, 25, 48);     // Texans Red
  if (team == "IND") return matrix.color565(255, 255, 255);   // Colts White
  if (team == "JAX") return matrix.color565(159, 121, 44);    // Jaguars Gold
  if (team == "TEN") return matrix.color565(75, 146, 219);    // Titans Light Blue
  
  // AFC West
  if (team == "DEN") return matrix.color565(0, 34, 68);       // Broncos Navy
  if (team == "KC")  return matrix.color565(255, 184, 28);    // Chiefs Gold
  if (team == "LV")  return matrix.color565(165, 172, 175);   // Raiders Silver
  if (team == "LAC") return matrix.color565(255, 194, 14);    // Chargers Gold
  
  // NFC East
  if (team == "DAL") return matrix.color565(134, 147, 151);   // Cowboys Silver
  if (team == "NYG") return matrix.color565(163, 13, 45);     // Giants Red
  if (team == "PHI") return matrix.color565(165, 172, 175);   // Eagles Silver
  if (team == "WAS") return matrix.color565(255, 182, 18);    // Commanders Gold
  
  // NFC North
  if (team == "CHI") return matrix.color565(200, 56, 3);      // Bears Orange
  if (team == "DET") return matrix.color565(176, 183, 188);   // Lions Silver
  if (team == "GB")  return matrix.color565(255, 184, 28);    // Packers Gold
  if (team == "MIN") return matrix.color565(255, 198, 47);    // Vikings Gold
  
  // NFC South
  if (team == "ATL") return matrix.color565(0, 0, 0);         // Falcons Black
  if (team == "CAR") return matrix.color565(0, 0, 0);         // Panthers Black
  if (team == "NO")  return matrix.color565(0, 0, 0);         // Saints Black
  if (team == "TB")  return matrix.color565(255, 121, 0);     // Buccaneers Orange
  
  // NFC West
  if (team == "ARI") return matrix.color565(255, 182, 18);    // Cardinals Gold
  if (team == "LAR") return matrix.color565(255, 209, 0);     // Rams Gold
  if (team == "SF")  return matrix.color565(173, 153, 93);    // 49ers Gold
  if (team == "SEA") return matrix.color565(105, 190, 40);    // Seahawks Green
  
  return COLOR_YELLOW; // Default
}

// Draw team name box with team colors
void drawTeamBox(const String& team, int x, int y, int width, int height) {
  uint16_t primaryColor = getTeamPrimaryColor(team);
  uint16_t secondaryColor = getTeamSecondaryColor(team);
  
  // Draw colored background box
  matrix.fillRect(x, y, width, height, primaryColor);
  
  // Draw team abbreviation in secondary color
  matrix.setTextColor(secondaryColor);
  matrix.setTextSize(1);
  matrix.setCursor(x + 2, y + 2);
  matrix.print(team);
}

// ===== OLYMPICS MEDAL FUNCTIONS =====

void getOlympicsMedals() {
  HTTPClient http;

  http.begin("https://www.whereig.com/olympics/winter-olympics/2026-winter-olympics-medal-table-milan-cortina.html");
  http.setTimeout(10000);

  int httpCode = http.GET();

  if (httpCode == 200) {
    WiFiClient* stream = http.getStreamPtr();
    String payload = stream->readString();

    Serial.print("Olympics payload size: ");
    Serial.println(payload.length());

    // Find "United States" in the HTML table
    int usaPos = payload.indexOf("United States");
    if (usaPos == -1) {
      Serial.println("USA not found in Olympics medal table");
      http.end();
      lastOlympicsUpdate = millis();
      return;
    }

    // After "United States", find the next 3 <td> values (gold, silver, bronze)
    int searchPos = usaPos;

    // Skip the country <td> closing tag
    int tdPos = payload.indexOf("<td>", searchPos);
    if (tdPos == -1 || tdPos > usaPos + 200) {
      Serial.println("Could not find gold medal <td>");
      http.end();
      lastOlympicsUpdate = millis();
      return;
    }

    // Parse gold
    tdPos += 4; // skip "<td>"
    int tdEnd = payload.indexOf("</td>", tdPos);
    if (tdEnd != -1) {
      usaGold = payload.substring(tdPos, tdEnd).toInt();
    }

    // Parse silver
    tdPos = payload.indexOf("<td>", tdEnd);
    if (tdPos != -1) {
      tdPos += 4;
      tdEnd = payload.indexOf("</td>", tdPos);
      if (tdEnd != -1) {
        usaSilver = payload.substring(tdPos, tdEnd).toInt();
      }
    }

    // Parse bronze
    tdPos = payload.indexOf("<td>", tdEnd);
    if (tdPos != -1) {
      tdPos += 4;
      tdEnd = payload.indexOf("</td>", tdPos);
      if (tdEnd != -1) {
        usaBronze = payload.substring(tdPos, tdEnd).toInt();
      }
    }

    Serial.print("Olympics medals - G:");
    Serial.print(usaGold);
    Serial.print(" S:");
    Serial.print(usaSilver);
    Serial.print(" B:");
    Serial.println(usaBronze);

    lastOlympicsUpdate = millis();
  } else {
    Serial.print("Olympics HTTP Error: ");
    Serial.println(httpCode);
  }

  http.end();
}

void displayOlympicsMedals() {
  matrix.fillScreen(0);
  matrix.setTextSize(1);

  // Row 1: Header
  matrix.setTextColor(COLOR_CYAN);
  matrix.setCursor(2, 1);
  matrix.print("USA MEDALS");

  // Row 2: Gold count
  uint16_t COLOR_GOLD = matrix.color565(255, 215, 0);
  matrix.setTextColor(COLOR_GOLD);
  matrix.setCursor(2, 11);
  matrix.print("G:");
  matrix.print(usaGold);

  // Silver count
  matrix.setTextColor(COLOR_WHITE);
  matrix.setCursor(26, 11);
  matrix.print("S:");
  matrix.print(usaSilver);

  // Bronze count
  matrix.setTextColor(COLOR_ORANGE);
  matrix.setCursor(50, 11);
  matrix.print("B:");
  matrix.print(usaBronze);

  // Row 3: Total
  matrix.setTextColor(COLOR_GREEN);
  matrix.setCursor(2, 21);
  matrix.print("Total: ");
  matrix.print(usaGold + usaSilver + usaBronze);

  matrix.show();
}

void getNFLScores() {
  HTTPClient http;
  
  http.begin("https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard");
  http.setTimeout(10000);

  int httpCode = http.GET();

  if (httpCode == 200) {
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
          
          // Adjust for timezone (handle day rollover)
          hour += saved_timezone;
          if (hour < 0) {
            hour += 24;
            day -= 1;
            if (day < 1) {
              month -= 1;
              if (month < 1) month = 12;
              // Approximate days in previous month
              int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
              day = daysInMonth[month - 1];
            }
          } else if (hour >= 24) {
            hour -= 24;
            day += 1;
          }
          
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