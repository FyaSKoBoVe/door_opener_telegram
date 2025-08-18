/*
 * #########################################################
 * # ESP8266 D1 mini - Telegram Bot Door Opener - Display  #
 * # Door and staircase light control via Telegram Bot     #
 * # Status display and log on OLED screen                 #
 * # WiFi and BOT_TOKEN setup via Web Portal               #
 * #########################################################
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
 * 2. On first boot or WiFi error, the device 
 *    creates a "ESP_Config" WiFi network.
 *    Connect to it with PC/smartphone and go to 192.168.4.1 to enter SSID, password and BOT_TOKEN.
 *    (default password is "admin")
 *    Enter the SSID and BOT_TOKEN.
 *    Enter the authorized user IDs separated by a comma
 *    (e.g., 123456789,987654321)
 *    It is also possible to change the configuration password
 *    and save.
 *
 * 3. After configuration, the system restarts 
 *    and connects automatically.
 *
 * 4. Pressing the RESET button for three seconds returns to configuration mode.
 * From here, in addition to changing users, SSIDs, and
 * BOT_TOKENs, you can reset and delete all data, returning
 * to the initial configuration.
 *
 * 5. It uses a 0.92-inch SSD1306 OLED display
 * with 128x64 pixels.
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
 * For a more detailed description:
 * https://github.com/FyaSKoBoVe/door_opener_telegram
 * 
 * Author: FyaSKoBoVe
 * 
 * Year: 2025*
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

// ============= INTERRUPT SERVICE ROUTINE (ISR) FOR THE BUTTON ==============
/**
 * @brief Physical button interrupt handler.
 *        Activates flag only after a debounce to avoid false triggers.
 *        This function is called when the interrupt is triggered.
 *        It checks if enough time has passed since the last button press
 *        and sets the flag if it has.
 *
 *        Note: This function is called from interrupt context,
 *        so it should be as short as possible and not block.
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
 * @param textSize Text size (1x, 2x...). Default is 1x.
 */
void drawCenteredText(String text, int y, int textSize = 1) {
  int16_t x1, y1;
  uint16_t w, h;
  // Set temporary text size
  display.setTextSize(textSize);
  // Calculate the size of the text
  display.getTextBounds(text.c_str(), 0, y, &x1, &y1, &w, &h);
  // Calculate centered position
  int x = (SCREEN_WIDTH - w) / 2;
  // Set the cursor and print the text
  display.setCursor(x, y);
  display.print(text);
  // Reset to standard size to avoid affecting other displays
  display.setTextSize(1);
}

// =================== DISPLAY FUNCTIONS ====================
/**
 * @brief Formats elapsed time in abbreviated readable format (e.g., "3m", "5h").
 *        Used in logs to show the last operation.
 *
 * @param ago Time interval in milliseconds
 * @return Formatted string (e.g. "3m", "5h", "2 days")
 */
String formatTimeAgoShort(unsigned long ago) {
  unsigned long seconds = ago / 1000;
  if (seconds < 60) {
    // Less than 1 minute
    return String(seconds) + "s ";
  }
  unsigned long minutes = seconds / 60;
  if (minutes < 60) {
    // Less than 1 hour
    return String(minutes) + "m ";
  }
  unsigned long hours = minutes / 60;
  if (hours < 24) {
    // Less than 1 day
    return String(hours) + "h ";
  }
  unsigned long days = hours / 24;
  return String(days) + "g ";
}

/**
 * @brief Returns a formatted line for the log on the OLED display.
 *        Shows user name, type of operation, and when it was performed.
 *        Formatted as: "username action timeago"
 *        e.g. "John Light 3m" or "Jane Door 5h"
 *
 * @param idx Index of the log entry to be formatted (0-4)
 * @return A formatted line for the OLED display
 */
String getShortLogLine(int idx) {
  if (operationLog[idx].timestamp == 0) return "";
  String action;
  // Determine the type of action to show
  if (operationLog[idx].operation == "LIGHT_ON") action = "Light";
  else if (operationLog[idx].operation == "DOOR_BUTTON") action = "Door";
  else action = "Door";
  String name = operationLog[idx].userName;
  String ago = formatTimeAgoShort(millis() - operationLog[idx].timestamp);
  // Format the line for the OLED display
  return name + " " + action + " " + ago;
}

/**
 * @brief Returns the status of the WiFi and Telegram connection
 *        as a single string for the display.
 *
 * @return A string containing the status of the WiFi and Telegram connection
 *         in the format "WiFi:<status> TG:<status>".
 *         <status> can be "OK" for connected or "--" for not connected.
 */
String getConnStatusLine() {
  String w = wifiOk ? "WiFi:OK" : "WiFi:--";
  String t = telegramOk ? "TG:OK" : "TG:--";
  return w + " " + t;
}

/**
 * @brief Refresh OLED display with current information:
 *        - Title
 *        - Latest operations
 *        - Connection status
 * 
 * This function checks if the display content has changed
 * since the last update to avoid unnecessary refreshes.
 * It updates the display with the latest log lines and
 * connection status.
 */
void updateDisplay() {
  // Define the lines to display
  String line1 = "D1 Mini Door Opener";
  String line2 = getShortLogLine(0);
  String line3 = getShortLogLine(1);
  String line4 = getShortLogLine(2);
  String line5 = getShortLogLine(3);
  String line6 = getConnStatusLine();

  // Concatenate all lines to create a single content string
  String displayContent = line1 + "|" + line2 + "|" + line3 + "|" + line4 + "|" + line5 + "|" + line6;
  
  // Check if the display content has changed
  if (displayContent == lastDisplayContent) return;
  lastDisplayContent = displayContent;

  // Clear the display and set text properties
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Display the title centered on the first line
  drawCenteredText(line1, 0, 1);
  
  // Display the log lines
  display.setCursor(0, 16);
  display.println(line2);
  display.setCursor(0, 26); 
  display.println(line3);
  display.setCursor(0, 36); 
  display.println(line4);
  display.setCursor(0, 46); 
  display.println(line5);
  
  // Display the connection status centered at the bottom
  drawCenteredText(getConnStatusLine(), 57);
  
  // Update the display with new content
  display.display();
}

