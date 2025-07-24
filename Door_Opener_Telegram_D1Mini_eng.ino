/*
 * ####################################################
 * ESP8266 D1 mini - Telegram Bot Intercom - Display  #
 * Door and staircase light control via Telegram Bot  #
 * Status display and log on OLED screen              #
 * WiFi and BOT_TOKEN setup via Web Portal            #
 * ####################################################
 *
 * This program turns an ESP8266 D1 mini into a remote control system
 * for an intercom/door opener via Telegram. It includes a web interface
 * for configuration, an OLED display for status, and a logging system for operations.
 *
 * Features:
 * - Door opening via physical button or Telegram command
 * - Staircase light control via Telegram command
 * - Real-time display on OLED
 * - WiFi and bot token setup via web page
 * - Operation logging (last 5 events)
 * - Access control for authorized chat IDs
 *
 * SETUP INSTRUCTIONS:
 * 1. Install these libraries via the Library Manager:
 *    - UniversalTelegramBot by Brian Lough (ESP8266 compatible)
 *    - ArduinoJson by Benoit Blanchon (v6.x)
 *    - Adafruit_GFX
 *    - Adafruit_SSD1306
 *    - ESP8266WiFi, ESP8266WebServer, DNSServer
 *
 * 2. On first boot or WiFi error, the device creates a "ESP_Config" WiFi network.
 *    Connect to it with PC/smartphone and go to 192.168.4.1 to enter SSID, password and BOT_TOKEN.
 *    (default password is "admin")
 *
 * 3. After configuration, the system restarts and connects automatically.
 *
 * 4. Use a 0.92 inch SSD1306 OLED display, 128x64 pixels.
 *
 * HARDWARE CONNECTIONS (for D1 mini):
 * - D5 (GPIO14)  -> Door Relay
 * - D8 (GPIO15)  -> Light Relay
 * - D4 (GPIO2)   -> Status LED (D1 built-in blue LED)
 * - D7 (GPIO13)  -> Buzzer (optional)
 * - D1 (GPIO5)   -> SCL OLED Display
 * - D2 (GPIO4)   -> SDA OLED Display
 * - D3 (GPIO0)   -> Door open button (NO=Normally Open) to GND
 * - D0 (GPIO16)  -> Config button (NO) to GND
 * - 3.3V/5V -> VCC of relays
 * - 3.3V VCC OLED Display
 * - GND     -> Common GND/OLED GND
 *
 */
 
// ====== INCLUDES AND DEFINITIONS ======
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>

// =================== DYNAMIC CONFIGURATION ===================
// Defining maximum size for strings in EEPROM
#define MAX_SSID_LEN 32
#define MAX_PASS_LEN 64
#define MAX_TOKEN_LEN 64
#define MAX_PASSCFG_LEN 32
#define MAX_USERS_LEN 128   // Authorized chat ID list
#define MAX_USERS 10        // Max number of users

// Structure to store configuration in EEPROM
struct Config {
  char ssid[MAX_SSID_LEN];           // WiFi SSID
  char password[MAX_PASS_LEN];       // WiFi Password
  char botToken[MAX_TOKEN_LEN];      // Telegram Bot Token
  char configPassword[MAX_PASSCFG_LEN]; // Web config page password
  char authorizedUsers[MAX_USERS_LEN]; // Authorized chat IDs, comma-separated
  bool configured;                   // True if configuration completed
};

Config userConfig; // Global config variable

#define EEPROM_SIZE sizeof(Config)

// ====== AUTHORIZED USERS ARRAY ======
long authorizedUsers[MAX_USERS];
int numAuthorizedUsers = 0;

// ========== PIN CONFIGURATION ==========
#define DOOR_RELAY D5        // GPIO14 <-- Door Relay HIGH activates door
#define LIGHT_RELAY D8       // GPIO15 <-- Light Relay HIGH activates light
#define LED_STATUS  D4       // GPIO2  <-- Built-in LED
#define BUZZER_PIN  D7       // GPIO13 <-- Buzzer
#define BUTTON_DOOR D3       // GPIO0  <-- Physical door open button
#define RESET_BUTTON_PIN D0      // GPIO16 <-- Config mode button

// ========== TIMING SETTINGS =========
const unsigned long DOOR_TIMEOUT = 1000;   // Door open for 1 second
const unsigned long LIGHT_TIMEOUT = 1000;  // Light on for 1 second
const int BOT_REQUEST_DELAY = 500;         // Check Telegram messages every 0.5 s

// ===================== OLED DISPLAY ======================
#define SCREEN_WIDTH 128 // OLED display width (in pixels)
#define SCREEN_HEIGHT 64 // OLED display height (in pixels)
#define OLED_RESET    -1 // Reset not necessary for SSD1306
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); // OLED display object

// =================== GLOBAL VARIABLES ====================
WiFiClientSecure client; // Client for secure connection
UniversalTelegramBot bot("", client); // Telegram bot (token will be set later)
unsigned long lastTimeBotRan; // Timestamp of the last Telegram polling
unsigned long doorOpenTime = 0; // Timestamp of the last door opening
unsigned long lightOpenTime = 0;    // Timestamp of the last time the light was turned on.

// =================== INTERRUPT VARIABLES =================
volatile bool buttonInterruptFlag = false; // Flag for the button interrupt
volatile unsigned long lastInterruptTime = 0; // Timestamp of the last interrupt
const unsigned long interruptDebounceDelay = 100; // Debounce time for the button

// =================== CONFIG BUTTON MODE ==================
unsigned long resetButtonPressTime = 0; // Tempo in cui è stato premuto il pulsante di reset
bool configModeRequested = false;       // Flag per entrare in modalità configurazione

// ============== DISPLAY CONNECTION STATUS ================
bool wifiOk = false;  // Flag for WiFi status
bool telegramOk = false; // Flag for Telegram status
String lastDisplayContent = ""; // Last content shown on the display

// ================= STRUCTURE FOR LOGGING =================
struct LogEntry {
  unsigned long timestamp; // Event timestamp
  long chatId;             // Telegram user chat ID
  String operation;        // Operation type (e.g. "DOOR_OPENED")
  String userName;         // Telegram username who performed the operation
};
LogEntry operationLog[5]; // Log of the last 5 transactions

// ============= ISR FUNCTION FOR THE BUTTON ==============
/**
 * @brief Physical button interrupt handler.
 *        Activates flag only after a debounce to avoid false triggers.
 */
void ICACHE_RAM_ATTR buttonISR() {
  unsigned long now = millis();
  // Debounce: accepts only if enough time has passed since the last trigger
  if (now - lastInterruptTime > interruptDebounceDelay) {
    buttonInterruptFlag = true;
    lastInterruptTime = now;
  }
}

