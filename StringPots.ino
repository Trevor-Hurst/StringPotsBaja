// Libraries -------------------------------------------------------
#include <LiquidCrystal.h>
#include <RTClib.h>
#include <SPI.h>
#include <SD.h>

// Constants -------------------------------------------------------
#define LOGGING_BUTTON 2
#define NEW_RUN_BUTTON 3
#define LOGGING_LED    5
#define HALT_LED       4
#define SD_CS          10
#define LCD_RS         23
#define LCD_RW         25
#define LCD_EN         27
#define LCD_DB4        29
#define LCD_DB5        31
#define LCD_DB6        33
#define LCD_DB7        35
#define SP1            A0
#define SP2            A1
#define SP3            A2
#define SP4            A3 
#define CALIBRATION_BUTTON 15   

// Configuration ---------------------------------------------------
const String  CODE_VERSION = "0.4.5";
const String  FILE_EXT     = ".CSV";
const String  TRASH_STR    = "TRASH/";
const String  FILE_HEADER  = "TIME ms, SP1, SP2, SP3, SP4";
const int     TRASH_TIMER  = 6000;
const int     LCD_WIDTH    = 16;
const int     LCD_HEIGHT   = 2;

// Devices ---------------------------------------------------------
LiquidCrystal lcd(LCD_RS, LCD_RW, LCD_EN, LCD_DB4, LCD_DB5, LCD_DB6, LCD_DB7);
RTC_PCF8523   rtc;

// Variables -------------------------------------------------------
//unsigned long currentMillis;
unsigned long logStartTime;
unsigned long lastMillis;
unsigned long prevHoldTime;
unsigned long initialMillisecondOfDay;
String        fileStr;
File          runFile;
bool          copyingFiles;
volatile bool collectingData;
bool          currentRunButtonState;
bool          currentLoggingButtonState;
volatile bool forceScreenDraw;
int           sampleRate;
unsigned int  lastSecond;
int           runIndex;

int           currentCalibrationButtonState;
int           zeroSp1;
int           zeroSp2;
int           zeroSp3;
int           zeroSp4;
int           maxSp1;
int           maxSp2;
int           maxSp3;
int           maxSp4;