// ==================== EEPROM CONFIG ======================
/**
 * @brief Loads the configuration saved in EEPROM.
 *        If never configured, use default values.
 *
 * We use EEPROM to store the user configuration.  The configuration
 * is stored in a struct, and we use the EEPROM library to read and
 * write the entire struct at once.
 */
void loadConfig() {
  // Initialize the EEPROM library to use the full EEPROM size
  EEPROM.begin(EEPROM_SIZE);

  // Read the configuration from EEPROM into the userConfig struct
  EEPROM.get(0, userConfig);

  // Ensure all strings are null-terminated
  userConfig.ssid[MAX_SSID_LEN-1] = '\0';
  userConfig.password[MAX_PASS_LEN-1] = '\0';
  userConfig.botToken[MAX_TOKEN_LEN-1] = '\0';
  userConfig.configPassword[MAX_PASSCFG_LEN-1] = '\0';
  userConfig.authorizedUsers[MAX_USERS_LEN-1] = '\0';

  // If the configuration is not valid, reset everything
  if (userConfig.configured != true) {
    // Initialize the userConfig struct to its default values
    memset(&userConfig, 0, sizeof(userConfig));
    userConfig.configured = false;
  }
  // Initialize default password if empty
  if (userConfig.configPassword[0] == '\0') {
    // Default password is "admin"
    strncpy(userConfig.configPassword, "admin", MAX_PASSCFG_LEN-1);
  }
  // Commit the changes to the EEPROM
  EEPROM.end();
}

// ============ SAVE CONFIGURATION IN EEPROM =============
/**
 * @brief Saves current configuration to EEPROM.
 *
 * This function is used to save the current configuration
 * to the EEPROM. It is called after the user has edited
 * the configuration in the web interface.
 */
void saveConfig() {
  // Initialize the EEPROM library to use the full EEPROM size
  EEPROM.begin(EEPROM_SIZE);

  // Write the userConfig struct to EEPROM
  EEPROM.put(0, userConfig);

  // Commit the changes to the EEPROM
  EEPROM.commit();

  // Release the EEPROM
  EEPROM.end();
}

// ======= EEPROM RESET FUNCTION =======
/**
 * @brief Resets the EEPROM completely by resetting all saved data.
 *        Useful in case of error or to restore default settings.
 *
 * This function writes zeros to all cells in the EEPROM,
 * effectively clearing all saved data. It is useful in
 * case of error or when the user wants to restore the
 * default settings.
 */
void resetEEPROM() {
  // Start the EEPROM library
  EEPROM.begin(EEPROM_SIZE);

  // Loop through all cells in the EEPROM
  for (int i = 0; i < EEPROM_SIZE; i++) {
    // Write zero in each cell
    EEPROM.write(i, 0);
  }

  // Commit the changes to the EEPROM
  EEPROM.commit();

  // Release the EEPROM
  EEPROM.end();
}

// ===== PARSING OF USERS STRING INTO ARRAY OF LONG =====
/**
 * @brief Converts string of authorized users to array of chat IDs.
 *        Each chat ID is separated by comma.
 *
 * The parsing is done by creating a copy of the userConfig.authorizedUsers
 * string, and then using the strtok() function to split the string into
 * tokens.  Each token is converted to a long using the atol() function and
 * stored in the authorizedUsers array.
 */
