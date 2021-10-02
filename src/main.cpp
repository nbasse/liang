#include <math.h>
#include "ArduinoOTA.h"
#include "updatehandler.h"
#include "WiFi.h"
#include "Wire.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include "MPU6050_light.h"

#include "operational_modes/operationalmode.h"
#include "operational_modes/OpModeCharge.h"
#include "operational_modes/OpModeMow.h"
#include "operational_modes/OpModeMowOnce.h"
#include "operational_modes/OpModeIdle.h"
#include "operational_modes/OpModeUpgrade.h"

#include "behaviors/behavior.h"
#include "behaviors/charge.h"
#include "behaviors/followbwf.h"
#include "behaviors/goaround.h"
#include "behaviors/idle.h"
#include "behaviors/launch.h"
#include "behaviors/lookforbwf.h"
#include "behaviors/mow.h"
#include "behaviors/sensordebug.h"

#include "Controller.h"
#include "webui.h"
#include "definitions.h"
#include "mowermodel.h"
#include "sensor.h"
#include "secrets.h"
#include "battery.h"
#include "gyro.h"
#include "display.h"

#include "utils.h"


unsigned long lastPrint= 0;
int expectedMode = 0;
int expectedBehavior = 0;
int manualMode = -1;
volatile bool updateInProgress = false;
volatile bool pollTaskInitDone = false;

TaskHandle_t pollTask;

MowerModel mowerModel;

void setManualMode(int manualMode_) {
  manualMode = manualMode_;
}

void updateEvent(int percentDone_) {
  Serial.println("Update started");

detachInterrupt(LEFT_SENSOR_PIN);
detachInterrupt(RIGHT_SENSOR_PIN);
  updateInProgress = true;
}

MPU6050 mpu(Wire);
AsyncWebServer webServer(80);
AsyncWebSocket webSocket("/ws");
LOGGER logger(&webSocket, &mowerModel);
MOWERDISPLAY display;
WEBUI webUi(&webServer, &webSocket, &logger, *setManualMode);
GYRO gyro(&logger, &mpu);
SENSOR leftSensor(LEFT_SENSOR_PIN, false, &logger);
SENSOR rightSensor(RIGHT_SENSOR_PIN, false, &logger);
BATTERY battery(BATTERY_SENSOR_PIN, BATTERY_CHARGE_PIN);
BUMPER bumper(BUMPER_PIN);

MOTOR leftMotor(LEFT_MOTOR_SENSE_PIN, LEFT_MOTOR_FORWARD_PWM_PIN, LEFT_MOTOR_BACKWARDS_PWM_PIN, LEFT_MOTOR_PWM_CHANNEL_FORWARD, LEFT_MOTOR_PWM_CHANNEL_BACKWARDS, LOAD_LIMIT_WHEEL, &logger);
MOTOR rightMotor(RIGHT_MOTOR_SENSE_PIN, RIGHT_MOTOR_FORWARD_PWM_PIN, RIGHT_MOTOR_BACKWARDS_PWM_PIN, RIGHT_MOTOR_PWM_CHANNEL_FORWARD, RIGHT_MOTOR_PWM_CHANNEL_BACKWARDS, LOAD_LIMIT_WHEEL, &logger);
MOTOR cutterMotor(CUTTER_MOTOR_SENSE_PIN, CUTTER_MOTOR_FORWARD_PWM_PIN, CUTTER_MOTOR_BACKWARDS_PWM_PIN, CUTTER_MOTOR_PWM_CHANNEL_FORWARD, CUTTER_MOTOR_PWM_CHANNEL_BACKWARDS, LOAD_LIMIT_CUTTER, &logger);

Controller controller(&leftMotor, &rightMotor, &cutterMotor, &gyro, &bumper, &leftSensor, &rightSensor, &logger);

UPDATEHANDLER uh(&logger, &controller, *updateEvent);

OpModeCharge opModeCharge(&controller, &logger, &battery);
OpModeMow opModeMow(&controller, &logger, &battery);
OpModeMowOnce opModeMowOnce(&controller, &logger, &battery);
OpModeIdle opModeIdle(&controller, &logger);
OpModeUpgrade opModeUpgrade(&controller, &logger);

OPERATIONALMODE* availableOpModes[] = { 
  &opModeIdle,
  &opModeMow,
  &opModeMowOnce,
  &opModeCharge,
  &opModeUpgrade,
};
OPERATIONALMODE* currentMode = availableOpModes[0];


Charge charge(&controller, &logger, &battery);
FollowBWF followBWF(&controller, &logger, &battery);
GoAround goAround(&controller, &logger, &battery, &leftSensor, &rightSensor);
Idle idle(&controller, &logger, &battery);
SensorDebug sensorDebug(&controller, &logger, &battery, &leftSensor, &rightSensor);
Launch launch(&controller, &logger, &battery);
LookForBWF lookForBwf(&controller, &logger, &battery, *setManualMode, currentMode);
Mow mow(&controller, &logger, &battery);


BEHAVIOR* currentBehavior;
BEHAVIOR* availableBehaviors[] = {
  &charge,
  &followBWF,
  &goAround,
  &idle,
  &launch,
  &lookForBwf,
  &mow,
  &sensorDebug,
};

void handleInterruptLeft() {
  leftSensor.handleInterrupt();
}

void handleInterruptRight() {
  rightSensor.handleInterrupt();
}


