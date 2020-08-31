#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "ArduinoJson.h"
#include "ArduinoOTA.h"
#include "GlobalConfig.h"
#include "Servo.h"

#define LED 2
#define pinMotionSensor 25
#define pinRelayShocker 26
#define pinCargoServo 14

AsyncWebServer server(80);
Servo myServo;

class TRPlog
{
public:
  tm detectedDTM;
};

LinkedList<TRPlog> trpLogList = LinkedList<TRPlog>(NULL);

struct TRPstatus
{
  int Detected;
  tm lastTimeDetected;
};

TRPstatus trpStatus;

void setup()
{
  Serial.begin(115200);

  pinMode(LED, OUTPUT);
  pinMode(pinMotionSensor, INPUT);
  pinMode(pinRelayShocker, OUTPUT);

  IPAddress ip(192, 168, 86, 120);
  IPAddress gateway(192, 168, 86, 1);
  IPAddress subnet(255, 255, 255, 0);

  WiFi.config(ip, gateway, subnet);
  WiFi.begin(Config.ssid, Config.password);

  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.printf("WiFi Failed!\n");
    return;
  }
  // configTime(-28800, 0, "pool.ntp.org");
  configTime(-25200, 0, "64.22.253.155");
  int timeSyncCounter = 0;
  //Need to check time function
  while (time(nullptr) <= 100000 && timeSyncCounter < 25)
  {
    Serial.println();
    Serial.print(".");
    timeSyncCounter++;
    delay(300);
  }

  Serial.println();
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
    {
      Serial.println("Auth Failed");
    }
    else if (error == OTA_BEGIN_ERROR)
    {
      Serial.println("Begin Failed");
    }
    else if (error == OTA_CONNECT_ERROR)
    {
      Serial.println("Connect Failed");
    }
    else if (error == OTA_RECEIVE_ERROR)
    {
      Serial.println("Receive Failed");
    }
    else if (error == OTA_END_ERROR)
    {
      Serial.println("End Failed");
    }
  });

  ArduinoOTA.begin();

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Get requested: ");
    struct tm timeinfo;
    getLocalTime(&timeinfo);

    String pageHead = "<html><head></head><body>";

    String pageLog = "";
    // for (int i = 0; i < trpLogList.size(); i++)
    for (int i = 0; i < trpLogList.length(); i++)
    {
      // TRPlog log = trpLogList.get(i);
      const TRPlog *log = trpLogList.nth(i);
      pageLog += "<div>" + String(log->detectedDTM.tm_hour) + ":" + String(log->detectedDTM.tm_min) + ":" + String(log->detectedDTM.tm_sec) + "</div>";

      // pageLog += "<div>" + String(now) + ":" + String(log.detectedDTM.tm_min) + ":" + String(log.detectedDTM.tm_sec) + "</div>";
    }

    String pageBottom = "<div>Time: #time</div>"
                        "<div>Total detected: #totaldetected</div>"
                        "<div>Motion sensor: " +
                        String(digitalRead(pinMotionSensor)) + "</div>" +
                        "<div>Temp: " +
                        String((temperatureRead() - 36) / 1.8) + "</div>" +
                        "</body</html>";

    pageBottom.replace("#time", String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min) + ":" + String(timeinfo.tm_sec));
    pageBottom.replace("#totaldetected", String(trpStatus.Detected));
    String page;
    page = pageHead + pageLog + pageBottom;

    // char timeStringBuff[150]; //50 chars should be enough
    // strftime(timeStringBuff, sizeof(timeStringBuff), "%A, %B %d %Y %H:%M:%S", timeinfo);
    request->send(200, "text/html", page);
  });

  server.on("/open-door", HTTP_GET, [](AsyncWebServerRequest *request) {
    // myServo.attach(pinCargoServo);
    myServo.write(0);
    delay(400);
    // myServo.detach();
    request->send(200, "text/html", "Open");
  });

  server.on("/close-door", HTTP_GET, [](AsyncWebServerRequest *request) {
    // myServo.attach(pinCargoServo);
    myServo.write(110);
    delay(400);
    // myServo.detach();
    request->send(200, "text/html", "Close");
  });

  server.begin();

  myServo.attach(pinCargoServo);
  delay(100);
  myServo.write(0);
  delay(200);
  // myServo.detach();

  trpStatus.Detected = 0;
  delay(5000);
}
//HV - high voltage
bool IsShokerOn = false;
bool IsDoorClosed = false;
unsigned long prevMS = 0;
unsigned long startMotionSensorDetMS = 0;
unsigned long startShockerMS = 0;
unsigned long lastHVRelayMS = 0;
unsigned long lastMtSensReadMS = 0;
unsigned long loopDelayMS = 15000;
unsigned long lastMtSensReadMS2 = 0;
unsigned long lastDoorClosedMS = 0;
uint8_t hvRelayState = LOW;