// ============= CENTERED DISPLAY FUNCTIONS ================
/**
 * @brief Prints centered text on OLED display.
 *        Useful for headlines or main messages.
 * 
 * @param text Text to be printed.
 * @param y Y position of text.
 * @param textSize Text size (1x, 2x...).
 */void drawCenteredText(String text, int y, int textSize = 1) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(textSize);
  display.getTextBounds(text.c_str(), 0, y, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - w) / 2; // Calculate centered position
  display.setCursor(x, y);
  display.print(text);
  display.setTextSize(1); // Reset to standard size
}

// =================== DISPLAY FUNCTIONS ====================
/**
 * @brief Formats elapsed time in abbreviated readable format (e.g., "3m", "5h").
 *        Used in logs to show the last operation.
 */
String formatTimeAgoShort(unsigned long ago) {
  unsigned long seconds = ago / 1000;
  if (seconds < 60) return String(seconds) + "s ";
  unsigned long minutes = seconds / 60;
  if (minutes < 60) return String(minutes) + "m ";
  unsigned long hours = minutes / 60;
  if (hours < 24) return String(hours) + "h ";
  unsigned long days = hours / 24;
  return String(days) + "g ";
}

/**
 * @brief Returns a formatted line for the log on the OLED display.
 *        Shows user name, type of operation, and when it was performed.
 */
String getShortLogLine(int idx) {
  if (operationLog[idx].timestamp == 0) return "";
  String action;
  if (operationLog[idx].operation == "LIGHT_ON") action = "Light";
  else if (operationLog[idx].operation == "DOOR_BUTTON") action = "Door";
  else action = "Door";
  String name = operationLog[idx].userName;
  String ago = formatTimeAgoShort(millis() - operationLog[idx].timestamp);
  return name + " " + action + " " + ago;
}

/**
 * @brief Returns the status of the WiFi and Telegram connection for the display.
 */
String getConnStatusLine() {
  String w = wifiOk ? "WiFi:OK" : "WiFi:--";
  String t = telegramOk ? "TG:OK" : "TG:--";
  return w + " " + t;
}

/**
 * @brief Refresh OLED display with current information:
 *       - Title
 *       - Latest operations
 *       - Connection status
 */
void updateDisplay() {
  String line1 = "D1 Mini Door Opener";
  String line2 = getShortLogLine(0);
  String line3 = getShortLogLine(1);
  String line4 = getShortLogLine(2);   
  String line5 = getShortLogLine(3);
  String line6 = getConnStatusLine();

  String displayContent = line1 + "|" + line2 + "|" + line3 + "|" + line4 + "|" + line5 + "|" + line6;
  
  // Avoid unnecessary updates
  if (displayContent == lastDisplayContent) return;
  lastDisplayContent = displayContent;

  // Cleans and resets the display
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Print rows
  drawCenteredText(line1, 0, 1);
  display.setCursor(0, 16);
  display.println(line2);
  display.setCursor(0, 26); 
  display.println(line3);
  display.setCursor(0, 36); 
  display.println(line4);
  display.setCursor(0, 46); 
  display.println(line5);
  drawCenteredText(getConnStatusLine(), 57);
  display.display();
}

// ==================== EEPROM CONFIG ======================
/**
 * @brief Loads the configuration saved in EEPROM.
 *        If never configured, use default values.
 */
void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, userConfig);

  // Force string termination
  userConfig.ssid[MAX_SSID_LEN-1] = '\0';
  userConfig.password[MAX_PASS_LEN-1] = '\0';
  userConfig.botToken[MAX_TOKEN_LEN-1] = '\0';
  userConfig.configPassword[MAX_PASSCFG_LEN-1] = '\0';
  userConfig.authorizedUsers[MAX_USERS_LEN-1] = '\0';

  // If not configured, reset everything
  if (userConfig.configured != true) {
    memset(&userConfig, 0, sizeof(userConfig));
    userConfig.configured = false;
  }
  // Initialize default password if empty
  if (userConfig.configPassword[0] == '\0') {
    strncpy(userConfig.configPassword, "admin", MAX_PASSCFG_LEN-1);
  }
  EEPROM.end();
}

// ============ SAVE CONFIGURATION IN EEPROM =============
/**
 * @brief Saves current configuration to EEPROM.
 */
void saveConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(0, userConfig);
  EEPROM.commit(); // Actually writes the data
  EEPROM.end();
}

// ===== PARSING OF USERS STRING INTO ARRAY OF LONG =====
/**
 * @brief Converts string of authorized users to array of chat IDs.
 *        Each chat ID is separated by comma.
 */
void parseAuthorizedUsers() {
  numAuthorizedUsers = 0;
  char usersCopy[MAX_USERS_LEN];
  strncpy(usersCopy, userConfig.authorizedUsers, MAX_USERS_LEN-1);
  usersCopy[MAX_USERS_LEN-1] = '\0'; // Ensure string termination

  char* token = strtok(usersCopy, ",");
  while (token != NULL && numAuthorizedUsers < MAX_USERS) {
    authorizedUsers[numAuthorizedUsers] = atol(token);
    Serial.print("Authorized user uploaded: ");
    Serial.println(authorizedUsers[numAuthorizedUsers]);
    numAuthorizedUsers++;
    token = strtok(NULL, ",");
  }
  Serial.print("Total authorized users: ");
  Serial.println(numAuthorizedUsers);
}

// =============== WEB PORTAL CONFIGURATION ===============
/**
 * @brief Port on which the DNS server responds.
 *        Usually port 53 for standard DNS traffic.
 */
const byte DNS_PORT = 53;

/**
 * @brief DNSServer object to handle DNS requests during configuration mode.
 *        Redirects all requests to the ESP8266 IP.
 */
DNSServer dnsServer;

/**
 * @brief Web server that manages the configuration page.
 *        Listen on port 80.
 */
ESP8266WebServer server(80);

/**
 * @brief Flag indicating whether the user has entered the configuration password correctly.
 */
bool passwordOk = false;

// ======= EEPROM RESET FUNCTION =======
/**
 * @brief Resets the EEPROM completely by resetting all saved data.
 *        Useful in case of error or to restore default settings.
 */
 void resetEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0); // writes zero in each cell
  }
  EEPROM.commit(); // saves the data on the flash
  EEPROM.end();
}

/**
 * @brief HTTP request handler to reset the EEPROM.
 *        Call `resetEEPROM()` and restart ESP8266.
 */
void handleResetEEPROM() {
  resetEEPROM();
  server.send(200, "text/html", "<h1>EEPROM Reset!</h1><p>Restarting up...</p>.");
  delay(2000);
  ESP.restart(); // Reboots the device after reset
}

// ====== MAIN PAGE / LOGIN / CHANGE PASSWORD ======
/**
 * @brief Main handler of HTTP requests to the root `/`.
 * Shows the login page if not authenticated,
 * or the configuration page if authenticated.
 */
