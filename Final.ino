#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <EEPROM.h>
#include <time.h>
#include <WiFiClientSecureBearSSL.h>
#include "cert.h"
#include "key.h"
#include "root.h"

// ==== Constants ====
#define EEPROM_SIZE 512
#define PIR_PIN D1
// ==== Set timezone EST is -4 ====
#define TIMEZONE_OFFSET -4 * 3600
#define NTP_SERVER "pool.ntp.org"

BearSSL::WiFiClientSecure client; 

// ==== Time slot definitions, written in 24 HR time ====
#define MORNING_START    400
#define MORNING_END      1000
#define AFTERNOON_START  1100
#define AFTERNOON_END    1400
#define EVENING_START    1500
#define EVENING_END      1700
#define NIGHT_START      1700
#define NIGHT_END        2200

struct TimeSlot {
  int start;
  int end;
  const char* name;
};

TimeSlot timeSlots[4] = {
  {MORNING_START, MORNING_END, "Morning"},
  {AFTERNOON_START, AFTERNOON_END, "Afternoon"},
  {EVENING_START, EVENING_END, "Evening"},
  {NIGHT_START, NIGHT_END, "Night"}
};

// ==== Hold user selected window ====
int selectedTimeSlot = -1;

// ==== IFTTT HTTPS INFO ====
const char* IFTTT_HOST = "maker.ifttt.com";
// ==== 443 to ensure HTTPS ====
const int HTTPS_PORT = 443;
const char* IFTTT_EVENT = "pill_reminder";
const char* IFTTT_KEY = "dCFdJC4O3NgirRsfpRXkCp";

// ==== Web Server ====
ESP8266WebServer server(80);

// ==== Global Variables ====
char ssid[32] = "";
char password[64] = "";
bool sensorTriggered[4] = {false, false, false, false};
char userEmail[64] = "";

// ==== Function Prototypes ====
void handleRoot();
void handleSave();
void sendIFTTTAlert(String uerEmail);
int getCurrentHour();
int getSlotForHour(int hour);
void handleEmailInput();
void handleWiFiStatus();

// ==== Setup Wi-Fi Connection ====
void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  EEPROM.begin(EEPROM_SIZE);

  EEPROM.get(0, ssid);
  EEPROM.get(32, password);
  EEPROM.get(96, selectedTimeSlot);
  EEPROM.get(128, userEmail);
  if (selectedTimeSlot < 0 || selectedTimeSlot > 3) selectedTimeSlot = -1;

  // Debug output: print what was loaded
  Serial.print("Loaded SSID: ");
  Serial.println(ssid);
  Serial.print("Loaded password: ");
  Serial.println(password);
  Serial.print("Loaded email:");
  Serial.println(userEmail);

  if (strlen(ssid) == 0 || strlen(password) == 0) {
    // No Wi-Fi credentials found, starting AP mode
    Serial.println("No Wi-Fi credentials found. Starting AP mode...");
    WiFi.softAPConfig(IPAddress(192, 168, 4, 111), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP("PillBox_Config");

    server.on("/", handleRoot);
    server.on("/save", handleSave);
    server.begin();
    Serial.print("AP Mode - Config page available at: ");
    Serial.println(WiFi.softAPIP());
  } else {
    // Force Station Mode
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    // Try to connect for 20 seconds
    Serial.print("Connecting to ");
    Serial.println(ssid);
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected to Wi-Fi!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());

      // Serve confirmation page
      server.on("/", handleWiFiStatus); // Redirect to confirmation page
      server.begin(); // Start the web server
    } else {
      Serial.println("\nFailed to connect. Restarting into AP mode...");
      WiFi.disconnect();
      delay(1000);
      ESP.restart();
    }
  }
}


// ==== Main Loop ====
void loop() {
  if (WiFi.getMode() == WIFI_AP) {
    server.handleClient();
    return;
  }

  int hour = getCurrentHour();
  int timeNow = hour * 100;

  if (selectedTimeSlot != -1) {
    int start = timeSlots[selectedTimeSlot].start;
    int end = timeSlots[selectedTimeSlot].end;

    if (timeNow >= start && timeNow < end) {
      if (!sensorTriggered[selectedTimeSlot] && digitalRead(PIR_PIN) == HIGH) {
        sensorTriggered[selectedTimeSlot] = true;
        Serial.printf("Pill taken for %s\n", timeSlots[selectedTimeSlot].name);
      }

      if (!sensorTriggered[selectedTimeSlot]) {
        Serial.printf("No pill taken for %s. Sending alert.\n", timeSlots[selectedTimeSlot].name);
        sendIFTTTAlert(userEmail);
        sensorTriggered[selectedTimeSlot] = true;
      }
    }
  }
}

