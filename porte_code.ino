/*
 * PORTE CONNECTÉE - Contrôle depuis PARTOUT dans le monde
 * Arduino UNO R4 WiFi + Servo Moteur 360° + Capteurs de fin de course
 */

#include <WiFiS3.h>
#include <ArduinoMqttClient.h>
#include <Servo.h>

// === CONFIGURATION MATÉRIELLE ===
Servo monServo;
const int SERVO_PIN    = 9;
const int ARRET_SERVO  = 90; // Point d'arrêt du servo 360° (ajuster si le moteur tourne tout seul)

const int PIN_BUTE_OUVERT = 2;  // Capteur fin d'ouverture
const int PIN_BUTE_FERME  = 4;  // Capteur fin de fermeture
const int PIN_LED_VERTE   = 6;  // LED verte (ouverture / porte ouverte)
const int PIN_LED_ROUGE   = 7;  // LED rouge (fermeture en cours)
const int PIN_BUZZER      = 8;  // Buzzer alarme
const int LED_PIN         = 13; // LED intégrée Arduino

const unsigned long TIMEOUT_MOTEUR = 5000; // Sécurité : 5 sec max par mouvement

// === CONFIGURATION RÉSEAU ===
char ssid[] = "VOTRE_NOM_WIFI";
char pass[] = "VOTRE_MOT_DE_PASSE_WIFI";

// === CONFIGURATION MQTT ===
const char broker[]           = "broker.hivemq.com";
int        port               = 1883; // Port TCP standard pour Arduino (sans TLS)
const char topicCmd[]         = "porte-vincent-2026/commande";
const char topicState[]       = "porte-vincent-2026/etat";
const char topicAddCode[]     = "porte-vincent-2026/addcode";    
const char topicUseCode[]     = "porte-vincent-2026/usecode";    
const char topicCodeResult[]  = "porte-vincent-2026/coderesult"; 
const char topicCodeUsed[]    = "porte-vincent-2026/codeused";   
const char topicAlarme[]      = "porte-vincent-2026/alarme";     

bool isDoorOpen = false;

// === GESTION DES CODES JETABLES ===
const int MAX_CODES = 20;
String validCodes[MAX_CODES];
int codeCount = 0;

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

unsigned long lastAlive     = 0;
unsigned long autoCloseTime = 0;

// ==========================================
//   FONCTIONS DE MOUVEMENT
// ==========================================

void actionOuvrirMecanisme() {
  if (!isDoorOpen) {
    digitalWrite(PIN_LED_VERTE, HIGH);
    digitalWrite(PIN_LED_ROUGE, LOW);

    Serial.println("-> Actionnement moteur : OUVERTURE");
    monServo.write(180); // Sens ouverture

    unsigned long debut = millis();
    while (digitalRead(PIN_BUTE_OUVERT) == HIGH && (millis() - debut < TIMEOUT_MOTEUR)) {
      delay(10); // Attend que le bouton soit pressé (LOW) ou timeout
    }
    monServo.write(ARRET_SERVO); // Stop immédiat

    if (digitalRead(PIN_BUTE_OUVERT) == HIGH) {
      Serial.println("!! ALERTE : Capteur D2 non atteint (Timeout sécurité)");
    } else {
      Serial.println("-> Capteur D2 heurté ! Porte ouverte.");
    }
  }
}

void actionFermerMecanisme() {
  if (isDoorOpen) {
    digitalWrite(PIN_LED_VERTE, LOW);
    digitalWrite(PIN_LED_ROUGE, HIGH);

    Serial.println("-> Actionnement moteur : FERMETURE");
    monServo.write(0); // Sens fermeture

    unsigned long debut = millis();
    while (digitalRead(PIN_BUTE_FERME) == HIGH && (millis() - debut < TIMEOUT_MOTEUR)) {
      delay(10);
    }
    monServo.write(ARRET_SERVO);
    digitalWrite(PIN_LED_ROUGE, LOW);

    if (digitalRead(PIN_BUTE_FERME) == HIGH) {
      Serial.println("!! ALERTE : Capteur D4 non atteint (Timeout sécurité)");
    } else {
      Serial.println("-> Capteur D4 heurté ! Porte fermée.");
    }
  }
}

// ==========================================
//   LOGIQUE DES CODES
// ==========================================

void addCode(String code) {
  if (codeCount >= MAX_CODES) {
    // Supprime le plus vieux code pour faire de la place
    for (int i = 0; i < MAX_CODES - 1; i++) validCodes[i] = validCodes[i + 1];
    codeCount = MAX_CODES - 1;
  }
  validCodes[codeCount++] = code;
  Serial.print("Code ajouté : "); Serial.print(code);
  Serial.print(" (Total: "); Serial.print(codeCount); Serial.println(")");
}

bool useCode(String code) {
  for (int i = 0; i < codeCount; i++) {
    if (validCodes[i] == code) {
      Serial.print("Code VALIDE utilisé : "); Serial.println(code);
      // Supprime le code en décalant le tableau
      for (int j = i; j < codeCount - 1; j++) validCodes[j] = validCodes[j + 1];
      codeCount--;
      return true;
    }
  }
  Serial.print("Code INVALIDE : "); Serial.println(code);
  return false;
}

