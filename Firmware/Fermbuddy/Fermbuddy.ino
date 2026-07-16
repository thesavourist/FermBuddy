#include <FS.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <NimBLEDevice.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <PNGdec.h>

PNG png;
File pngFile;
int16_t pngX;
int16_t pngY;
AsyncWebServer server(80);
DNSServer dnsServer;
// ----------------------------------------------------
// BUTTONS and GLOBALS
// ----------------------------------------------------

#define BTN_LEFT  0
#define BTN_RIGHT 14
#define MAX_TILTS 8 // There are only 8 Tilt colors
#define TFT_BL 15
#define BAT_PIN 4


TFT_eSPI tft = TFT_eSPI();
uint16_t FB_BG = tft.color565(34, 38, 46);

DynamicJsonDocument doc(8192);

uint16_t raw = analogRead(BAT_PIN);


enum OperatingMode {
    MODE_STANDALONE,
    MODE_BLE,
    MODE_API,
    MODE_BLECONFIG
};

enum TempUnits {
    TEMP_FAHRENHEIT,
    TEMP_CELSIUS
};

enum GravityUnits {
    GRAV_SG,
    GRAV_PLATO
};

struct Config {

    OperatingMode mode;

    TempUnits tempUnit;
    GravityUnits gravityUnit;

    int displayTimeout;
    int screenOrientation;

    String wifiSSID;
    String wifiPassword;

    bool hiddenSSID;

    String apiUrl;
};

Config config;

Preferences configPrefs, 
            tiltPrefs;

String dataSource;

int tiltCount = 0;
int currentTilt = 0;
int len25Count = 0;

unsigned long lastActivity = 0;

bool screenOn = true;
bool configAPActive = false;
bool shouldReboot = false;
unsigned long rebootAt = 0;

/* BLE for Tilt */
struct TiltDataBLE {
    String color;
    float gravity;
    float tempF;
    bool valid = false;
    unsigned long lastSeen = 0;
};

String tiltColor = "";
String curColor = "";

struct TiltColorDef {
    String name;
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

TiltColorDef tiltColors[] = {
    {"RED",    255,  0,   0},
    {"GREEN",    47, 115,   29},
    {"BLACK",    0,   0,   0},
    {"PURPLE", 128,   0, 128},
    {"ORANGE", 255, 165,   0},
    {"BLUE",     34,   0, 245},
    {"YELLOW", 253, 255,   69},
    {"PINK",   255, 105, 180}
};

TiltDataBLE tiltBLE[8];

struct TiltDisplayData {

    bool valid = false;

    String beerName;

    float og = 0;
    float gravity = 0;

    float temperature = 0;

    float attenuation = 0;
    float abv = 0;
};



String getTiltName(uint8_t colorCode) {
    switch(colorCode) {
        case 0x10: return "RED";
        case 0x20: return "GREEN";
        case 0x30: return "BLACK";
        case 0x40: return "PURPLE";
        case 0x50: return "ORANGE";
        case 0x60: return "BLUE";
        case 0x70: return "YELLOW";
        case 0x80: return "PINK";
        default:   return "UNKNOWN";
    }
}

int getTiltIndex(uint8_t colorCode) {
    switch(colorCode) {
        case 0x10: return 0; // RED
        case 0x20: return 1; // GREEN
        case 0x30: return 2; // BLACK
        case 0x40: return 3; // PURPLE
        case 0x50: return 4; // ORANGE
        case 0x60: return 5; // BLUE
        case 0x70: return 6; // YELLOW
        case 0x80: return 7; // PINK
        default:   return -1;
    }
}

TiltDataBLE* getTiltByColor(String color) {
    for (auto& tilt : tiltBLE) {

        if (tilt.valid &&
            tilt.color == color) {

            return &tilt;
        }
    }

    return nullptr;
}

String getFirstValidColor() {
    for (auto& tilt : tiltBLE) {

        if (tilt.valid) {
            return tilt.color;
        }
    }
    return "";
}

String getNextValidColor(String curColor, int direction) {
    int currentIdx = -1;

    for (int i = 0; i < MAX_TILTS; i++) {

        if (tiltColors[i].name == curColor) {

            currentIdx = i;
            break;
        }
    }

    if (currentIdx < 0) {
        return getFirstValidColor();
    }

    for (int step = 1; step <= MAX_TILTS; step++) {

        int idx = (currentIdx + direction * step + MAX_TILTS) % MAX_TILTS;

        for (auto& tilt : tiltBLE) {

            if (tilt.valid &&
                tilt.color == tiltColors[idx].name) {

                return tiltColors[idx].name;
            }
        }
    }

    return curColor;
}

// BLE Class 
class MyScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* device) override {
        if (!device->haveManufacturerData())
            return;
    
        std::string data = device->getManufacturerData();
        if (data.length() == 25) {
            len25Count++;
        }
        if (data.length() < 25)
            return;

        if ((uint8_t)data[0] != 0x4C ||
            (uint8_t)data[1] != 0x00 ||
            (uint8_t)data[2] != 0x02 ||
            (uint8_t)data[3] != 0x15 ||
            (uint8_t)data[4] != 0xA4 ||
            (uint8_t)data[5] != 0x95 ||
            (uint8_t)data[6] != 0xBB)
        {
            return;
        }

        uint8_t colorCode = (uint8_t)data[7];

        int idx = getTiltIndex(colorCode);

        if (idx < 0)
            return;

        uint16_t tempF =
            ((uint8_t)data[20] << 8) |
            (uint8_t)data[21];

        uint16_t gravityRaw =
            ((uint8_t)data[22] << 8) |
            (uint8_t)data[23];

        tiltBLE[idx].color    = getTiltName(colorCode);
        tiltBLE[idx].tempF    = tempF;
        tiltBLE[idx].gravity  = gravityRaw / 1000.0;
        tiltBLE[idx].valid    = true;
        tiltBLE[idx].lastSeen = millis();
    }
};


float readBatteryVoltage() {
    uint16_t raw = analogRead(BAT_PIN);

    float v = (raw / 4095.0f) * 3.3f * 2.0f;

    return v;
}