void handleRoot() {
  if (!passwordOk) {
    // Login page
    String html = R"rawliteral(
      <!DOCTYPE HTML>
      <html>
      <head>
        <title>Login Configuration</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <style>
          button, input[type=submit] {
            background-color: #2196F3;
            color: white;
            font-size: 12px;
            padding: 10px 20px;
            border: none;
            border-radius: 5px;
            margin-top: 10px;
            cursor: pointer;
          }
          button:active, input[type=submit]:active {
            background-color: #1769aa;
          }
          .red {
            background-color: #e53935 !important;
          }
        </style>
      </head>
      <body>
        <h1>Enter configuration password</h1>
        <form action="/login" method="post">
          <label for="cfgpass">Password:</label><br>
          <input type="password" id="cfgpass" name="cfgpass" required><br><br>
          <input type="submit" value="LogIn">
        </form>
        <br><br><br>
        <form action="/reset_eeprom" method="post" onsubmit="return confirm('Are you sure you want to clear all memory and return the system to factory settings?');">
          <button type="submit" class="red">Reset EEPROM (Factory Reset)</button>
        </form>
      </body>
      </html>
    )rawliteral";
    server.send(200, "text/html", html);
    return;
  }

  // Pagina di configurazione principale
  String html = R"rawliteral(
    <!DOCTYPE HTML>
    <html>
    <head>
      <title>ESP configuration</title>
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <style>
        button, input[type=submit] {
          background-color: #2196F3;
          color: white;
          font-size: 12px;
          padding: 10px 20px;
          border: none;
          border-radius: 5px;
          margin-top: 10px;
          cursor: pointer;
        }
        button:active, input[type=submit]:active {
          background-color: #1769aa;
        }
        a.button-link {
          display: inline-block;
          background-color: #2196F3;
          color: white !important;
          font-size: 12px;
          padding: 10px 20px;
          border: none;
          border-radius: 5px;
          margin-top: 10px;
          text-decoration: none;
          cursor: pointer;
        }
        a.button-link:active {
          background-color: #1769aa;
        }
      </style>
    </head>
    <body>
      <h1>D1 Mini Door Opener Configuration</h1>
      <form action="/save" method="post">
        <label for="ssid">SSID WiFi network:</label><br>
        <input type="text" id="ssid" name="ssid" value="%SSID%" required><br><br>
        <label for="pass">WiFi Network Password:</label><br>
        <input type="password" id="pass" name="pass" value="%PASS%" required><br><br>
        <label for="token">BOT_TOKEN Telegram:</label><br>
        <input type="text" id="token" name="token" value="%BOT_TOKEN%" required><br><br>
        <label for="users">Authorized users (comma-separated chat IDs):</label><br>
        <input type="text" id="users" name="users" value="%USERS%" required><br><br>
        <input type="submit" value="Save and Restart">
      </form>
      <br>
      <a href="/changepw" class="button-link">Change Configuration Password</a>
    </body>
    </html>
  )rawliteral";

  html.replace("%SSID%", String(userConfig.ssid));
  html.replace("%PASS%", String(userConfig.password));
  html.replace("%BOT_TOKEN%", String(userConfig.botToken));
  html.replace("%USERS%", String(userConfig.authorizedUsers));

  server.send(200, "text/html", html);
}

/**
 * @brief Handler of POST request for login to configuration page.
 *        Verifies password and allows login if correct.
 */
void handleLogin() {
  if (server.hasArg("cfgpass") && strcmp(server.arg("cfgpass").c_str(), userConfig.configPassword) == 0) {
    passwordOk = true;
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(403, "text/html", "<h1>Wrong configuration password!</h1>");
  }
}

/**
 * @brief POST request handler to save new configuration parameters.
 *        Update SSID, WiFi password, bot tokens and authorized users.
 */
void handleSave() {
  if (!passwordOk) {
    server.send(403, "text/html", "<h1>Unauthorized</h1>");
    return;
  }
  if (server.hasArg("ssid") && server.hasArg("pass") && server.hasArg("token") && server.hasArg("users")) {
    String newSsid = server.arg("ssid");
    String newPass = server.arg("pass");
    String newToken = server.arg("token");
    String newUsers = server.arg("users");

    memset(userConfig.ssid, 0, sizeof(userConfig.ssid));
    memset(userConfig.password, 0, sizeof(userConfig.password));
    memset(userConfig.botToken, 0, sizeof(userConfig.botToken));
    memset(userConfig.authorizedUsers, 0, sizeof(userConfig.authorizedUsers));

    strncpy(userConfig.ssid, newSsid.c_str(), sizeof(userConfig.ssid) - 1);
    strncpy(userConfig.password, newPass.c_str(), sizeof(userConfig.password) - 1);
    strncpy(userConfig.botToken, newToken.c_str(), sizeof(userConfig.botToken) - 1);
    strncpy(userConfig.authorizedUsers, newUsers.c_str(), sizeof(userConfig.authorizedUsers) - 1);

    userConfig.configured = true;
    saveConfig();
    server.send(200, "text/html", "<h1>Configuration Saved!</h1><p>Il dispositivo si riavvia...</p>");
    delay(2000);
    ESP.restart();
  } 
  else {
    server.send(400, "text/plain", "Missing parameters.");
  }
}

/**
 * @brief Handler for requests not found.
 *        Always redirects to root `/`.
 */
void handleNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// ======= CONFIGURATION PASSWORD CHANGE PAGE =======
/**
 * @brief Page to change configuration access password.
 */
void handleChangePassword() {
  String html = R"rawliteral(
    <!DOCTYPE HTML>
    <html>
    <head>
      <title>Change Password</title>
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <style>
        button, input[type=submit] {
          background-color: #2196F3;
          color: white;
          font-size: 12px;
          padding: 10px 20px;
          border: none;
          border-radius: 5px;
          margin-top: 10px;
          cursor: pointer;
        }
        button:active, input[type=submit]:active {
          background-color: #1769aa;
        }
        a.button-link {
          display: inline-block;
          background-color: #2196F3;
          color: white !important;
          font-size: 12px;
          padding: 10px 20px;
          border: none;
          border-radius: 5px;
          margin-top: 10px;
          text-decoration: none;
          cursor: pointer;
        }
        a.button-link:active {
          background-color: #1769aa;
        }
      </style>
    </head>
    <body>
      <h1>Change Password Configuration</h1>
      <form action="/changepw" method="post" onsubmit="return checkPwMatch();">
        <label for="oldpw">Current password:</label><br>
        <input type="password" id="oldpw" name="oldpw" required><br><br>
        <label for="newpw">New password:</label><br>
        <input type="password" id="newpw" name="newpw" required><br><br>
        <label for="newpw2">Repeat new password:</label><br>
        <input type="password" id="newpw2" name="newpw2" required><br><br>
        <input type="submit" value="Change Password">
      </form>
      <br>
      <a href="/" class="button-link">Back to configuration</a>
      <script>
        function checkPwMatch() {
          var pw1 = document.getElementById('newpw').value;
          var pw2 = document.getElementById('newpw2').value;
          if (pw1 !== pw2) {
            alert('The new passwords do not match!');
            return false;
          }
          return true;
        }
      </script>
    </body>
    </html>
  )rawliteral";
  server.send(200, "text/html", html);
}

