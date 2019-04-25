/*ICE #3.1
 * Program Description
  This app takes data from a DHT22 and MPL115 then prints it to the serial, an OLED display, and then passes the data it to adafruit io.
*/
#include "config.h" //wifi and adafruit configuration

#include <Wire.h> //i2c inlcude

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

//ML115A2 includes
#include <Adafruit_MPL115A2.h>
Adafruit_MPL115A2 mpl115a2;

//DHT includes
#include <Adafruit_Sensor.h>
#include "DHT.h"
#define DHTPIN 2 // pin connected to DH22 data line
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

//OLED Display includes
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//OLED display settings
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


// set up feeds for adafruit io
AdafruitIO_Feed *temperature = io.feed("temperature");
AdafruitIO_Feed *humidity = io.feed("humidity");
AdafruitIO_Feed *MPLTemperature = io.feed("MPLTemperature");
AdafruitIO_Feed *MPLPressure = io.feed("MPLPressure");
AdafruitIO_Feed *airquality = io.feed("airquality");
AdafruitIO_Feed *airqualityresponse = io.feed("airqualityresponse");

// ------------- API CALLS -------------
String airVisualKey = "5DmgG7vy34ic6u3WS";

typedef struct {
  String st; //Status of data call
  String city; //Severity level of traffic incident.
  String ts;  //Time stamp.
  String hu; //Humidity
  String pr; //Atmospheric pressure
  String tp; //Temperature in celsius
  String aqi; //AQI pollution level in PM2.5
} airData;

airData air; //allows the data to be passed to the struct through a call of traffic.

typedef struct {
  String value; //Status of data call
} airQualityValue;

airQualityValue aq;
void setup() {
  Serial.begin(115200); // start the serial connection
  while (! Serial); // wait for serial monitor to open
  adafruitConnect(); // create connection to adafruit io
  startDisplay(); // initialize display
  mpl115a2.begin(); // start MPL sensor
  dht.begin();
  getAir(); //Calls function to get Air data then prints the data that gets passed to the struct.
  serialPrintAirInfo();
}

void loop() {
  io.run(); // continually checkin with adafruit io
  getPressure(); // get the data from the MPL sensor, print to serial, print to display, and post to adafruit io.
  readDHT();
  //getAir();
  checkAirQaulityFeed();
}

// ------------- ADAFRUIT CONNECT -------------