void drawBattery(int x, int y, int pct) {
    uint16_t col = TFT_GREEN;

    if (pct < 50) col = TFT_YELLOW;
    if (pct < 20) col = TFT_RED;

    // Border
    tft.drawRoundRect(x, y, 32, 14, 2, TFT_WHITE);

    // Pin
    tft.fillRect(x + 32, y + 4, 3, 6, TFT_WHITE);

    // Filling
    int fill = map(pct, 0, 100, 0, 28);

    tft.fillRect(
        x + 2,
        y + 2,
        fill,
        10,
        col
    );

    // Percentage (not accurate)
    /*String s = String(pct) + "%";

    int w = tft.textWidth(s);

    tft.drawString(
        s,
        x + (32 - w) / 2,
        y + 20
    );*/
}
void drawLightning(int x, int y, uint16_t color) {
    
    x = x + 20;
    
    tft.fillTriangle(
        x+6, y,
        x,   y+12,
        x+5, y+12,
        color
    );

    tft.fillTriangle(
        x+4, y+12,
        x+10,y+12,
        x+2, y+24,
        color
    );

    // Text when plugged in (needs space)
   /*String s = "USB";

    int w = tft.textWidth(s);
    x = x - 10;
    tft.drawString(
        s,
        x + (32 - w) / 2,
        y + 30
    );*/
}


int PNGDraw(PNGDRAW *pDraw) {
    uint16_t lineBuffer[pDraw->iWidth];

    png.getLineAsRGB565(
        pDraw,
        lineBuffer,
        PNG_RGB565_LITTLE_ENDIAN,
        0xffffffff
    );

    tft.pushImage(
        pngX,
        pngY + pDraw->y,
        pDraw->iWidth,
        1,
        lineBuffer
    );

    return 1;
}

// Splash image
void* PNGOpen(const char *filename, int32_t *size) {
    pngFile = LittleFS.open(filename);

    if (!pngFile)
        return nullptr;

    *size = pngFile.size();

    return &pngFile;
}
void PNGClose(void *handle) {
    File *f = static_cast<File *>(handle);

    if (f)
        f->close();
}
int32_t PNGRead(
    PNGFILE *page,
    uint8_t *buffer,
    int32_t length
) {
    File *f = static_cast<File *>(page->fHandle);

    return f->read(buffer, length);
}
int32_t PNGSeek(
    PNGFILE *page,
    int32_t position
) {
    File *f = static_cast<File *>(page->fHandle);

    f->seek(position);

    return position;
}
void drawPNG(
    const char *filename,
    int16_t x,
    int16_t y
) {
    pngX = x;
    pngY = y;

    int rc = png.open(
        filename,
        PNGOpen,
        PNGClose,
        PNGRead,
        PNGSeek,
        PNGDraw
    );

    if (rc == PNG_SUCCESS) {
        png.decode(NULL,0);

        png.close();
    }
    else
    {
        Serial.printf(
            "PNG open failed %d\n",
            rc
        );
    }
}

// UMRECHNUNGEN
float SGtoPlato(float sg) {
    return -616.868
         + 1111.14 * sg
         - 630.272 * sg * sg
         + 135.997 * sg * sg * sg;
}
float PlatoToSG(float plato) {
    return 1.0 +
           (plato /
           (258.6 - ((plato / 258.2) * 227.1)));
}
float fahrenheitToCelsius(float tempF) {
    return (tempF - 32.0) * 5.0 / 9.0;
}

// ----------------------------------------------------
// HELPERS
// ----------------------------------------------------
bool isBleMode() {
    return config.mode == MODE_BLE ||
           config.mode == MODE_STANDALONE;
}

bool isApiMode() {
    return config.mode == MODE_API;
}

bool needsWifi() {
    return config.mode == MODE_BLE ||
           config.mode == MODE_API ||
           config.mode == MODE_BLECONFIG;
}

bool startNetwork() {
    if (!needsWifi()) {
        return true;
    }

    if (config.wifiSSID.isEmpty()) {

        Serial.println("No WiFi configured");

        startConfigAP();

        return false;
    }

    if (connectWifi()) {
        return true;
    }

    startConfigAP();

    return false;
}

// Konfiguration
void loadConfig(){
    config.wifiSSID     = configPrefs.getString("ssid", "");
    config.wifiPassword = configPrefs.getString("pass", "");
    config.apiUrl       = configPrefs.getString("apiurl", "");
    config.hiddenSSID =
          configPrefs.getBool(
              "hiddenwifi",
              false
          );
    config.mode =
            (OperatingMode)configPrefs.getInt(
                "mode",
                MODE_BLE
            );

    config.tempUnit =
        (TempUnits)
        configPrefs.getInt(
            "tempunit",
            0
        );
    config.gravityUnit =
        (GravityUnits)
        configPrefs.getInt(
            "fermunit",
            0
        );

    config.displayTimeout = configPrefs.getInt("displaytimeout", 3);
    config.screenOrientation = configPrefs.getInt("orientation", 1);

}

// Factory Reset with check
void checkFactoryReset() {
    pinMode(
        BTN_LEFT,
        INPUT_PULLUP
    );

    pinMode(
        BTN_RIGHT,
        INPUT_PULLUP
    );

    if (
        digitalRead(BTN_LEFT) == LOW &&
        digitalRead(BTN_RIGHT) == LOW
    ) {
            factoryReset();
    }
}

void factoryReset() {
    configPrefs.clear();
    delay(500);
    tiltPrefs.clear();
    delay(500); 
    ESP.restart();
}

// WIFI
bool connectWifi() {
    if(!needsWifi()) {
        return true;
    } 
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
    if(config.hiddenSSID) {
      WiFi.begin(
          config.wifiSSID.c_str(),
          config.wifiPassword.c_str(),
          0,
          NULL,
          true
      );
    }
    else {
      WiFi.begin(
          config.wifiSSID.c_str(),
          config.wifiPassword.c_str()
      );
    }

    int timeout = 0;

    while (
        WiFi.status() != WL_CONNECTED &&
        timeout < 20
    )
    {
        delay(600);
        timeout++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        configAPActive = false;
        WiFi.softAPdisconnect(true);
        dnsServer.stop();
        WiFi.mode(WIFI_STA);

        showNetworkInfo(
            "WiFi Connected",
            WiFi.localIP().toString()
        );
        return true;
    }
    else {
        Serial.println("WIFI Fallback to AP");
    }
}


void startConfigAP() {
    configAPActive = true;

    WiFi.disconnect(true, true);

    WiFi.mode(WIFI_OFF);

    delay(1000);

    WiFi.mode(WIFI_AP);

    WiFi.softAP(
        "FermBuddy Setup",
        "fermbuddy"
    );

    dnsServer.start(
        53,
        "*",
        WiFi.softAPIP()
    );

    showNetworkInfo(
        "Config AP Active",
        WiFi.softAPIP().toString()
    );

}

void showNetworkInfo(String title,String ip) {
    tft.fillScreen(FB_BG);
    tft.setFreeFont(
        &FreeMonoBold12pt7b
    );
    tft.drawString(
        title,
        10,
        20
    );

    tft.drawString(
        ip,
        10,
        50
    );
}
// WIFI 