// Code ------------------------------------------------------------
void setup() {
  // Serial setup
  //Serial.begin(9600);

  // Pin setup
  pinMode(LOGGING_LED, OUTPUT);
  pinMode(HALT_LED, OUTPUT);
  pinMode(LOGGING_BUTTON, INPUT);
  pinMode(NEW_RUN_BUTTON, INPUT);
  
  // Start up screen 
  customDrawScreen("HELLO, EMS BAJA", "VERSION" + CODE_VERSION);
  delay(2000);

  customDrawScreen(F("SAMPLE RATE:"), F("1000/SECOND..1MS"));  
  /* Removed because limiting sample rate was causing major decreases in actual sample rate.
     Like a third of what we wanted at the ideal 1000 s / sec
  // Set the sample rate
  if (digitalRead(NEW_RUN_BUTTON)){
    sampleRate = 1;
    customDrawScreen("SAMPLE RATE:", "1000/SECOND..1MS");
  }
  else{
    sampleRate = 10;
    customDrawScreen("SAMPLE RATE:", "100/SECOND..10MS");
  }
  */
  delay(2000);

  // SD initialization
  if (!sdCardMounted()){
    customDrawScreen(F("SD ERROR:"), F("NO CARD DETECTED"));
    digitalWrite(HALT_LED, HIGH);
    while(!sdCardMounted());
    digitalWrite(HALT_LED, LOW);
  }

  // RTC initialization
  if (!rtc.begin()){
    customDrawScreen(F("RTC ERROR:"), F("NO RTC DETECTED"));
    digitalWrite(HALT_LED, HIGH);
    while(!rtc.begin());
    digitalWrite(HALT_LED, LOW);
  }

  if (rtc.lostPower()){
    customDrawScreen(F("RTC ERROR:"), F("RTC LOST POWER"));

    // Set the rtc time to when the code was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    rtc.start();

    digitalWrite(HALT_LED, HIGH);
    delay(2000);
    digitalWrite(HALT_LED, LOW);
  }

  // Cache file address
  DateTime now = rtc.now();

  // Year, Month, Day
  String rtcYearStr = String(now.year());
  rtcYearStr = rtcYearStr.substring(2, 4);
 
  String rtcMonthStr = '0' + String(now.month());
  rtcMonthStr = rtcMonthStr.substring(rtcMonthStr.length() - 2, 3);

  String rtcDayStr = '0' + String(now.day());
  rtcDayStr = rtcDayStr.substring(rtcDayStr.length() - 2, 3);

  // Hour, Minute, Second
  int rtcHour = now.hour();
  int rtcMinute = now.minute();
  int rtcSecond = now.second();
  
  String rtcHourStr = '0' + String(rtcHour);
  rtcHourStr = rtcHourStr.substring(rtcHourStr.length() - 2, 3);
  
  String rtcMinuteStr = '0' + String(rtcMinute);
  rtcMinuteStr = rtcMinuteStr.substring(rtcMinuteStr.length() - 2, 3);

  String rtcSecondStr = '0' + String(rtcSecond);
  rtcSecondStr = rtcSecondStr.substring(rtcSecondStr.length() - 2, 3);
  
  fileStr = rtcMonthStr + '-' + rtcDayStr + '-' + rtcYearStr + '/' + rtcHourStr + '-' + rtcMinuteStr + '-' + rtcSecondStr + '/';

  // Cache exact millisecond of the day
  initialMillisecondOfDay = (3600000 * rtcHour) + (60000 * rtcMinute) + (1000 * rtcSecond);

  collectingData = false;
  forceScreenDraw = false;

  // Start the first run
  startNewRun(false);
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Update clock every second
  if (lastSecond != currentMillis / 1000 || forceScreenDraw){
    lastSecond = currentMillis / 1000;
    forceScreenDraw = false;
    drawRunScreen();
  }

  // Switches are active low
  bool isRunButtonPressed = !digitalRead(NEW_RUN_BUTTON);
  bool isLoggingButtonPressed = !digitalRead(LOGGING_BUTTON);
  bool isCalibrationButtonPressed = !digitalRead(CALIBRATION_BUTTON);
  
  // Next run button
  if (isRunButtonPressed && !currentRunButtonState){
    currentRunButtonState = true;
    prevHoldTime = millis();
  }
  else if (!isRunButtonPressed && currentRunButtonState && !collectingData){
    currentRunButtonState = false;
    runIndex++;
    startNewRun(false);
  }

  // Hold the run button to clear the last run
  if (isRunButtonPressed && currentRunButtonState){
    if ((millis() - prevHoldTime) > TRASH_TIMER){
      startNewRun(true);
      currentRunButtonState = false;
    }
  }

  // Logging button
  if (isLoggingButtonPressed && !currentLoggingButtonState){
    currentLoggingButtonState = true;
    openDataFile();
    collectingData = true;

    digitalWrite(LOGGING_LED, collectingData);
    forceScreenDraw = true;
  }
  else if (!isLoggingButtonPressed && currentLoggingButtonState){
    currentLoggingButtonState = false;
    saveDataFile();
    collectingData = false;

    digitalWrite(LOGGING_LED, collectingData);
    forceScreenDraw = true;
  }

  //Calibration Mode
   if (isCalibrationButtonPressed && currentCalibrationButtonState==0)
 {
  currentCalibrationButtonState++;
  if (currentCalibrationButtonState==1) {
    zeroSp1 = analogRead(SP1);//zero value = 0 inches
    zeroSp2 = analogRead(SP2); 
    zeroSp3 = analogRead(SP3); 
    zeroSp4 = analogRead(SP4);
    }
 }
else if (isCalibrationButtonPressed && currentCalibrationButtonState>=1) {
  currentCalibrationButtonState++;
      if (currentCalibrationButtonState==2) {
      maxSp1 = analogRead(SP1);//max value = 26 inches
      maxSp2 = analogRead(SP2);
      maxSp3 = analogRead(SP3);
      maxSp4 = analogRead(SP4);
    }
  currentCalibrationButtonState = 0;
 }

  // Write new line to sd card
  if (collectingData && runFile && !copyingFiles){


    //ADD Formulas for Velocity here. All that's needed is formula and values for distance traveled vs analog input.---------------------------------------
    uint32_t time = millis() - logStartTime;
    uint16_t sp1 = analogRead(SP1);
    uint16_t sp2 = analogRead(SP2);
    uint16_t sp3 = analogRead(SP3);
    uint16_t sp4 = analogRead(SP4);
    uint16_t shock1 = ((sp1-zeroSp1)/maxSp1)*26;
    uint16_t shock2 = ((sp2-zeroSp2)/maxSp2)*26;
    uint16_t shock3 = ((sp3-zeroSp3)/maxSp3)*26;
    uint16_t shock4 = ((sp4-zeroSp4)/maxSp4)*26;
    char data[32];
    sprintf(data, "%lu,%u,%u,%u,%u,%u,%u,%u,%u", time, sp1, sp2, sp3, sp4, shock1, shock2, shock3, shock4);
    runFile.println(data);
    
    lastMillis = millis();
  }
}

