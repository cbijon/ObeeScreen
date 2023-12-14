#include <WiFi.h>
#include <WireGuard-ESP32.h>
#include <HTTPClient.h>
#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <Arduino_GFX_Library.h>  // Start of Arduino_GFX setting
#include "Images.c"
#include <time.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>


#define IMG_WIDTH 800      // screen definition
#define IMG_HEIGHT 480     //
#define GFX_BL 2           // LED BACKLIGHT
#define OFFSETLINE 10      // pxl
#define GFX_HZ 60          // refresh
#define SPISPEED 80000000  // 80 MHz


/* More dev device declaration: https://github.com/moononournation/Arduino_GFX/wiki/Dev-Device-Declaration */
//#if defined(DISPLAY_DEV_KIT)
//Arduino_GFX *gfx = create_default_Arduino_GFX();
//#else /* !defined(DISPLAY_DEV_KIT) */

// ILI6122
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  40 /* DE */, 41 /* VSYNC */, 39 /* HSYNC */, 0 /* PCLK */,
  45 /* R0 */, 48 /* R1 */, 47 /* R2 */, 21 /* R3 */, 14 /* R4 */,
  5 /* G0 */, 6 /* G1 */, 7 /* G2 */, 15 /* G3 */, 16 /* G4 */, 4 /* G5 */,
  8 /* B0 */, 3 /* B1 */, 46 /* B2 */, 9 /* B3 */, 1 /* B4 */,
  0 /* hsync_polarity */, 8 /* hsync_front_porch */, 1 /* hsync_pulse_width */, 32 /* hsync_back_porch */,
  0 /* vsync_polarity */, 8 /* vsync_front_porch */, 1 /* vsync_pulse_width */, 8 /* vsync_back_porch */,
  1 /* pclk_active_neg */, 16000000 /* prefer_speed */);
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
  800 /* width */, 480 /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */);
int32_t w, h, n, n1, cx, cy;


//#endif /* !defined(DISPLAY_DEV_KIT) */

int LINEOFFSET = 0;

/*******************************************************************************
 * End of Arduino_GFX setting
 ******************************************************************************/

#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"


// WiFi configuration --- UPDATE this configuration for your WiFi AP
//const char *ssid = "untrustedpeople";
//const char *password = "bipross42";
File configFile;
const char *configFilePath = "/config.txt";  // Assurez-vous que le chemin est correct

// Wi-Fi credentials OTA UPDATE
// Hostname for better identification
const char *HOSTNAME = "ObeeMonitor";
const char *OTA_ssid = "myphonesharing";  // PLEASE SET IT FIRST
const char *OTA_PASSWORD = "adminme";     // PLEASE SET IT FIRST : Password for OTA updates
bool OTAupdate = false;
const int MAX_WIFI_ATTEMPT = 3;  // Retry to catch the AP at boot

// WireGuard configuration --- UPDATE this configuration from JSON

const char private_key[] = "aJS48AxKxkqyVt+jjjRQaKmyZOc3APzVb9Y+tJCqalk=";  // [Interface] PrivateKey
IPAddress local_ip(10, 7, 6, 4);                                            // [Interface] Address
const char public_key[] = "E7adxDRSpygYcE9Xpv2dNFlTumPQEYLEiGqMBpiiM1A=";   // [Peer] PublicKey
const char endpoint_address[] = "78.192.232.17";                            // [Peer] Endpoint
const int endpoint_port = 51820;                                            // [Peer] Endpoint

String URLJSON = "http://obeesmart.bijon.fr/data/demoscreen";

static constexpr const uint32_t UPDATE_INTERVAL_MS = 60000;

static WireGuard wg;

WiFiClient client;

AsyncWebServer server(80);



