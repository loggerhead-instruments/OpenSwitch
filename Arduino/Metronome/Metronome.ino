// Metronome Switch Timer
// Loggerhead Instruments
// c2020
// David Mann

// Voltage input: 2.5-12 V regulated to 3.6V by boost-buck converter

// Schedule file loaded from card is list of times and durations
// separated by a space. Maximum of 24 times
// HH:MM duration(minutes)
// example
// 01:00 10
// 09:00 60
// 21:00 2

// Display will stay on for first minute of switches powering on

/*
Current draw with LiPo 3.7V
 - 56 mA at startup with GPS, display On, LED ON
 - 26 mA with display waiting to start, GPS disabled
 - 12 mA sleeping with display on, LED on
 - 1.3 mA sleeping with LED off, display off
 - 5 mA switched on (but sleeping), LED off, display on
 - 1.3 mA On, LED off, display off
 */

#define metronomeVersion 20200127

#define MAXTIMES 200
volatile int nTimes = 4;
volatile int scheduleHour[MAXTIMES];
volatile int scheduleMinute[MAXTIMES];
volatile int duration[MAXTIMES];
float scheduleFracHour[MAXTIMES];

int nextOnTimeIndex;

#include <SPI.h>
#include <Wire.h>
#include <RTCZero.h>
#include <SdFat.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#define BOTTOM 55

#define ledOn LOW
#define ledOff HIGH
#define ledGreen 5
#define ledRed 2
#define relay1 8
#define relay2 9
#define relay3 4
#define relay4 3
#define upButton A3
#define downButton A2
#define enterButton A1
#define pin13 13
#define pin12 12
#define pin6 6
#define pin7 7
#define vSense A4 
#define gpsEnable 30


volatile float voltage;

int rec_dur = 60;
int rec_int = 60;

/* Create an rtc object */
RTCZero rtc;
/* Change these values to set the current initial time and date */
volatile byte second = 0;
volatile byte minute = 0;
volatile byte hour = 17;
volatile byte day = 1;
volatile byte month = 1;
volatile byte year = 20;

boolean introPeriod=1;  //flag for introductory period; used for keeping LED on for a little while

// GPS
#define gpsSerial Serial1
volatile float latitude = 0.0;
volatile float longitude = 0.0;
char latHem, lonHem;
int gpsYear = 20, gpsMonth = 1, gpsDay = 4, gpsHour = 22, gpsMinute = 5, gpsSecond = 0;
int goodGPS = 0;
long gpsTimeOutThreshold = 120000;

// SD file system
SdFat sd;
File dataFile;
int sdFlag = 1; // =0 if can't see sd

