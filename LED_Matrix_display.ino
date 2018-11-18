// Wunderground
// ArduinoJSON using code from book
// timezone DST auto adjustment
// Wemos D1
// Dimming added v2.0.4b
#include <ArduinoOTA.h>
#include <Adafruit_HT1632.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <Time.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <ESP8266HTTPClient.h>

#define DATA 4 // GREEN
#define WRITE 5 // YELLOW
#define CS0 2 // BLUE

// Wunderground Settings
const boolean IS_METRIC = false;
const String WUNDERGROUND_API_KEY = "key_goes_here";
const String WUNDERGROUND_LANGUAGE = "EN";
const String WUNDERGROUND_COUNTRY = "US";
const String WUNDERGROUND_CITY = "CA/Davis";
const String WUNDERGROUND_PWS = "KCADAVIS46";
bool USE_WUNDERGROUND_PWS = false;
bool USE_DIMMING = true;

const char* ssid = "ssid_goes_here";
const char* password = "wifi_pw_goes_here";

struct History {
  int hour;
  int minute;
  float temp;
};
History observations[72];

String history_date;
float forecastLow;
float forecastHigh;
String displayTemp;
float hourTemps[24];

String myYear;
String myMonth;
String myDay;
String myHour;
String myMinute;
String mySecond;

TimeChangeRule usPDT = {"PDT", Second, Sun, Mar, 2, -420};  //UTC - 7 hours
TimeChangeRule usPST = {"PST", First, Sun, Nov, 2, -480};   //UTC - 8 hours
Timezone usTZ(usPDT, usPST);

Adafruit_HT1632LEDMatrix matrix = Adafruit_HT1632LEDMatrix(DATA, WRITE, CS0);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Beginning setup()...");

  matrix.begin(ADA_HT1632_COMMON_16NMOS);
  matrix.setTextSize(1);    // size 1 == 8 pixels high
  matrix.setTextColor(1);   // 'lit' LEDs
  matrix.setRotation(0);
  matrix.setBrightness(5);
  matrix.clearScreen();
  matrix.writeScreen();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(3000);
    ESP.restart();
  }

  updateDateTime();
  setupArduinoOTA();
  getForecastHighLow();

  if (getHistory() == -99) {
    ESP.restart();
  }

  //  draw lines for previous myHours
  for (int myHourBar = 0; myHourBar < myHour.toInt(); myHourBar++) {
    int barTemp = map(hourTemps[myHourBar], forecastLow, forecastHigh, 0, 7);
    constrain(barTemp, 0, 7); // lock values in case the temp is above or below forecast values
    matrix.drawLine(myHourBar, 15, myHourBar, 15 - barTemp, 1);
    matrix.writeScreen();
    Serial.print("Writing myHourBar ");
    Serial.print(myHourBar);
    Serial.print(" as line from 15 to ");
    Serial.print(15 - barTemp);
    Serial.print(" using temp of ");
    Serial.println(hourTemps[myHourBar]);
    delay(100);
  }
  Serial.println("Done with setup()");
}

void loop() {
  Serial.println("In loop()...");

  float temp = getCurrentTemp();
  if (temp == -99) {
    return;
  }
  int displayTemp = round(temp);

  updateDateTime();

  if (USE_DIMMING == true && (myHour.toInt() >= 18 || myHour.toInt() <= 6) ) {
    matrix.setBrightness(1);
  }
  else {
    matrix.setBrightness(5);
  }
  Serial.print("Display temp: ");
  Serial.println(displayTemp);
  Serial.print("Time: ");
  Serial.print(myHour);
  Serial.print(":");
  Serial.print(myMinute);
  Serial.print(":");
  Serial.println(mySecond);

  matrix.fillRect(0, 0, matrix.width(), 7, 0); // clear top half
  if (myHour.toInt() == 0) {
    matrix.fillRect(0, 8, matrix.width(), 15, 0); // clear bottom half
  }

  if (displayTemp < 10) {
    matrix.setCursor(13, 0);   // start farther right for single digit temps
  }
  else if (displayTemp > 99) {
    matrix.setCursor(2, 0);   // start farther left for triple digit temps
  }
  else {
    matrix.setCursor(7, 0);   // start position for double digit temps
  }
  matrix.print(displayTemp);
  matrix.drawCircle(21, 1, 1, 1); // degree symbol, always in the upper right

  int barTemp = map(temp, forecastLow, forecastHigh, 0, 7);
  constrain(barTemp, 0, 7); // lock values in case the temp is above or below forecast values
  matrix.drawLine(myHour.toInt(), 15, myHour.toInt(), 15 - barTemp, 1);
  Serial.print("Writing myHourBar ");
  Serial.print(myHour);
  Serial.print(" as line from 15 to ");
  Serial.print(15 - barTemp);
  Serial.print(" using temp of ");
  Serial.println(temp);

  matrix.writeScreen();

  Serial.println("Done with loop(), waiting for next round...");

  for (int i = 0; i < 301; i++) {
    ArduinoOTA.handle();
    delay(1000); // update every 5 minutes
  }
}

