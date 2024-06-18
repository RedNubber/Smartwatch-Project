#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else 
#define ARDUINO_RUNNING_CORE 1
#endif

#include <TFT_eSPI.h>  // khai báo thư viện TFT dùng để hiển thị trên màn hình
#include <SPI.h>
#include "RTClib.h" //khai báo thư viện sử dụng đồng hồ
#include "MAX30100_PulseOximeter.h" // khai báo thư viện cho cảm biến nhịp tim và SPO2
#include <Adafruit_MPU6050.h> // khai báo thư viện cho cảm biến gia tốc
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include "FS.h"
#include "BluetoothSerial.h" // khai báo thư viện Bluetooth


#define BLACK_SPOT
#define REPORTING_PERIOD_MS 1000
#define HEART_RATE_THRESHOLD 140
#define touch 25
#define TFT_GREY 0x5AEB

//----------------------------------------------MAX 30100 Variable-------------------------------------------//
PulseOximeter pox;
float SpO2;
float BPM;

//-------------------------------------------------RTC variable----------------------------------------------//
RTC_DS1307 rtc;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
byte omm = 99, oss = 99;
byte xcolon = 0, xsecs = 0;
int year;
int month;
int day;
int hh;
int mm;
int ss;
String date;

//-------------------------------------------------TFT variable----------------------------------------------//
TFT_eSPI tft = TFT_eSPI();       // Invoke custom library
unsigned int colour = 0;

unsigned char touch_value2 = 0;

//--------------------------------------------------MPU Variable---------------------------------------------//
Adafruit_MPU6050 mpu;
float vectorprevious;
float vector;
float totalvector;
int Steps = 0;
int acc_x;
int acc_y;
int acc_z;
int gyro_x;
int gyro_y;
int gyro_z;


//------------------------------------------------------Task-----------------------------------------------------//
TaskHandle_t Task_1; //Khai bao Task1: thay doi man hinh
TaskHandle_t Task_2; //Khai bao Task2: RTC, PULSE and SPO2
TaskHandle_t Task_3; //Khai bao Task3: Warning



int incoming;
int button;
int value;

BluetoothSerial ESP_BT; //khai báo bluetooth cho esp
void onBeatDetected()
{
  Serial.println("Beat detected!");
}

static TimerHandle_t auto_reload_timer = NULL;

void myTimerCallback(TimerHandle_t xTimer) {
  switch (button) {
    case 3:  
      Serial.print("Button 1:"); Serial.println(value);
      if(value==2) ESP_BT.write(BPM); //giá trị value = 2 thì gửi giá trị BPM về app
      if(value==0) ESP_BT.write(SpO2);//giá trị value = 0 thì gửi giá trị SpO2 về app
      if(value==1) ESP_BT.write(Steps);//giá trị value = 1 thì gửi giá trị Steps về app
      break;
    }
  if (ESP_BT.available()) //nếu esp được kết nối với app
  {
      
    Serial.println("Connected");
    incoming = ESP_BT.read(); //đọc giá trị nút nhấn từ app gửi về esp

    button = floor(incoming / 10); 
    value = incoming%10; //có 3 giá trị value 0, 1, 2 tương ứng với 3 giá trị gửi từ esp về app
  }
}