void parseAuthorizedUsers() {
  numAuthorizedUsers = 0;
  char usersCopy[MAX_USERS_LEN];
  strncpy(usersCopy, userConfig.authorizedUsers, MAX_USERS_LEN-1);
  usersCopy[MAX_USERS_LEN-1] = '\0'; // Ensure string termination

  // Split the string into tokens
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

/**
 * @brief HTTP request handler to reset the EEPROM.
 *        This function clears all saved data by calling `resetEEPROM()`
 *        and then restarts the ESP8266 to apply changes.
 */
void handleResetEEPROM() {
  // Reset the EEPROM to clear all stored configurations
  resetEEPROM();
  
  // Send a response to the client indicating the EEPROM has been reset
  server.send(200, "text/html", "<h1>EEPROM Reset!</h1><p>Restarting up...</p>.");
  
  // Wait for a short delay to ensure the message is sent before restarting
  delay(2000);
  
  // Restart the ESP8266 to apply the reset changes
  ESP.restart();
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

  // Main configuration page
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
 *
 * @details
 *   - Check if the "cfgpass" parameter is present in the request.
 *   - Compare it with the stored configuration password.
 *   - If match, set the passwordOk flag to true and redirect to the
 *     configuration page using 302 status code.
 *   - If the password is wrong, send a 403 status code with an error
 *     message.
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
 * @brief Handler for POST requests to save new configuration parameters.
 *        Updates SSID, WiFi password, bot token, and authorized users.
 *        Only proceeds if the user has entered the correct password.
 *
 * @details
 *   - Checks if the password authentication was successful.
 *   - Retrieves new configuration values from the POST request.
 *   - Copies new values into the userConfig structure.
 *   - Marks the configuration as completed.
 *   - Saves the updated configuration to EEPROM.
 *   - Sends a success response and restarts the device after a delay.
 */
void handleSave() {
  // Check if the password authentication was successful
  if (!passwordOk) {
    // Send an unauthorized error response
    server.send(403, "text/html", "<h1>Unauthorized</h1>");
    return;
  }

  // Check if all required parameters are present in the POST request
  if (server.hasArg("ssid") && server.hasArg("pass") && server.hasArg("token") && server.hasArg("users")) {
    // Retrieve new configuration values from the POST request
    String newSsid = server.arg("ssid");
    String newPass = server.arg("pass");
    String newToken = server.arg("token");
    String newUsers = server.arg("users");

    // Clear the existing configuration fields
    memset(userConfig.ssid, 0, sizeof(userConfig.ssid));
    memset(userConfig.password, 0, sizeof(userConfig.password));
    memset(userConfig.botToken, 0, sizeof(userConfig.botToken));
    memset(userConfig.authorizedUsers, 0, sizeof(userConfig.authorizedUsers));

    // Copy new values into the userConfig structure
    strncpy(userConfig.ssid, newSsid.c_str(), sizeof(userConfig.ssid) - 1);
    strncpy(userConfig.password, newPass.c_str(), sizeof(userConfig.password) - 1);
    strncpy(userConfig.botToken, newToken.c_str(), sizeof(userConfig.botToken) - 1);
    strncpy(userConfig.authorizedUsers, newUsers.c_str(), sizeof(userConfig.authorizedUsers) - 1);

    // Mark the configuration as completed
    userConfig.configured = true;

    // Save the updated configuration to EEPROM
    saveConfig();

    // Send a success response and restart the device after a delay
    server.send(200, "text/html", "<h1>Configuration Saved!</h1><p>Il dispositivo si riavvia...</p>");
    delay(2000);
    ESP.restart();
  } else {
    // Send an error response if any parameter is missing
    server.send(400, "text/plain", "Missing parameters.");
  }
}

/**
 * @brief Handle requests not found.
 *        Always redirects to the root `/` page.
 *
 * @details
 *   - If a request is received that does not match any of the defined
 *     routes, this function is called to handle the request.
 *   - It sends a 302 status code and redirects the client to the
 *     root `/` page.
 */
void handleNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// ======= CONFIGURATION PASSWORD CHANGE PAGE =======
/**
 * @brief Page to change configuration access password.
 *
 * @details
 *   - The page is accessed from the root `/` page.
 *   - It checks that the new password and its repetition match.
 *   - It sends a POST request to the `/changepw` route.
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
 * @brief POST handler for changing the configuration password.
 * 
 * @details
 *   - Validates presence of required parameters: "oldpw", "newpw", "newpw2".
 *   - Checks if the current password matches the stored configuration password.
 *   - Validates that the new password and its repetition match.
 *   - Updates the configuration password and saves it to EEPROM if valid.
 *   - Sends appropriate HTTP responses based on the outcome.
 */
void handleChangePasswordPost() {
  // Ensure all required parameters are present
  if (!server.hasArg("oldpw") || !server.hasArg("newpw") || !server.hasArg("newpw2")) {
    server.send(400, "text/plain", "Missing parameters.");
    return;
  }

  // Retrieve parameters from the POST request
  String oldpw = server.arg("oldpw");
  String newpw = server.arg("newpw");
  String newpw2 = server.arg("newpw2");

  // Check if the provided current password is correct
  if (strcmp(oldpw.c_str(), userConfig.configPassword) != 0) {
    server.send(403, "text/html", "<h1>Current password incorrect!</h1>");
    return;
  }

// Validate that the new password entries match
if (newpw != newpw2) {
  server.send(400, "text/html",
    "<h1>The new passwords do not match!</h1>"
    "<a href=\"/changepw\" class=\"button-link\">Riprova</a>");
  return;
}

// Update the configuration password and save changes
strncpy(userConfig.configPassword, newpw.c_str(), MAX_PASSCFG_LEN - 1);
saveConfig();

// Send success response with a link back to the configuration page
server.send(200, "text/html",
  "<h1>Password updated!</h1>"
  "<a href=\"/\" class=\"button-link\">Back to configuration</a>");
}

// ========== SETUP ACCESS POINT AND SERVER ==========
/**
 * @brief Configures the ESP8266 in access point (AP) mode.
 *        Starts the web server and DNS for initial configuration.
 * @details
 *   - Set the ESP8266 in AP mode.
 *   - Start the DNS server.
 *   - Configure the web server to:
 *     - Handle the root URL with the handleRoot() function.
 *     - Handle the POST request to the "/login" URL with 
 *       the handleLogin() function.
 *     - Handle the POST request to the "/save" URL with 
 *       the handleSave() function.
 *     - Handle the GET and POST requests to the "/changepw" URL with the 
 *       handleChangePassword() and handleChangePasswordPost() functions.
 *     - Handle the POST request to the "/reset_eeprom" URL with the 
 *       handleResetEEPROM() function.
 *   - Set the NotFound handler to handle all other URLs.
 *   - Start the web server.
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
  server.on("/reset_eeprom", HTTP_POST, handleResetEEPROM); 
  server.onNotFound(handleNotFound);

  server.begin();

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText("CONFIG MODE", 0, 1);
  drawCenteredText("SSID: ESP_Config", 16, 1);
  drawCenteredText("Open 192.168.4.1", 32, 1);
  display.display();
}

// =================== DYNAMIC WIFI =======================
/**
 * @brief Tries to connect to the configured WiFi network.
 *        Attempts for up to 30 seconds (30 attempts of 1s).
 *        Refresh WiFi status and display.
 * 
 * @return True if connection is successful, false otherwise.
 */
bool connectToWiFiAndCheck() {
  // Return false if not configured
  if (!userConfig.configured) return false;

  WiFi.mode(WIFI_STA); // Set ESP8266 to Station mode
  WiFi.begin(userConfig.ssid, userConfig.password); // Start connection

  int attempts = 0; // Initialize attempts counter
  // Attempt to connect for a maximum of 30 seconds
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(1000); // Wait 1 second
    attempts++;  // Increment attempts counter
  }

  wifiOk = (WiFi.status() == WL_CONNECTED); // Update WiFi status flag
  updateDisplay(); // Refresh the OLED display with new status

  // Notify connection status via serial and sound feedback
  if (wifiOk) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal intensity: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    successBeep(); // Play success sound
  } else {
    Serial.println("\nWiFi connection error!");
    errorBeep(); // Play error sound
  }

  return wifiOk; // Return the connection status
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
  // Set the serial communication speed
  Serial.begin(115200);

  // ====== WELCOME TO SERIAL ======
  Serial.println("\n\n=================================");
  Serial.println("=== D1 mini Telegram Opener ===");
  Serial.println("=================================\n");
  Serial.println("System initialization...\n");

  // ====== OLED DISPLAY INITIALIZATION ======
  // Initialize the OLED display and show an error if not found
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 not found!"));
  }
  display.clearDisplay(); // Clean the display
  display.display(); // Show the screen

  // ====== LOAD CONFIGURATION FROM EEPROM ======
  // Load the configuration from EEPROM
  loadConfig();

  // ========= PARSE AUTHORIZED USERS ===========
  // Parse the authorized users from the configuration
  parseAuthorizedUsers();

  // ====== PIN CONFIGURATION ======
  pinMode(DOOR_RELAY, OUTPUT); // Relay to open the door
  pinMode(LIGHT_RELAY, OUTPUT);  // Relay to turn on the light
  pinMode(LED_STATUS, OUTPUT); // Blue LED for status
  pinMode(BUZZER_PIN, OUTPUT); // Buzzer for sound feedback
  pinMode(BUTTON_DOOR, INPUT_PULLUP); // Physical door opener button with pull-up
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP); // Configuration reset button

  // ====== ATTACH INTERRUPT FOR THE BUTTON ======
  // Attach interrupt to the button to detect a falling edge
  attachInterrupt(digitalPinToInterrupt(BUTTON_DOOR), buttonISR, FALLING);

  // ====== RESET RELAYS AND OUTPUT ======
  digitalWrite(DOOR_RELAY, LOW);  // Ensures that the door is closed
  digitalWrite(LIGHT_RELAY, LOW);   // Ensures that the light is off
  digitalWrite(LED_STATUS, LOW);  // Turn off status LED
  digitalWrite(BUZZER_PIN, LOW);  // Turn off buzzer

  // ====== CONNECTION TO WIFI ======
  // Connect to the configured WiFi network
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
  // Set the bot token and disable certificate check for compatibility
  bot = UniversalTelegramBot(userConfig.botToken, client);
  client.setInsecure(); // Disabilita controllo certificati per compatibilita' 

  // ====== INITIALIZE LOG ======
  // Reset the operation log
  initializeLog();

  // ====== TELEGRAM BOT TEST ======
  // Verify the connection to the bot
  testTelegramBot();

  // ====== SYSTEM MESSAGE READY ======
  // Show initial status and play startup melody
  systemReady();

  // ====== REFRESH THE DISPLAY ======
  // Show initial status on OLED display
  updateDisplay();
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
        dnsServer.processNextRequest(); // Process DNS requests
        server.handleClient();          // Handle web server requests
        delay(10);                      // Small break to avoid overloading 
      }                                 // the processor
    }
  } else {
    // Resets the state of the button when released
    resetButtonPressTime = 0;
    configModeRequested = false;
  }

  // ====== POLLING TELEGRAM MESSAGES ======
  /**
   * Polls for new Telegram messages at a regular interval.
   * Updates the last time the bot ran to ensure periodic checks.
   */
  if (millis() > lastTimeBotRan + BOT_REQUEST_DELAY) {
    checkTelegramMessages();
    lastTimeBotRan = millis();
  }

  // ====== AUTOMATIC TIMEOUT MANAGEMENT ======
  /**
   * Manages automatic timeouts for the door and light.
   * Closes the door or turns off the light after a predefined period.
   */
  handleTimeouts();

  // ====== STATUS LED FLASHING ======
  /**
   * Flashes the status LED to indicate system activity.
   * The LED flashes approximately every second.
   */
  blinkStatusLED();

  // ====== WIFI CONNECTION CONTROL ======
  /**
   * Periodically checks the WiFi connection status.
   * Attempts reconnection if the WiFi is disconnected.
   */
  checkWiFiConnection();

  // ====== PHYSICAL DOOR OPENER BUTTON CONTROL ======
  /**
   * Checks whether the physical button has been pressed to open the door.
   * If pressed, it triggers the door opening mechanism.
   */
  checkButtonOpenDoor();

  // ====== UPDATE OLED DISPLAY ======
  /**
   * Updates the OLED display with the current system status.
   * Provides real-time feedback to the user.
   */
  updateDisplay();
}