static bool ConnectWifi(const char *_ssid, const char *wifipass) {
  WiFi.disconnect(true, true);
  WiFi.begin(_ssid, wifipass);
  uint8_t wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
    Serial.print(".");
    delay(1000);
    if (wifiAttempts == 10) {
      WiFi.disconnect(true, true);  //Switch off the wifi on making 10 attempts and start again.
      WiFi.begin(_ssid, wifipass);
    }
    wifiAttempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setAutoReconnect(true);          //Not necessary
    Serial.println();                     //Not necessary
    Serial.print("Connected with IP: ");  //Not necessary
    Serial.println(WiFi.localIP());       //Not necessary
    static HTTPClient httpClient;
    return true;
  } else {
    WiFi.disconnect(true, true);
    return false;
  }
  if (wifiAttempts > MAX_WIFI_ATTEMPT) {
    return false;
  }
  delay(100);
}

void Booting_GFX() {
  Serial.println("System");

#ifdef GFX_EXTRA_PRE_INIT
  GFX_EXTRA_PRE_INIT();
#endif

  // Init Display
  if (!gfx->begin(SPISPEED)) {
    Serial.println("gfx->begin() failed!");
  }
  w = gfx->width();
  h = gfx->height();
  n = min(w, h);
  n1 = n - 1;
  cx = w / 2;
  cy = h / 2;
  gfx->fillScreen(BLACK);

#ifdef GFX_BL
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
#endif
}

void splashScreen() {
  gfx->fillScreen(BLACK);

#ifdef GFX_BL
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
#endif
  gfx->draw16bitRGBBitmap(0, 0, (uint16_t *)bootup, IMG_WIDTH, IMG_HEIGHT);
}

void blackScreen() {
  gfx->fillScreen(BLACK);

#ifdef GFX_BL
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
#endif
}

void WriteLineBoot(int OFFSET, char *messages) {
  gfx->setCursor(10, OFFSET);
  gfx->setTextColor(RED);
  gfx->println(messages);
}

void WriteOnScreen(int x, int y, int size, char *messages, uint16_t macro) {
  gfx->setCursor(x, y);
  gfx->setTextSize(size, size, size);
  gfx->setTextColor(macro);
  gfx->println(messages);
}

void WriteDate(int x, int y, int size, char *messages, uint16_t macro) {
  gfx->setCursor(x, y);

  gfx->setTextSize(2, 2, 1);
  gfx->setTextColor(macro);
  gfx->println(messages);
}

void buildGrid() {


  gfx->drawRect(0, 0, w, 40, GREEN);

  gfx->drawRect(0, 40, w, 80, GREEN);

  for (int i = 0; i < 7; i++) {
    gfx->drawRect(i * (w / 7), 120, (w / 7), h - 120, GREEN);  // each hive + 1 meteo
  }
  char *title = "La Butinerie de Neuilly";
  WriteOnScreen((w - sizeof(title)) / 3, 12, 2, title, RED);
}