// ==== Web Interface Handlers ====
void handleRoot() {
  String page = "<!DOCTYPE html><html><head>";
  page += "<title>Pill Box Config</title>";
  page += "<style>";
  page += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #f0f8ff; margin: 0; padding: 0; }";
  page += ".wrapper { display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100vh; }";
  page += ".container { background-color: #ffffff; padding: 40px; border-radius: 10px; box-shadow: 0 4px 12px rgba(0,0,0,0.1); width: 90%; max-width: 500px; }";
  page += "h1, h2 { text-align: center; color: #333; }";
  page += "form { display: flex; flex-direction: column; }";
  page += "input[type='text'], input[type='password'], input[type='email'] { padding: 10px; margin-bottom: 15px; border: 1px solid #ccc; border-radius: 5px; }";
  page += "input[type='radio'] { margin-right: 8px; }";
  page += "input[type='submit'] { background-color: #4CAF50; color: white; padding: 12px; border: none; border-radius: 5px; cursor: pointer; }";
  page += "input[type='submit']:hover { background-color: #45a049; }";
  page += "</style></head><body>";
  
  page += "<div class='wrapper'><div class='container'>";
  page += "<h1>Pill Box Config</h1>";
  page += "<form action='/save' method='POST'>";
  
  page += "<label>Wi-Fi SSID:</label>";
  page += "<input type='text' name='ssid' value='" + String(ssid) + "'>";
  
  page += "<label>Wi-Fi Password:</label>";
  page += "<input type='password' name='password'>";

  page += "<label>Email for Alerts:</label>";
  page += "<input type='email' name='email' value='" + String(userEmail) + "'>";
  
  page += "<h2>Select Your Pill Reminder Time</h2>";
  for (int i = 0; i < 4; i++) {
    page += "<label><input type='radio' name='slot' value='" + String(i) + "'";
    if (i == selectedTimeSlot) page += " checked";
    page += "> " + String(timeSlots[i].name) + " (" + String(timeSlots[i].start) + "-" + String(timeSlots[i].end) + ")</label><br>";
  }

  page += "<br><input type='submit' value='Save'>";
  page += "</form></div></div></body></html>";

  server.send(200, "text/html", page);
}


// ==== Save Wi-Fi Credentials and Restart ====
void handleSave() {
  String newSSID = urldecode(server.arg("ssid"));
  String newPassword = urldecode(server.arg("password"));
  String slot = server.arg("slot");

  // Save Wi-Fi credentials
  newSSID.toCharArray(ssid, sizeof(ssid));
  newPassword.toCharArray(password, sizeof(password));
  selectedTimeSlot = slot.toInt();

  // Save data to EEPROM
  EEPROM.put(0, ssid);
  EEPROM.put(32, password);
  EEPROM.put(96, selectedTimeSlot);
  EEPROM.commit();
  String newEmail = urldecode(server.arg("email"));
  newEmail.toCharArray(userEmail, sizeof(userEmail));
  EEPROM.put(128, userEmail);  // Example EEPROM slot
  EEPROM.commit();


  // Inform the user that the Wi-Fi credentials are saved
  server.send(200, "text/html", 
    "<html><body><h1>Saved!</h1><p>Wi-Fi credentials saved. Restarting...</p></body></html>");
  
  delay(2000);  // Give the user time to read the message

  // Restart the device
  ESP.restart();
}
// ==== Web Interface Handler for Wi-Fi Connection Confirmation ====
void handleWiFiStatus() {
  String page = "<html><body>";
  page += "<h1>Wi-Fi Connected!</h1>";
  page += "<p>The device is now connected to Wi-Fi.</p>";
  page += "<p>IP address: " + WiFi.localIP().toString() + "</p>";
  page += "</body></html>";
  server.send(200, "text/html", page);
}


// ==== HTTPS IFTTT Alert with Dynamic Email ==== 
void sendIFTTTAlert(String userEmail) {
  WiFiClientSecure client;

  BearSSL::X509List cert(cert_data);
  client.setTrustAnchors(&cert);

  // Attempt connection to IFTTT
  if (!client.connect(IFTTT_HOST, HTTPS_PORT)) {
    Serial.println("Connection to IFTTT failed!");
    return;
  }

  // Construct the URL for the IFTTT event
  String url = "/trigger/" + String(IFTTT_EVENT) + "/with/key/" + IFTTT_KEY;

  // JSON body with dynamic user email
  String body = "{\"value1\":\"" + userEmail + "\"}";  // Use user-entered email

  // Send the POST request to IFTTT
  client.println("POST " + url + " HTTP/1.1");
  client.println("Host: " + String(IFTTT_HOST));
  client.println("User-Agent: PillBoxESP8266");
  client.println("Connection: close");
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(body.length());
  client.println();
  client.println(body);

  // Wait for the response
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;  // End of headers
  }

  // Read and print the IFTTT response
  String response = client.readString();
  Serial.println("IFTTT Response:");
  Serial.println(response);
}

// ==== Time Utilities ====
// Get the current hour from the system
int getCurrentHour() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  return timeinfo->tm_hour;
}

// Get the time slot index based on the current hour
int getSlotForHour(int hour) {
  for (int i = 0; i < 4; i++) {
    // Check if the hour falls within the start and end range of the time slot
    if (hour >= timeSlots[i].start / 100 && hour < timeSlots[i].end / 100) {
      return i;  // Return the index of the matching time slot
    }
  }
  return -1;  // Return -1 if no matching slot is found
}

// ==== URL decoder (for SSID and password) ====
String urldecode(String input) {
  String decoded = "";
  char c;
  for (int i = 0; i < input.length(); i++) {
    if (input[i] == '+') {
      decoded += ' ';
    } else if (input[i] == '%' && i + 2 < input.length()) {
      String hex = input.substring(i + 1, i + 3);
      c = (char) strtol(hex.c_str(), NULL, 16);
      decoded += c;
      i += 2;
    } else {
      decoded += input[i];
    }
  }
  return decoded;
}
