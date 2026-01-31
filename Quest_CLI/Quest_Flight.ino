/*
****************************************************************************************
****************************************************************************************
20231121
Files Required to make a complete program - 
  CLI_V1.0, Quest_CLI.h, Quest_Flight.h, Quest_flight.cpp
******************************************************************************************                                   
******************************************************************************************                  
*/

#include "Quest_Flight.h"
#include "Quest_CLI.h"
#include <sequencer2.h>
#include <Wire.h>
#include <Ezo_i2c.h>
#include <Ezo_i2c_util.h>

// ONE PUMP - 4 wires to IO pins
#define pumpIN_A IO2      // Pump IN terminal (positive side)
#define pumpIN_B IO3      // Pump IN terminal (positive side)
#define pumpOUT_A IO0     // Pump OUT terminal (negative side)
#define pumpOUT_B IO1     // Pump OUT terminal (negative side)
#define buzzer IO6        // Buzzer

//////////////////////////////////////////////////////////////////////////
//    This defines the timers used to control flight operations
//////////////////////////////////////////////////////////////////////////
#define SpeedFactor 1    // = times faster -> DO NOT CHANGE FOR ISS; KEEP AT 1 UNLESS FOR TESTING

#define one_sec   1000                       //one second = 1000 millis
#define one_min   60*one_sec                 // one minute of time
#define one_hour  60*one_min                 // one hour of time
#define one_day   24*one_hour               //one day of time

// Pump and buzzer timing
#define PumpForwardTime       ((one_sec * 7) / SpeedFactor)
#define PumpReverseTime       ((one_sec * 15) / SpeedFactor)
#define Buzzer1Time           ((one_sec * 10) / SpeedFactor)
#define Buzzer2Time           ((one_sec * 30) / SpeedFactor)
#define WaitTime              ((one_sec * 5) / SpeedFactor)
#define BuzzerOnTime          ((one_sec * 15) / SpeedFactor)
#define HourlyInterval        ((one_hour) / SpeedFactor)

  int sensor1count = 0;
  int State = 0;
  int counter = 0;
  int pumpSequenceState = 0;
  Ezo_board PH = Ezo_board(100, "PH");  // Silver tip pH sensor at address 100

  float current_pH = 0.0;
  float current_EC = 0.0;  // Will always be 0 (no EC sensor)

  void ph_step1();
  void ph_step2();
  void add2text(int value1, int value2, int value3);
  void dataappend(int counts, int ampli, int SiPM, int Deadtime);
  void appendToBuffer(const char* data);
  void pumpForward();
  void pumpReverse();
  void pumpStop();
  Sequencer2 pH_Seq(&ph_step1, 1000, &ph_step2, 0);