// ==================== SETUP FUNCTIONS =====================
/**
 * @brief Configures all pins used as input or output.
 *        Sets relays, LEDs, buttons, and attaches interrupt for physical button.
 *
 * This function configures all the pins used in the program.
 * It sets the relays for the door and light, LEDs for status,
 * buttons for user input, and attaches an interrupt to the
 * physical door opener button.
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
  /**
   * We attach an interrupt to the physical door opener button.
   * This allows the program to trigger an interrupt when the button is pressed.
   * The interrupt is set to trigger on a falling edge, which means that
   * the interrupt is triggered when the button is pressed (i.e., the voltage
   * on the pin goes from high to low).
   */
  attachInterrupt(digitalPinToInterrupt(BUTTON_DOOR), buttonISR, FALLING);

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
 *        It sets the `telegramOk` flag to `true` if the connection is successful,
 *        or `false` if the connection fails.
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
 *        Displays a welcome message via serial and OLED display.
 *        Plays an initial melody to indicate readiness.
 */
void systemReady() {
  // Print system ready header on serial monitor
  Serial.println("\n======== SYSTEM IS READY ========");

  // Display available commands
  Serial.println("Commands available to enter");
  Serial.println("in the Telegram BOT:");
  Serial.println("- /open  -> Opens the door");
  Serial.println("- /light  -> Turns on stair light");
  Serial.println("- /status -> Show system status");
  Serial.println("- /log   -> Show operation log");
  Serial.println("- /help  -> Show help");
  Serial.println("================================\n");

  // Turn on status LED to indicate system is ready
  digitalWrite(LED_STATUS, HIGH);
  
  // Play a confirmation beep melody
  confirmationBeep();
}

// =============== TELEGRAM MESSAGE MANAGEMENT ==============
/**
 * @brief Checks for new Telegram messages.
 *        Sends a request to the bot and updates the connection status.
 *        It also handles any new messages received and updates the display.
 */
void checkTelegramMessages() {
  // Get the number of new messages
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

  // Update the Telegram connection status flag
  telegramOk = (numNewMessages >= 0);

  // Update the display with the current connection status
  updateDisplay();

  // Process any new messages
  while (numNewMessages) {
    handleNewMessages(numNewMessages);

    // Get the number of new messages again
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    // Update the Telegram connection status flag
    telegramOk = (numNewMessages >= 0);

    // Update the display with the current connection status
    updateDisplay();
  }
}