int getHistory() {
  const char* host = "api.wunderground.com";
  const int port = 80;
  String history_date;
  //  WiFiClient client;
  HTTPClient http;

  Serial.println("In getHistory()...");

  updateDateTime();
  history_date = myYear + myMonth + myDay;

  //  history_date = "20180816"; // value for testing full array
  Serial.print("Date strings: ");
  Serial.print(myYear);
  Serial.print("-");
  Serial.print(myMonth);
  Serial.print("-");
  Serial.println(myDay);

  String url = "http://api.wunderground.com/api/" + WUNDERGROUND_API_KEY + "/history_" + history_date + "/q/" + WUNDERGROUND_COUNTRY + "/" + WUNDERGROUND_CITY + ".json";
  Serial.print("History using url: ");
  Serial.println(url);


  http.begin(url);
  int status = http.GET();
  Stream& stream = http.getStream();

  stream.find("\"history\"") && stream.find("\"observations\"") && stream.find("[");

  int n = 0;
  while (n < 128) {
    // We are now in the array, we can read the objects one after the other
    DynamicJsonBuffer jb(2048);

    JsonObject &obj = jb.parseObject(stream);
    if (!obj.success())
      return -99;

    observations[n].hour = obj["date"]["hour"];
    observations[n].minute = obj["date"]["min"];
    observations[n].temp = obj["tempi"];

    //    if (observations[n].minute < 16) {
    Serial.print("n: ");
    Serial.print(n);
    Serial.print(" - ");
    Serial.print("hour: ");
    Serial.print(observations[n].hour);
    Serial.print(" - ");
    Serial.print("minute: ");
    Serial.print(observations[n].minute);
    Serial.print(" - ");
    Serial.print("temp: ");
    Serial.println(observations[n].temp);
    if (observations[n].temp > hourTemps[observations[n].hour]) { // use the max value for each hour
      hourTemps[observations[n].hour] = observations[n].temp;
    }
    //    }
    // After reading a forecast object, the next character is either a comma (,) or the closing bracket (])
    if (!stream.findUntil(",", "]")) {
      break;
    }
    n++;
  };
  http.end();
  Serial.println("Done with getHistory()");
}