// WEBSERVER
void setupWebServer() {
    server.serveStatic("/", LittleFS, "/");
    // Root when in AP or Webmode
    server.on("/", HTTP_GET,
    [](AsyncWebServerRequest *request) {

        if (configAPActive) {
            if(config.mode == MODE_BLECONFIG) {
                request->send(
                    LittleFS,
                    "/index.html",
                    "text/html"
                );
            }
            else {
                request->send(
                    LittleFS,
                    "/setup.html",
                    "text/html"
                );
            }
        } else {
            request->send(
                LittleFS,
                "/index.html",
                "text/html"
            );
        }
    });

    // setup manually
    server.on("/setup", HTTP_GET,
    [](AsyncWebServerRequest *request) {

        request->send(
            LittleFS,
            "/setup.html",
            "text/html"
        );
    });

     // connection page
    server.on("/connection", HTTP_GET,
    [](AsyncWebServerRequest *request) {

        request->send(
            LittleFS,
            "/connection.html",
            "text/html"
        );
    });


    // hydrometer page 
    server.on("/hydrometers", HTTP_GET,
    [](AsyncWebServerRequest *request) {

        request->send(
            LittleFS,
            "/hydrometers.html",
            "text/html"
        );
    });

    // settings page
    server.on("/settings", HTTP_GET,
    [](AsyncWebServerRequest *request) {

        request->send(
            LittleFS,
            "/settings.html",
            "text/html"
        );
    });
    
     // resets page
    server.on("/resets", HTTP_GET,
    [](AsyncWebServerRequest *request) {

        request->send(
            LittleFS,
            "/resets.html",
            "text/html"
        );
    });   
    // reboot via url
    server.on("/reboot", HTTP_GET,
    [](AsyncWebServerRequest *request) {

        request->send(
            200,
            "text/html",
            "<h2>FermBuddy is restarting...</h2>"
            "<p>You may close this page.</p>"
        );
         shouldReboot = true;
         rebootAt = millis() + 500;
    });
    // reboot via api
    server.on("/api/reboot", HTTP_POST,
    [](AsyncWebServerRequest *request) {
         request->send(
            200,
            "text/html",
            "<h2>FermBuddy is restarting...</h2>"
            "<p>You may close this page.</p>"
        );
         shouldReboot = true;
         rebootAt = millis() + 500;
    }); 

    // save Configdata
    server.on(
        "/api/resetdata",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {
        },
        NULL,
        [](AsyncWebServerRequest *request,
        uint8_t *data,
        size_t len,
        size_t index,
        size_t total) {

            String body;

            for (size_t i = 0; i < len; i++) {
                body += (char)data[i];
            }
            JsonDocument doc;

            DeserializationError err =
                deserializeJson(doc, body);

            if (err) {

                request->send(
                    400,
                    "text/plain",
                    "Invalid JSON"
                );

                return;
            }
            int clearTilts =
                doc["clearTilts"].as<int>();

            int clearAll =
                doc["clearAll"].as<int>();

            if(clearTilts == 1) {
                tiltPrefs.clear();
                 request->send(
                    200,
                    "text/plain",
                    "1"
                );
            }
            if(clearAll == 1) {
                tiltPrefs.clear();
                configPrefs.clear();
                request->send(
                    200,
                    "text/plain",
                    "2"
                );
            }
            
           
        }
    );

    // see the status of Fermbuddy
    server.on("/status", HTTP_GET,
    [](AsyncWebServerRequest *request) {

        request->send(
            200,
            "text/plain",
            "FermBuddy is running."
        );
    });

    // get data for dashboard
    server.on(
        "/api/dashboard",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            JsonDocument out;

            JsonArray tilts =
                out.to<JsonArray>();

            if (isApiMode()) {
                loadData();
                for (
                    int i = 0;
                    i < tiltCount;
                    i++
                ) {

                    JsonObject src =
                        doc[i];

                    JsonObject dst =
                        tilts.add<JsonObject>();
                    String tiltCol =
                        src["Color"];

                    dst["color"] =
                        src["Color"];

                    dst["beername"] =
                        src["Beer"][0];


                    String cloudurl = src["doclongurl"];
                    cloudurl.replace("| View Cloud Log", "View Cloud Log");
                    dst["cloudUrl"] = cloudurl;
                        

                    float abv = 0;
                    float evg = 0;
                     float curGravity =
                        src["SG"].as<float>();

                    float og =
                        tiltPrefs.getFloat(
                            tiltCol.c_str(),
                            -1.0
                        );
                    if(og > 0) {

                        float calcOG =
                            tiltPrefs.getFloat(
                                tiltCol.c_str(),
                                -1.0
                            );

                        float calcCurrent =
                            curGravity;

                        float platoOG =
                            SGtoPlato(calcOG);

                        float platoCurrent =
                            SGtoPlato(calcCurrent);

                        float sgag =
                            1 + (
                                platoCurrent /
                                (
                                    258.6 -
                                    0.879 * platoCurrent
                                )
                            );

                        float abw =
                            81.92 *
                            (
                                (platoOG - platoCurrent)
                                /
                                (
                                    205.65 -
                                    1.0665 * platoOG
                                )
                            );

                        abv =
                            sgag *
                            abw /
                            0.7894;

                        evg =
                            (
                                (platoOG - platoCurrent)
                                /
                                platoOG
                            ) * 100.0;
                    }
                   
                    float displayGravity =
                        curGravity;

                    float displayOG =
                        og;

                    if(config.gravityUnit == GRAV_PLATO) {

                        displayGravity =
                            SGtoPlato(curGravity);

                        if(og > 0) {
                            displayOG =
                                SGtoPlato(og);
                        }

                        // 1 Nachkommastelle für Plato
                        displayGravity =
                            roundf(displayGravity * 10.0f) / 10.0f;

                        if(displayOG > 0) {
                            displayOG =
                                roundf(displayOG * 10.0f) / 10.0f;
                        }
                    }
                    else {

                        // 4 Nachkommastellen für SG
                        displayGravity =
                            roundf(displayGravity * 10000.0f) / 10000.0f;

                        if(displayOG > 0) {
                            displayOG =
                                roundf(displayOG * 10000.0f) / 10000.0f;
                        }
                    }
                    dst["gravity"] = displayGravity;
                    dst["og"] = displayOG;

                    float roundabv = roundf(abv * 10) / 10;
                    float roundatt = roundf(evg * 10) / 10;
                    if(roundabv <= 0) { roundabv = 0; }
                    dst["abv"] =
                        roundabv;

                    if(roundatt <= 0) { roundatt = 0; }
                    dst["attenuation"] =
                        roundatt;
                        
                        
                    float temp = 0;

                    if(config.tempUnit == TEMP_CELSIUS) {
                        temp =
                            src["displayTemp"];
                    }
                    else {
                        temp =
                            src["Temp"];
                    }

                    dst["temp"] =
                        temp;

                    dst["gravityUnit"] =
                        config.gravityUnit == GRAV_PLATO
                            ? "°P"
                            : "SG";

                    dst["tempUnit"] =
                        config.tempUnit == TEMP_CELSIUS
                            ? "°C"
                            : "°F";    
                }
            }
            else {

                for(int i = 0; i < MAX_TILTS; i++) {

                    if(!tiltBLE[i].valid) {
                        continue;
                    }

                    JsonObject dst =
                        tilts.add<JsonObject>();

                    String color = 
                        tiltBLE[i].color;
                         
                    dst["color"] =
                        color;

                    dst["beername"] = 
                        tiltPrefs.getString(
                            (color+"_name").c_str(),
                             "Untitled"
                        );
                    
                    float abv = 0;
                    float evg = 0;

                    float og =
                    tiltPrefs.getFloat(
                        color.c_str(),
                        -1
                    );

                    float offset =
                    tiltPrefs.getFloat(
                        (color + "_offset").c_str(),
                        0
                    );

                    bool isHighResolution =
                        (tiltBLE[i].tempF > 300 && tiltBLE[i].gravity > 9);
                    
                    float newTempF, newGravity;

                    if (isHighResolution) {
                        newTempF   = tiltBLE[i].tempF / 10.0f;
                        newGravity = tiltBLE[i].gravity / 10.0f;
                    }
                    else {
                        newTempF   = tiltBLE[i].tempF;
                        newGravity = tiltBLE[i].gravity;
                    }

                    float curGravity =
                        newGravity +
                        offset;

                    if(og > 0) {

                        float calcOG =
                            tiltPrefs.getFloat(
                                color.c_str(),
                                -1.0
                            );

                        float calcCurrent =
                            curGravity;

                        float platoOG =
                            SGtoPlato(calcOG);

                        float platoCurrent =
                            SGtoPlato(calcCurrent);

                        float sgag =
                            1 + (
                                platoCurrent /
                                (
                                    258.6 -
                                    0.879 * platoCurrent
                                )
                            );

                        float abw =
                            81.92 *
                            (
                                (platoOG - platoCurrent)
                                /
                                (
                                    205.65 -
                                    1.0665 * platoOG
                                )
                            );

                        abv =
                            sgag *
                            abw /
                            0.7894;

                        evg =
                            (
                                (platoOG - platoCurrent)
                                /
                                platoOG
                            ) * 100.0;
                    }
                    float displayGravity =
                        curGravity;

                    float displayOG =
                        og;

                    if(config.gravityUnit == GRAV_PLATO) {

                        displayGravity =
                            SGtoPlato(curGravity);

                        if(og > 0) {
                            displayOG =
                                SGtoPlato(og);
                        }

                        // 1 Nachkommastelle für Plato
                        displayGravity =
                            roundf(displayGravity * 10.0f) / 10.0f;

                        if(displayOG > 0) {
                            displayOG =
                                roundf(displayOG * 10.0f) / 10.0f;
                        }
                    }
                    else {

                        // 4 Nachkommastellen für SG
                        displayGravity =
                            roundf(displayGravity * 10000.0f) / 10000.0f;

                        if(displayOG > 0) {
                            displayOG =
                                roundf(displayOG * 10000.0f) / 10000.0f;
                        }
                    }
                    dst["gravity"] = displayGravity;
                    dst["og"] = displayOG;

                    float roundabv = roundf(abv * 10) / 10;
                    float roundatt = roundf(evg * 10) / 10;
                    if(roundabv <= 0) { roundabv = 0; }
                    dst["abv"] =
                        roundabv;

                    if(roundatt <= 0) { roundatt = 0; }
                    dst["attenuation"] =
                        roundatt;

                    float temp = 0;

                    if(config.tempUnit == TEMP_CELSIUS) {
                        temp =
                            fahrenheitToCelsius(
                                newTempF
                            );
                    }
                    else {
                        temp =
                            newTempF;
                    }

                    dst["temp"] =
                        temp;

                    dst["gravityUnit"] =
                        config.gravityUnit == GRAV_PLATO
                            ? "°P"
                            : "SG";

                    dst["tempUnit"] =
                        config.tempUnit == TEMP_CELSIUS
                            ? "°C"
                            : "°F";    

                }

            }

            String json;

            serializeJson(
                out,
                json
            );

            request->send(
                200,
                "application/json",
                json
            );
        }
    );


    // get connection configdata for setup
    server.on(
    "/api/setupconfig",
    HTTP_GET,
    [](AsyncWebServerRequest *request) {

        JsonDocument doc;

        doc["operatemode"] = config.mode;

        doc["ssid"] = config.wifiSSID;

        doc["password"] = config.wifiPassword;

        doc["hiddenwifi"] =  config.hiddenSSID;

        doc["apiUrl"] = config.apiUrl;

        doc["tempunit"] = config.tempUnit;

        doc["fermunit"] = config.gravityUnit;

        int distimeout = config.displayTimeout;

        if(distimeout == -1) distimeout = 0;

        doc["timeout"] = distimeout;

        String json;

        serializeJson(
            doc,
            json
        );

        request->send(
            200,
            "application/json",
            json
        );
    });


    // get connection configdata
    server.on(
    "/api/connectionconfig",
    HTTP_GET,
    [](AsyncWebServerRequest *request) {

        JsonDocument doc;

        doc["operatemode"] = config.mode;

        doc["ssid"] = config.wifiSSID;

        doc["password"] = config.wifiPassword;

        doc["hiddenwifi"] =  config.hiddenSSID;

        doc["apiUrl"] = config.apiUrl;

        String json;

        serializeJson(
            doc,
            json
        );

        request->send(
            200,
            "application/json",
            json
        );
    });

    // saving WIFI settings only!
    server.on(
        "/api/connectionsave",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {
        },
        NULL,
        [](AsyncWebServerRequest *request,
        uint8_t *data,
        size_t len,
        size_t index,
        size_t total) {

            String body;

            for (size_t i = 0; i < len; i++) {
                body += (char)data[i];
            }
            JsonDocument doc;

            DeserializationError err =
                deserializeJson(doc, body);

            if (err) {

                request->send(
                    400,
                    "text/plain",
                    "Invalid JSON"
                );

                return;
            }
           int mode =
                 doc["operatemode"] | MODE_BLE;

            String ssid =
                doc["ssid"] | "";

            String pass =
                doc["password"] | "";

            bool hiddenwifi =
                doc["hiddenwifi"] | false;

            String api =
                doc["apiUrl"] | "";

            ssid.trim();
            pass.trim();
            api.trim();

            configPrefs.putInt(
                "mode",
                mode
            );

            
            config.mode =
                (OperatingMode)mode;

            configPrefs.putString(
                "ssid",
                ssid
            );

            configPrefs.putString(
                "pass",
                pass
            );

            configPrefs.putBool(
                "hiddenwifi",
                hiddenwifi
            );

            configPrefs.putString(
                "apiurl",
                api
            );  

            request->send(
                200,
                "text/html",
                "<h2>FermBuddy is restarting...</h2>"
                "<p>You may close this page.</p>"
            );
            shouldReboot = true;
            rebootAt = millis() + 500;
        }
    );
    // saving WIFI and Connection Settings 
    server.on(
        "/api/settings",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {
        },
        NULL,
        [](AsyncWebServerRequest *request,
        uint8_t *data,
        size_t len,
        size_t index,
        size_t total) {

            String body;

            for (size_t i = 0; i < len; i++) {
                body += (char)data[i];
            }
            JsonDocument doc;

            DeserializationError err =
                deserializeJson(doc, body);

            if (err) {

                request->send(
                    400,
                    "text/plain",
                    "Invalid JSON"
                );

                return;
            }
           int mode =
                 doc["operatemode"] | MODE_BLE;

            String ssid =
                doc["ssid"] | "";

            String pass =
                doc["password"] | "";

            bool hiddenwifi =
                doc["hiddenwifi"] | false;

            String api =
                doc["apiUrl"] | "";
            
            int tempunit = 
                doc["tempunit"].as<int>();

            int fermunit =
                doc["fermunit"].as<int>();

            int displaytimeout =
                doc["displaytimeout"].as<int>();
                
            if(displaytimeout==0) displaytimeout = -1;

            ssid.trim();
            pass.trim();
            api.trim();

            configPrefs.putInt(
                "mode",
                mode
            );

            
            config.mode =
                (OperatingMode)mode;

            configPrefs.putString(
                "ssid",
                ssid
            );

            configPrefs.putString(
                "pass",
                pass
            );

            configPrefs.putBool(
                "hiddenwifi",
                hiddenwifi
            );

            configPrefs.putString(
                "apiurl",
                api
            );

             configPrefs.putInt(
                "tempunit",
                tempunit
            );
             configPrefs.putInt(
                "fermunit",
                fermunit
            );
             configPrefs.putInt(
                "displaytimeout",
                displaytimeout
            );

            config.displayTimeout = displaytimeout;

            config.tempUnit = (TempUnits)tempunit;
            config.gravityUnit = (GravityUnits)fermunit;

            request->send(
                200,
                "text/html",
                "<h2>FermBuddy is restarting...</h2>"
                "<p>You may close this page.</p>"
            );
            shouldReboot = true;
            rebootAt = millis() + 500;
        }
    );

    // get hydrometer configdata
    server.on(
        "/api/hydrometerconfig",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if (
                !request->hasParam(
                    "color"
                )
            )
            {
                request->send(
                    400,
                    "text/plain",
                    "Missing color"
                );

                return;
            }

            String color =
                request->getParam(
                    "color"
                )->value();

            JsonDocument doc;

            if (
                tiltPrefs.isKey(
                    color.c_str()
                )
            )
            {
                doc["ogstart"] =
                    tiltPrefs.getFloat(
                        color.c_str()
                    );
            }
            else
            {
                doc["ogstart"] = "";
            }

            float measuredSG =
                tiltPrefs.getFloat(color.c_str());

            float offsetSG =
                tiltPrefs.getFloat((color + "_offset").c_str());

            float tiltSG =
                measuredSG - offsetSG;


            doc["beername"] =
                tiltPrefs.getString(
                    (color + "_name").c_str(),
                    ""
                );
            
            doc["tiltog"] =
                tiltSG;

            String json;

            serializeJson(
                doc,
                json
            );

            request->send(
                200,
                "application/json",
                json
            );
        }
    );

    // saving Hydrometer Settings
    server.on(
        "/api/hydrometersettings",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {
        },
        NULL,
        [](AsyncWebServerRequest *request,
        uint8_t *data,
        size_t len,
        size_t index,
        size_t total) {

            String body;

            for (size_t i = 0; i < len; i++) {
                body += (char)data[i];
            }
            JsonDocument doc;

            DeserializationError err =
                deserializeJson(doc, body);

            if (err) {

                request->send(
                    400,
                    "text/plain",
                    "Invalid JSON"
                );

                return;
            }
            String preftiltcolor =
                doc["tiltcol"] | "";

            String prefunit =
                doc["unit"] | "sg";

            float prefogstart =
                doc["ogstart"].as<float>();

            String prefbeername =
                doc["beername"] | "";

            float prefoffset =
                doc["offset"] | 0.0f; 
            
            float putog = prefogstart;
            float putoffset = prefoffset;

            prefbeername.trim();
            if(prefunit != "sg") {

                float tiltOG =
                    prefogstart -
                    prefoffset;

                float measuredSG =
                    PlatoToSG(prefogstart);

                float tiltSG =
                    PlatoToSG(tiltOG);

                putog =
                    measuredSG;

                putoffset =
                    measuredSG -
                    tiltSG;
            }

            tiltPrefs.putFloat(
                preftiltcolor.c_str(),
                putog
            );

            tiltPrefs.putString(
                (preftiltcolor + "_name").c_str(),
                prefbeername
            );

            tiltPrefs.putFloat(
                (preftiltcolor + "_offset").c_str(),
                putoffset
            );
            request->send(
                200,
                "text/plain",
                "1"
            );

        }
    );

    // get Configdata
        server.on(
        "/api/mainconfig",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            JsonDocument doc;


            doc["tempUnit"] = config.tempUnit;

            doc["fermUnit"] = config.gravityUnit;

            doc["displaytimeout"] = config.displayTimeout;

            doc["screenOrientation"] = config.screenOrientation;

            if(config.displayTimeout == -1) {
                doc["displaytimeout"] = 0;
            }

            String json;

            serializeJson(
                doc,
                json
            );

            request->send(
                200,
                "application/json",
                json
            );
        }
    );

    // save Configdata
        server.on(
        "/api/savemainconfig",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {
        },
        NULL,
        [](AsyncWebServerRequest *request,
        uint8_t *data,
        size_t len,
        size_t index,
        size_t total) {

            String body;

            for (size_t i = 0; i < len; i++) {
                body += (char)data[i];
            }
            JsonDocument doc;

            DeserializationError err =
                deserializeJson(doc, body);

            if (err) {

                request->send(
                    400,
                    "text/plain",
                    "Invalid JSON"
                );

                return;
            }
            int fermunit =
                doc["fermunit"].as<int>();

            int tempunit =
                doc["tempunit"].as<int>();

            String timeoutStr =
                doc["displaytimeout"];

            int displaytimeout;

            if (timeoutStr.length() == 0) {
                displaytimeout = 3;
            }
            else {
                displaytimeout =
                    timeoutStr.toInt();
            }

            if (displaytimeout == 0) {
                displaytimeout = -1;
            }

            int orientation =
                doc["screenOrientation"].as<int>();

            configPrefs.putInt(
                "fermunit",
                fermunit
            );
            
            configPrefs.putInt(
                "tempunit",
                tempunit
            );
            configPrefs.putInt(
                "displaytimeout",
                displaytimeout
            );

            configPrefs.putInt(
                "orientation",
                orientation
            );
          
            config.screenOrientation = orientation;
            config.displayTimeout = displaytimeout;

            config.tempUnit = (TempUnits)tempunit;
            config.gravityUnit = (GravityUnits)fermunit;
            
            request->send(
                200,
                "text/plain",
                "1"
            );
        }
    );


    server.begin();
}
// WEBSERVER 



