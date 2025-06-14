#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <TimeLib.h>
#include <WiFiUdp.h>

#define BUZZER_PIN D5 

const int lcdCols = 16;
const int lcdRows = 2;

LiquidCrystal_I2C lcd(0x27, lcdCols, lcdRows);

const char* ssid = "Lab Tugas Akhir";
const char* password = "tugas akhir";

const char* mqtt_server = "10.2.22.51";
const int mqtt_port = 1883;
const char* mqtt_topic = "/tugas/akhir";
const char* mqtt_username = "tes";
const char* mqtt_password = "123";

SoftwareSerial mySerial(D3, D4);

Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
WiFiClient espClient;
PubSubClient client(espClient);

uint8_t id;
bool runVerification = false;
bool enrollingInProgress = false;

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  lcd.init();
  lcd.backlight();
  Serial.begin(9600);
  pinMode(BUZZER_PIN, OUTPUT);  // Inisialisasi pin buzzer sebagai output
  digitalWrite(BUZZER_PIN, LOW);  // Matikan buzzer pada awalnya
  while (!Serial);
  delay(100);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  Serial.println("\n\nAdafruit Fingerprint sensor enrollment, verification, and template extraction");
  finger.begin(57600);

  if (finger.verifyPassword()) {
    Serial.println("Found fingerprint sensor!");
  } else {
    Serial.println("Did not find fingerprint sensor :(");
    while (1);
  };

  Serial.print("Connecting to ");
  Serial.println(ssid);
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("Callback triggered");
  Serial.print("Message received on topic: ");
  Serial.println(topic);

  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Received message: ");
  Serial.println(message);

  if (strcmp(topic, mqtt_topic) == 0) {
    if (message == "enroll") {
      // Jika pesan "enroll" masuk, panggil fungsi enroll dan set runverification menjadi true
      runVerification = true;
      enrollFingerprint();
      runVerification = false;
    } else if (message.startsWith("setId")) {
      // Jika pesan dimulai dengan "setId", atur ID sesuai nilai yang dikirim dalam pesan
      int newId = message.substring(5).toInt();
      if (newId > 0 && newId <= 255) {
        id = newId;
        Serial.print("ID set to: ");
        Serial.println(id);
        getFingerprintEnroll();
      } else {
        Serial.println("Invalid ID value");
      }
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }

  client.loop();

  if (!runVerification && !enrollingInProgress) {  // Menambahkan kondisi !enrollingInProgress
    client.subscribe(mqtt_topic);
    client.setCallback(callback);
  }

  if (!enrollingInProgress) {
    verifyFingerprint();
  }

  delay(500);
 
}

uint8_t readNumber() {
  uint8_t num = 0;

  while (num == 0) {
    while (!Serial.available());
    num = Serial.parseInt();
  }
  return num;
}

void clearSerialBuffer() {
  while (Serial.available() > 0) {
    char _ = Serial.read();
  }
}

void enrollFingerprint() {
  reconnect();
  Serial.println("Ready to enroll a fingerprint!");

  while (id < 1 || id > 255) {
    client.subscribe(mqtt_topic);  // Subscribe ke topik MQTT
    client.setCallback(callback);
  }

  Serial.print("Enrolling ID #");
  Serial.println(id);

  int enrollAttempts = 1;

  while (enrollAttempts++) {
    if (getFingerprintEnroll()) {
      // Successfully enrolled
      clearSerialBuffer(); // Clear serial buffer after completion
      runVerification = false; // Stop verification loop after enrollment

      return;
    }

    if (Serial.available()) {
      // User pressed a key, exit the loop
      runVerification = false;
      return;
    }
  }

  // If we reach here, it means enrollment failed twice
  lcd.setCursor(5,0);
  lcd.print("DAFTAR GAGAL!");
  Serial.println("Enrollment failed. Returning to the main menu.");
}