void adafruitConnect(){
  Serial.print("Connecting to Adafruit IO"); // connect to io.adafruit.com
  io.connect();
  while (io.status() < AIO_CONNECTED) { // wait for a connection
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.println(io.statusText()); // we are connected
}


// ------------- AIR VISUAL API CALL -------------
void getAir() { //Get Air grabs json data from Air Visual. It is localized to the IP address to get you the closest statiuon available and prints the relevant information.
  HTTPClient theClient;
  Serial.println();
  Serial.println("Making HTTP request to Air Visual");
  theClient.begin("http://api.airvisual.com/v2/nearest_city?key=" + airVisualKey);
  int httpCode = theClient.GET();
  if (httpCode > 0) {
    if (httpCode == 200) {
      Serial.println("Received HTTP payload.");
      DynamicJsonBuffer jsonBuffer;
      String payload = theClient.getString();
      Serial.println("Parsing...");
      JsonObject& root = jsonBuffer.parse(payload);

      // Test if parsing succeeds.
      if (!root.success()) {
        Serial.println("parseObject() failed");
        Serial.println(payload);
        return;
      }
      air.st = root["status"].as<String>(); //If it grabbed data successfully
      air.city = root["data"]["city"].as<String>(); //City that you are in.
      air.ts = root["data"]["current"]["weather"]["ts"].as<String>();  //Time stamp.
      air.hu = root["data"]["current"]["weather"]["hu"].as<String>(); //Humidity
      air.pr = root["data"]["current"]["weather"]["pr"].as<String>(); //Atmospheric pressure
      air.tp = root["data"]["current"]["weather"]["tp"].as<String>(); //Temperature in celsius
      air.aqi = root["data"]["current"]["pollution"]["aqius"].as<String>(); //AQI pollution level in PM2.5
      airquality->save(air.aqi);
    }
    else {
      Serial.println(httpCode);
      Serial.println("Something went wrong with connecting to the endpoint.");
    }
  }
}

void serialPrintAirInfo(){ //printing out all of the api data from Air Visual. I only print this at the beginning because the station only updates once an hour.
        Serial.println();
        Serial.println("Heres local weathr information.");
        Serial.println("Status: " + air.st);
        Serial.println("City: " + air.city);
        Serial.println("Time: " + air.ts);
        Serial.println("Humidy: " + air.hu);
        Serial.println("Pressure: " + air.pr);
        Serial.println("Temperature: " + air.tp + "C");
        Serial.println("Air Quality: " + air.aqi + " pm2.5");
}

void checkAirQaulityFeed(){ //This grabs a public feed from adafruit that triggers a value if air quality is bad.
  HTTPClient theClient;
  Serial.println();
  Serial.println("Getting Air Quality Feed...");
  theClient.begin("http://io.adafruit.com/api/v2/rossmonroe/feeds/airqualityresponse");
  int httpCode = theClient.GET();
  if (httpCode > 0) {
    if (httpCode == 200) {
      DynamicJsonBuffer jsonBuffer;
      String payload = theClient.getString();
      JsonObject& root = jsonBuffer.parse(payload);

      // Test if parsing succeeds.
      if (!root.success()) {
        Serial.println("parseObject() failed");
        Serial.println(payload);
        return;
      }
      aq.value = root["last_value"].as<String>(); //If it grabbed data successfully
    }
    else {
      Serial.println(httpCode);
      Serial.println("Something went wrong with connecting to the endpoint.");
    }
  }
  Serial.println();
  if (aq.value == "BAD"){ //If Adafruit tells us the airquality is bad then it will print it to the serial and oled display.
     Serial.print("AIR QUALITY: ");
     Serial.println(aq.value);
     String airMessage = "AIR QUALITY SUCKS!";
     Serial.print(airMessage);
     displayAirQuality(airMessage);
  }else{
      Serial.print("AIR QUALITY: "); //If air quality is not bad then print that its good to the serial and oled display.
      Serial.println(aq.value);
      String airMessage = "AIR QUALITY IS GOOD!";
      Serial.print(airMessage);
      displayAirQuality(airMessage);
  }
}

// ------------- SENSOR READINGS -------------

void getPressure(){
  Serial.println("");
  Serial.println("--------- MPL115A2 ---------"); //titles section the sensor name with a space above to make it clear where this data is coming from.
  float pressureKPA = 0, temperatureC = 0; //creates float variables for pressure and temperature
  pressureKPA = mpl115a2.getPressure(); //assigns the pressure data from the MPL sensor to the float variable
  temperatureC = mpl115a2.getTemperature(); //assigns the temperature data from the MPL sensor to the float variable
  Serial.print("Pressure: "); Serial.print(pressureKPA); Serial.println(" kPa"); // rints pressure data to serial
  Serial.print("Temp: "); Serial.print(temperatureC); Serial.println(" C"); //prints temperature data to serial
  MPLTemperature->save(temperatureC); //saves temperature data to adafruit io feed.
  MPLPressure->save(pressureKPA); //saves pressure data to adafruit io feed.
  displayPressTemp(temperatureC, pressureKPA); //sends temperature and pressure data to be printed to the OLED display.
}


void readDHT(){ //Reads data from the DHT sensor. I switched libraries and used a read method instead of an event listener. Still intermittently working.
  float h = dht.readHumidity(); 
  // Read temperature as Celsius (the default)
  float t = dht.readTemperature();
  // Read temperature as Fahrenheit (isFahrenheit = true)
  float f = dht.readTemperature(true);

  // Check if any reads failed and exit early (to try again).
  if (isnan(h) || isnan(t) || isnan(f)) {
    Serial.println();
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }
  Serial.println("");
  Serial.println("--------- DHT22 ---------"); // titles section the sensor name with a space above to make it clear where this data is coming from.
  Serial.print("celsius: "); Serial.print(t); Serial.println("C"); // print temperature in celsius to serial.
  Serial.print("fahrenheit: "); Serial.print(f); Serial.println("F"); // print temperature in fahrenheit to serial.
  Serial.print("humidity: "); Serial.print(h); Serial.println("%"); // print humidity data to serial.
  temperature->save(f); // save fahrenheit (or celsius) to Adafruit IO
  humidity->save(h); // save humidity to Adafruit IO
  displayTempHumi(f, h); // send temperature and humidity to be printed on the OLED display.
}


// ------------- OLED DISPLAY FUNCTIONS -------------

void setDisplay() { // this function clears the display, sets the text size and color, then the start point of the text.
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
}

void startDisplay() { // The initiallizes the display at the start of the program.
  displaySetup();
  setDisplay();
  display.println(F("Hello!"));
  display.display();
  delay(1000);
}

void displaySetup(){ //Initializes OLED display.
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Don't proceed, loop forever
  }
  display.display();
}

/*All of the diplsay functions occur with a similar structure. They call setDisplay
 * that will reset the screen and the start point so its ready to receive data.
 * Then I set the cursor for the second line of the message if its necessary.
 * All of the display functions are recieving variabls from their relevant get data
 * functions to display so its easily read. There is a delay of 4 seconds to give
 * time before the next call so I'm not constantly making posts and requests to the API's
 */

void displayTempHumi(float f, float h) { //passes in temperature and humidity.
  setDisplay(); //sets initial display information and start point.
  display.print(F("Temperature: ")); display.print(f, 1); display.println(" F"); //structure of first line
  display.display(); //assigns the printed data to the display
  display.setCursor(0, 20); //create new start point for second line.
  display.print(F("Humidity: ")); display.print(h , 1); display.println(" %"); // structure of information for second line.
  display.display(); //prints to display
  delay(4000); //delay to keep information on screen long enough to read.
}
void displayPressTemp(float temperatureC, float pressureKPA) { //passes in termperature and pressure.
  setDisplay(); //set initial display values.
  display.print(F("Pressure: ")); display.print(pressureKPA, 1); display.println(" kPa"); //structure for first line to be displayed.
  display.display(); // posts to diplsay.
  display.setCursor(0, 20); //moves to second line
  display.print(F("Temperature: ")); display.print(temperatureC, 1); display.println(" C"); //second line of information
  display.display(); //posts line to display.
  delay(4000); //delay so information can be read.
}
void displayAirQuality(String airMessage) { 
  setDisplay(); //sets initial display information and start point.
  display.println(airMessage); //structure of first line
  display.display(); //assigns the printed data to the display
  delay(3000); //delay to keep information on screen long enough to read.
}