void refreshdata() {
  // DATE
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  char today[11];
  char now[9];
  strftime(today, 11, "%F", &timeinfo);
  strftime(now, 6, "%H:%M", &timeinfo);
  WriteDate(8, 12, 2, today, BLUE);
  WriteDate(w - 70, 12, 2, now, BLUE);

  // DATA from JSON
  HTTPClient http;
  http.begin(URLJSON);

  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    // Récupérer la réponse et mesurer la capacité nécessaire
    String payload = http.getString();
    //size_t capacity = measureJson(payload);
    // Allouer la mémoire nécessaire
    DynamicJsonDocument doc(4000);
    // Désérialiser le JSON
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      Serial.print("Failed to parse JSON: ");
      Serial.println(error.c_str());
    } else {
      // Extraction des données
      long frelons_0_frags = long(doc["frelons"][0]["frags"]);  // 263
      char frags[20];
      sprintf(frags, "Frelons : %d", frelons_0_frags);
      WriteOnScreen(20, 75, 4, frags, YELLOW);

      // Meteo exterieur basé sur la premiere balance
      char tempOut[8];
      float tempOut_0_ObeeTwo = doc["tempOut"][0]["ObeeTwo"];
      char humidityOut[5];
      int humidityOut_0_ObeeTwo = doc["humidityOut"][0]["ObeeTwo"];
      char pressureOut[10];
      float pressureOut_0_ObeeTwo = doc["pressureOut"][0]["ObeeTwo"];
      sprintf(tempOut, "T %d C ", int(tempOut_0_ObeeTwo));
      sprintf(humidityOut, "H %d \%", humidityOut_0_ObeeTwo);
      sprintf(pressureOut, "P %d hPa", int(pressureOut_0_ObeeTwo));
      WriteOnScreen(500, 51, 2, tempOut, YELLOW);
      WriteOnScreen(500, 75, 2, humidityOut, YELLOW);
      WriteOnScreen(500, 100, 2, pressureOut, YELLOW);

      // Nommage Ruche
      for (int i = 0; i < 7; i++) {
        char ruchename[7];
        sprintf(ruchename, "Ruche %d", i + 1);
        WriteOnScreen(i * (w / 7) + 15, 130, 2, ruchename, RED);
      }

      // Informations harpes
      JsonArray harpeStats = doc["harpeStats"];
      JsonArray harpePower = doc["harpePower"];

      const int HARPE_STATS_COUNT = 7;
      int harpeStatsValues[HARPE_STATS_COUNT];
      int harpePowerValues[HARPE_STATS_COUNT];
      const char *harpeStatsNames[HARPE_STATS_COUNT] = { "Harpe1", "Harpe2", "Harpe3", "Harpe4", "Harpe5", "Harpe6", "Harpe7" };
      const char *harpePowerNames[HARPE_STATS_COUNT] = { "Harpe1power", "Harpe2power", "Harpe3power", "Harpe4power", "Harpe5power", "Harpe6power", "Harpe7power" };
      for (int i = 0; i < HARPE_STATS_COUNT; ++i) {
        harpeStatsValues[i] = doc["harpeStats"][i][harpeStatsNames[i]];
        harpePowerValues[i] = doc["harpePower"][i][harpePowerNames[i]];
        char harpestatuts[6];
        char harpename[7];
        sprintf(harpename, "Harpe %d", i + 1);
        WriteOnScreen(i * (w / 7) + 15, 400, 2, harpename, YELLOW);

        if (harpeStatsValues[i] == 0) {
          strcpy(harpestatuts, "OFF");
          WriteOnScreen(i * (w / 7) + 15, 430, 2, harpestatuts, RED);
        }
        if (harpeStatsValues[i] == 1) {
          strcpy(harpestatuts, "SLEEP");
          WriteOnScreen(i * (w / 7) + 15, 430, 2, harpestatuts, BLUE);
        }
        if (harpeStatsValues[i] == 2) {
          strcpy(harpestatuts, "ON");
          WriteOnScreen(i * (w / 7) + 15, 430, 2, harpestatuts, GREEN);
        }
        char harpePower[6];
        sprintf(harpePower, "%d mW", harpePowerValues[i]);
        WriteOnScreen(i * (w / 7) + 15, 455, 2, harpePower, RED);
      }
      // infos Ruche 1
      char Ruche1poid[10];
      if (doc["poid"][6]["ObeeTwo"].as<int>() > 1000) {
        sprintf(Ruche1poid, "%d Kg", doc["poid"][6]["ObeeTwo"].as<int>() / 1000);
      } else {
        sprintf(Ruche1poid, "%d Gr", doc["poid"][6]["ObeeTwo"].as<int>());
      }
      WriteOnScreen(15, 170, 3, Ruche1poid, YELLOW);

      float pmax = 0;
      float rmax = 100;
      for (int i = 0; i < 7; i++) {
        if (doc["poid"][i]["ObeeTwo"].as<float>() > pmax)
          pmax = doc["poid"][i]["ObeeTwo"].as<float>();
        //Serial.println(pmax);
      }
      for (int i = 0; i < 7; i++) {
        float level = rmax / pmax * doc["poid"][i]["ObeeTwo"].as<float>();
        gfx->fillRect(i * (100 / 7) + 10, 305, 8, 0 - int(level), GREEN);
      }

      char temp[8];
      float temp_0_ObeeTwo = doc["temp"][0]["ObeeTwo"];
      char tempMiel[8];
      float tempMiel_0_ObeeTwo = doc["tempMiel"][0]["ObeeTwo"];
      char humidity[5];
      int humidity_0_ObeeTwo = doc["humidity"][0]["ObeeTwo"];

      sprintf(temp, "T %d C ", int(tempOut_0_ObeeTwo));
      sprintf(tempMiel, "M %d C", int(tempMiel_0_ObeeTwo));
      sprintf(humidity, "H %d \%", humidity_0_ObeeTwo);
      WriteOnScreen(10, 320, 2, temp, YELLOW);
      WriteOnScreen(10, 340, 2, tempMiel, YELLOW);
      WriteOnScreen(10, 360, 2, humidity, YELLOW);
    }
  } else {
    Serial.print("Failed to connect to server. HTTP code: ");
    Serial.println(httpCode);
  }

  http.end();
}