/**
 * @brief Handles new messages received from the Telegram bot.
 *        Checks the user's authorization and calls the appropriate handler
 *        based on the message type (text or callback query).
 * 
 * @param numNewMessages The number of new messages received.
 */
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    // Handle standard text messages
    if (bot.messages[i].type == "message") {
      String chat_id = bot.messages[i].chat_id;
      String text = bot.messages[i].text;
      String from_name = bot.messages[i].from_name;
      long user_id = chat_id.toInt();

      // Check if the user is authorized
      if (!isAuthorizedUser(user_id)) {
        // Respond to unauthorized user
        handleUnauthorizedUser(chat_id, from_name); 
        continue; // Skip further processing for this message
      }
      // Process the command if user is authorized
      processCommand(chat_id, text, from_name, user_id);
    } 
    // Handle callback queries (e.g., button presses)
    else if (bot.messages[i].type == "callback_query") {
      String chat_id = bot.messages[i].chat_id;
      String callback_data = bot.messages[i].text;
      String from_name = bot.messages[i].from_name;
      String callback_query_id = bot.messages[i].query_id;
      long user_id = chat_id.toInt();

      // Check if the user is authorized
      if (!isAuthorizedUser(user_id)) {
        // Respond to unauthorized callback
        bot.answerCallbackQuery(callback_query_id, "Unauthorized"); 
        continue; // Skip further processing for this callback
      }
      // Process the callback if user is authorized
      processCallback(chat_id, callback_data, from_name, user_id, callback_query_id);
    }
  }
}

/**
 * @brief Processes a text command received from Telegram.
 *        Directs the command to the appropriate handler function.
 * 
 * @param chat_id   The ID of the Telegram chat where the command was received.
 * @param text      The command text received from the user.
 * @param from_name The name of the user who sent the command.
 * @param user_id   The ID of the user who sent the command.
 */
void processCommand(String chat_id, String text, String from_name, long user_id) {
  // Handle the /start command to send a welcome message
  if (text == "/start") {
    sendWelcomeMessage(chat_id);
  }
  // Handle the /open command to open the door
  else if (text == "/open") {
    handleOpenDoor(chat_id, from_name, user_id);
  }
  // Handle the /light command to turn on the light
  else if (text == "/light") {
    handleTurnOnLight(chat_id, from_name, user_id);
  }
  // Handle the /status command to show the system status
  else if (text == "/status") {
    handleStateSystem(chat_id);
  }
  // Handle the /log command to show the operations log
  else if (text == "/log") {
    handleShowLog(chat_id);
  }
  // Handle the /help command to send help information
  else if (text == "/help") {
    sendHelpMessage(chat_id);
  }
  // Handle the /menu command to display interactive menu buttons
  else if (text == "/menu") {
    sendMainMenu(chat_id);
  }
  // Handle unknown commands and guide the user to available options
  else {
    handleUnknownCommand(chat_id);
  }
}

/**
 * @brief Processes a command sent via inline Telegram buttons.
 *
 * @param chat_id     The ID of the Telegram chat where the button was pressed.
 * @param callback_data The type of button pressed (e.g. "OPEN_DOOR",  etc.).
 * @param from_name    The name of the user who pressed the button.
 * @param user_id      The ID of the user who pressed the button.
 * @param query_id     The ID of the callback query to respond to.
 */