/**
 * @brief POST manager to change configuration password.
 */
void handleChangePasswordPost() {
  if (!server.hasArg("oldpw") || !server.hasArg("newpw") || !server.hasArg("newpw2")) {
    server.send(400, "text/plain", "Missing parameters.");
    return;
  }

  String oldpw = server.arg("oldpw");
  String newpw = server.arg("newpw");
  String newpw2 = server.arg("newpw2");

  if (strcmp(oldpw.c_str(), userConfig.configPassword) != 0) {
    server.send(403, "text/html", "<h1>Current password incorrect!</h1>");
    return;
  }

  if (newpw != newpw2) {
    server.send(400, "text/html", "<h1>The new passwords do not match!</h1><a href=\"/changepw\" class=\"button-link\">Riprova</a>");
    return;
  }

  strncpy(userConfig.configPassword, newpw.c_str(), MAX_PASSCFG_LEN-1);
  saveConfig();

  server.send(200, "text/html", "<h1>Password updated!</h1><a href=\"/\" class=\"button-link\">Back to configuration</a>");
}

// ========== SETUP ACCESS POINT AND SERVER ==========
/**
 * @brief Configures the ESP8266 in access point (AP) mode.
 *        Starts the web server and DNS for initial configuration.
 */
void setupAP() {
  passwordOk = false; // Always ask for password when entering config!
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP_Config", "");
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  server.on("/", handleRoot);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/changepw", HTTP_GET, handleChangePassword);
  server.on("/changepw", HTTP_POST, handleChangePasswordPost);
  server.on("/reset_eeprom", HTTP_POST, handleResetEEPROM); // <-- handler reset
  server.onNotFound(handleNotFound);

  server.begin();

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText("MODALITA' CONFIG", 0, 1);
  drawCenteredText("SSID: ESP_Config", 16, 1);
  drawCenteredText("Apri 192.168.4.1", 32, 1);
  display.display();
}

// =================== DYNAMIC WIFI =======================
/**
 * @brief Tries to connect to the configured WiFi network.
 *        Attempts for up to 30 seconds (30 attempts of 1s).
 *        Refresh WiFi status and display.
 */
bool connectToWiFiAndCheck() {
  if (!userConfig.configured) return false; // Not configured: impossible

  WiFi.mode(WIFI_STA); // Set ESP8266 to Station mode
  WiFi.begin(userConfig.ssid, userConfig.password); // Start connection

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(1000); // Wait 1 second
    attempts++;  // Increase attempts counter
  }

  wifiOk = (WiFi.status() == WL_CONNECTED); // Update WiFi Status
  updateDisplay(); // Update the OLED display

  if (wifiOk) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address:");
    Serial.println(WiFi.localIP());
    Serial.print("Signal intensity: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    successBeep();
  } else {
    Serial.println("\nWiFi connection error!");
    errorBeep();
  }

  return wifiOk;
} 



// ======================== SETUP ==========================
/**
 * @brief Initial setup function of ESP8266.
 *        Initialize:
 *        - Serial
 *        - OLED display
 *        - Configuration from EEPROM
 *        - Parsing of authorized users.
 *        - I/O pins
 *        - WiFi connection
 *        - Telegram Bots
 *        - Logging
 *        - Telegram bot testing
 *        - Initial melody
 */
 void setup() {
  // ====== SERIAL INITIALIZATION ======
  Serial.begin(115200); // Serial communication speed
  //delay(1500);

  // ====== WELCOME TO SERIAL ======
  Serial.println("\n\n=================================");
  Serial.println("=== D1 mini Telegram Opener ===");
  Serial.println("=================================\n");
  Serial.println("System initialization...\n");

  // ====== OLED DISPLAY INITIALIZATION ======
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // 0x3C is the I2C address of the display
    Serial.println(F("SSD1306 not found!")); // Error if the display does not respond
  }
  display.clearDisplay(); // Cleans the display
  display.display(); // Show the screen

  // ====== LOAD CONFIGURATION FROM EEPROM ======
  loadConfig(); // Upload SSID, password, bot token, etc.

  // ========= PARSE AUTHORIZED USERS ===========
  parseAuthorizedUsers(); // Converts string to array of chat IDs

  // ====== PIN CONFIGURATION ======
  pinMode(DOOR_RELAY, OUTPUT); // Relay to open the door
  pinMode(LIGHT_RELAY, OUTPUT);  // Relay to turn on the light
  pinMode(LED_STATUS, OUTPUT); // Blue LED for status
  pinMode(BUZZER_PIN, OUTPUT); // Buzzer for sound feedback
  pinMode(BUTTON_DOOR, INPUT_PULLUP); // Physical door opener button with pull-up
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP); // Configuration reset button

  // ====== ATTACH INTERRUPT FOR THE BUTTON ======
  attachInterrupt(digitalPinToInterrupt(BUTTON_DOOR), buttonISR, FALLING); // Triggers interrupt on falling edge
  
  // ====== RESET RELAYS AND OUTPUT ======
  digitalWrite(DOOR_RELAY, LOW);  // Ensures that the door is closed
  digitalWrite(LIGHT_RELAY, LOW);   // Ensures that the light is off
  digitalWrite(LED_STATUS, LOW);  // Turn off status LED
  digitalWrite(BUZZER_PIN, LOW);  // Turn off buzzer

  // ====== CONNECTION TO WIFI ======
  if (!userConfig.configured || !connectToWiFiAndCheck()) {
    // If not configured or WiFi error, enter configuration mode
    setupAP();
    while (true) {
      dnsServer.processNextRequest(); // DNS Manager
      server.handleClient(); // HTTP Manager
      delay(10); // Small pause so as not to overload the processor
    }
  }

  // ====== CONFIGURA IL BOT TELEGRAM ======
  bot = UniversalTelegramBot(userConfig.botToken, client); // Set the bot token
  client.setInsecure(); // Disable certificate check for compatibility

  // ====== INITIALIZE LOG ======
  initializeLog(); // Reset the operation log

  // ====== TELEGRAM BOT TEST ======
  testTelegramBot(); // Verify the connection to the bot

  // ====== SYSTEM MESSAGE READY ======
  systemReady(); // Show initial status and play startup melody

  // ====== REFRESH THE DISPLAY ======
  updateDisplay(); // Show initial status on OLED display
}