void setup(void) {
  Serial.begin(115200);
  ESP_BT.begin("ESP32_Control"); //đặt tên Bluetooth cho ESP là ESP32_Control
  //------------------------------------------Touch sensor-----------------------------------//
  pinMode(touch, INPUT); //khai báo input cho touch sensor

 //-----------------------cài đặt giao diện màn hình ban đầu-----------------------//
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  //--------------------------------------------RTC set up-----------------------------------//
  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    while (1) delay(10);
  }

  if (! rtc.isrunning()) {
    Serial.println("RTC is NOT running, let's set the time!");
    // When time needs to be set on a new device, or after a power loss, the
    // following line sets the RTC to the date & time this sketch was compiled
    Serial.println("Please Reset");
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }
  
  //-------------------------------------------------MPU6050 set up--------------------------//
  while (!Serial)
    delay(10); // will pause Zero, Leonardo, etc until serial console opens

  Serial.println("Adafruit MPU6050 test!");

  // Try to initialize!
  if (!mpu.begin(0x69)) {
    Serial.println("Failed to find MPU6050 chip");
    while (1) {
      delay(10);
    }
  }
  Serial.println("MPU6050 Found!");
  // set accelerometer range to +-8G
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  // set gyro range to +- 500 deg/s
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  // set filter bandwidth to 21 Hz
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  delay(100);
  Serial.println("");
  delay(100);
  //------------------------------------------Pulse and spo2 set up-------------------------------//
  Serial.println("Initializing pulse oximeter..");

  // Initialize the PulseOximeter instance
  if (!pox.begin()) {
      Serial.println("FAILED");
      for(;;);
  } else {
      Serial.println("SUCCESS");
      pox.setOnBeatDetectedCallback(onBeatDetected);
  }

  // Register a callback for the beat detection
    
  pox.setIRLedCurrent(MAX30100_LED_CURR_7_6MA);

  xTaskCreatePinnedToCore(Task1Code,"Task_1",10000,NULL,3,&Task_1,1); // Task 1 thay doi man hinh
  delay(10);
  xTaskCreatePinnedToCore(Task2Code,"Task_2",50000,NULL,3,&Task_2,1); // Task 2 cap nhat cam bien
  delay(10);
  xTaskCreatePinnedToCore(Task3Code,"Task_3",10000,NULL,3,&Task_3,0); // Task 3 canh bao
  delay(10);


// dùng timer để có thể upload giá trị sensor từ esp lên app mỗi 2 giây 1 lần
  auto_reload_timer = xTimerCreate(
                      "Auto-reload timer",        // Name of timer
                      2000 / portTICK_PERIOD_MS,  // Period of timer (in ticks)
                      pdTRUE,                     // Auto-reload
                      (void *)1,                  // Timer ID
                      myTimerCallback);
  xTimerStart(auto_reload_timer, portMAX_DELAY);
}

void loop() {
}

void Screen1() //màn hình đồng hồ
{
  tft.drawRoundRect(10, 10, 300, 220, 20, TFT_WHITE);
  tft.drawRoundRect(11, 11, 298, 218, 20, TFT_WHITE);
  tft.drawRoundRect(12, 12, 296, 216, 20, TFT_WHITE);

  // Update digital time
  int xpos = 50;
  int ypos = 50; // Top left corner ot clock text, about half way down
  int ysecs = ypos + 24;

    if (hh < 10) xpos += tft.drawChar('0', xpos, ypos, 7); // Add hours leading zero for 24 hr clock
    xpos += tft.drawNumber(hh, xpos, ypos, 7);             // Draw hours
    xcolon = xpos; // Save colon coord for later to flash on/off later
    xpos += tft.drawChar(':', xpos, ypos , 7);
    if (mm < 10) xpos += tft.drawChar('0', xpos, ypos, 7); // Add minutes leading zero
    xpos += tft.drawNumber(mm, xpos, ypos, 7);             // Draw minutes
    xsecs = xpos; // Sae seconds 'x' position for later display updates
  
  if (oss != ss) { // Redraw seconds time every second
    oss = ss;
    xpos = xsecs;

    if (ss % 2) { // Flash the colons on/off
      tft.setTextColor(0x39C4, TFT_BLACK);        // Set colour to grey to dim colon
      tft.drawChar(':', xcolon, ypos, 7);     // Hour:minute colon
      xpos += tft.drawChar(':', xsecs, ysecs - 24, 7); // Seconds colon
      tft.setTextColor(TFT_WHITE, TFT_BLACK);    // Set colour back to yellow
    }
    else {
      tft.drawChar(':', xcolon, ypos, 7);     // Hour:minute colon
      xpos += tft.drawChar(':', xsecs, ysecs - 24, 7); // Seconds colon
    }

    //Draw seconds
    if (ss < 10) xpos += tft.drawChar('0', xpos, ysecs -24, 7); // Add leading zero
    tft.drawNumber(ss, xpos, ysecs - 24, 7);                     // Draw seconds
  }

  tft.drawRoundRect(35, 35, 245, 80, 20, TFT_WHITE);
  tft.drawRoundRect(36, 36, 243, 78, 20, TFT_WHITE);
  tft.drawRoundRect(37, 37, 241, 76, 20, TFT_WHITE);


  int shift = 60; //biến shift dùng để dịch các bit ảnh
  tft.drawNumber(day, 15+shift, 130, 4);
  tft.drawChar('-', 50+shift, 130, 4);
  if (month < 10) tft.drawChar('0', 70+shift, 130, 4);
  tft.drawNumber(month, 85+shift, 130, 4);
  tft.drawChar('-', 110+shift, 130, 4);
  tft.drawNumber(year, 125+shift, 130, 4);
  tft.setTextSize(1);
  tft.drawCentreString(date, 150, 160, 4);
  tft.setTextSize(1);
  delay(10);
}

