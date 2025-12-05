int inputPin = 4;  // IR sensor on pin 4
int outputPin = 5;    // LED PWM driver on pin 5
int currentPwmValue = 255;
int prevSensorVal = 1;
int ctr = 0;

bool ledState = false;   // LED state tracker
unsigned long detectTime = 0;  
bool handDetected = false;
bool pwmMotionDetected = false;
bool isPwmIncreasing = false;

void setup() {
  pinMode(outputPin, OUTPUT);
  pinMode(inputPin, INPUT);
  Serial.begin(9600);
}



void loop() {
  int val = digitalRead(inputPin);   //0 - LOW when hand in front, 1 HIGH when hand is not there
  if (val == LOW && prevSensorVal == HIGH) { //enter hand
    detectTime = millis();
    handDetected = true;
    Serial.println("Hand entered");
  }
  if (millis() - detectTime > 2000 && handDetected) {
    pwmMotionDetected = true;
    ctr = 0;
  }

  if (val == HIGH && prevSensorVal == LOW && handDetected) { //exit hand
    if(pwmMotionDetected) {
      pwmMotionDetected = false;
    } else {
      ledState = !ledState;
      analogWrite(outputPin, ledState ? currentPwmValue : 0);
      Serial.println("Change");
    }
    
    Serial.println("Hand exit");
    handDetected = false;
  }

  if (pwmMotionDetected && ctr % 20 == 0) {
    currentPwmValue = isPwmIncreasing ? currentPwmValue + 1 : currentPwmValue - 1;
    if (currentPwmValue == 256) {
      currentPwmValue = 255;
      isPwmIncreasing = false;
      delay(40);
    }
    if (currentPwmValue == -1) {
      currentPwmValue = 0;
      isPwmIncreasing = true;
      delay(40);
    }
    analogWrite(outputPin, currentPwmValue);
  }


  prevSensorVal = val;
  ctr++;
  delay(5);
  
}