// ======================== LOOP ===========================
/**
 * @brief Main function executed in loop.
 *        Handles:
 *        - Manual configuration mode (reset button).
 *        - Telegram polling.
 *        - Port/light timeout.
 *        - Flashing status LEDs.
 *        - WiFi connection control.
 *        - Physical door opener button.
 *        - OLED display update.
 */
void loop() {
  // ====== MANUAL CONFIGURATION MODE ======
 /**
   * If the reset button is pressed for more than 3 seconds,
   * enters the web configuration mode.
   */
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    if (resetButtonPressTime == 0) {
      resetButtonPressTime = millis();
    } else if (millis() - resetButtonPressTime > 3000 && !configModeRequested) {
      configModeRequested = true;

      // Confirmation sound signal
      tone(BUZZER_PIN, 1000, 500);
      delay(600);
      tone(BUZZER_PIN, 1500, 500);
      delay(600);
      noTone(BUZZER_PIN);

      // Start configuration mode
      setupAP();
      while (true) {
        dnsServer.processNextRequest(); // DNS Manager
        server.handleClient();          // Server web
        delay(10);                      // Small break to avoid overloading
      }
    }
  } else {
    // Resets the state of the button when released
    resetButtonPressTime = 0;
    configModeRequested = false;
  }

  // ====== POLLING TELEGRAM MESSAGES ======
  if (millis() > lastTimeBotRan + BOT_REQUEST_DELAY) {
    checkTelegramMessages();
    lastTimeBotRan = millis();
  }

  // ====== AUTOMATIC TIMEOUT MANAGEMENT ======
  handleTimeouts(); // Closes the door or turns off the light after timeout

  // ====== STATUS LED FLASHING ======
  blinkStatusLED(); // Status LED flashes regularly

  // ====== WIFI CONNECTION CONTROL ======
  checkWiFiConnection(); // Periodically check the connection

  // ====== PHYSICAL DOOR OPENER BUTTON CONTROL ======
  checkButtonOpenDoor(); // Checks whether the button has been pressed

  // ====== UPDATE OLED DISPLAY ======
  updateDisplay(); // Show current status on display
}

// ==================== SETUP FUNCTIONS =====================
/**
 * @brief Configures all pins used as input or output.
 *        Sets relays, LEDs, buttons, and attaches interrupt for physical button.
 */
void setupPins() {
  // ====== PIN CONFIGURATION ======
  pinMode(DOOR_RELAY, OUTPUT); // Relay to open the door
  pinMode(LIGHT_RELAY, OUTPUT);  // Relay to turn on the light
  pinMode(LED_STATUS, OUTPUT); // Blue LED for status
  pinMode(BUZZER_PIN, OUTPUT); // Buzzer for sound feedback
  pinMode(BUTTON_DOOR, INPUT_PULLUP); // Physical door opener button with pull-up
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP); // Configuration reset button


  // ====== ATTACH INTERRUPT FOR THE BUTTON ======
  attachInterrupt(digitalPinToInterrupt(BUTTON_DOOR), buttonISR, FALLING); // Triggers interrupt on falling edge

  // ====== RESET RELAYS AND OUTPUT ======
  digitalWrite(DOOR_RELAY, LOW);  // Ensures that the door is closed
  digitalWrite(LIGHT_RELAY, LOW);   // Ensures that the light is off
  digitalWrite(LED_STATUS, LOW);  // Turn off status LED
  digitalWrite(BUZZER_PIN, LOW);  // Turn off buzzer

  Serial.println("Configured Pins");
}

/**
 * @brief Tests the connection to the Telegram bot to check if the token is valid.
 *        Sends a positive or negative beep based on the outcome.
 */
void testTelegramBot() {
  Serial.print("Telegram bot connection test...");
  if (bot.getMe()) {
    Serial.println(" OK!");
    Serial.println("Bot ready to receive commands");
    telegramOk = true;
    successBeep();
  } else {
    Serial.println(" ERROR!");
    Serial.println("Check the bot token");
    telegramOk = false;
    errorBeep();
  }
  updateDisplay();
}

/**
 * @brief Function performed once at system startup.
 *        Shows welcome message on serial and OLED display.
 *        Plays an initial melody.
 */
void systemReady() {
  Serial.println("\n======== SYSTEM IS READY ========");
  Serial.println("Commands available to enter");
  Serial.println("in the Telegram BOT:");
  Serial.println("- /open  -> Opens the door");
  Serial.println("- /light  -> Turns on stair light");
  Serial.println("- /status -> Show system status");
  Serial.println("- /log   -> Show operation log");
  Serial.println("- /help  -> Show help");
  Serial.println("================================\n");

  digitalWrite(LED_STATUS, HIGH);
  confirmationBeep();
}

// =============== TELEGRAM MESSAGE MANAGEMENT ==============
/**
 * @brief Checks for new Telegram messages.
 *        Sends a request to the bot and updates the connection status.
 */
void checkTelegramMessages() {
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  telegramOk = (numNewMessages >= 0);
  updateDisplay();
  while (numNewMessages) {
    handleNewMessages(numNewMessages);
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    telegramOk = (numNewMessages >= 0);
    updateDisplay();
  }
}

/**
 * @brief Handles new messages received from the Telegram bot.
 *        Checks the user's authorization and calls the command processor.
 */
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    if (bot.messages[i].type == "message") {
      String chat_id = bot.messages[i].chat_id;
      String text = bot.messages[i].text;
      String from_name = bot.messages[i].from_name;
      long user_id = chat_id.toInt();

      if (!isAuthorizedUser(user_id)) {
        handleUnauthorizedUser(chat_id, from_name);
        continue;
      }
      processCommand(chat_id, text, from_name, user_id);
    } else if (bot.messages[i].type == "callback_query") {
      String chat_id = bot.messages[i].chat_id;
      String callback_data = bot.messages[i].text;
      String from_name = bot.messages[i].from_name;
      String callback_query_id = bot.messages[i].query_id;
      long user_id = chat_id.toInt();

      if (!isAuthorizedUser(user_id)) {
        bot.answerCallbackQuery(callback_query_id, "❌ Unauthorized");
        continue;
      }
      processCallback(chat_id, callback_data, from_name, user_id, callback_query_id);
    }
  }
}

/**
 * @brief Processes a text command received from Telegram.
 */
void processCommand(String chat_id, String text, String from_name, long user_id) {
  if (text == "/start") {
    sendWelcomeMessage(chat_id);
  }
  else if (text == "/open") {
    handleOpenDoor(chat_id, from_name, user_id);
  }
  else if (text == "/light") {
    handleTurnOnLight(chat_id, from_name, user_id);
  }
  else if (text == "/status") {
    handleStateSystem(chat_id);
  }
  else if (text == "/log") {
    handleShowLog(chat_id);
  }
  else if (text == "/help") {
    sendHelpMessage(chat_id);
  }
  else if (text == "/menu") {
    sendMainMenu(chat_id);
  }
  else {
    handleUnknownCommand(chat_id);
  }
}