void Screen3() //màn hình nhịp tim và spo2
{
  tft.fillRect(30, 140, 100, 39, TFT_BLACK);
  tft.fillRect(180, 140, 100, 39, TFT_BLACK);
  tft.drawNumber(BPM, 40, 140, 4);
  tft.drawNumber(SpO2, 220, 140, 4);

  heart();
  SPo2_icon();
}
void Screen2()//màn hình steps
{
  Step();
  tft.setTextSize(1);
  tft.drawNumber(Steps, 50, 180, 6);
  tft.setTextSize(2);
  tft.drawCentreString("Steps", 250, 180, 4);
  tft.setTextSize(1);
}

void heart() //dùng thư viện tft để vẽ icon trái tim
{
  int shift_x = 0;
  int shift_y = 0;
  
  //-----------------------------------------Hàng 1------------------------------------//
  tft.fillRect(48+shift_x, 40+shift_y, 16, 4, TFT_WHITE);
  tft.fillRect(76+shift_x, 40+shift_y, 16, 4, TFT_WHITE);

  //-----------------------------------------Hàng 2-----------------------------------//
  tft.fillRect(44+shift_x, 44+shift_y, 4, 4, TFT_WHITE);
  tft.fillRect(64+shift_x, 44+shift_y, 4, 4, TFT_WHITE);
  tft.fillRect(72+shift_x, 44+shift_y, 4, 4, TFT_WHITE);
  tft.fillRect(92+shift_x, 44+shift_y, 4, 4, TFT_WHITE);
  //-----------------------------------------Hàng 3-----------------------------------//
  tft.fillRect(40+shift_x, 48+shift_y, 4, 4, TFT_WHITE);
  tft.fillRect(68+shift_x, 48+shift_y, 4, 4, TFT_WHITE);
  tft.fillRect(96+shift_x, 48+shift_y, 4, 4, TFT_WHITE);
  //---------------------------------------Hàng 4,5,6---------------------------------//
  tft.fillRect(40+shift_x, 52+shift_y, 4, 12, TFT_WHITE);
  tft.fillRect(96+shift_x, 52+shift_y, 4, 12, TFT_WHITE);
  //------------------------------------Hàng 7,8,9,10,11,12---------------------------//
  tft.fillRect(44+shift_x, 64+shift_y, 4, 4, TFT_WHITE);
  tft.fillRect(48+shift_x, 68+shift_y, 4, 4, TFT_WHITE);
  tft.fillRect(52+shift_x, 72+shift_y, 4, 4, TFT_WHITE);
  tft.fillRect(56+shift_x, 76+shift_y, 4, 4, TFT_WHITE);
  tft.fillRect(60+shift_x, 80+shift_y, 4, 4, TFT_WHITE);
  tft.fillRect(64+shift_x, 84+shift_y, 4, 4, TFT_WHITE);

  tft.fillRect(72+shift_x, 84+shift_y, 4, 4, TFT_WHITE);
  tft.fillRect(76+shift_x, 80+shift_y, 4, 4, TFT_WHITE);
  tft.fillRect(80+shift_x, 76+shift_y, 4, 4, TFT_WHITE);
  tft.fillRect(84+shift_x, 72+shift_y, 4, 4, TFT_WHITE);
  tft.fillRect(88+shift_x, 68+shift_y, 4, 4, TFT_WHITE);
  tft.fillRect(92+shift_x, 64+shift_y, 4, 4, TFT_WHITE);

  tft.fillRect(68+shift_x, 88+shift_y, 4, 4, TFT_WHITE);
  //----------------------------------------Filler-----------------------------------//
  tft.fillRect(48+shift_x, 44+shift_y, 16, 4, TFT_RED);
  tft.fillRect(76+shift_x, 44+shift_y, 16, 4, TFT_RED);

  tft.fillRect(44+shift_x, 48+shift_y, 24, 4, TFT_RED);
  tft.fillRect(72+shift_x, 48+shift_y, 24, 4, TFT_RED);

  tft.fillRect(44+shift_x, 52+shift_y, 52, 12, TFT_RED);

  tft.fillRect(48+shift_x, 64+shift_y, 44, 4, TFT_RED);
  tft.fillRect(52+shift_x, 68+shift_y, 36, 4, TFT_RED);
  tft.fillRect(56+shift_x, 72+shift_y, 28, 4, TFT_RED);
  tft.fillRect(60+shift_x, 76+shift_y, 20, 4, TFT_RED);
  tft.fillRect(64+shift_x, 80+shift_y, 12, 4, TFT_RED);
  tft.fillRect(68+shift_x, 84+shift_y, 4, 4, TFT_RED);

  tft.setTextColor(TFT_WHITE);  
  tft.drawCentreString("BPM", 56, 180, 4);
  tft.drawCentreString("%", 240, 180, 4);
}