void processCallback(String chat_id, String callback_data, String from_name, 
                     long user_id, String query_id) {
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
 * @brief Handles the `/start` command to send a welcome message.
 *
 * @details The welcome message contains a brief description of the system,
 *          available text commands, and a hint about the security features.
 */
void sendWelcomeMessage(String chat_id) {
  String welcome = "🏠 *Welcome to the Door Opener System*\n\n";
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
  
  // Send the message using Markdown formatting
  bot.sendMessage(chat_id, welcome, "Markdown");
  delay(500);
  // Send the menu buttons after the welcome message
  sendMainMenu(chat_id);
}

// =================== DOOR OPENER MANAGEMENT ==================
/**
 * @brief Handles the request to open the door.
 *        Activates the door relay and logs the operation.
 *
 * @details The function will send a confirmation message to the Telegram chat
 *          with the username of the user who requested the operation.
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
  logOperation(user_id, "OPEN_DOOR", from_name);

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
 *
 * This function is called when the user requests to turn on the stair light
 * via the Telegram bot.
 *
 * @param chat_id   ID of the Telegram chat requesting the operation
 * @param from_name Name of the user who requested the operation
 * @param user_id   ID of the user who requested the operation
 */
void handleTurnOnLight(String chat_id, String from_name, long user_id) {
  lightOn(); // Turn on the light by setting the corresponding relay

  // Construct the message to notify the user
  String message = "💡 *Light On*\n\n";
  message += "✅ Staircase light on\n";
  message += "⏰ It will automatically turn off in few seconds\n";
  message += "👤 Requested by: " + from_name;

  // Send the notification message to the Telegram chat
  bot.sendMessage(chat_id, message, "Markdown");

  // Log the operation for audit purposes
  logOperation(user_id, "LIGHT_ON", from_name);

  // Update the display to reflect the new state
  updateDisplay();

  // Output a confirmation to the serial monitor
  Serial.println("✅ Light turned on at the request of: " + from_name);

  // Provide an audible confirmation
  confirmationBeep();

  // Delay to allow the user to see the main menu
  delay(2000);

  // Send the main menu back to the user
  sendMainMenu(chat_id);
}

// ==================== HANDLE PHYSICAL DOOR OPENER BUTTON ====================
/**
 * @brief Monitors the state of the physical button to open the door.
 * 
 * This function checks if the physical button interrupt flag has been set,
 * indicating that the button was pressed. If so, it resets the flag and 
 * triggers the door opening sequence through the `openDoorFromButton()` 
 * function.
 */
void checkButtonOpenDoor() {
  // Check if the button interrupt flag is set, indicating a button press
  if (buttonInterruptFlag) {
    // Reset the interrupt flag to detect future button presses
    buttonInterruptFlag = false;
    
    // Initiate the door opening process via the physical button
    openDoorFromButton();
  }
}

// ================= DOOR OPENER FROM BUTTON =================
/**
 * @brief Operates the door via the physical button and records the event.
 *
 * This function is called by the `checkButtonOpenDoor()` function when the
 * physical button interrupt flag is set, indicating that the button was pressed.
 * It resets the flag and triggers the door opening sequence by calling the
 * `openDoor()` function. The operation is also logged with the user "Button"
 * to identify the source of the command.
 */
void openDoorFromButton() {
  // Reset the interrupt flag to detect future button presses
  buttonInterruptFlag = false;

  // Open the door by activating the relay
  openDoor();

  // Log the operation for audit purposes
  logOperation(0, "DOOR_BUTTON", "Button");

  // Output a confirmation to the serial monitor
  Serial.println("✅ Door opened by physical button");

  // Provide an audible confirmation
  confirmationBeep();

  // Update the display to reflect the new state
  updateDisplay();
}

// ============ DISPLAY SYSTEM STATUS ===========
/**
 * @brief Handles the `/status` command by sending the full system status 
 via Telegram.
 *        Calls the `getDetailedSystemStatus()` function to get 
 the formatted data.
 *
 * @param chat_id ID of the Telegram chat requesting the operation
 */
void handleStateSystem(String chat_id) {
  // Get the formatted system status
  String status = getDetailedSystemStatus();

  // Send the system status to the Telegram chat
  bot.sendMessage(chat_id, status, "Markdown");

  // Delay for 1 second to allow the user to see the main menu
  delay(1000);

  // Send the main menu back to the user
  sendMainMenu(chat_id);
}

// ====================== SHOW LOG =======================
/**
 * @brief Handles the `/log` command by sending via Telegram the log of 
 *        the last operations.
 *        Calls the `getFormattedLog()` function to get the formatted data.
 *
 * @param chat_id ID of the Telegram chat requesting the operation
 */
void handleShowLog(String chat_id) {
  // Get the formatted log string
  String logStr = getFormattedLog();

  // Send the log string to the Telegram chat
  bot.sendMessage(chat_id, logStr, "Markdown");

  // Delay for 1 second to allow the user to see the main menu
  delay(1000);

  // Send the main menu back to the user
  sendMainMenu(chat_id);
}

// =================== HELP MESSAGE ===================
/**
 * @brief Handles the `/help` command by showing a complete guide to 
 *        all available commands.
 *        This includes main commands, informational commands, 
 *        security guidelines, and system usage.
 *
 * @param chat_id ID of the Telegram chat where the help message should be sent.
 */
void sendHelpMessage(String chat_id) {
  // Construct the help message with details on available 
  // commands and system guidelines
  String help = "❓ *Door Opener System Control Guide*\n\n";
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

  // Send the help message using Markdown formatting
  bot.sendMessage(chat_id, help, "Markdown");

  // Delay to allow the user to read the help message before showing
  //  the main menu
  delay(1000);

  // Send the main menu to provide quick access to system controls
  sendMainMenu(chat_id);
}

// ================== UNKNOWN COMMAND ==================
/**
 * @brief Responds to an unrecognized command by providing directions on 
 *        how to proceed.
 * 
 * @details Suggests the use of `/help` to display all available 
 *          commands or `/menu` to access the interactive menu with buttons.
 *
 * @param chat_id ID of the Telegram chat where the unknown command was received.
 */
void handleUnknownCommand(String chat_id) {
  // Construct the message to inform the user about the unknown command
  String message = "❓ *Command Not Recognized*\n\n";
  message += "The entered command is invalid.\n";
  message += "Use /help to see all available commands or /menu for buttons.";

  // Send the message using Markdown formatting to the user
  bot.sendMessage(chat_id, message, "Markdown");
}

// ================ AUTHORIZATION CHECK ===============
/**
 * @brief Manages unauthorized access to the system.
 *        Sends an error message and an audible signal.
 *        It is called whenever a user not in the authorized list
 *        attempts to execute a command.
 * @param chat_id ID of the Telegram chat requesting the operation
 * @param from_name Username of the Telegram user requesting the operation
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
 * @brief Sends a main menu with interactive buttons to control 
 *        the system via Telegram.
 *        Allows you to:
 *        - Open the door
 *        - Turn on the light
 *        - View the status of the system
 *        - Show the operation log
 *        - Access the help
 * @param chat_id Telegram chat ID where the menu will be sent
 */
void sendMainMenu(String chat_id) {
  String menuText = "🎛️ *Door Opener Control Menu*\n\n";
  menuText += "Choose an action using the buttons below:";

  // Construct the inline keyboard with the available actions
  String keyboard = "[[{\"text\":\"🚪 Open Door\",\"callback_data\":\"OPEN_DOOR\"}],"
                    "[{\"text\":\"💡 Turn on Light\",\"callback_data\":\"LIGHT_ON\"}],"
                    "[{\"text\":\"ℹ️ System Status\",\"callback_data\":\"STATE_SYSTEM\"},"
                    "{\"text\":\"📋 Show Log\",\"callback_data\":\"SHOW_LOG\"}],"
                    "[{\"text\":\"❓ Help\",\"callback_data\":\"HELP\"}]]";

  // Send the message using Markdown and the inline keyboard
  bot.sendMessageWithInlineKeyboard(chat_id, menuText, "Markdown", keyboard);
}

/**
 * @brief Handles the callback of the “Open Door” button in the Telegram menu.
 *        Opens the door by activating the relay and logs the operation 
 *        in the system log.
 *
 * @param chat_id   Telegram chat ID where the button was pressed
 * @param from_name Username of the Telegram user who pressed the button
 * @param user_id   Telegram user ID who pressed the button
 * @param query_id  Telegram callback query ID to respond to
 */
void handleOpenDoorCallback(String chat_id, String from_name, 
                            long user_id, String query_id) {
  // Open the door by activating the relay
  openDoor();

  // Answer the callback query with a confirmation message
  bot.answerCallbackQuery(query_id, "🚪 Door Open! Door Opener Pressed!");

  // Construct the message to notify the user
  String message = "🚪 *Open Door!*\n\n";
  message += "✅ Command executed successfully\n";
  message += "⏰ The door has been opened!\n";
  message += "👤 Requested by: " + from_name;

  // Send the notification message to the Telegram chat
  bot.sendMessage(chat_id, message, "Markdown");

  // Log the operation for audit purposes
  logOperation(user_id, "OPEN_DOOR", from_name);

  // Update the display to reflect the new state
  updateDisplay();

  // Output a confirmation to the serial monitor
  Serial.println("✅ Open door at the request of: " + from_name);

  // Provide an audible confirmation
  confirmationBeep();

  // Send the main menu back to the user
  sendMainMenu(chat_id);
}

/**
 * @brief Handles the callback of the "Turn on Light" button in the Telegram menu.
 *        Turns on the light and records the operation in the log.
 * @param chat_id   Telegram chat ID where the button was pressed
 * @param from_name Username of the Telegram user who pressed the button
 * @param user_id   Telegram user ID who pressed the button
 * @param query_id  Telegram callback query ID to respond to
 */
void handleLightOnCallback(String chat_id, String from_name, 
                          long user_id, String query_id) {
  // Turn on the light by setting the relay
  lightOn();

  // Answer the callback query with a confirmation message
  bot.answerCallbackQuery(query_id, "💡 Light On. It will automatically turn off");

  // Construct the message to notify the user
  String message = "💡 *Light On*\n\n";
  message += "✅ Staircase light on\n";
  message += "⏰ It will turn off automatically!\n";
  message += "👤 Requested by: " + from_name;

  // Send the notification message to the Telegram chat
  bot.sendMessage(chat_id, message, "Markdown");

  // Log the operation for audit purposes
  logOperation(user_id, "LIGHT_ON", from_name);

  // Update the display to reflect the new state
  updateDisplay();

  // Output a confirmation to the serial monitor
  Serial.println("✅ Light turned on at the request of: " + from_name);

  // Provide an audible confirmation
  confirmationBeep();

  // Send the main menu back to the user
  delay(1000);
  sendMainMenu(chat_id);
}

/**
 * @brief Handles the callback of the “System Status” button in the Telegram menu.
 *        Retrieves and shows the complete system status to the user.
 *
 * @param chat_id  The ID of the Telegram chat where the request originated.
 * @param query_id The ID of the callback query to respond to.
 */
void handleStateSystemCallback(String chat_id, String query_id) {
  // Acknowledge the callback query to inform the user that the request is being processed
  bot.answerCallbackQuery(query_id, "📊 Retrieving system status...");

  // Get the detailed system status as a formatted string
  String status = getDetailedSystemStatus();

  // Send the system status back to the Telegram chat using Markdown formatting
  bot.sendMessage(chat_id, status, "Markdown");

  // Delay for a second to ensure the user has time to read 
  // the status before displaying the menu again
  delay(1000);

  // Send the main menu back to the user for further actions
  sendMainMenu(chat_id);
}

/**
 * @brief Handles the callback of the “Show Log” button in the Telegram menu.
 *        Shows the last logged operations in the system.
 * @param chat_id  The ID of the Telegram chat where the request originated.
 * @param query_id The ID of the callback query to respond to.
 */
void handleShowLogCallback(String chat_id, String query_id) {
  // Acknowledge the callback query to inform the user that the request is being processed
  bot.answerCallbackQuery(query_id, "📋 Retrieval log operations...");

  // Get the formatted log string
  String logStr = getFormattedLog();

  // Send the log string to the Telegram chat using Markdown formatting
  bot.sendMessage(chat_id, logStr, "Markdown");

  // Delay for a second to ensure the user has time to read 
  // the log before displaying the menu again
  delay(1000);

  // Send the main menu back to the user for further actions
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
 *
 * @details This function sets all the values of the operation log structure
 *          to empty values to ensure that the log is clean and ready to
 *          be used.
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
 * @param chatId   ID of the Telegram chat of the user who performed the operation.
 * @param operation String describing the type of operation (e.g., “DOOR_OPENED”).
 * @param userName  Name of the user who performed the operation.
 */
void logOperation(long chatId, String operation, String userName) {
  // Move each log element down one position,
  // effectively shifting the oldest entries to the bottom of the log.
  for (int i = 4; i > 0; i--) {
    operationLog[i] = operationLog[i - 1];
  }

  // Insert the new operation in the first position of the log.
  operationLog[0].timestamp = millis();     // Current timestamp (in milliseconds)
  operationLog[0].chatId = chatId;         // Chat ID of the user who performed the action.
  operationLog[0].operation = operation;   // Type of operation (e.g., “DOOR_OPENED”)
  operationLog[0].userName = userName;     // Name of the user who performed the action

  // Refresh the OLED display to show the updated log.
  updateDisplay();
}

/**
 * @brief Formats the operation log into a string
 *        useful for showing the log on Telegram or display.
 *
 * @details This function takes the operation log and formats it into a string
 *          that can be sent to the Telegram chat or displayed on the OLED screen.
 *          The string includes the date, time, type of operation, and the user name.
 *          Each operation is separated by a newline and indented for readability.
 *          If there are no recorded transactions, the function returns a string
 *          indicating that there are no operations to show.
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
    String operationName = "DOOR OPENED";

    // Change emoji and name if it is light
    if (operationLog[i].operation == "LIGHT_ON") {
      emoji = "💡";
      operationName = "LIGHT ON";
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
 * 
 * @details This function compiles a detailed status report of the system,
 *          including WiFi connectivity, device states (door and light),
 *          and general system information. It is used for the `/status` 
 *          command and the main menu display.
 *
 * @return A string containing the formatted system status for display or messaging.
 */
String getDetailedSystemStatus() {
  // Initialize the status message header
  String status = "ℹ️ *Door Opener System Status*\n\n";

  // Add WiFi connectivity status
  status += "*Connectivity:*\n";
  status += "📶 WiFi: " + String(WiFi.status() == WL_CONNECTED ? "✅ Connected" : "❌ Disconnected") + "\n";
  status += "📡 Signal: " + String(WiFi.RSSI()) + " dBm\n\n";

  // Add device statuses (door and light)
  status += "*Devices:*\n";
  status += "🚪 Door: " + String(doorOpenTime > 0 ? "🟢 Open" : "🔴 Closed") + "\n";
  status += "💡 Light: " + String(lightOpenTime > 0 ? "🟢 Turned on" : "🔴 Turned off") + "\n\n";

  // Add general system information
  status += "*System:*\n";
  status += "⏱️ Uptime: " + formatUptime(millis()) + "\n";
  status += "🔋 Free memory: " + String(ESP.getFreeHeap()) + " bytes\n";
  status += "👥 Authorized users: " + String(numAuthorizedUsers);

  // Return the full status message
  return status;
}

/**
 * @brief Formats the elapsed time in a readable format.
 *
 * @details This function formats the elapsed time from milliseconds to
 *          a human-readable format. The format is "1d, 3h, 45m, 20s"
 *          and it shows the most significant units first.
 *
 * @param milliseconds The elapsed time in milliseconds.
 *
 * @return A string containing the formatted elapsed time.
 */
String formatUptime(unsigned long milliseconds) {
  unsigned long seconds = milliseconds / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;

  String uptime = "";

  if (days > 0) uptime += String(days) + "d "; // Show days
  if (hours % 24 > 0) uptime += String(hours % 24) + "h "; // Show hours
  if (minutes % 60 > 0) uptime += String(minutes % 60) + "m "; // Show minutes
  uptime += String(seconds % 60) + "s"; // Show seconds

  return uptime;
}

/**
 * @brief Formats a time interval in a readable format.
 *
 * @details Formats a time interval in a human-readable format, such as
 *          "3 minutes ago," "2 hours ago," etc.
 *
 * @param ago The time interval in milliseconds.
 *
 * @return A string containing the formatted time interval.
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
 *
 * @details This function is called when a door opening command is received.
 *          It sets the timestamp of the last opening, which is used to
 *          determine when the door should be closed again.
 */
void openDoor() {
  digitalWrite(DOOR_RELAY, HIGH);
  doorOpenTime = millis();
  Serial.println("🚪 Open door");
}

/**
 * @brief Turns on the stair light by activating the corresponding relay.
 *        Sets the timestamp of the last power on.
 *
 * @details This function is called when a light on command is received.
 *          It sets the timestamp of the last power on, which is used to
 *          determine when the light should be turned off again.
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
 * @details This function is called periodically to check if the door or
 *          light have been open/active for too long and should be closed/off.
 */
void handleTimeouts() {
  // ====== PORT TIMEOUT ======
  // If the door has been open for the timeout period, close it
  if (doorOpenTime > 0 && millis() - doorOpenTime > DOOR_TIMEOUT) {
    digitalWrite(DOOR_RELAY, LOW);
    doorOpenTime = 0;
    Serial.println("🚪 Automatically closed door");
  }

  // ====== TIMEOUT LIGHT ======
  // If the light has been on for the timeout period, turn it off
  if (lightOpenTime > 0 && millis() - lightOpenTime > LIGHT_TIMEOUT) {
    digitalWrite(LIGHT_RELAY, LOW);
    lightOpenTime = 0;
    Serial.println("💡 Light off automatically");
  }
}

// =============== USER AUTHORIZATION CHECK ==============
/**
 * @brief Verifies if a chat ID belongs to the list of authorized users.
 * 
 * @details This function iterates through the authorizedUsers array, 
 *          checking if the provided chat_id matches any stored chat ID.
 *          It is used to restrict access to certain Telegram commands.
 * 
 * @param chat_id The chat ID to be checked against the authorized list.
 * @return true if the chat ID is authorized, false otherwise.
 */
bool isAuthorizedUser(long chat_id) {
  Serial.print("Authorization check for chat_id: ");
  Serial.println(chat_id);

  // Iterate over each authorized user and check for a match
  for (int i = 0; i < numAuthorizedUsers; i++) {
    Serial.print("Check against: ");
    Serial.println(authorizedUsers[i]);

    if (authorizedUsers[i] == chat_id) {
      return true; // User is authorized
    }
  }
  
  return false; // User is not authorized
}

// ================= SOUND FEEDBACK AND MELODIES =================
/**
 * @brief Plays a short positive "beep" to confirm the correct execution of an action.
 *
 * @details This function is called when a command is successfully executed.
 *          It plays a short and simple melody to provide an audible feedback.
 */
void confirmationBeep() {
  // Melody for the confirmation beep
  int melody[] = {587, 659, 523, 262, 392};
  int duration[] = {200, 200, 200, 200, 800};

  // Play the melody
  for (int i = 0; i < 5; i++) {
    tone(BUZZER_PIN, melody[i], duration[i]);
    delay(250);
  }

  // Turn off the buzzer
  noTone(BUZZER_PIN);
}

/**
 * @brief Beep signal of success (e.g., successful WiFi connection).
 *
 * @details This function plays a short melody to indicate
 *          successful completion of an action, such as connecting
 *          to WiFi. The melody consists of three notes played in
 *          sequence. After the melody, the buzzer is turned off
 *          and a delay is added before exiting the function.
 */
void successBeep() {
  // Define melody notes and their corresponding durations
  int melody[] = {262, 330, 392}; // Frequencies in Hz
  int duration[] = {200, 200, 200}; // Durations in ms

  // Play each note in the melody
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, melody[i], duration[i]); // Play note
    delay(250); // Delay between notes
  }

  noTone(BUZZER_PIN); // Turn off the buzzer
  delay(2000); // Additional delay after the melody
}

