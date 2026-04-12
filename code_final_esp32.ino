#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <DHT.h>
#include <ESP32Servo.h>

// Provide the token generation process info.
#include <addons/TokenHelper.h>
// Provide the RTDB payload printing info and other helper functions.
#include <addons/RTDBHelper.h>

// WiFi credentials
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// Firebase credentials
#define API_KEY ""
#define DATABASE_URL ""
#define USER_EMAIL ""
#define USER_PASSWORD ""

// === Window Sensors ===
#define DHT_PIN 14       // DHT11 
#define MQ5_PIN 34       // Gas sensor (analog)
#define RAIN_PIN 35      // Rain sensor (analog)
#define SERVO_PIN 13     // Servo motor for window

// === LED Pins ===
#define LED1_PIN 4       // LED 1
#define LED2_PIN 12      // LED 2  
#define LED3_PIN 27      // LED 3

// === Water Level Sensor ===
#define WATER_LEVEL_PIN 32  // Water level sensor (analog)

// === Sensor Thresholds ===
#define SEUIL_GAZ 2000     // Gas threshold
#define SEUIL_PLUIE 1500   // Rain threshold  
#define SEUIL_TEMP 30.0    // Temperature threshold

// === Objects ===
DHT dht(DHT_PIN, DHT11);
Servo servoFenetre;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
int interval = 5000; // Send data every 5 seconds

void setup() {
  Serial.begin(115200);
  
  // Initialize DHT sensor
  dht.begin();
  
  // Initialize servo
  servoFenetre.attach(SERVO_PIN, 500, 2400);
  
  // Initialize pins
  pinMode(MQ5_PIN, INPUT);
  pinMode(RAIN_PIN, INPUT);
  pinMode(WATER_LEVEL_PIN, INPUT);
  
  // Initialize LED pins
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  
  // Turn off all LEDs initially
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);

  // Connect to Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Firebase setup
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  // Assign the callback function for the long running token generation task
  config.token_status_callback = tokenStatusCallback;

  Firebase.reconnectNetwork(true);
  Firebase.begin(&config, &auth);
  Firebase.setDoubleDigits(2);

  Serial.println("Firebase initialized!");
}

void loop() {
  if (Firebase.ready() && (millis() - sendDataPrevMillis > interval || sendDataPrevMillis == 0)) {
    sendDataPrevMillis = millis();

    // === Read all sensors ===
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    int gasLevel = analogRead(MQ5_PIN);
    int rainLevel = analogRead(RAIN_PIN);
    int waterLevel = analogRead(WATER_LEVEL_PIN);
    
    // Convert analog readings to meaningful values
    bool gasDetected = gasLevel > SEUIL_GAZ;
    bool rainDetected = rainLevel < SEUIL_PLUIE;
    int waterLevelPercent = map(waterLevel, 0, 4095, 0, 100); // Convert to percentage
    
    Serial.println("=== Sensor Readings ===");
    Serial.printf("Temperature: %.2f°C\n", temperature);
    Serial.printf("Humidity: %.2f%%\n", humidity);
    Serial.printf("Gas Level: %d (Detected: %s)\n", gasLevel, gasDetected ? "YES" : "NO");
    Serial.printf("Rain Level: %d (Detected: %s)\n", rainLevel, rainDetected ? "YES" : "NO");
    Serial.printf("Water Level: %d%% (%d raw)\n", waterLevelPercent, waterLevel);

    // === Upload sensor data to Firebase ===
    if (!isnan(temperature)) {
      Firebase.RTDB.setFloat(&fbdo, "data/temperature", temperature);
    }
    
    if (!isnan(humidity)) {
      Firebase.RTDB.setFloat(&fbdo, "data/moisture", humidity); // Using 'moisture' as per your database
    }
    
    Firebase.RTDB.setBool(&fbdo, "data/gas", gasDetected);
    Firebase.RTDB.setBool(&fbdo, "data/rain", rainDetected);
    Firebase.RTDB.setInt(&fbdo, "data/waterLevel", waterLevelPercent);

    // === Control LEDs based on Firebase ===
    bool led1State, led2State, led3State;
    
    if (Firebase.RTDB.getBool(&fbdo, "led1/state", &led1State)) {
      digitalWrite(LED1_PIN, led1State ? HIGH : LOW);
      Serial.printf("LED1 set to: %s\n", led1State ? "ON" : "OFF");
    } else {
      Serial.println("Failed to get LED1 state: " + fbdo.errorReason());
    }
    
    if (Firebase.RTDB.getBool(&fbdo, "led2/state", &led2State)) {
      digitalWrite(LED2_PIN, led2State ? HIGH : LOW);
      Serial.printf("LED2 set to: %s\n", led2State ? "ON" : "OFF");
    } else {
      Serial.println("Failed to get LED2 state: " + fbdo.errorReason());
    }
    
    if (Firebase.RTDB.getBool(&fbdo, "led3/state", &led3State)) {
      digitalWrite(LED3_PIN, led3State ? HIGH : LOW);
      Serial.printf("LED3 set to: %s\n", led3State ? "ON" : "OFF");
    } else {
      Serial.println("Failed to get LED3 state: " + fbdo.errorReason());
    }

    // === Automatic Window Control Logic ===
    controlWindow(gasDetected, rainDetected, temperature);

    Serial.println("=========================\n");
  }
  
  // Small delay to prevent watchdog issues
  delay(100);
}

void controlWindow(bool gasDetected, bool rainDetected, float temperature) {
  // === Automatic window control with priorities ===
  if (gasDetected) {
    Serial.println("  Gas detected - Priority 1 → Window OPEN");
    servoFenetre.write(0);  // Open window
  }
  else if (rainDetected) {
    Serial.println("  Rain detected - Priority 2 → Window CLOSED");
    servoFenetre.write(90);   // Close window
  }
  else if (temperature > SEUIL_TEMP) {
    Serial.println("  High temperature - Priority 3 → Window OPEN");
    servoFenetre.write(0);  // Open window
  }
  else {
    Serial.println("  Normal temperature → Window CLOSED");
    servoFenetre.write(90);   // Close window
  }

}