void Step() //dùng thư viện tft để vẽ icon step
{
  int shift_x_step = 90;
  int shift_y_step = -60;

  //-----------------------------------------Step-----------------------------------//
  tft.fillRect(52+shift_x_step, 100+shift_y_step, 8, 4, TFT_WHITE);
  tft.fillRect(48+shift_x_step, 104+shift_y_step, 4, 4, TFT_WHITE);
  tft.fillRect(60+shift_x_step, 104+shift_y_step, 4, 4, TFT_WHITE);
  tft.fillRect(44+shift_x_step, 108+shift_y_step, 4, 4, TFT_WHITE);
  tft.fillRect(64+shift_x_step, 108+shift_y_step, 4, 4, TFT_WHITE);

  tft.fillRect(40+shift_x_step, 112+shift_y_step, 4, 32, TFT_WHITE);
  tft.fillRect(64+shift_x_step, 112+shift_y_step, 4, 32, TFT_WHITE);
  tft.fillRect(44+shift_x_step, 144+shift_y_step, 20, 4, TFT_WHITE);

  tft.fillRect(44+shift_x_step, 156+shift_y_step, 20, 4, TFT_WHITE);
  tft.fillRect(40+shift_x_step, 160+shift_y_step, 4, 12, TFT_WHITE);
  tft.fillRect(64+shift_x_step, 160+shift_y_step, 4, 12, TFT_WHITE);
  tft.fillRect(44+shift_x_step, 172+shift_y_step, 20, 4, TFT_WHITE);
  //-----------------------------------------Step 2--------------------------------//
  tft.fillRect(90+shift_x_step, 116+shift_y_step, 8, 4, TFT_WHITE);
  tft.fillRect(86+shift_x_step, 120+shift_y_step, 4, 4, TFT_WHITE);
  tft.fillRect(98+shift_x_step, 120+shift_y_step, 4, 4, TFT_WHITE);
  tft.fillRect(82+shift_x_step, 124+shift_y_step, 4, 4, TFT_WHITE);
  tft.fillRect(102+shift_x_step, 124+shift_y_step, 4, 4, TFT_WHITE);

  tft.fillRect(82+shift_x_step, 128+shift_y_step, 4, 32, TFT_WHITE);
  tft.fillRect(106+shift_x_step, 128+shift_y_step, 4, 32, TFT_WHITE);
  tft.fillRect(86+shift_x_step, 160+shift_y_step, 20, 4, TFT_WHITE);

  tft.fillRect(86+shift_x_step, 172+shift_y_step, 20, 4, TFT_WHITE);
  tft.fillRect(82+shift_x_step, 176+shift_y_step, 4, 12, TFT_WHITE);
  tft.fillRect(106+shift_x_step, 176+shift_y_step, 4, 12, TFT_WHITE);
  tft.fillRect(86+shift_x_step, 188+shift_y_step, 20, 4, TFT_WHITE);
  //---------------------------------------Step Filler 1-----------------------------//
  tft.fillRect(52+shift_x_step, 104+shift_y_step, 8, 4, TFT_GREEN);
  tft.fillRect(48+shift_x_step, 108+shift_y_step, 16, 4, TFT_GREEN);
  tft.fillRect(44+shift_x_step, 112+shift_y_step, 20, 32, TFT_GREEN);
  tft.fillRect(44+shift_x_step, 160+shift_y_step, 20, 12, TFT_GREEN);
  //---------------------------------------Step Filler 2-----------------------------//
  tft.fillRect(90+shift_x_step, 120+shift_y_step, 8, 4, TFT_GREEN);
  tft.fillRect(86+shift_x_step, 124+shift_y_step, 16, 4, TFT_GREEN);
  tft.fillRect(86+shift_x_step, 128+shift_y_step, 20, 32, TFT_GREEN);
  tft.fillRect(86+shift_x_step, 176+shift_y_step, 20, 12, TFT_GREEN);
}