void Flying() {
  Wire.begin();
  pH_Seq.reset();

  Serial.println("\n\rRun flight program\n\r");

  pinMode(pumpIN_A, OUTPUT);
  pinMode(pumpIN_B, OUTPUT);
  pinMode(pumpOUT_A, OUTPUT);
  pinMode(pumpOUT_B, OUTPUT);
  pinMode(buzzer, OUTPUT);
  
  pumpStop();
  digitalWrite(buzzer, LOW);

  uint32_t HourlyTimer = millis();
  uint32_t BuzzerTimer = millis();
  uint32_t SequenceTimer = millis();
  uint32_t PHReadingTimer = millis();
  bool buzzerIsOn = false;
  bool phReadingInProgress = false;
  uint32_t Sensor2Deadmillis = millis();
  uint32_t one_secTimer = millis();
  uint32_t sec60Timer = millis();

  Serial.println("Flying NOW  -  x=abort");
  Serial.println("Terminal must be reset after abort");

  missionMillis = millis();
  
  DateTime now = rtc.now();
  currentunix = (now.unixtime());
  writelongfram(currentunix, PreviousUnix);

  while (1) {
    while (Serial.available()) {
      if (Serial.read() == 'x') return;
    }

//------------------------------------------------------------------
//*********** PUMP SEQUENCE (after 24h) ****************************
//------------------------------------------------------------------

    // STATE 0: Start Pump Forward (7 seconds)
    if (millis() > one_hour * 24 && pumpSequenceState == 0) {
      Serial.println("=== 24h 0m 0s: Pump FORWARD for 7 seconds ===");
      pumpForward();
      SequenceTimer = millis();
      pumpSequenceState = 1;
    }

    // STATE 1→2: Stop pump, start buzzer #1 (10 seconds)
    if (pumpSequenceState == 1 && (millis() - SequenceTimer) > PumpForwardTime) {
      pumpStop();
      Serial.println("=== 24h 0m 7s: Pump stopped. Buzzer ON for 10 seconds ===");
      digitalWrite(buzzer, HIGH);
      SequenceTimer = millis();
      pumpSequenceState = 2;
    }

    // STATE 2→3: Stop buzzer, start wait (5 seconds)
    if (pumpSequenceState == 2 && (millis() - SequenceTimer) > Buzzer1Time) {
      digitalWrite(buzzer, LOW);
      Serial.println("=== 24h 0m 17s: Buzzer OFF. Waiting 5 seconds ===");
      SequenceTimer = millis();
      pumpSequenceState = 3;
    }

    // STATE 3→4: Start Pump Reverse (15 seconds)
    if (pumpSequenceState == 3 && (millis() - SequenceTimer) > WaitTime) {
      Serial.println("=== 24h 0m 22s: Pump REVERSE for 15 seconds ===");
      pumpReverse();
      SequenceTimer = millis();
      pumpSequenceState = 4;
    }

    // STATE 4→5: Stop pump, start buzzer #2 (30 seconds)
    if (pumpSequenceState == 4 && (millis() - SequenceTimer) > PumpReverseTime) {
      pumpStop();
      Serial.println("=== 24h 0m 37s: Pump stopped. Buzzer ON for 30 seconds ===");
      digitalWrite(buzzer, HIGH);
      SequenceTimer = millis();
      pumpSequenceState = 5;
    }

    // STATE 5→6: Stop buzzer, pump sequence complete
    if (pumpSequenceState == 5 && (millis() - SequenceTimer) > Buzzer2Time) {
      digitalWrite(buzzer, LOW);
      Serial.println("=== 24h 1m 7s: Buzzer OFF. PUMP SEQUENCE COMPLETE ===");
      Serial.println("pH sensor ready (silver tip at address 100). Ready for hourly readings.");
      HourlyTimer = millis();
      pumpSequenceState = 6;
    }

//------------------------------------------------------------------
//*********** HOURLY CYCLE: Buzzer (15s) → pH Reading *************
//------------------------------------------------------------------

    if ((millis() - HourlyTimer) > HourlyInterval && pumpSequenceState == 6) {
      HourlyTimer = millis();
      
      Serial.println("=== HOURLY CYCLE: Starting buzzer for 15 seconds ===");
      digitalWrite(buzzer, HIGH);
      buzzerIsOn = true;
      BuzzerTimer = millis();
    }
    
    if (buzzerIsOn && (millis() - BuzzerTimer) > BuzzerOnTime) {
      digitalWrite(buzzer, LOW);
      Serial.println("Buzzer OFF - Starting pH reading now");
      buzzerIsOn = false;
      
      phReadingInProgress = true;
      PHReadingTimer = millis();
      pH_Seq.reset();
      PH.send_read_cmd();
    }
    
    if (phReadingInProgress && (millis() - PHReadingTimer) > 1000) {
      Serial.println("DEBUG: Attempting to read pH sensor (address 100)...");
      phReadingInProgress = false;
      
      PH.receive_read_cmd();
      if (PH.get_error() == Ezo_board::SUCCESS) {
        current_pH = PH.get_last_received_reading();
        Serial.print("DEBUG: pH sensor SUCCESS - value: ");
        Serial.println(current_pH, 3);
      } else {
        Serial.print("DEBUG: pH sensor ERROR - error code: ");
        Serial.println(PH.get_error());
      }
      
      // No EC sensor - always 0
      current_EC = 0.0;
      
      sensor1count++;
      int pH_int = (int)(current_pH * 1000);
      int EC_int = 0;  // No EC sensor
      add2text(sensor1count, pH_int, EC_int);
      nophotophoto();
      
      Serial.print("pH reading #");
      Serial.print(sensor1count);
      Serial.print(" complete. pH=");
      Serial.print(current_pH, 3);
      Serial.println(" (No EC sensor)");
    }

//------------------------------------------------------------------
//*********** One second counter timer ***************************
//------------------------------------------------------------------

    if ((millis() - one_secTimer) > one_sec) {
      one_secTimer = millis();
      DotStarYellow();
      
      DateTime now = rtc.now();
      currentunix = (now.unixtime());
      Serial.print(currentunix); Serial.print(" ");
      uint32_t framdeltaunix = (currentunix - readlongFromfram(PreviousUnix));
      uint32_t cumunix = readlongFromfram(CumUnix);
      writelongfram((cumunix + framdeltaunix), CumUnix);
      writelongfram(currentunix, PreviousUnix);
      
      Serial.print("Time: ");
      Serial.print(": Mission Clock = ");
      Serial.print(readlongFromfram(CumUnix));
      Serial.print(" is ");
      
      getmissionclk();
      Serial.print(xd); Serial.print(" Days  ");
      Serial.print(xh); Serial.print(" Hours  ");
      Serial.print(xm); Serial.print(" Min  ");
      Serial.print(xs); Serial.println(" Sec");
      
      DotStarOff();
    }
  }
}