int newLineOffSet() {
  LINEOFFSET = LINEOFFSET + OFFSETLINE;
  if (LINEOFFSET > 480)
    LINEOFFSET = 0;
  return LINEOFFSET;
}


void setup(void) {
  Booting_GFX();
  Serial.begin(115200);
  WriteLineBoot(newLineOffSet(), "booting system....");
  // identify MCU device(MAC)
  uint64_t chipid = ESP.getEfuseMac();
  uint16_t chip = (uint16_t)(chipid >> 32);
  Serial.printf("ESP32 Chip ID = %04X", (uint16_t)(chipid >> 32));
  Serial.printf("%08X\n", (uint32_t)chipid);
  // Initialiser la carte SD
  if (!SD.begin(10)) {  // Assurez-vous que le pin CS (chip select) est correct
    Serial.println("Erreur lors de l'initialisation de la carte SD.");
    return;
  }

  // Ouvrir le fichier de configuration
  configFile = SD.open(configFilePath);
  if (!configFile) {
    Serial.println("Erreur lors de l'ouverture du fichier de configuration.");
    return;
  }

  // Lire le contenu du fichier
  String ssid = configFile.readStringUntil('\n');
  String password = configFile.readStringUntil('\n');

  // Fermer le fichier
  configFile.close();

  if (ConnectWifi(OTA_ssid, OTA_PASSWORD)) {
    WriteLineBoot(newLineOffSet(), "MODE OTA ACTIVE : waitting for update or reboot");
    Serial.println("OTA mode activated");
    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(200, "text/plain", "Hi! I am ESP32.");
    });
    AsyncElegantOTA.begin(&server);  // Start ElegantOTA
    server.begin();
    Serial.println("HTTP server started");
    WriteLineBoot(newLineOffSet(), "HTTP Started !");
    OTAupdate = true;
  } else {
    WriteLineBoot(newLineOffSet(), "Connecting...");
    Serial.println("Connecting to the AP...");
    if (ConnectWifi(ssid.c_str(), password.c_str())) {
      delay(1000);
      WriteLineBoot(newLineOffSet(), "Networking OK");
      Serial.println("Adjusting system time...");
      WriteLineBoot(newLineOffSet(), "Adjusting system time...");
      configTime(1 * 60 * 60, 0, "fr.pool.ntp.org", "time.nist.gov", "time.google.com");

      /**Serial.println("Connected. Initializing WireGuard...");
      wg.begin(
        local_ip,
        private_key,
        endpoint_address,
        public_key,
        endpoint_port);*/
    }
    splashScreen();
    delay(3000);  // 5 seconds
    blackScreen();
    buildGrid();
  }
}

const size_t bufferSize = 256;

void loop() {
  blackScreen();
  buildGrid();
  if (!OTAupdate) {
    //Serial.println("REGULAR : salut kdecherf !");
    refreshdata();
  } else {
    Serial.println("OTA");
  }

  delay(UPDATE_INTERVAL_MS);
}