void SPo2_icon() //dùng thư viện tft để vẽ icon SpO2
{
  int shift_x_O2 = 0;
  int shift_y_O2 = -68;
  //----------------------------------------SPO2-----------------------------------//

  //------------------------fill color-------------------------------//
  tft.fillRect(202+shift_x_O2, 132+shift_y_O2, 74, 12, TFT_PINK);
  tft.fillRect(204+shift_x_O2, 124+shift_y_O2, 70, 8, TFT_PINK);
  tft.fillRect(204+shift_x_O2, 144+shift_y_O2, 70, 8, TFT_PINK);

  tft.fillRect(206+shift_x_O2, 116+shift_y_O2, 66, 8, TFT_PINK);
  tft.fillRect(206+shift_x_O2, 152+shift_y_O2, 66, 8, TFT_PINK);
  tft.fillRect(210+shift_x_O2, 110+shift_y_O2, 58, 8, TFT_PINK);
  tft.fillRect(212+shift_x_O2, 108+shift_y_O2, 54, 8, TFT_PINK);
  tft.fillRect(220+shift_x_O2, 104+shift_y_O2, 38, 4, TFT_PINK);
  tft.fillRect(224+shift_x_O2, 100+shift_y_O2, 32, 4, TFT_PINK);

  
  
  
  tft.fillRect(214+shift_x_O2, 158+shift_y_O2, 16, 2, TFT_RED);
  tft.fillRect(218+shift_x_O2, 156+shift_y_O2, 10, 2, TFT_RED);

  tft.fillRect(238+shift_x_O2, 158+shift_y_O2, 16, 2, TFT_RED);
  tft.fillRect(242+shift_x_O2, 156+shift_y_O2, 10, 2, TFT_RED);



  tft.fillRect(212+shift_x_O2, 162+shift_y_O2, 52, 4, TFT_RED);

  tft.fillRect(210+shift_x_O2, 160+shift_y_O2, 58, 4, TFT_RED);
  tft.fillRect(214+shift_x_O2, 164+shift_y_O2, 48, 6, TFT_RED);
  tft.fillRect(218+shift_x_O2, 170+shift_y_O2, 38, 4, TFT_RED);
  tft.fillRect(234+shift_x_O2, 174+shift_y_O2, 12, 2, TFT_RED);


  tft.fillRect(200+shift_x_O2, 132+shift_y_O2, 2, 12, TFT_WHITE);
  //---------------------------------------cột 2----------------------------------//
  tft.fillRect(202+shift_x_O2, 124+shift_y_O2, 2, 8, TFT_WHITE);
  tft.fillRect(202+shift_x_O2, 144+shift_y_O2, 2, 8, TFT_WHITE);
  //---------------------------------------cột 3----------------------------------//
  tft.fillRect(204+shift_x_O2, 120+shift_y_O2, 2, 4, TFT_WHITE);
  tft.fillRect(204+shift_x_O2, 152+shift_y_O2, 2, 4, TFT_WHITE);
  //---------------------------------------cột 4----------------------------------//
  tft.fillRect(206+shift_x_O2, 116+shift_y_O2, 2, 4, TFT_WHITE);
  tft.fillRect(206+shift_x_O2, 156+shift_y_O2, 2, 4, TFT_WHITE);
  //---------------------------------------cột 5----------------------------------//
  tft.fillRect(208+shift_x_O2, 114+shift_y_O2, 2, 4, TFT_WHITE);
  tft.fillRect(208+shift_x_O2, 158+shift_y_O2, 2, 4, TFT_WHITE);
  
  //---------------------------------------cột 6----------------------------------//
  tft.fillRect(210+shift_x_O2, 112+shift_y_O2, 2, 2, TFT_WHITE);
  tft.fillRect(210+shift_x_O2, 162+shift_y_O2, 2, 2, TFT_WHITE);

  //---------------------------------------cột 7----------------------------------//
  tft.fillRect(210+shift_x_O2, 110+shift_y_O2, 2, 2, TFT_WHITE);
  tft.fillRect(210+shift_x_O2, 164+shift_y_O2, 2, 2, TFT_WHITE);
  
  //---------------------------------------cột 8----------------------------------//
  tft.fillRect(212+shift_x_O2, 108+shift_y_O2, 2, 2, TFT_WHITE);
  tft.fillRect(212+shift_x_O2, 166+shift_y_O2, 2, 2, TFT_WHITE);

  //---------------------------------------cột 9----------------------------------//
  tft.fillRect(214+shift_x_O2, 108+shift_y_O2, 2, 2, TFT_WHITE);
  tft.fillRect(214+shift_x_O2, 166+shift_y_O2, 2, 2, TFT_WHITE);

  //---------------------------------------cột 10----------------------------------//
  tft.fillRect(216+shift_x_O2, 106+shift_y_O2, 2, 2, TFT_WHITE);
  tft.fillRect(216+shift_x_O2, 168+shift_y_O2, 2, 2, TFT_WHITE);

  //---------------------------------------cột 10----------------------------------//
  tft.fillRect(218+shift_x_O2, 106+shift_y_O2, 2, 2, TFT_WHITE);
  tft.fillRect(218+shift_x_O2, 166+shift_y_O2, 2, 4, TFT_WHITE);

  //---------------------------------------cột 11----------------------------------//
  tft.fillRect(220+shift_x_O2, 104+shift_y_O2, 2, 2, TFT_WHITE);
  tft.fillRect(220+shift_x_O2, 170+shift_y_O2, 2, 2, TFT_WHITE);

  tft.fillRect(222+shift_x_O2, 102+shift_y_O2, 4, 2, TFT_WHITE);
  tft.fillRect(222+shift_x_O2, 172+shift_y_O2, 4, 2, TFT_WHITE);

  tft.fillRect(226+shift_x_O2, 100+shift_y_O2, 8, 2, TFT_WHITE);
  tft.fillRect(226+shift_x_O2, 174+shift_y_O2, 8, 2, TFT_WHITE);

  tft.fillRect(234+shift_x_O2, 98+shift_y_O2, 12, 2, TFT_WHITE);/////////////////////////////////
  tft.fillRect(234+shift_x_O2, 176+shift_y_O2, 12, 2, TFT_WHITE);///////////////////////////////////

  tft.fillRect(246+shift_x_O2, 100+shift_y_O2, 8, 2, TFT_WHITE);
  tft.fillRect(246+shift_x_O2, 174+shift_y_O2, 8, 2, TFT_WHITE);

  tft.fillRect(252+shift_x_O2, 102+shift_y_O2, 4, 2, TFT_WHITE);
  tft.fillRect(252+shift_x_O2, 172+shift_y_O2, 4, 2, TFT_WHITE);

  tft.fillRect(256+shift_x_O2, 104+shift_y_O2, 2, 2, TFT_WHITE);
  tft.fillRect(256+shift_x_O2, 170+shift_y_O2, 2, 2, TFT_WHITE);

  //---------------------------------------cột 10----------------------------------//
  tft.fillRect(258+shift_x_O2, 106+shift_y_O2, 4, 2, TFT_WHITE);
  tft.fillRect(258+shift_x_O2, 168+shift_y_O2, 4, 2, TFT_WHITE);



  //---------------------------------------cột 10----------------------------------//
  tft.fillRect(262+shift_x_O2, 108+shift_y_O2, 2, 2, TFT_WHITE);
  tft.fillRect(262+shift_x_O2, 166+shift_y_O2, 2, 2, TFT_WHITE);


  //---------------------------------------cột 7----------------------------------//
  tft.fillRect(264+shift_x_O2, 110+shift_y_O2, 2, 2, TFT_WHITE);
  tft.fillRect(264+shift_x_O2, 164+shift_y_O2, 2, 2, TFT_WHITE);

  //---------------------------------------cột 7----------------------------------//
  tft.fillRect(266+shift_x_O2, 112+shift_y_O2, 2, 2, TFT_WHITE);
  tft.fillRect(266+shift_x_O2, 162+shift_y_O2, 2, 2, TFT_WHITE);

  //---------------------------------------cột 5----------------------------------//
  tft.fillRect(268+shift_x_O2, 114+shift_y_O2, 2, 4, TFT_WHITE);
  tft.fillRect(268+shift_x_O2, 158+shift_y_O2, 2, 4, TFT_WHITE);

  //---------------------------------------cột 4----------------------------------//
  tft.fillRect(270+shift_x_O2, 116+shift_y_O2, 2, 4, TFT_WHITE);
  tft.fillRect(270+shift_x_O2, 156+shift_y_O2, 2, 4, TFT_WHITE);

  //---------------------------------------cột 3----------------------------------//
  tft.fillRect(272+shift_x_O2, 120+shift_y_O2, 2, 4, TFT_WHITE);
  tft.fillRect(272+shift_x_O2, 152+shift_y_O2, 2, 4, TFT_WHITE);


  //---------------------------------------cột 2----------------------------------//
  tft.fillRect(274+shift_x_O2, 124+shift_y_O2, 2, 8, TFT_WHITE);
  tft.fillRect(274+shift_x_O2, 144+shift_y_O2, 2, 8, TFT_WHITE);

  tft.fillRect(276+shift_x_O2, 132+shift_y_O2, 2, 12, TFT_WHITE);

  //----------------------------------------fix-----------------------------------//
  tft.fillRect(254+shift_x_O2, 100+shift_y_O2, 2, 2, TFT_BLACK);
  tft.fillRect(266+shift_x_O2, 110+shift_y_O2, 2, 2, TFT_BLACK);
  tft.fillRect(268+shift_x_O2, 112+shift_y_O2, 2, 2, TFT_BLACK);
  tft.fillRect(218+shift_x_O2, 172+shift_y_O2, 4, 2, TFT_BLACK);
  tft.fillRect(218+shift_x_O2, 170+shift_y_O2, 2, 2, TFT_BLACK);
  tft.fillRect(214+shift_x_O2, 168+shift_y_O2, 2, 2, TFT_BLACK);


  //-------------------------cur line-------------------------//

  tft.fillRect(210+shift_x_O2, 160+shift_y_O2, 4, 2, TFT_WHITE);
  tft.fillRect(214+shift_x_O2, 158+shift_y_O2, 4, 2, TFT_WHITE);
  tft.fillRect(218+shift_x_O2, 156+shift_y_O2, 4, 2, TFT_WHITE);
  tft.fillRect(222+shift_x_O2, 154+shift_y_O2, 4, 2, TFT_WHITE);
  tft.fillRect(226+shift_x_O2, 156+shift_y_O2, 4, 2, TFT_WHITE);
  tft.fillRect(230+shift_x_O2, 158+shift_y_O2, 4, 2, TFT_WHITE);
  tft.fillRect(234+shift_x_O2, 160+shift_y_O2, 4, 2, TFT_WHITE);
  tft.fillRect(238+shift_x_O2, 158+shift_y_O2, 4, 2, TFT_WHITE);
  tft.fillRect(242+shift_x_O2, 156+shift_y_O2, 4, 2, TFT_WHITE);
  tft.fillRect(246+shift_x_O2, 154+shift_y_O2, 4, 2, TFT_WHITE);
  tft.fillRect(250+shift_x_O2, 156+shift_y_O2, 4, 2, TFT_WHITE);
  tft.fillRect(254+shift_x_O2, 158+shift_y_O2, 4, 2, TFT_WHITE);
  tft.fillRect(258+shift_x_O2, 160+shift_y_O2, 4, 2, TFT_WHITE);
  tft.fillRect(262+shift_x_O2, 158+shift_y_O2, 4, 2, TFT_WHITE);
  tft.fillRect(266+shift_x_O2, 156+shift_y_O2, 4, 2, TFT_WHITE);
  tft.fillRect(270+shift_x_O2, 154+shift_y_O2, 4, 2, TFT_WHITE);
}