uint8_t getFingerprintEnroll() {
  int p = -1;
  Serial.print("Waiting for a valid finger to enroll as #"); Serial.println(id);
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Image taken");
        break;
      case FINGERPRINT_NOFINGER:
        Serial.println("Take your fingerprint");
        lcd.clear();
        lcd.setCursor(1,0);
        lcd.print("TEMPELKAN JARI");
        lcd.setCursor(6,1);
        lcd.print("ANDA");
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        client.publish("/absen/1", "ulangi");
        return p;
      case FINGERPRINT_IMAGEFAIL:
        Serial.println("Imaging error");
        return p;
      default:
        Serial.println("Unknown error");
        return p;
    }
  }

  p = finger.image2Tz(1);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      client.publish("/absen/1", "ulangi");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      return p;
    default:
      Serial.println("Unknown error");
      return p;
  }

  Serial.println("Remove finger");
  lcd.clear();
  lcd.setCursor(1,0);
  lcd.print("LEPASKAN JARI");
  lcd.setCursor(1,1);
  lcd.print("DARI SENSOR");
  delay(2000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }
  Serial.print("ID "); Serial.println(id);
  p = -1;
  Serial.println("Place the same finger again");
  lcd.clear();
  lcd.setCursor(1,0);
  lcd.print("TEMPELKAN LAGI");
  lcd.setCursor(1,1);
  lcd.print("JARI YANG SAMA");
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Image taken");
        break;
      case FINGERPRINT_NOFINGER:
        Serial.print(".");
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        client.publish("/absen/1", "ulangi");
        return p;
      case FINGERPRINT_IMAGEFAIL:
        Serial.println("Imaging error");
        return p;
      default:
        Serial.println("Unknown error");
        return p;
    }
  }

  p = finger.image2Tz(2);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      client.publish("/absen/1", "ulangi");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      return p;
    default:
      Serial.println("Unknown error");
      return p;
  }

  Serial.print("Creating model for #");  Serial.println(id);

  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    Serial.println("Prints matched!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    client.publish("/absen/1", "ulangi");
    return p;
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
    Serial.println("Fingerprints did not match");
    client.publish("/absen/1", "ulangi");    
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("JARI TIDAK SAMA!");
    lcd.setCursor(5,1);
    lcd.print("ULANGI!");
    return p;
  } else {
    Serial.println("Unknown error");
    return p;
  }

  Serial.println(id);
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("DAFTAR BERHASIL!");
    delay(1000);
    Serial.println("Stored!");
    client.publish("/absen/1", "selesai");
    runVerification = false;
    return p;
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    return p;
  } else if (p == FINGERPRINT_BADLOCATION) {
    Serial.println("Could not store in that location");
    return p;
  } else if (p == FINGERPRINT_FLASHERR) {
    Serial.println("Error writing to flash");
    return p;
  } else {
    Serial.println("Unknown error");
    return p;
  }
}

void verifyFingerprint() {
  delay(1000);
  lcd.clear();
  Serial.println("Ready to verify a fingerprint!");

  int p = finger.getImage();
  if (p != FINGERPRINT_OK) {
    Serial.println("No finger detected");
    lcd.clear();
    lcd.setCursor(1,0);
    lcd.print("SILAHKAN ABSEN");
    delay(1000);
    return;
  }

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    Serial.println("Image conversion failed");
    delay(1000);
    return;
  }

  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    reconnect();
    Serial.print("Fingerprint verified with ID #");
    Serial.println(finger.fingerID);
    for (int i = 0; i < 2; i++) {
      digitalWrite(BUZZER_PIN, HIGH);  // Aktifkan buzzer
      delay(200);  // Durasi beep pendek
      digitalWrite(BUZZER_PIN, LOW);   // Matikan buzzer
      delay(200);  // Jeda antara beep
    }
    client.publish("/absen/magang", String(finger.fingerID).c_str());
    lcd.clear();
    lcd.setCursor(4,0);
    lcd.print("ABSEN");
    lcd.setCursor(4,1);
    lcd.print("BERHASIL!");
    delay(500);
    runVerification = false;
    reconnect();
    return; 
  } else if (p == FINGERPRINT_NOTFOUND) {
    Serial.println("Fingerprint not found");
    digitalWrite(BUZZER_PIN, HIGH);  // Aktifkan buzzer
    delay(500);  // Durasi beep panjang (2 detik)
    digitalWrite(BUZZER_PIN, LOW);
    lcd.setCursor(1,0);
    lcd.print("SIDIK JARI");
    lcd.setCursor(1,1);
    lcd.print("TIDAK DITEMUKAN!");
    delay(2000);
    lcd.clear();
    lcd.setCursor(1,0);
    lcd.print("GUNAKAN JARI");
    lcd.setCursor(1,1);
    lcd.print("YANG TERDAFTAR!");
    delay(1000);
  } else {
    Serial.println("Unknown error");
  }

  clearSerialBuffer(); // Clear serial buffer after completion
}