// Get data for display / API
void loadData() {
    if (WiFi.status() != WL_CONNECTED) {
        if(!connectWifi()) {
            Serial.println("No WIFI");
            return;
        }
    }
    // ------------------------------------------------
    // HTTP REQUEST
    // ------------------------------------------------
    if(isApiMode()) {
        WiFiClient client;

        HTTPClient http;

        http.useHTTP10(true);

        http.setTimeout(5000);

        delay(500);

        http.begin(
            client,
            config.apiUrl
        );
        int httpCode =
            http.GET();

        if (httpCode == HTTP_CODE_OK) {
            String payload =
                http.getString();
            DeserializationError error =
                deserializeJson(
                    doc,
                    payload
                );

            if (error) {

                Serial.println(
                    "JSON ERROR"
                );

                return;
            }
            tiltCount =
                doc.size();

            if (httpCode == HTTP_CODE_OK) {

                printTilt(currentTilt);
            }
        }
        else {

            Serial.println(
                "HTTP FAILED"
            );

            Serial.println(
                http.errorToString(
                    httpCode
                )
            );
        }

        http.end();
    }
    else { // BLE
        curColor = getFirstValidColor();
        printTiltBLE(curColor); 
    }
}

// API SOURCE
void printTilt(int index) {
    JsonObject tilt = doc[index];

    String TiltColor =
        tilt["Color"]
            .as<String>();

    String Name =
        tilt["Beer"][0]
            .as<String>();

    String tiltCol = 
        tilt["Color"];

    float og =
    tiltPrefs.getFloat(
        tiltCol.c_str(),
        -1.0
    );

    float plato, platoOG, curGravity, displaySG = 0;
    int fermround = 0;
    String fermUnit, tempUnit = "";
    curGravity = tilt["SG"];
    plato = SGtoPlato(curGravity);
    platoOG = SGtoPlato(og); 

    if(config.gravityUnit == GRAV_SG) {
        displaySG = curGravity;
        fermUnit = "";
        fermround = 3;
    }
    else {
        displaySG = plato;
        fermUnit = " P";
        fermround = 1;
        og = SGtoPlato(og);
    }

    float temp = 0;
    if(config.tempUnit == TEMP_CELSIUS) {
      temp = tilt["displayTemp"];
      tempUnit = " C";
    }
    else {
      temp = tilt["Temp"];
      tempUnit = " F";
    }

    tft.fillScreen(FB_BG);

    tft.setTextColor(TFT_WHITE);
    tft.setFreeFont(
        &FreeMonoBold12pt7b
    );

    tft.fillRect(
        5,   // x
        70,    // y
        40,    // Breite
        95,    // Höhe
        getTiltColor(tiltCol)
    );


    String beerName = 
        String(Name);

    int maxWidth = 230;

    if(
        tft.textWidth(
            beerName
        ) > maxWidth
    )
    {
        int splitPos = -1;

        for(
            int i = 0;
            i < beerName.length();
            i++
        )
        {
            if(
                beerName.charAt(i) == ' '
            )
            {
                String testLine =
                    beerName.substring(
                        0,
                        i
                    );

                if(
                    tft.textWidth(
                        testLine
                    ) > maxWidth
                )
                {
                    break;
                }

                splitPos = i;
            }
        }

        if(splitPos > 0)
        {
            String line1 =
                beerName.substring(
                    0,
                    splitPos
                );

            String line2 =
                beerName.substring(
                    splitPos + 1
                );

            tft.drawString(
                line1,
                5,
                10
            );

            tft.drawString(
                line2,
                5,
                40
            );
        }
        else
        {
            tft.drawString(
                beerName,
                5,
                10
            );
        }
    }
    else
    {
        tft.drawString(
            beerName,
            5,
            10
        );
    }

    // INFO FONT
     tft.setFreeFont(
        &FreeMono9pt7b
    );

    const int labelX = 65;
    const int valueX = 220;

    

    if (og < 0) {
         tft.drawString(
            "No OG stored",
            labelX,
            70
        );
    }
    else {
        tft.drawString(
            "OG:",
            labelX,
            70
        );
        tft.drawRightString(String(String(og, fermround)+fermUnit), valueX, 70, 1);
        

        float sgag = 
            1 + (plato / (258.6 - 0.879 * plato));

        float abw = 
            81.92 * ((platoOG - plato) / (205.65 - 1.0665 * platoOG));

        float alcohol = 
           sgag * abw / 0.7894;

        float evg =
            ((platoOG - plato)
            / platoOG) * 100.0;
        if(evg > 0) { 
            tft.drawString("Att.:", labelX, 130);
            tft.drawRightString(String(evg, 1)+" %", valueX, 130, 1);
        }
        if(alcohol > 0) {
            tft.drawString("ABV:", labelX, 150);
            tft.drawRightString(String(alcohol, 1)+" %", valueX, 150, 1);
        }
    }

    tft.drawString("Cur.:", labelX, 90);
    tft.drawString("Temp.:", labelX, 110);

    String curGrav = 
        String(displaySG, fermround) + fermUnit;
    tft.drawRightString(curGrav, valueX, 90, 1);
   
    String curTemp = 
        String(temp, 1) + tempUnit;
    tft.drawRightString(curTemp, valueX, 110, 1);

    float battStatus = readBatteryVoltage();

    bool usbConnected = battStatus > 4.15f;
    if(usbConnected) {
        drawLightning(270, 140, TFT_YELLOW);
    }
    else {
        float battPercent =
            constrain(
                (battStatus - 3.3) / (4.2 - 3.3) * 100.0,
                0,
                100
            ); 

        drawBattery(270, 150, battPercent);
    }    

}