void Heart_and_SPo2() //hàm đo nhịp tim và SpO2
{
  pox.update();
  BPM = pox.getHeartRate();
  SpO2 = pox.getSpO2();

}


void Step_counting() //hàm đếm bước chân
{
  get6050Data();
  vector = sqrt( (acc_x * acc_x) + (acc_y * acc_y) + (acc_z * acc_z) );
  /* Print out the values */
  totalvector = vector - vectorprevious;
  if (totalvector > 2 ){
    Steps++;
  }
  Serial.print("Steps: ");
  Serial.print(Steps);
  Serial.println("");
  vectorprevious = vector;
  delay(100);
}


void get6050Data(){
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  acc_x = round(a.acceleration.x);
  acc_y = round(a.acceleration.y);
  acc_z = round(a.acceleration.z);
}


void RTC_up()
{
  DateTime now = rtc.now();

  //-------------------------------------------Variable----------------------------------------//
  year = now.year();
  month = now.month();
  day = now.day();
  hh = now.hour();
  mm = now.minute();
  ss = now.second();
  date = daysOfTheWeek[now.dayOfTheWeek()];
}


void Task1Code(void * parameter)
{
  for(;;)
  {
    int touch_value = digitalRead(touch);
    if(touch_value)//nếu phát hiện cảm biến chạm
    {
      delay(200);
      if(touch_value) 
      {
        pox.begin();
        tft.fillScreen(TFT_BLACK);
        touch_value2 = touch_value2 + 1; //sau mỗi lần chạm sẽ tăng 1 giá trị
        if(touch_value2 == 3) touch_value2 = 0;
      }
    }
    if(touch_value2 == 0) Screen1(); //nếu giá trị chạm bằng 0 thì hiển thị màn hình đồng hồ

    if(touch_value2 == 1) Screen2(); //nếu giá trị chạm bằng 1 thì hiển thị màn hình step

    if(touch_value2 == 2) Screen3(); //nếu giá trị chạm bằng 2 thì hiển thị màn hình nhịp tim và spo2
  }
}