void getForecastHighLow() {
  const char* host = "api.wunderground.com";
  const int port = 80;
  WiFiClient client;
  String url = "/api/" + WUNDERGROUND_API_KEY + "/forecast/lang:" + WUNDERGROUND_LANGUAGE + "/q/" + WUNDERGROUND_COUNTRY + "/" + WUNDERGROUND_CITY + ".json";

  Serial.println("In getForecastHighLow()...");
  Serial.print("Using url: ");
  Serial.println(url);

  while (!client.connect(host, port)) {
    delay(250); // keep trying to connect every 250 millimySeconds
  }

  client.print(String("GET ") + url + " HTTP/1.0\r\n" +
               "Host: api.wunderground.com" + "\r\n" +
               "Connection: close\r\n\r\n");

  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      //      Serial.println(">>> Client Timeout !");
      return;
    }
    delay(100);
  }

  // Check HTTP status
  char status[32] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.0 200 OK") != 0) {
    Serial.print(F("Unexpected response: "));
    Serial.println(status);
    return;
  }

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders)) {
    Serial.println(F("Invalid response"));
    return;
  }

  unsigned int bufferSize = client.available();
  Serial.print("getForecastHighLow buffer size: ");
  Serial.println(bufferSize);

  DynamicJsonBuffer jsonBuffer(client.available());
  JsonObject& root = jsonBuffer.parseObject(client);

  if (!root.success()) {
    Serial.println(F("Parsing failed, trying again."));
    return;
  }

  JsonArray& forecast_simpleforecast_forecastday = root["forecast"]["simpleforecast"]["forecastday"];
  JsonObject& forecast_simpleforecast_forecastday0 = forecast_simpleforecast_forecastday[0];

  forecastHigh = forecast_simpleforecast_forecastday0["high"]["fahrenheit"];
  forecastLow = forecast_simpleforecast_forecastday0["low"]["fahrenheit"];

  Serial.print("ForecastHigh: ");
  Serial.println(forecastHigh);
  Serial.print("ForecastLow: ");
  Serial.println(forecastLow);

  Serial.println("Done with getForecastHighLow()");
}

void setupArduinoOTA() {
  Serial.println("In setupArduinoOTA()...");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  WiFi.hostname("led_matrix_temp");
  ArduinoOTA.begin();
  Serial.println("Done with setupArduinoOTA()");
}

float getCurrentTemp() {
  const char* host = "api.wunderground.com";
  const int port = 80;
  String url;
  WiFiClient client;

  // City example URL: http://api.wunderground.com/api/API_STRING_GOES_HERE/conditions/q/CA/Davis.json
  //  PWS example URL: http://api.wunderground.com/api/API_STRING_GOES_HERE/conditions/lang:EN/q/pws:KCADAVIS46.json

  if (USE_WUNDERGROUND_PWS == true) {
    url = "/api/" + WUNDERGROUND_API_KEY + "/conditions/lang:" + WUNDERGROUND_LANGUAGE + "/q/pws:" + WUNDERGROUND_PWS + ".json";
  }
  else {
    url = "/api/" + WUNDERGROUND_API_KEY + "/conditions/q/" + WUNDERGROUND_CITY + ".json";
  }

  Serial.println("In getCurrentTemp()...");
  Serial.print("Using url: ");
  Serial.println(url);

  while (!client.connect(host, port)) {
    delay(250); // keep trying to connect every 250 millimySeconds
  }

  client.print(String("GET ") + url + " HTTP/1.0\r\n" +
               "Host: api.wunderground.com" + "\r\n" +
               "Connection: close\r\n\r\n");

  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      //      Serial.println(">>> Client Timeout !");
      return -99;
    }
    delay(100);
  }

  // Check HTTP status
  char status[32] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.0 200 OK") != 0) {
    Serial.print(F("Unexpected response: "));
    Serial.println(status);
    return -99;
  }

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders)) {
    Serial.println(F("Invalid response"));
    return -99;
  }

  DynamicJsonBuffer jsonBuffer(client.available());

  JsonObject& root = jsonBuffer.parseObject(client);
  while (!root.success()) {
    Serial.println(F("Parsing failed, trying again."));
    return -99;
  }

  JsonObject& current_observation = root["current_observation"];
  float current_observation_temp_f = current_observation["temp_f"];
  return current_observation_temp_f;
}

void updateDateTime() {
  time_t thisTZ, utc;

  timeClient.update();
  utc = timeClient.getEpochTime();  //current time from the Time Library
  thisTZ = usTZ.toLocal(utc);

  myYear = year(thisTZ);
  myMonth = month(thisTZ);
  if (myMonth.toInt() < 10) {
    myMonth = "0" + myMonth;
  }
  myDay = day(thisTZ);
  if (myDay.toInt() < 10) {
    myDay = "0" + myDay;
  }
  myHour = hour(thisTZ);
  myMinute = minute(thisTZ);
  mySecond = second(thisTZ);
}