/**
 * @brief Generates an audible error signal (e.g., incorrect password, access denied).
 *
 * @details Plays a distinctive beep sequence to signal that an error occurred.
 *          The sequence consists of two beeps: a low-pitched beep (1000 Hz) followed
 *          by a higher-pitched beep (1500 Hz). The duration of each beep is 100 ms,
 *          with an 80 ms pause between them.
 */
void errorBeep() {
  // Low-pitched beep (1000 Hz)
  tone(BUZZER_PIN, 1000);
  delay(100);

  // Pause between beeps
  noTone(BUZZER_PIN);
  delay(80);

  // High-pitched beep (1500 Hz)
  tone(BUZZER_PIN, 1500);
  delay(100);

  // Turn off the buzzer
  noTone(BUZZER_PIN);
}

// ==================== STATUS LED ====================
/**
 * @brief Flashes the built-in blue LED on the ESP8266 to indicate that the system is active.
 *
 * The status LED blinks approximately every second to indicate that the system is
 * operational. This is useful for debugging purposes, as it provides visual feedback
 * that the device is running.
 */
void blinkStatusLED() {
  static unsigned long lastBlink = 0; ///< Timestamp of the last blink
  static bool ledState = false; ///< Current state of the LED (true = on, false = off)

  // Check if enough time has elapsed since the last blink
  if (millis() - lastBlink > 1000) {
    // Toggle the LED state
    ledState = !ledState;

    // Update the LED
    digitalWrite(LED_STATUS, ledState ? HIGH : LOW);

    // Update the last blink timestamp
    lastBlink = millis();
  }
}

// ================= CONNECTION CHECK =================
/**
 * @brief Checks WiFi connection periodically and attempts reconnection if necessary.
 *        Also updates the status of the OLED display.
 * 
 * @details This function is called every 30 seconds to check if the WiFi connection
 *          is still active. If the connection is lost, it attempts to reconnect.
 *          The status of the connection is also used to update the OLED display.
 */
void checkWiFiConnection() {
  static unsigned long lastCheck = 0;

  // Check every 30 seconds if the WiFi connection is still active
  if (millis() - lastCheck > 30000) {
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