void loop()
{
  ArduinoOTA.handle();
  unsigned long currentMS = millis();
  struct tm timeinfo;

  if (IsShokerOn)
  {
    if (startShockerMS == 0)
    {
      startShockerMS = currentMS;
    }
    //###### High Voltage on/off #######
    if (currentMS - lastHVRelayMS > 70)
    {
      hvRelayState = hvRelayState == HIGH ? LOW : HIGH;
      digitalWrite(pinRelayShocker, hvRelayState);
      digitalWrite(LED, hvRelayState);
      lastHVRelayMS = currentMS;
    }
    //###### END: High Voltage on/off #######

    //If TIME ended turn off high volage and LED
    if (currentMS - startShockerMS > 5000)
    {
      IsShokerOn = false;
      startShockerMS = 0;
      digitalWrite(pinRelayShocker, LOW);
      digitalWrite(LED, LOW);
      //That will be delay for reading motion sensor after last turning on
      loopDelayMS = 2000;
    }
  }
  //Adding some delay for reading Sensor, to avoid reading Motion Sensor's value every millisecond
  if (currentMS - lastMtSensReadMS > loopDelayMS)
  {
    loopDelayMS = 200;
    Serial.println("MotionSens data: " + String(digitalRead(pinMotionSensor)));
    if (digitalRead(pinMotionSensor) == HIGH && IsShokerOn == false)
    {
      if (startMotionSensorDetMS == 0)
      {
        startMotionSensorDetMS = currentMS;
      }

      if (currentMS - startMotionSensorDetMS > 3500)
      {
        // ###### Let's close the door
        myServo.write(110);
        IsDoorClosed = true;
        lastDoorClosedMS = currentMS;
        delay(400);
        // ####################
        IsShokerOn = true;
        startMotionSensorDetMS = 0;
        digitalWrite(pinRelayShocker, HIGH);
        digitalWrite(LED, HIGH);

        getLocalTime(&timeinfo);
        trpStatus.Detected++;

        TRPlog trpLog;
        trpLog.detectedDTM = timeinfo;
        trpLogList.add(trpLog);
      }
    }
    else
    {
      startMotionSensorDetMS = 0;
    }

    if (currentMS > 10000)
    {
      Serial.println(getLocalTime(&timeinfo));
    }
    Serial.println("#######");
    lastMtSensReadMS = currentMS;
  }

  //If the door is closed let's open it after a while
  if (IsDoorClosed == true && currentMS - lastDoorClosedMS > 150000)
  {    
    myServo.write(0);
    IsDoorClosed = false;
    delay(400);
  }

  // ######################################## TEST ###################################
  // Servo and sensor test
  // if (digitalRead(pinMotionSensor) == HIGH)
  // {
  //   digitalWrite(LED, HIGH);
  //   myServo.attach(pinCargoServo);
  //   delay(400);
  //   myServo.write(90);
  //   delay(400);
  //   myServo.detach();
  // }
  // else
  // {
  //   digitalWrite(LED, LOW);
  //   myServo.attach(pinCargoServo);
  //   delay(1000);
  //   myServo.write(0);
  //   delay(400);
  //   myServo.detach();
  // }
  // delay(3000);

  // ######################################## TEST ###################################
}