// Data from BLE source
void printTiltBLE(String curColor) {
    int foundHydro = 0;
  for(int i = 0; i < MAX_TILTS; i++) {
    if (!tiltBLE[i].valid) {
            continue;
    }

      
    if (tiltBLE[i].color == curColor) {
        if (millis() - tiltBLE[i].lastSeen > 60000) {
            return;
        }
        foundHydro++;
        tft.fillScreen(FB_BG);

        tft.setTextColor(TFT_WHITE);
        tft.setFreeFont(
            &FreeMonoBold12pt7b
        );

        tft.fillRect(
            5,   // x
            70,    // y
            30,    // Breite
            95,    // Höhe
            getTiltColor(curColor)
        );
        String beerName = 
           tiltPrefs.getString(
                (curColor+"_name").c_str(),
                ""
            );

        int maxWidth = 230;

        if(
            tft.textWidth(
                beerName
            ) > maxWidth
        )
        {
            int splitPos = -1;

            for(
                int i = 0;
                i < beerName.length();
                i++
            )
            {
                if(
                    beerName.charAt(i) == ' '
                )
                {
                    String testLine =
                        beerName.substring(
                            0,
                            i
                        );

                    if(
                        tft.textWidth(
                            testLine
                        ) > maxWidth
                    )
                    {
                        break;
                    }

                    splitPos = i;
                }
            }

            if(splitPos > 0)
            {
                String line1 =
                    beerName.substring(
                        0,
                        splitPos
                    );

                String line2 =
                    beerName.substring(
                        splitPos + 1
                    );

                tft.drawString(
                    line1,
                    5,
                    10
                );

                tft.drawString(
                    line2,
                    5,
                    40
                );
            }
            else
            {
                tft.drawString(
                    beerName,
                    5,
                    10
                );
            }
        }
        else
        {
            tft.drawString(
                beerName,
                5,
                10
            );
        }

        tft.setFreeFont(
            &FreeMono9pt7b
        );
         TiltDataBLE* tilt = getTiltByColor(curColor);

        if (tilt == nullptr) {
            return;
        }
            float TiltOffset =
                tiltPrefs.getFloat(
                    (curColor+"_offset").c_str(),
                    0
                );
            float og =
            tiltPrefs.getFloat(
                curColor.c_str(),
                -1.0
            );
            float plato, platoOG, curGravity, displaySG, newTempF, newGravity = 0;
            int fermround = 0;
            String fermUnit, tempUnit = "";
            // the difference between tilt2 and pro / pro mini is factor 10 - so if there is a high resolution packet we have to divide by 10
            bool isHighResolution =
                (tilt->tempF > 300 && tilt->gravity > 9);

            if (isHighResolution) {
                newTempF   = tilt->tempF / 10.0f;
                newGravity = tilt->gravity / 10.0f;
            }
            else {
                newTempF   = tilt->tempF;
                newGravity = tilt->gravity;
            }
            
            curGravity = newGravity;
            plato = SGtoPlato(curGravity+TiltOffset);
            platoOG = SGtoPlato(og);
          
            if(config.gravityUnit == GRAV_SG) {
                displaySG = curGravity+TiltOffset;
                fermUnit = "";
                fermround = 3;
            }
            else {
                displaySG = plato;
                fermUnit = " P";
                fermround = 1;
                og = SGtoPlato(og);
            }
            float temp = 0;
            if(config.tempUnit == TEMP_CELSIUS) {
              temp = fahrenheitToCelsius(newTempF);
              tempUnit = " C";
            }
            else {
              temp = newTempF;
              tempUnit = " F";
            }


        const int labelX = 65;
        const int valueX = 220;

        if (og < 0) {
            tft.drawString(
                "No OG stored",
                labelX,
                70
            );
        }
        else {
            tft.drawString(
                "OG:",
                labelX,
                70
            );
            tft.drawRightString(String(String(og, fermround)+fermUnit), valueX, 70, 1);
        

            float sgag = 
                1 + (plato / (258.6 - 0.879 * plato));

            float abw = 
                81.92 * ((platoOG - plato) / (205.65 - 1.0665 * platoOG));

            float alcohol = 
            sgag * abw / 0.7894;

            float evg =
                ((platoOG - plato)
                / platoOG) * 100.0;
            if(evg > 0) { 
                tft.drawString("Att.:", labelX, 130);
                tft.drawRightString(String(evg, 1)+" %", valueX, 130, 1);
            }
            if(alcohol > 0) {
                tft.drawString("ABV:", labelX, 150);
                tft.drawRightString(String(alcohol, 1)+" %", valueX, 150, 1);
            }
        }

        tft.drawString("Cur.:", labelX, 90);
        tft.drawString("Temp.:", labelX, 110);

        String curGrav = 
            String(displaySG, fermround) + fermUnit;
        tft.drawRightString(curGrav, valueX, 90, 1);
    
        String curTemp = 
            String(temp, 1) + tempUnit;
        tft.drawRightString(curTemp, valueX, 110, 1);
    }
  }
  if(foundHydro == 0) {
     tft.fillScreen(FB_BG);

        tft.setTextColor(TFT_WHITE);
        tft.setFreeFont(
            &FreeMonoBold12pt7b
        );

        tft.fillRect(
            5,   // x
            70,    // y
            30,    // Breite
            95,    // Höhe
            TFT_WHITE
        );
         tft.drawString(
                "No Hydrometer found",
                5,
                10
            );

  }
  float battStatus = readBatteryVoltage();

    bool usbConnected = battStatus > 4.15f;
    if(usbConnected) {
        drawLightning(270, 140, TFT_YELLOW);
    }
    else {
        float battPercent =
            constrain(
                (battStatus - 3.3) / (4.2 - 3.3) * 100.0,
                0,
                100
            ); 

        drawBattery(270, 150, battPercent);
    }    
}