void openDoorTemporary() {
  actionOuvrirMecanisme();
  isDoorOpen = true;
  digitalWrite(LED_PIN, HIGH);
  Serial.println(">> PORTE OUVERTE (invité) - fermeture auto dans 3s");

  mqttClient.beginMessage(topicState);
  mqttClient.print("ON");
  mqttClient.endMessage();

  autoCloseTime = millis() + 3000; // Timer 3 secondes
}

// ==========================================
//   CONNEXION & MQTT
// ==========================================

void connectWiFi() {
  Serial.print("Connexion WiFi à "); Serial.print(ssid);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWiFi connecté ! IP : " + WiFi.localIP().toString());
}

void connectMQTT() {
  Serial.print("Connexion Broker MQTT...");
  String clientId = "arduino-porte-" + String(random(10000));
  mqttClient.setId(clientId);

  while (!mqttClient.connect(broker, port)) {
    Serial.print(".");
    delay(2000);
  }
  Serial.println(" Connecté !");

  mqttClient.subscribe(topicCmd);
  mqttClient.subscribe(topicAddCode);
  mqttClient.subscribe(topicUseCode);
  mqttClient.subscribe(topicAlarme);

  mqttClient.beginMessage(topicState);
  mqttClient.print(isDoorOpen ? "ON" : "OFF");
  mqttClient.endMessage();
}

void onMqttMessage(int messageSize) {
  String topic = mqttClient.messageTopic();
  String message = "";
  while (mqttClient.available()) message += (char)mqttClient.read();

  // --- Commande Admin (ON/OFF) ---
  if (topic == String(topicCmd)) {
    if (message == "ON") {
      actionOuvrirMecanisme();
      isDoorOpen = true;
      autoCloseTime = millis() + 3000;
      digitalWrite(LED_PIN, HIGH);
    } else if (message == "OFF") {
      actionFermerMecanisme();
      isDoorOpen = false;
      autoCloseTime = 0;
      digitalWrite(LED_PIN, LOW);
    }
    mqttClient.beginMessage(topicState);
    mqttClient.print(isDoorOpen ? "ON" : "OFF");
    mqttClient.endMessage();
  }

  // --- Ajouter un code (Admin) ---
  else if (topic == String(topicAddCode)) {
    addCode(message);
  }

  // --- Utiliser un code (Invité) ---
  else if (topic == String(topicUseCode)) {
    if (useCode(message)) {
      mqttClient.beginMessage(topicCodeResult); mqttClient.print("OK"); mqttClient.endMessage();
      mqttClient.beginMessage(topicCodeUsed);   mqttClient.print(message); mqttClient.endMessage();
      openDoorTemporary();
    } else {
      mqttClient.beginMessage(topicCodeResult); mqttClient.print("FAIL"); mqttClient.endMessage();
    }
  }

  // --- Alarme sonore ---
  else if (topic == String(topicAlarme)) {
    if (message == "ON") {
      tone(PIN_BUZZER, 1000); // 1kHz
    } else if (message == "OFF") {
      noTone(PIN_BUZZER);
    }
  }
}

// ==========================================
//   SETUP & LOOP
// ==========================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pinMode(PIN_BUTE_OUVERT, INPUT_PULLUP);
  pinMode(PIN_BUTE_FERME,  INPUT_PULLUP);
  pinMode(PIN_LED_VERTE, OUTPUT); digitalWrite(PIN_LED_VERTE, LOW);
  pinMode(PIN_LED_ROUGE, OUTPUT); digitalWrite(PIN_LED_ROUGE, LOW);
  pinMode(PIN_BUZZER,    OUTPUT); noTone(PIN_BUZZER);
  pinMode(LED_PIN,       OUTPUT); digitalWrite(LED_PIN, LOW);

  monServo.attach(SERVO_PIN);
  monServo.write(ARRET_SERVO); // Empêche le servo de bouger au démarrage

  connectWiFi();
  connectMQTT();
  mqttClient.onMessage(onMqttMessage);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqttClient.connected()) connectMQTT();

  mqttClient.poll(); // Traite les messages entrants

  // Fermeture automatique après délai
  if (autoCloseTime > 0 && millis() >= autoCloseTime) {
    autoCloseTime = 0;
    actionFermerMecanisme();
    isDoorOpen = false;
    digitalWrite(LED_PIN, LOW);
    Serial.println(">> FERMETURE AUTO (3s) - Porte FERMÉE");
    
    mqttClient.beginMessage(topicState);
    mqttClient.print("OFF");
    mqttClient.endMessage();
  }

  // Envoi régulier de l'état (Keep-Alive)
  if (millis() - lastAlive > 30000) {
    lastAlive = millis();
    mqttClient.beginMessage(topicState);
    mqttClient.print(isDoorOpen ? "ON" : "OFF");
    mqttClient.endMessage();
  }
}