//////////////////////////////////////////////////////////////////////////
// PUMP CONTROL FUNCTIONS
//////////////////////////////////////////////////////////////////////////
void pumpForward() {
  // Forward: IN (IO2+IO3) HIGH, OUT (IO0+IO1) LOW
  digitalWrite(pumpIN_A, HIGH);   // IO2 = HIGH
  digitalWrite(pumpIN_B, HIGH);   // IO3 = HIGH
  digitalWrite(pumpOUT_A, LOW);   // IO0 = LOW
  digitalWrite(pumpOUT_B, LOW);   // IO1 = LOW
  Serial.println("DEBUG: Pump FORWARD - IO2=HIGH, IO3=HIGH, IO0=LOW, IO1=LOW");
}

void pumpReverse() {
  // Reverse: OUT (IO0+IO1) HIGH, IN (IO2+IO3) LOW
  digitalWrite(pumpIN_A, LOW);    // IO2 = LOW
  digitalWrite(pumpIN_B, LOW);    // IO3 = LOW
  digitalWrite(pumpOUT_A, HIGH);  // IO0 = HIGH
  digitalWrite(pumpOUT_B, HIGH);  // IO1 = HIGH
  Serial.println("DEBUG: Pump REVERSE - IO0=HIGH, IO1=HIGH, IO2=LOW, IO3=LOW");
}

void pumpStop() {
  // Stop: All pins LOW
  digitalWrite(pumpIN_A, LOW);    // IO2 = LOW
  digitalWrite(pumpIN_B, LOW);    // IO3 = LOW
  digitalWrite(pumpOUT_A, LOW);   // IO0 = LOW
  digitalWrite(pumpOUT_B, LOW);   // IO1 = LOW
  Serial.println("DEBUG: Pump STOPPED - All pins LOW");
}

//////////////////////////////////////////////////////////////////////////
// pH SENSOR READING FUNCTIONS (ONLY pH, NO EC)
//////////////////////////////////////////////////////////////////////////
void ph_step1(){
  PH.send_read_cmd();
}

void ph_step2(){
  PH.receive_read_cmd();
  if (PH.get_error() == Ezo_board::SUCCESS) {
    current_pH = PH.get_last_received_reading();
  }
}

//////////////////////////////////////////////////////////////////////////
// TEXT BUFFER FUNCTIONS
//////////////////////////////////////////////////////////////////////////
void add2text(int value1,int value2,int value3){
        if (strlen(user_text_buf0) >= (sizeof(user_text_buf0)-100)){
          Serial.println("text buffer full");
          return;
        }
        char temp[11];
        int index = 10;
        char str[12];
        
        DateTime now = rtc.now();
        uint32_t value = now.unixtime();
        do {
            temp[index--] = '0' + (value % 10);
            value /= 10;
        } while (value != 0);
        strcpy(str, temp + index +1);
        
        strcat(user_text_buf0, (str));
        strcat(user_text_buf0, (" - count= "));
        strcat(user_text_buf0, (itoa(value1, ascii, 10)));
        strcat(user_text_buf0, (", pH= "));  
        strcat(user_text_buf0, (itoa(value2, ascii, 10)));
        strcat(user_text_buf0, (", EC= "));  
        strcat(user_text_buf0, (itoa(value3,  ascii, 10)));
        strcat(user_text_buf0, ("\r\n"));
}

void dataappend(int counts,int ampli,int SiPM,int Deadtime) {
  DateTime now = rtc.now();
  String stringValue = String(now.unixtime());
  const char* charValue = stringValue.c_str();
  appendToBuffer(charValue);
  
  String results = " - " + String(counts) + " " + String(ampli) + " " + String(SiPM) + " " + String (Deadtime) + "\r\n";
  const char* charValue1 = results.c_str();
  appendToBuffer(charValue1);
}

void  appendToBuffer(const char* data) {
  int dataLength = strlen(data);
  if (databufferLength + dataLength < sizeof(databuffer)) {
    strcat(databuffer, data);
    databufferLength += dataLength;
  } else {
    Serial.println("Buffer is full. Data not appended.");
  }
}