/**
 * @brief Processes a command sent via inline Telegram buttons.
 */
void processCallback(String chat_id, String callback_data, String from_name, long user_id, String query_id) {
  if (callback_data == "OPEN_DOOR") {
    handleOpenDoorCallback(chat_id, from_name, user_id, query_id);
  }
  else if (callback_data == "LIGHT_ON") {
    handleLightOnCallback(chat_id, from_name, user_id, query_id);
  }
  else if (callback_data == "STATE_SYSTEM") {
    handleStateSystemCallback(chat_id, query_id);
  }
  else if (callback_data == "SHOW_LOG") {
    handleShowLogCallback(chat_id, query_id);
  }
  else if (callback_data == "HELP") {
    handleHelpCallback(chat_id, query_id);
  }
  else {
    bot.answerCallbackQuery(query_id, "❌ Unrecognized command");
  }
}

// ===================== COMMAND MANAGEMENT ==================
/**
 * @brief Sends a welcome message to the user using `/start`.
 */
void sendWelcomeMessage(String chat_id) {
  String welcome = "🏠 *Welcome to the Intercom System*\n\n";
  welcome += "The system is up and running and ready to use.\n\n";
  welcome += "Use the buttons below to control the system or type in commands manually. \n\n";
  welcome += "*Text commands available: *\n";
  welcome += "🚪 /open - Opens the front door\n";
  welcome += "💡 /light - Turns on stair light\n";
  welcome += "ℹ️ /status - System status\n";
  welcome += "📋 /log - Operations log\n";
  welcome += "🎛️ /menu - Show menu buttons\n";
  welcome += "❓ /help - Complete guide\n";
  welcome += "_Secure system with access control_";
  
  bot.sendMessage(chat_id, welcome, "Markdown");
  delay(500);
  sendMainMenu(chat_id);
}

// =================== DOOR OPENER MANAGEMENT ==================
/**
 * @brief Handles the request to open the door.
 *        Activates the door relay and logs the operation.
 *
 * @param chat_id   ID of the Telegram chat requesting the operation
 * @param from_name Name of the user who requested the operation
 * @param user_id   ID of the user who requested the operation
 */
void handleOpenDoor(String chat_id, String from_name, long user_id) {
  // Open the door by activating the relay
  openDoor();

  // Construct the message to notify the user
  String message = "🚪 *Open Door*\n\n";
  message += "✅ Command executed successfully\n";
  message += "⏰ The door has been OPENED\n";
  message += "👤 Requested by: " + from_name;

  // Send the notification message to the Telegram chat
  bot.sendMessage(chat_id, message, "Markdown");

  // Log the operation for audit purposes
  logOperation(user_id, "PORTA_APERTA", from_name);

  // Update the display to reflect the new state
  updateDisplay();

  // Output a confirmation to the serial monitor
  Serial.println("✅ Open door at the request of: " + from_name);

  // Provide an audible confirmation
  confirmationBeep();

  // Delay to allow the user to see the main menu
  delay(2000);

  // Send the main menu back to the user
  sendMainMenu(chat_id);
}

// ================= MANAGEMENT TURN ON LIGHT =================
/**
 * @brief Turn on the stair light and record the operation.
 */
void handleTurnOnLight(String chat_id, String from_name, long user_id) {
  lightOn();

  String message = "💡 *Light On*\n\n";
  message += "✅ Staircase light on\n";
  message += "⏰ It will automatically turn off in few seconds\n";
  message += "👤 Requested by:" + from_name;

  bot.sendMessage(chat_id, message, "Markdown");
  logOperation(user_id, "LIGHT_ON", from_name);
  updateDisplay();

  Serial.println("✅ Light turned on at the request of:" + from_name);
  confirmationBeep();

  delay(2000);
  sendMainMenu(chat_id);
}

// =========== HANDLE PHYSICAL DOOR OPENER BUTTON ==========
/**
 * @brief Checks whether the physical button has been pressed to open the door.
 */
void checkButtonOpenDoor() {
  if (buttonInterruptFlag) {
    buttonInterruptFlag = false;
    openDoorFromButton();
  }
}

// ================= DOOR OPENER FROM BUTTON =================
/**
 * @brief Operates the door via the physical button and records the event.
 */
void openDoorFromButton() {
  openDoor();
  logOperation(0, "DOOR_BUTTON", "Button");
  //portaApertaDaPulsante = true;
  Serial.println("✅ Door opened by physical button");
  confirmationBeep();
  updateDisplay();
}

// ============ DISPLAY SYSTEM STATUS ===========
/**
 * @brief Handles the `/status` command by sending the full system status via Telegram.
 *        Calls the `getDetailedSystemStatus()` function to get the formatted data.
 */
void handleStateSystem(String chat_id) {
  String status = getDetailedSystemStatus();
  bot.sendMessage(chat_id, status, "Markdown");
  delay(1000);
  sendMainMenu(chat_id);
}

// ====================== SHOW LOG =======================
/**
 * @brief Handles the `/log` command by sending via Telegram the log of the last operations.
 *        Calls the `getFormattedLog()` function to get the formatted data.
 */
void handleShowLog(String chat_id) {
  String logStr = getFormattedLog();
  bot.sendMessage(chat_id, logStr, "Markdown");
  delay(1000);
  sendMainMenu(chat_id);
}

// =================== HELP MESSAGE ===================
/**
 * @brief Handles the `/help` command by showing a complete guide to available commands.
 *        Includes information on the main commands, informational, security, and system usage.
 */
void sendHelpMessage(String chat_id) {
  String help = "❓ *Intercom System Control Guide*\n\n";
  help += "*Main Commands:*\n";
  help += "🚪 `/open` - Opens the door\n";
  help += "💡 `/light` - Turns on stair light\n";
  help += "*Information Commands:*\n";
  help += "ℹ️ `/status` - Full system status\n";
  help += "📋 `/log` - Latest operation logs\n";
  help += "❓ `/help` - This guide\n\n";
  help += "*Security:*\n";
  help += "🔒 Only authorized users can use the system\n";
  help += "📊 Only the last 5 transactions are recorded\n";
  help += "🔄 Automatic release Relay for safety";

  bot.sendMessage(chat_id, help, "Markdown");
  delay(1000);
  sendMainMenu(chat_id);
}

// ================== UNKNOWN COMMAND ==================
/**
 * @brief Responds to an unrecognized command by providing directions on how to proceed.
 *        Suggests the use of `/help` or `/menu`.
 */
void handleUnknownCommand(String chat_id) {
  String message = "❓ *Command Not Recognized*\n\n";
  message += "The entered command is invalid.\n";
  message += "Use /help to see all available commands or /menu for buttons.";
  bot.sendMessage(chat_id, message, "Markdown");
}