void pollPollables(void * parameter) {
  //Wire must be begin():ed on the same core it's going to be used
  Wire.begin();

  display.setup();
  mowerModel.message = "BOOTING";
  gyro.setup();
  battery.resetVoltage();
  pollTaskInitDone = true;
  while (!updateInProgress)
  {
    if (hasTimeout(lastPrint, 200)) {
      
      lastPrint = millis();
      display.DrawMowerModel(&mowerModel);
    }

    bumper.doLoop();
    if (bumper.IsStuck()) {
      controller.SetError(ERROR_BUMPER_STUCK);
    }

    leftMotor.doLoop();
    mowerModel.LeftMotorLoad = leftMotor.getLoad();
    mowerModel.LeftMotorSpeed = leftMotor.getSpeed();

    rightMotor.doLoop();
    mowerModel.RightMotorLoad = rightMotor.getLoad();
    mowerModel.RightMotorSpeed = rightMotor.getSpeed();

    cutterMotor.doLoop();
    mowerModel.CutterMotorLoad = cutterMotor.getLoad();
    mowerModel.CutterMotorSpeed = cutterMotor.getSpeed();

    gyro.loop();
    mowerModel.Tilt = gyro.getTilt();
    mowerModel.Heading = gyro.getHeading();

    mowerModel.BatteryVoltage = battery.updateVoltage();
    mowerModel.IsDocked = battery.isBeingCharged();

    mowerModel.LeftSensorIsOutOfBounds = leftSensor.IsOutOfBounds();
    mowerModel.RightSensorIsOutOfBounds = rightSensor.IsOutOfBounds();

    delay(2);
  }


  while (true) //Just chill and don't return while waiting for update
  {
    if (millis() - lastPrint >= 200) {
      
      lastPrint = millis();
      display.DrawMowerModel(&mowerModel);
    }
  }
}


void setup() {
  Serial.begin(115200);


  analogReadResolution(ANALOG_RESOLUTION);
  analogSetAttenuation(ADC_11db);  



  mowerModel.OpMode = "Booting";
  mowerModel.Behavior = "Polltask";

  xTaskCreatePinnedToCore(pollPollables, "pollTask", 8192, NULL, 5, &pollTask, 1);

  while(!pollTaskInitDone) {
    delay(1);
  }
  mowerModel.Behavior = "SPIFFS";
  if(!SPIFFS.begin()){
    delay(5000);
    ESP.restart();
  }

  mowerModel.Behavior = "WIFI";

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
      Serial.println("Connection Failed! Rebooting...");
      mowerModel.message = "Connection Failed! Rebooting...";
      delay(5000);
      ESP.restart();
    }

  logger.log("IP: " + WiFi.localIP().toString(), true);
  delay(1000);

  mowerModel.Behavior = "Setup";

  uh.setup();
  webUi.setup();

  battery.setup();
  bumper.setup();

  leftSensor.setup();
  attachInterrupt(LEFT_SENSOR_PIN, handleInterruptLeft, RISING);

  rightSensor.setup();
  attachInterrupt(RIGHT_SENSOR_PIN, handleInterruptRight, RISING);

  leftMotor.setup();
  rightMotor.setup();
  cutterMotor.setup();

  mowerModel.Behavior = "Starting";



  expectedMode = currentMode->id();
  expectedBehavior = currentMode->start();
  currentBehavior = availableBehaviors[0]; //Just something
  mowerModel.OpMode = currentMode->desc();
  mowerModel.Behavior = currentBehavior->desc();

  pinMode(LED_PIN, OUTPUT);
  pinMode(SWITCH_3_PIN, INPUT_PULLUP);
  pinMode(SWITCH_BOOT_PIN, INPUT_PULLUP);

  

}



void loop() {

  
  //digitalWrite(LED_PIN, (millis() % 2000) < 300);
  //digitalWrite(LED_PIN, bumper.IsBumped());
  //digitalWrite(LED_PIN, (digitalRead(SWITCH_3_PIN) == LOW));
  //digitalWrite(LED_PIN, battery.isBeingCharged());
  digitalWrite(LED_PIN, 
    (digitalRead(SWITCH_3_PIN) == LOW) 
    || (digitalRead(SWITCH_BOOT_PIN) == LOW)
    || bumper.IsBumped());


  //Handle
  uh.doLoop();
  webUi.doLoop();
  if (manualMode >= 0) {
    expectedMode = manualMode;
    manualMode = -1;
  }

  if (controller.IsFlipped()) {
    controller.SetError(ERROR_FLIPPED);
  }

  if(controller.GetError() != ERROR_NOERROR) {
    controller.StopCutter();
    controller.StopMovement();
    mowerModel.OpMode = "ERROR";
    mowerModel.Behavior = String(controller.GetError());
    return;
  }

  if (expectedMode != currentMode->id()) {
    int c = sizeof(availableOpModes) / sizeof(availableOpModes[0]);
    bool foundIt = false;
    for (int i = 0; i < c; i++)
    {
      if (availableOpModes[i]->id() == expectedMode) {
        foundIt = true;
        currentMode = availableOpModes[i];
        mowerModel.OpMode = currentMode->desc();
        expectedBehavior = currentMode->start();
        controller.SetError(ERROR_NOERROR);
        break;
      }
    }

    if (!foundIt) {
      logger.log("Could not locate OpMode with id:" + String(expectedMode), true);
    }
  }

  
  if (expectedBehavior != currentBehavior->id()) {
    int c = sizeof(availableBehaviors) / sizeof(availableBehaviors[0]);
    bool foundIt = false;
    for (int i = 0; i < c; i++)
    {
      if (availableBehaviors[i]->id() == expectedBehavior) {
        foundIt = true;
        currentBehavior = availableBehaviors[i];
        mowerModel.Behavior = currentBehavior->desc();
        currentBehavior->start();
        break;
      }
    }

    if (!foundIt) {
      logger.log("Could not locate behavior with id:" + String(expectedBehavior), true);
    }
  }

  expectedMode = currentMode->loop();
  expectedBehavior = currentBehavior->loop();

  
}