void setup() {
  SerialUSB.begin(115200); // Serial monitor
  // while(!SerialUSB);  //wait for Serial (debugging)
  SerialUSB.println("Metronome");
  
  rtc.begin();
  pinMode(ledGreen, OUTPUT);
  pinMode(ledRed, OUTPUT);
  pinMode(relay1, OUTPUT);
  pinMode(relay2, OUTPUT);
  pinMode(relay3, OUTPUT);
  pinMode(relay4, OUTPUT);
  pinMode(upButton, INPUT_PULLUP);
  pinMode(downButton, INPUT_PULLUP);
  pinMode(enterButton, INPUT_PULLUP);
  pinMode(pin13, OUTPUT);
  pinMode(pin12, OUTPUT);
  pinMode(pin6, OUTPUT);
  pinMode(pin7, OUTPUT);
  pinMode(vSense, INPUT);
  pinMode(gpsEnable, OUTPUT);
  
  digitalWrite(ledGreen, ledOn);
  digitalWrite(ledRed, ledOff);
  digitalWrite(relay1, LOW);
  digitalWrite(relay2, LOW);
  digitalWrite(relay3, LOW);
  digitalWrite(relay4, LOW);
  digitalWrite(pin13, LOW);
  digitalWrite(pin12, LOW);
  digitalWrite(pin6, LOW);
  digitalWrite(pin7, LOW);
  digitalWrite(gpsEnable, HIGH);

  Wire.begin();

  displayOn();
  delay(140);
  cDisplay();
  display.println("Metronome");
  display.display();

 while (!sd.begin(10, SPI_FULL_SPEED)) {
    cDisplay();
    display.println("Error");
    display.println();
    SerialUSB.println("Card failed");
    display.println("No SD Card");
    sdFlag = 0;
    display.display();
    delay(2000);
    cDisplay();
    display.println("Error");
    display.display();
    delay(100);
  }
  if(sdFlag){
    logFileHeader();
    int nLines = loadSchedule();
    if(nLines>0) nTimes = nLines;
    SerialUSB.print("Lines:");
    SerialUSB.println(nLines);
  }

    gpsGetTimeLatLon();
    if(!goodGPS){
        SerialUSB.println("Unable to get GPS");
        cDisplay();
        display.println("GPS No Fix");
        display.setTextSize(1);
        display.println("Check Time");
        display.display();
        delay(10000);
    }

  rtc.setTime(gpsHour, gpsMinute, gpsSecond);
  rtc.setDate(gpsDay, gpsMonth, gpsYear);

  manualSettings();
  
  for(int i=0; i<nTimes; i++){
    scheduleFracHour[i] = scheduleHour[i] + (scheduleMinute[i]/60.0);
  }
}

void loop() {
  // get next wake time from list based on current time
  getTime();
  nextOnTimeIndex = getNextOnTime();
  printTime();
  SerialUSB.print("Next Start:");
  SerialUSB.print(scheduleHour[nextOnTimeIndex]);SerialUSB.print(":");
  SerialUSB.print(scheduleMinute[nextOnTimeIndex]);

  cDisplay();
  display.println("Sleeping");
  display.setTextSize(1);
  display.println();
  display.print("Next:");
  display.print(scheduleHour[nextOnTimeIndex]); display.print(":");
  printDigits(scheduleMinute[nextOnTimeIndex]);
  display.println();
  display.print("Dur: ");
  display.print(duration[nextOnTimeIndex]); display.println(" minutes");
  displayVoltage();
  display.display();
  delay(5000);
  digitalWrite(ledGreen, ledOff);
  displayOff();

  
  // set alarm and sleep
  rtc.setAlarmTime(scheduleHour[nextOnTimeIndex],scheduleMinute[nextOnTimeIndex], 0);
  rtc.enableAlarm(rtc.MATCH_HHMMSS);
  rtc.attachInterrupt(alarmMatch);
  rtc.standbyMode();

  // ... Sleeping here ...

  // ... Awake ...
  rtc.detachInterrupt();
  rtc.disableAlarm();

  displayOn();
  cDisplay();
  display.println("On");
  display.setTextSize(1);
  display.println();
  display.print("Dur: ");
  display.print(duration[nextOnTimeIndex]);
  display.println(" minutes");
  displayVoltage();
  display.display();
  // turn on all 4 channels
  relayOn();
  if(sdFlag) logEntry(1);

  // sleep 1 minute at a time and flash led
  rtc.setAlarmSeconds(0);

  for(int i = 0; i<duration[nextOnTimeIndex]; i++){
    getTime();
    int alarmMinute = minute + 1;
    if(alarmMinute>59) alarmMinute = 0;
    rtc.setAlarmMinutes(alarmMinute);
    rtc.enableAlarm(rtc.MATCH_MMSS);
    rtc.attachInterrupt(alarmMatch);
    rtc.standbyMode();
    rtc.detachInterrupt();
    digitalWrite(ledGreen, ledOn);
    delay(100);
    digitalWrite(ledGreen, ledOff);
    displayOff();
  }
  relayOff(); // turn off
  if(sdFlag) logEntry(0);
  displayOn();
  cDisplay();
  display.println("GPS");
  display.setTextSize(1);
  display.print("Clock Update");
  display.display();

  updateGpsTime();  // update real-time clock with GPS time

}

