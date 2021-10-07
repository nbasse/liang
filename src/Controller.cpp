#include "Controller.h"
#include "definitions.h"

Controller::Controller(MOTOR* leftMotor_, MOTOR* rightMotor_, MOTOR* cutterMotor_, GYRO* gyro_, BUMPER* bumper_, SENSOR* leftSensor_, SENSOR* rightSensor_, LOGGER* logger_){
    leftMotor = leftMotor_;
    rightMotor = rightMotor_;
    cutterMotor = cutterMotor_;
    gyro = gyro_;
    bumper = bumper_;
    leftSensor = leftSensor_;
    rightSensor = rightSensor_;
    logger = logger_;
}

void Controller::TurnAngle(int degrees){
    degrees = (degrees % 360);
    float targetHeading = Heading() + degrees;
    logger->log("TurnAngle " + String(degrees) + " start " + String(Heading()) + " target: " + String(targetHeading));
    unsigned long t = millis();

    while (abs(targetHeading - Heading()) > 3)
    {
        TurnAsync(degrees < 0);
        if (hasTimeout(t, 2500)) {
            logger->log("TurnAngle " + String(degrees) + "timed out. Now at: " + String(Heading()));
            return;
        }
    }
    logger->log("TurnAngle " + String(degrees) + "ok. Now at: " + String(Heading()));
}

void Controller::TurnAsync(bool isLeftTurn){
    leftMotor->setSpeed(FULL_SPEED * (isLeftTurn ? -1 : 1) , NORMAL_ACCELERATION_TIME);
    rightMotor->setSpeed(FULL_SPEED * (isLeftTurn ? 1 : -1), NORMAL_ACCELERATION_TIME);
}

bool Controller::RunAsync(int leftSpeed, int rightSpeed, int actionTime){
    return leftMotor->setSpeed(leftSpeed, actionTime) + rightMotor->setSpeed(rightSpeed, actionTime) == 0;
}

void Controller::Move(int distanceInCm){
    logger->log("Move " + String(distanceInCm));
    unsigned long moveEnd = millis() + abs(distanceInCm) * 30;
    if (distanceInCm > 0){
        while (moveEnd > millis())
        {
            leftMotor->setSpeed(FULL_SPEED, NORMAL_ACCELERATION_TIME);
            rightMotor->setSpeed(FULL_SPEED, NORMAL_ACCELERATION_TIME);
            
        }
    }

    else {
        while (moveEnd > millis())
        {
            leftMotor->setSpeed(-FULL_SPEED, NORMAL_ACCELERATION_TIME);
            rightMotor->setSpeed(-FULL_SPEED, NORMAL_ACCELERATION_TIME);
        }

    }   
    StopMovement();
}

void Controller::StopMovement(){
    
    while(rightMotor->setSpeed(0,SHORT_ACCELERATION_TIME) + leftMotor->setSpeed(0,SHORT_ACCELERATION_TIME) > 0)
    {
        delay(1);
    }
}

void Controller::RunCutterAsync(){
    cutterMotor->setSpeed(CUTTER_SPEED, 7000);
}

void Controller::StopCutter(){
    cutterMotor->setSpeed(0,0);
}

bool Controller::HandleObsticle(){
    if (IsBumped()) {
        logger->log("Bumped");
        DoEvadeObsticle();
        return true;
    }

    if (IsTilted()) {
        logger->log("Tilted");
        DoEvadeObsticle();
        return true;
    }

    
    if (IsWheelOverload()) {
        logger->log("Wheel overload");
        DoEvadeObsticle();
        return true;
    }

    return false;
}

bool Controller::IsBumped() {
    return bumper->IsBumped();
}

bool Controller::IsLeftOutOfBounds() {
    return leftSensor->IsOutOfBounds();
}

bool Controller::IsRightOutOfBounds() {
    return rightSensor->IsOutOfBounds();
}

bool Controller::IsWheelOverload() {
    return leftMotor->isOverload() || rightMotor->isOverload();
}

bool Controller::IsTilted() {
    return gyro->getAngleY() > TILT_ANGLE;

}

bool Controller::IsFlipped() {
    return max(abs(gyro->getAngleX()), abs(gyro->getAngleY())) > FLIP_ANGLE;
}

int Controller::Heading() {
    return gyro->getHeading();
}

void Controller::SetError(int error_) {
    if (error == error_) return;
    error = error_;
    logger->log("ERROR: " + String(error));
}

int Controller::GetError() {
    return error;
}



void Controller::DoEvadeObsticle(){
    StopMovement();
    Move(-20);
    int turnAngle = random(90, 160);;
    if (random(0, 100) % 2 == 0) {
        turnAngle = -turnAngle;
    }

    TurnAngle(turnAngle);
}