void openDataFile(){
  // Verify an sd card is mounted
  if (!sdCardMounted()){
    customDrawScreen(F("SD ERROR:"), F("NO CARD DETECTED"));
    digitalWrite(HALT_LED, HIGH);
    while(!sdCardMounted());
    digitalWrite(HALT_LED, LOW);
  }

  // Ensure the file address exists
  if(!SD.exists(fileStr))
    SD.mkdir(fileStr);

  // Open/create run file
  runFile = SD.open(fileStr + "RUN" + String(runIndex) + FILE_EXT, FILE_WRITE);
  logStartTime = millis();
  
  // Write headers if no data is present in the file
  if (runFile.peek() == -1)
    runFile.println(FILE_HEADER);
}

void saveDataFile(){
  // Verify an sd card is mounted
  if (!sdCardMounted()){
    customDrawScreen(F("SD ERROR:"), F("NO CARD DETECTED"));
    digitalWrite(HALT_LED, HIGH);
    while(!sdCardMounted());
    digitalWrite(HALT_LED, LOW);
  }

  // Close the run file if one exists
  if (runFile)
    runFile.close();
}

void startNewRun(bool trashLastRun){
  // Stop collecting data
  saveDataFile();

  if (trashLastRun)
    trashLastFile();

  drawRunScreen();
}

void trashLastFile(){
  customDrawScreen(F("TRASHING DATA"), F("PLEASE WAIT..."));
  digitalWrite(HALT_LED, HIGH);
  copyingFiles = true;

  // Re-open run file in read mode
  String currFileStr = fileStr + "RUN" + String(runIndex) + FILE_EXT;
  File readFile = SD.open(currFileStr, FILE_READ);
  
  if(!readFile){
    customDrawScreen("ERROR: FILE DNE", "FAIL TRASH RUN" + String(runIndex));
    digitalWrite(HALT_LED, HIGH);
    delay(5000);
    digitalWrite(HALT_LED, LOW);
    readFile.close();
    copyingFiles = false;

    return;
  }

  // Create trash folder if not created already
  if (!SD.exists(TRASH_STR + fileStr))
    SD.mkdir(TRASH_STR + fileStr);

  // Create new duplicate run file in trash folder
  String dupeFileStr = TRASH_STR + currFileStr;
  File dupeFile = SD.open(dupeFileStr, FILE_WRITE);

  // Copy and paste data from original file to duplicate
  size_t data;
  uint8_t buf[64];
  while ((data = readFile.read(buf, sizeof(buf))) > 0)
    dupeFile.write(buf, data);
    
  // Close all files
  readFile.close();
  dupeFile.close();
  SD.remove(currFileStr);

  digitalWrite(HALT_LED, LOW);
  copyingFiles = false;
  // Decrease run counter so we don't skip run counts after trashing the last file
  runIndex--;
}

void drawRunScreen(){
  lcd.begin(LCD_WIDTH, LCD_HEIGHT);

  // Draw run index
  lcd.setCursor(0, 0);
  lcd.print("RUN " + String(runIndex));

  // Draw time
  DateTime now = rtc.now();

  String rtcHour = '0' + String(now.hour());
  rtcHour = rtcHour.substring(rtcHour.length() - 2, 3);
  
  String rtcMinute = '0' + String(now.minute());
  rtcMinute = rtcMinute.substring(rtcMinute.length() - 2, 3);

  String rtcSecond = '0' + String(now.second());
  rtcSecond = rtcSecond.substring(rtcSecond.length() - 2, 3);
  
  lcd.setCursor(8, 0);
  lcd.print(rtcHour + ':' + rtcMinute + ':' + rtcSecond);

  // Draw logging status
  lcd.setCursor(0, 1);
  lcd.print((collectingData) ? F("LOGGING..WRITING") : F("LOGGING....READY"));
}

void customDrawScreen(String top, String bottom){
  lcd.begin(LCD_WIDTH, LCD_HEIGHT);
  lcd.setCursor(0, 0);
  lcd.print(top);
  lcd.setCursor(0, 1);
  lcd.print(bottom);
}

bool sdCardMounted(){
  return SD.begin(SD_CS);
}