float readVoltage(){
  float vDivider = 39.2/139.2;
  float vReg = 3.6;
  float adcRead = 0;
  int nReads = 10;
  for(int i=0; i<nReads; i++){
    adcRead += (float) analogRead(vSense);
  }
  adcRead = adcRead/nReads;
  float voltage = adcRead * vReg / (vDivider * 1024.0);
  return voltage;
}

int updateGpsTime(){
  gpsGetTimeLatLon();
  if(goodGPS){
    rtc.setTime(gpsHour, gpsMinute, gpsSecond);
    rtc.setDate(gpsDay, gpsMonth, gpsYear);
    if(sdFlag) logEntry(2);
  }
  else{
    if(sdFlag) logEntry(3);
  }
  return(goodGPS);
}

void logEntry(int relayStatus){
   getTime();
   // log file
   float voltage = readVoltage();

   if(File logFile = sd.open("LOG.CSV",  O_CREAT | O_APPEND | O_WRITE)){
      char timestamp[30];
      sprintf(timestamp,"%d-%02d-%02dT%02d:%02d:%02d", year+2000, month, day, hour, minute, second);
      logFile.print(timestamp);
      logFile.print(',');
      logFile.print(voltage); 
      logFile.print(',');
      logFile.print(metronomeVersion);
      logFile.print(',');
      logFile.print(nextOnTimeIndex);
      logFile.print(',');
      logFile.print(duration[nextOnTimeIndex]);
      logFile.print(',');
      switch(relayStatus){
        case 0:
          logFile.print("Off");
          break;
        case 1:
          logFile.print("On");
          break;
        case 2:
          logFile.print("GPS Time");
          break;
        case 3:
          logFile.print("GPS no fix");
          break;
      }
      
      logFile.print(',');
      logFile.print(latitude, 4);
      logFile.print(',');
      logFile.println(longitude, 4);
      logFile.close();
   }
}

void logFileHeader(){
  if(File logFile = sd.open("LOG.CSV",  O_CREAT | O_APPEND | O_WRITE)){
      logFile.println("Datetime,Voltage,Version,Index,Duration,Status,Latitude,Longitude");
      logFile.close();
   }
}

int getNextOnTime(){
  float curHour = hour + (minute/60.0);
  float difHour[MAXTIMES];
  // calc time to next hour from list
  SerialUSB.print("nTimes:"); SerialUSB.println(nTimes);
  SerialUSB.print("Cur hour:"); SerialUSB.println(curHour);
  SerialUSB.println("Frac Hour   Dif Hour");
  for(int i=0; i<nTimes; i++){
    difHour[i] = scheduleFracHour[i] - curHour;
    SerialUSB.print(scheduleFracHour[i]); SerialUSB.print("  ");
    SerialUSB.println(difHour[i]);
  } 

  // what is minimum positive value
  float minDif = 25;
  int nextIndex;
  for(int i=0; i<nTimes; i++){
    if(difHour[i]>0){
      if(difHour[i]<minDif) {
        minDif = difHour[i];
        nextIndex = i;
      }
    }
  }

  // no positive values; so pick most negative one
  if(minDif==25){
    for(int i=0; i<nTimes; i++){
      if(difHour[i]<minDif){
        minDif = difHour[i];
        nextIndex = i;
      }
    }
  }
  return nextIndex;
}

void relayOn(){
  digitalWrite(relay1, HIGH);
  digitalWrite(relay2, HIGH);
  digitalWrite(relay3, HIGH);
  digitalWrite(relay4, HIGH);
}

void relayOff(){
  digitalWrite(relay1, LOW);
  digitalWrite(relay2, LOW);
  digitalWrite(relay3, LOW);
  digitalWrite(relay4, LOW);
}

void alarmMatch()
{
  digitalWrite(LED_BUILTIN, HIGH);
}