// ================ AUTHORIZATION CHECK ===============
/**
 * @brief Manages unauthorized access to the system.
 *        Sends an error message and an audible signal.
 */
void handleUnauthorizedUser(String chat_id, String from_name) {
  String message = "🚫 *Access Denied*\n\n";
  message += "You are not authorized to use this system.\n";
  message += "Contact the administrator to request access.";
  bot.sendMessage(chat_id, message, "Markdown");
  Serial.println("🚫 Access denied for: " + from_name + " (ID: " + chat_id + ")");
  errorBeep();
}

// ==================== MENUS AND CALLBACKS ====================
/**
 * @brief Sends a main menu with interactive buttons to control the system via Telegram.
 *        Allows you to:
 *        - Open the door
 *        - Turn on the light
 *        - View the status of the system
 *        - Show the operation log
 *        - Access the help
 */
void sendMainMenu(String chat_id) {
  String menuText = "🎛️ *Intercom Control Menu*\n\n";
  menuText += "Choose an action using the buttons below:";
  String keyboard = "[[{\"text\":\"🚪 Open Door\",\"callback_data\":\"OPEN_DOOR\"}],"
                    "[{\"text\":\"💡 Turn on Light\",\"callback_data\":\"LIGHT_ON\"}],"
                    "[{\"text\":\"ℹ️ System Status\",\"callback_data\":\"STATE_SYSTEM\"},"
                    "{\"text\":\"📋 Show Log\",\"callback_data\":\"SHOW_LOG\"}],"
                    "[{\"text\":\"❓ Help\",\"callback_data\":\"HELP\"}]]";
  bot.sendMessageWithInlineKeyboard(chat_id, menuText, "Markdown", keyboard);
}

/**
 * @brief Handles the callback of the “Open Port” button in the Telegram menu.
 * Open Port and logs the operation in the log.
 */
void handleOpenDoorCallback(String chat_id, String from_name, long user_id, String query_id) {
  openDoor();
  bot.answerCallbackQuery(query_id, "🚪 Door Open! Door Opener Pressed!");
  String message = "🚪 *Open Door!*\n\n";
  message += "✅ Command executed successfully\n";
  message += "⏰ The door has been opened!\n";
  message += "👤 Requested by: " + from_name;
  bot.sendMessage(chat_id, message, "Markdown");
  logOperation(user_id, "PORTA_APERTA", from_name);
  updateDisplay();
  Serial.println("✅ Open door at the request of: " + from_name);
  confirmationBeep();
  sendMainMenu(chat_id);
}

/**
 * @brief Handles the callback of the “Turn on Light” button in the Telegram menu.
 *        Turns on the light and records the operation in the log.
 */
void handleLightOnCallback(String chat_id, String from_name, long user_id, String query_id) {
  lightOn();
  bot.answerCallbackQuery(query_id, "💡 Light On. It will automatically turn off");
  String message = "💡 *Light On*\n\n";
  message += "✅ Staircase light on\n";
  message += "⏰ It will turn off automatically!\n";
  message += "👤 Requested by: " + from_name;
  bot.sendMessage(chat_id, message, "Markdown");
  logOperation(user_id, "LIGHT_ON", from_name);
  updateDisplay();
  Serial.println("✅ Light turned on at the request of: " + from_name);
  confirmationBeep();
  delay(1000);
  sendMainMenu(chat_id);
}

/**
 * @brief Handles the callback of the “System Status” button in the Telegram menu.
 *        Shows the complete system status.
 */
void handleStateSystemCallback(String chat_id, String query_id) {
  bot.answerCallbackQuery(query_id, "📊 Recovery system status...");
  String status = getDetailedSystemStatus();
  bot.sendMessage(chat_id, status, "Markdown");
  delay(1000);
  sendMainMenu(chat_id);
}

/**
 * @brief Handles the callback of the “Show Log” button in the Telegram menu.
 * Shows the last logged operations in the system.
 */
void handleShowLogCallback(String chat_id, String query_id) {
  bot.answerCallbackQuery(query_id, "📋 Retrieval log operations...");
  String logStr = getFormattedLog();
  bot.sendMessage(chat_id, logStr, "Markdown");
  delay(1000);
  sendMainMenu(chat_id);
}

/**
 * @brief Handles the callback of the “Help” button in the Telegram menu.
 *        Callbacks the full help on system commands.
 */
void handleHelpCallback(String chat_id, String query_id) {
  bot.answerCallbackQuery(query_id, "❓ Command guide...");
  sendHelpMessage(chat_id);
}



// ================ OPERATION LOGGING FUNCTIONS
/**
 * @brief Initializes the operation log with empty values.
 */
 void initializeLog() {
  for (int i = 0; i < 5; i++) {
    operationLog[i].timestamp = 0;
    operationLog[i].chatId = 0;
    operationLog[i].operation = "";
    operationLog[i].userName = "";
  }
}

/**
 * @brief Records a new operation in the operation log (log).
 *        Keeps the last 5 operations, moving the oldest ones to the bottom
 *        and adding the new operation to the top of the log.
 *
 * @param chatId ID of the Telegram chat of the user who performed the operation.
 * @param operation String describing the type of operation (e.g., “PORT_OPEN”).
 * @param userName Name of the user who performed the operation.
 */
void logOperation(long chatId, String operation, String userName) {
  // Moves each log element down one position,
  for (int i = 4; i > 0; i--) {
    operationLog[i] = operationLog[i - 1];
  }

  // Inserts the new operation in the first position of the log
  operationLog[0].timestamp = millis();     // Current timestamp (in milliseconds)
  operationLog[0].chatId = chatId;         // Chat ID of the user who performed the action.
  operationLog[0].operation = operation;   // Type of operation (e.g., “DOOR_OPEN”)
  operationLog[0].userName = userName;     // Name of the user who performed the action

  // Refresh the OLED display to show the updated log
  updateDisplay();
}

/**
 * @brief Returns a string formatted with the operation log,
 * useful for showing the log on Telegram or display.
 */
String getFormattedLog() {
  String logStr = "📋 *Operations Log*\n\n";
  int validEntries = 0;

  // Count how many operations are valid
  for (int i = 0; i < 5; i++) {
    if (operationLog[i].timestamp > 0) validEntries++;
  }

  // If there are no recorded transactions
  if (validEntries == 0) {
    logStr += "No operations recorded.";
    return logStr;
  }

  // Format operations to show date/time, type, user name
  for (int i = 0; i < validEntries; i++) {
    String timeAgo = formatTimeAgo(millis() - operationLog[i].timestamp);
    String emoji = "🚪";
    String operationName = "PORTA APERTA";

    // Change emoji and name if it is light
    if (operationLog[i].operation == "LIGHT_ON") {
      emoji = "💡";
      operationName = "LUCE ACCESA";
    }
    logStr += "• " + emoji + " " + operationName + "\n";
    logStr += "  👤 " + operationLog[i].userName + "\n";
    logStr += "  ⏰ " + timeAgo + "\n\n";
  }
  return logStr;
}

