#include <WiFi.h>
#include <time.h>
#include <Firebase_ESP_Client.h>
#include <Preferences.h>
#include "HX711.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define DOUT 13
#define CLK 12

// WiFi
#define WIFI_SSID "SSID"
#define WIFI_PASSWORD "PASSWORD"

// Firebase
#define API_KEY "APIKey"
#define DATABASE_URL "databaseURL"

HX711 scale;

LiquidCrystal_I2C lcd(0x27,16,2);

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
Preferences prefs;

// الوزن
float calibration_factor = 109500;

float stableWeight = 0;
float shelfWeight = 0;
float productWeight = 0;
int quantity = 0;

bool weightCaptured = false;
bool captureProcessed = false;
String currentSSID = "";
String currentPASS = "";

unsigned long wifiTimer = 0;

bool wifiConfigRejected = false;
String lastTestedSSID = "";
String lastTestedPASS = "";

// بيانات المنتج (لاحقًا من الويب)
String productName = "";
float price = 0;
String expiryDate = "";

// قراءة مستقرة
float readStableWeight() {

  float sum = 0;
  int samples = 20;

  for (int i = 0; i < samples; i++) {
    sum += scale.get_units(1);
    delay(5);
  }

  return sum / samples;
}

void handleCommand(String cmd, float netWeight) {

  // حفظ وزن القطعة فقط عند Capture
  if (cmd == "CAPTURE_WEIGHT") {


    if (netWeight > 0.003) {

      productWeight = netWeight;
      weightCaptured = true;


      Firebase.RTDB.setFloat(
        &fbdo,
        "/Shelf1/UnitWeight",
        productWeight
      );

      if (Firebase.RTDB.setString(&fbdo, "/Shelf1/Command", "NONE")) {
        Serial.println("Command cleared");
      }
      else {
       Serial.println("Failed to clear command");
       Serial.print("Error: ");
       Serial.println(fbdo.errorReason());
      }

      Serial.print("Unit Weight Saved: ");
      Serial.println(productWeight, 4);
    }
    else {
      Serial.println("No valid product weight detected");
    }
  }

  // حفظ بيانات المنتج
  if (cmd == "SAVE_PRODUCT") {

    Firebase.RTDB.setString(
      &fbdo,
      "/Shelf1/ProductName",
      productName
    );

    Firebase.RTDB.setFloat(
      &fbdo,
      "/Shelf1/Price",
      price
    );

    Firebase.RTDB.setString(
      &fbdo,
      "/Shelf1/ExpiryDate",
      expiryDate
    );

    Serial.println("Product saved to Firebase");
  }
}

void checkWiFiConfig(){

  if(!Firebase.RTDB.getString(
      &fbdo,
      "/WiFiConfig/SSID"))
      return;

  String newSSID =
  fbdo.stringData();

  if(!Firebase.RTDB.getString(
      &fbdo,
      "/WiFiConfig/PASSWORD"))
      return;

  String newPASS =
  fbdo.stringData();

  if(
   wifiConfigRejected &&
   newSSID == lastTestedSSID &&
   newPASS == lastTestedPASS
){
   return;
}

  if(newSSID == "")
      return;

  if(
      newSSID == currentSSID &&
      newPASS == currentPASS
    )
      return;

  Serial.println(
    "New WiFi Found In Firebase"
  );

  String oldSSID = currentSSID;
  String oldPASS = currentPASS;

Serial.println("Testing New WiFi...");

WiFi.disconnect(true);

WiFi.begin(
  newSSID.c_str(),
  newPASS.c_str()
);

int timeout = 0;

while(
  WiFi.status() != WL_CONNECTED &&
  timeout < 20
){

  delay(500);
  Serial.print(".");

  timeout++;

}

if(WiFi.status() == WL_CONNECTED){

  Serial.println("\nNew WiFi Connected");

  prefs.putString(
    "ssid",
    newSSID
  );

  prefs.putString(
    "pass",
    newPASS
  );

  currentSSID = newSSID;
  currentPASS = newPASS;

  Serial.println(
    "WiFi Saved In Memory"
  );

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println(
  "Firebase Reconnected"
);

wifiConfigRejected = false;

lastTestedSSID = "";
lastTestedPASS = "";

}
else{

  Serial.println(
    "\nNew WiFi Failed"
  );

  wifiConfigRejected = true;

lastTestedSSID = newSSID;
lastTestedPASS = newPASS;

  Serial.println(
    "Restoring Old WiFi..."
  );

  WiFi.disconnect(true);

  WiFi.begin(
    oldSSID.c_str(),
    oldPASS.c_str()
  );

  int restoreTimeout = 0;

  while(
    WiFi.status() != WL_CONNECTED &&
    restoreTimeout < 20
  ){

    delay(500);
    Serial.print("*");

    restoreTimeout++;

  }

 if(WiFi.status() == WL_CONNECTED){

  Serial.println(
    "\nOld WiFi Restored"
  );

  currentSSID = oldSSID;
  currentPASS = oldPASS;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println(
    "Firebase Reconnected"
  );

}
  else{

    Serial.println(
      "\nFailed To Restore Old WiFi"
    );

  }

}

}