void Task2Code(void * parameter) //task này dùng để đọc các giá trị của sensor và đọc thời gian
{
  for(;;)
  {
    Heart_and_SPo2();
    RTC_up();
    Step_counting();
  }
}
void Task3Code(void * parameter) 
{
  for(;;)
  {
    if((SpO2<90)||(BPM>140)||(BPM<75)) //nếu giá trị vượt ngưỡng an toàn sẽ hiển thị khung cảnh báo 
    {
      tft.drawRoundRect(2, 2, 316, 236, 6, TFT_RED);
      tft.drawRoundRect(3, 3, 314, 234, 6,TFT_RED);
      tft.drawRoundRect(4, 4, 312, 232, 6,TFT_RED);
      vTaskDelay(300);
      tft.drawRoundRect(2, 2, 316, 236, 6,TFT_WHITE);
      tft.drawRoundRect(3, 3, 314, 234, 6,TFT_WHITE);
      tft.drawRoundRect(4, 4, 312, 232, 6,TFT_WHITE);
      vTaskDelay(300);
    }
    else
    {
      tft.drawRoundRect(2, 2, 316, 236, 6,TFT_BLACK);
      tft.drawRoundRect(3, 3, 314, 234, 6,TFT_BLACK);
      tft.drawRoundRect(4, 4, 312, 232, 6,TFT_BLACK);
    }
    vTaskDelay(2000/portTICK_PERIOD_MS);
    
  }
}