// ================ FUNCTIONS BY SYSTEM STATE =================
/**
 * @brief Returns a formatted string with the complete system state.
 *        Used for the `/status` command and the main menu.
 */
String getDetailedSystemStatus() {
  String status = "ℹ️ *Intercom System Status*\n\n";

  // Stato connessione WiFi
  status += "*Connectivity:*\n";
  status += "📶 WiFi: " + String(WiFi.status() == WL_CONNECTED ? "✅ Connected" : "❌  Disconnected") + "\n";
  status += "📡 Signal: " + String(WiFi.RSSI()) + " dBm\n\n";

  // Stato dispositivi (porta e luce)
  status += "*Devices:*\n";
  status += "🚪 Door: " + String(doorOpenTime > 0 ? "🟢 Open" : "🔴 Closed") + "\n";
  status += "💡 Light: " + String(lightOpenTime > 0 ? "🟢 Turned on" : "🔴 Turned off") + "\n\n";

  // Informazioni di sistema
  status += "*System:*\n";
  status += "⏱️ Uptime: " + formatUptime(millis()) + "\n";
  status += "🔋 Free memory: " + String(ESP.getFreeHeap()) + " bytes\n";
  status += "👥 Authorized users: " + String(numAuthorizedUsers);

  return status;
}

/**
 * @brief Formats the elapsed time in a readable format.
 *        Ex: “1d, 3h, 45m, 20s”
 */
String formatUptime(unsigned long milliseconds) {
  unsigned long seconds = milliseconds / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;

  String uptime = "";

  if (days > 0) uptime += String(days) + "d ";
  if (hours % 24 > 0) uptime += String(hours % 24) + "h ";
  if (minutes % 60 > 0) uptime += String(minutes % 60) + "m ";
  uptime += String(seconds % 60) + "s";

  return uptime;
}

/**
 * @brief Formats a time interval in readable format.
 * Ex: “3 minutes ago,” “2 hours ago,” etc.
 */
String formatTimeAgo(unsigned long ago) {
  unsigned long seconds = ago / 1000;
  if (seconds < 60) return String(seconds) + " seconds ago";
  unsigned long minutes = seconds / 60;
  if (minutes < 60) return String(minutes) + " minutes ago";
  unsigned long hours = minutes / 60;
  if (hours < 24) return String(hours) + " hours ago";
  unsigned long days = hours / 24;
  return String(days) + " days ago";
}

// =================== RELAY CONTROL AND TIMEOUT ===================
/**
 * @brief Opens the door by activating the corresponding relay.
 *        Sets the timestamp of the last opening.
 */
void openDoor() {
  digitalWrite(DOOR_RELAY, HIGH);
  doorOpenTime = millis();
  Serial.println("🚪 Open door");
}

/**
 * @brief Turns on the stair light by activating the corresponding relay.
 *        Sets the timestamp of the last power on.
 */
void lightOn() {
  digitalWrite(LIGHT_RELAY, HIGH);
  lightOpenTime = millis();
  Serial.println("💡 Light on");
}

/**
 * @brief Manages automatic timeouts:
 *        - Closes the door after a certain period.
 *        - Turns off the light after a certain period.
 */
void handleTimeouts() {
  // ====== PORT TIMEOUT ======
  if (doorOpenTime > 0 && millis() - doorOpenTime > DOOR_TIMEOUT) {
    digitalWrite(DOOR_RELAY, LOW);
    doorOpenTime = 0;
    Serial.println("🚪 Automatically closed door");
  }

  // ====== TIMEOUT LIGHT ======
  if (lightOpenTime > 0 && millis() - lightOpenTime > LIGHT_TIMEOUT) {
    digitalWrite(LIGHT_RELAY, LOW);
    lightOpenTime = 0;
    Serial.println("💡 Light off automatically");
  }
}

// =============== USER AUTHORIZATION CHECK ==============
/**
 * @brief Checks whether a chat ID is in the list of authorized users.
 *        Useful for restricting access to Telegram commands.
 */
bool isAuthorizedUser(long chat_id) {
  Serial.print("Authorization check for chat_id: ");
  Serial.println(chat_id);

  for (int i = 0; i < numAuthorizedUsers; i++) {
    Serial.print("Check against: ");
    Serial.println(authorizedUsers[i]);

    if (authorizedUsers[i] == chat_id) {
      return true; // User found
    }
  }
  return false; // User not found
}

// ================= SOUND FEEDBACK AND MELODIES =================
/**
 * @brief Plays a short positive "beep" to confirm the correct execution of an action.
 */
void confirmationBeep() {
  int melody[] = {587, 659, 523, 262, 392};
  int duration[] = {200, 200, 200, 200, 800};

  for (int i = 0; i < 5; i++) {
    tone(BUZZER_PIN, melody[i], duration[i]);
    delay(250);
  }

  noTone(BUZZER_PIN);
}

/**
 * @brief Beep signal of success (e.g., successful WiFi connection).
 */
void successBeep() {
  int melody[] = {262, 330, 392}; 
  int duration[] = {200, 200, 200};

  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, melody[i], duration[i]);
    delay(250);
  }

  noTone(BUZZER_PIN);
  delay(2000);
}

/**
 * @brief Audible error signal (e.g., incorrect password, access denied).
 */
void errorBeep() {
  tone(BUZZER_PIN, 1000);
  delay(100);
  noTone(BUZZER_PIN);
  delay(80);
  tone(BUZZER_PIN, 1500);
  delay(100);
  noTone(BUZZER_PIN);
}

// ==================== STATUS LED ====================
/**
 * @brief Flashes the built-in blue LED on the ESP8266 to indicate that the system is active.
 * LED flashes approximately every second.
 */
void blinkStatusLED() {
  static unsigned long lastBlink = 0;
  static bool ledState = false;
  if (millis() - lastBlink > 1000) {
    ledState = !ledState;
    digitalWrite(LED_STATUS, ledState ? HIGH : LOW);
    lastBlink = millis();
  }
}

// ================= CONNECTION CHECK =================
/**
 * @brief Checks WiFi connection periodically and attempts reconnection if necessary.
 * Update the status of the OLED display.
 */
void checkWiFiConnection() {
  static unsigned long lastCheck = 0;

  if (millis() - lastCheck > 30000) { // Every 30 seconds
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected, reconnect...");
      WiFi.reconnect(); // Attempts to reconnect
      wifiOk = false; // Update connection status
    } else {
      wifiOk = true; // Connection OK
    }

    updateDisplay(); // Update the OLED display
    lastCheck = millis(); // Update timestamp of the last check
  }
}