void setup() {

  Serial.begin(9600);

Wire.begin(25,26);

lcd.init();
lcd.backlight();

lcd.setCursor(0,0);
lcd.print("Smart Shelf");

lcd.setCursor(0,1);
lcd.print("Starting...");

prefs.begin("wifi", false);

String savedSSID =
prefs.getString("ssid", "");

String savedPASS =
prefs.getString("pass", "");

  WiFi.setSleep(false);

if(savedSSID != ""){

  Serial.println("Using Saved WiFi");

  WiFi.begin(
    savedSSID.c_str(),
    savedPASS.c_str()
  );

}
else{

  Serial.println("Using Default WiFi");

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

}

  Serial.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");

currentSSID = WiFi.SSID();

if(savedSSID != ""){
  currentPASS = savedPASS;
}
else{
  currentPASS = WIFI_PASSWORD;
}

  // SSL FIX TIME
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  Serial.print("Syncing Time");

  time_t now = time(nullptr);

  while (now < 100000) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }

  Serial.println("\nTime Synced");

  // Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  Firebase.signUp(&config, &auth, "", "");

  Firebase.reconnectNetwork(true);

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Firebase.RTDB.setString(&fbdo, "/Shelf1/Command", "NONE");

  // HX711
  scale.begin(DOUT, CLK);
  scale.set_scale(calibration_factor);

  delay(3000);

  shelfWeight = scale.get_units(20);

  Serial.println("Jamal 1.5 Ready");
  Serial.println("Waiting for CAPTURE_WEIGHT...");

  productWeight = 0;
  quantity = 0;
  weightCaptured = false;

}

void loop() {

  stableWeight = readStableWeight();

  float netWeight = stableWeight - shelfWeight;

  if (netWeight < 0)
    netWeight = 0;

  // 🔵 عرض الوزن فقط
  Serial.print("Weight: ");
  Serial.print(netWeight, 4);
  Serial.print(" Kg");

  // 🔵 حساب الكمية فقط إذا تم تسجيل الوزن
  if (productWeight > 0 && weightCaptured == true) {

    quantity = (int)(netWeight / productWeight + 0.5);

    Firebase.RTDB.setInt(
      &fbdo,
      "/Shelf1/Quantity",
      quantity
    );

    Serial.print(" | Qty: ");
    Serial.print(quantity);
  }

  Serial.println();

  // 🔵 قراءة الأوامر من Firebase
  if (Firebase.RTDB.getString(
        &fbdo,
        "/Shelf1/Command")) {

    String cmd = fbdo.stringData();

    if (cmd != "" && cmd != "NONE") {

      handleCommand(cmd, netWeight);

     Firebase.RTDB.setString(
     &fbdo,
     "/Shelf1/Command",
     "NONE"
  );
    }
  }

if(millis() - wifiTimer > 10000){

  wifiTimer = millis();

  checkWiFiConfig();

}

static unsigned long statusTimer = 0;

if(millis() - statusTimer > 5000){

  statusTimer = millis();

  Firebase.RTDB.setInt(
    &fbdo,
    "/ESPStatus/LastSeen",
    time(nullptr)
  );
}

static unsigned long lcdTimer = 0;

if(millis() - lcdTimer > 1000)
{
    lcdTimer = millis();

    String lcdProductName = "No Product";
    String lcdProductPrice = "0 DA";

    if(Firebase.RTDB.getString(
        &fbdo,
        "/Shelf1/ProductName"))
    {
        lcdProductName = fbdo.stringData();
    }

    if(Firebase.RTDB.getString(
        &fbdo,
        "/Shelf1/Price"))
    {
        lcdProductPrice = fbdo.stringData();
    }

    int posName =
    (16 - lcdProductName.length()) / 2;

    int posPrice =
    (16 - lcdProductPrice.length()) / 2;

    if(posName < 0) posName = 0;
    if(posPrice < 0) posPrice = 0;

    lcd.clear();

    lcd.setCursor(posName,0);
    lcd.print(lcdProductName);

    lcd.setCursor(posPrice,1);
    lcd.print(lcdProductPrice);
}

  delay(300);
}