// Colors
uint16_t getTiltColor(String tiltCol) {
    for(int i = 0; i < MAX_TILTS; i++)
    {
        if(tiltCol == tiltColors[i].name)
        {
            return tft.color565(
                tiltColors[i].r,
                tiltColors[i].g,
                tiltColors[i].b
            );
        }
    }

    return TFT_WHITE;
}

// Screen functions and Setup / Loop
void wakeScreen() {
    lastActivity = millis();
    if(isBleMode()) {
        NimBLEScan* pScan = NimBLEDevice::getScan();
        if (pScan && !pScan->isScanning()) {
            pScan->start(0);
            Serial.printf("Start BLE, isScanning=%d\n",
              pScan->isScanning());
        }
    }
    if (!screenOn) {
        screenOn = true;

        digitalWrite(38, HIGH);

        tft.writecommand(0x11);
    }
  
    delay(300);

}
void showBootLogo() {
    tft.fillScreen(TFT_WHITE);

    drawPNG(
        "/img/bootlogo.png",
        40,
        15
    );
    if(configAPActive) {
        showOverlay(
            "Config AP Active • "
            + WiFi.softAPIP().toString()
        );
    }
    else if(needsWifi()) {
        showOverlay(
            "Connected • "
            + WiFi.localIP().toString()
        );
    }
    else {
        showOverlay(
            "Standalone Mode activated."
        );
    }
    int startupDelay = 2000;
    if(config.mode == MODE_STANDALONE) {
        startupDelay = 1000;
    }
    delay(startupDelay);
}
void showOverlay(const String& text) {
    const int h = 20;

    tft.fillRoundRect(
        70,
        145,
        180,
        h,
        4,
        TFT_WHITE
    );

    tft.drawRoundRect(
        70,
        145,
        180,
        h,
        4,
        TFT_WHITE
    );

    tft.setTextDatum(MC_DATUM);

    tft.setTextFont(2);          // klein
    tft.setTextColor(TFT_BLACK, TFT_WHITE);

    tft.drawString(
        text,
        160,
        155
    );
    tft.setTextDatum(TL_DATUM);
}


void setup() {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    delay(100);

    Serial.begin(115200);
    checkFactoryReset();

    delay(3000);
    if (!LittleFS.begin(true)) {
        return;
    }

    // BACKLIGHT
    pinMode(38, OUTPUT);
    digitalWrite(38, HIGH);

    // BUTTONS
    pinMode(BTN_LEFT, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    
    //config
    configPrefs.begin("tiltdisplay", false);
    tiltPrefs.begin("tiltdata", false);
    loadConfig();
    
    // TFT
    tft.init();
    Serial.println(config.screenOrientation);
    tft.setRotation(config.screenOrientation);

    tft.setSwapBytes(true);
    tft.fillScreen(FB_BG);

    // WIFI
    WiFi.setSleep(false);
    startNetwork();  
    
    if(needsWifi()) {
        setupWebServer();
    }
    showBootLogo();

    if(isBleMode() &&
            !configAPActive) {
        NimBLEDevice::init("");

        NimBLEScan* pScan = NimBLEDevice::getScan();
        pScan->setDuplicateFilter(false);
        pScan->setScanCallbacks(new MyScanCallbacks(), true);

        pScan->setActiveScan(false);
        pScan->setInterval(100);
        pScan->setWindow(100);
        pScan->setMaxResults(0);
        pScan->start(0);
    }

}

unsigned long lastRotate = 0;

void changeTilt(int direction) {
    if (isApiMode()) {
        loadData();
        currentTilt += direction;

        if (currentTilt >= tiltCount)
            currentTilt = 0;

        if(currentTilt < 0)
            currentTilt = tiltCount - 1;
    
        printTilt(currentTilt);
    }
    if(isBleMode()) {

        if (curColor == "") {
            curColor = getFirstValidColor();
        }

        curColor = getNextValidColor(
            curColor,
            direction
        );

        printTiltBLE(curColor);
    }
}

void loop() {
    if (shouldReboot && millis() > rebootAt) {
        ESP.restart();
    }
    if (configAPActive) {
        dnsServer.processNextRequest();
    }

    if (config.displayTimeout != -1 && !configAPActive) {

        if (
            screenOn &&
            millis() - lastActivity >
            config.displayTimeout * 1000
        ) {

            screenOn = false;

            digitalWrite(38, LOW);

            tft.writecommand(0x10);
            if (!screenOn && millis()-lastActivity > 180000) { // 3 min
                if (isBleMode()) {
                    NimBLEScan* pScan = NimBLEDevice::getScan();
                    if (pScan && pScan->isScanning()) {
                        pScan->stop();
                        Serial.printf("Stopped BLE, isScanning=%d\n",
                                        pScan->isScanning());
                    }
                }
            }
        }
    }
    else {

        screenOn = true;

        digitalWrite(38, HIGH);

        tft.writecommand(0x11);
    }

    static bool holdMessageShown = false;
    static unsigned long lastAutoChange = 0;

    if (config.displayTimeout == -1) {

        if (millis() - lastAutoChange > 10000) {
            changeTilt(1);
            lastAutoChange = millis();
        }
    }


    bool left  = digitalRead(BTN_LEFT)  == LOW;
    bool right = digitalRead(BTN_RIGHT) == LOW;

    static unsigned long bothPressedSince = 0;

    //
    // both buttons
    //
    if (left && right) {

        if (bothPressedSince == 0) {
            bothPressedSince = millis();
        }

        //
        // Standalone → Config AP
        //
        if (config.mode == MODE_STANDALONE) {
            if (!holdMessageShown) {
                String configGateway = "for Config AP";
                if(config.wifiSSID != "") {
                    configGateway = "for booting to Wifi";
                }
                showNetworkInfo(
                    "Hold 1 sec.",
                    configGateway
                );
                holdMessageShown = true;
            }

            if (millis() - bothPressedSince > 1000) {
                
               NimBLEScan* pScan = NimBLEDevice::getScan();
                if (pScan) {
                    pScan->stop();
                    Serial.println("SCAN STOPPED");
                }

                NimBLEDevice::deinit(true);
                tft.fillScreen(FB_BG);
                configAPActive = true;
                config.mode = MODE_BLECONFIG;
                startNetwork();
                setupWebServer();
                showBootLogo();

                while (
                    digitalRead(BTN_LEFT) == LOW ||
                    digitalRead(BTN_RIGHT) == LOW
                ) {
                    delay(10);
                }

                bothPressedSince = 0;
            }
            
        }

        //
        // other modes → Factory Reset
        //
        else {
             if (!holdMessageShown) {
                showNetworkInfo(
                    "Hold 3 sec.",
                    "for Reset"
                );
                holdMessageShown = true;
            }   
            
            if (millis() - bothPressedSince > 3000) {

                showNetworkInfo(
                    "Resetting to",
                    "factory defaults"
                );

                factoryReset();

                bothPressedSince = 0;
            }
        }
    }

    //
    // single buttons
    //
    else {

        bothPressedSince = 0;

        if (right) {

            changeTilt(1);

            wakeScreen();

            lastActivity = millis();
        }

        else if (left) {

            changeTilt(-1);

            wakeScreen();

            lastActivity = millis();
        }
    }

}
