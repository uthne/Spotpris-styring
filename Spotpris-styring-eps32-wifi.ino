#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <new>

// =====================================================
// BRUKERINNSTILLINGER
// =====================================================

// Antall billigste sammenhengende timer, forhåndsvalgt 4
int CHEAPEST_HOURS = 4;

// GPIO
#define OUTPUT_PIN 26
#define SETUP_BUTTON 32

// OLED
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDRESS 0x3C

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// WiFiManager
const char* AP_NAME = "ESP32-Stromstyring";
const char* AP_PASSWORD = "Aass1111";

// API
const char* API_BASE =
  "https://api.strompriseridag.no/v1/";

// Norsk tidssone
const char* TZ_NORWAY =
  "CET-1CEST,M3.5.0,M10.5.0/3";

// Intervaller
const unsigned long WIFI_RETRY_MS = 15000;
const unsigned long API_RETRY_MS = 600000;
const unsigned long DISPLAY_UPDATE_MS = 1000;

// Hold knappen inne i tre sekunder
const unsigned long BUTTON_HOLD_MS = 3000;

// WiFi-oppsettet lukkes etter 5 minutter
const unsigned long WIFI_PORTAL_TIMEOUT = 300000;

// Slå av skjermen etter 2 minutter
const unsigned long OLED_TIMEOUT_MS = 120000UL;

// Tidspunkt for siste OLED aktivitet
unsigned long lastOledActivity = 0;

// Skjermstatus
bool oledSleeping = false; 

// Registrer om WiFi-portalen var aktiv
static bool portalWasActive = false;


// =====================================================
// PRISOMRÅDER
// =====================================================

const char* PRICE_AREAS[] = {
  "NO1",
  "NO2",
  "NO3",
  "NO4",
  "NO5"
};

const char* AREA_NAMES[] = {
  "Ostlandet",
  "Sorlandet",
  "Midt-Norge",
  "Nord-Norge",
  "Vestlandet"
};

const int AREA_COUNT = 5;

// 0 = NO1
int selectedArea = 0;

// =====================================================
// OLED
// =====================================================

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

bool oledReady = false;

// =====================================================
// WIFI MANAGER
// =====================================================

WiFiManager wm;

// HTML for prisområdevelgeren.
// Bufferen må eksistere så lenge WiFiManager
// bruker parameteren.

// Valg av prisområde og atktiv tid
char areaHtml[800];
WiFiManagerParameter areaParameter("");

// Valg av antall billigste timer
char hoursHtml[800];
WiFiManagerParameter hoursParameter("");

// Når brukeren lagrer et nytt område og/eller antall timer,
// håndteres restart i hovedprogrammet.
bool pendingRestart = false;

// =====================================================
// PRISDATA
// =====================================================

struct PricePeriod {
  time_t start;
  time_t end;
  float price;
};

struct CheapWindow {
  time_t start;
  time_t end;
  float averagePrice;
  bool valid;
};

const int MAX_PRICE_PERIODS = 120;

PricePeriod prices[MAX_PRICE_PERIODS];
int priceCount = 0;
 
CheapWindow todayWindow = {};
CheapWindow tomorrowWindow = {};

float currentPrice = 0;
bool currentPriceValid = false;

bool outputActive = false;

// =====================================================
// PERMANENT LAGRING
// =====================================================

// Fast lagringsformat med 64-bits tidsstempler.
struct StoredWindow {
  int64_t start;
  int64_t end;
  float averagePrice;
  uint32_t valid;
};

struct StoredData {
  uint32_t magic;
  uint32_t version;
  int32_t hours;

  StoredWindow today;
  StoredWindow tomorrow;

  uint32_t checksum;
};

const uint32_t STORAGE_MAGIC = 0x5354524D;
const uint32_t STORAGE_VERSION = 2;

// =====================================================
// TIMERE
// =====================================================

unsigned long lastDisplayUpdate = 0;
unsigned long lastWifiRetry = 0;
unsigned long lastApiAttempt = 0;

bool apiAttempted = false;

int lastDownloadDate = 0;
int lastAfternoonDate = 0;

// Knapp
unsigned long buttonStart = 0;
bool buttonHandled = false;

// =====================================================
// DATO OG KLOKKE
// =====================================================

bool clockValid() {

  time_t now = time(nullptr);

  return now > 1700000000;
}

int dateKey(const struct tm &t) {

  return
    (t.tm_year + 1900) * 10000 +
    (t.tm_mon + 1) * 100 +
    t.tm_mday;
}

int dateKey(time_t timestamp) {

  struct tm t;

  localtime_r(&timestamp, &t);

  return dateKey(t);
}

int nextDateKey(const struct tm &today) {

  struct tm next = today;

  next.tm_mday++;
  next.tm_hour = 12;
  next.tm_min = 0;
  next.tm_sec = 0;
  next.tm_isdst = -1;

  return dateKey(mktime(&next));
}

void formatTime(
  time_t timestamp,
  char* buffer,
  size_t length
) {

  if (timestamp <= 0) {

    snprintf(buffer, length, "--:--");
    return;
  }

  struct tm t;

  localtime_r(&timestamp, &t);

  snprintf(
    buffer,
    length,
    "%02d:%02d",
    t.tm_hour,
    t.tm_min
  );
}

// =====================================================
// ISO8601 TIL UNIX-TID
// =====================================================


time_t makeUTC(struct tm *t) {

  // Lagre gjeldende tidssone
  const char* oldTZ = getenv("TZ");
  String savedTZ = oldTZ ? String(oldTZ) : "";

  // Bruk UTC midlertidig
  setenv("TZ", "UTC0", 1);
  tzset();

  t->tm_isdst = 0;
  time_t result = mktime(t);

  // Gjenopprett norsk tidssone
  if (oldTZ) {
    setenv("TZ", savedTZ.c_str(), 1);
  } else {
    unsetenv("TZ");
  }

  tzset();

  return result;
}


time_t parseISO(const char* str) {

  if (!str)
    return 0;

  int y, mo, d;
  int h, mi, s;
  int oh, om;
  char sign;

  int n = sscanf(
    str,
    "%d-%d-%dT%d:%d:%d%c%d:%d",
    &y, &mo, &d,
    &h, &mi, &s,
    &sign, &oh, &om
  );

  if (n != 9)
    return 0;

  if (sign != '+' && sign != '-')
    return 0;

  if (mo < 1 || mo > 12 ||
      d < 1 || d > 31 ||
      h < 0 || h > 23 ||
      mi < 0 || mi > 59 ||
      s < 0 || s > 59 ||
      oh < 0 || oh > 23 ||
      om < 0 || om > 59)
    return 0;

  struct tm t = {};

  t.tm_year = y - 1900;
  t.tm_mon = mo - 1;
  t.tm_mday = d;
  t.tm_hour = h;
  t.tm_min = mi;
  t.tm_sec = s;

  time_t result = makeUTC(&t);

  int offset = oh * 3600 + om * 60;

  if (sign == '+')
    result -= offset;
  else
    result += offset;

  return result;
}

// =====================================================
// LAGRE PRISOMRÅDE
// =====================================================

void loadSelectedArea() {

  Preferences prefs;

  if (!prefs.begin("settings", true))
    return;

  selectedArea = 
    prefs.getInt("area", 0);

  CHEAPEST_HOURS =
    prefs.getInt("cheapHours", 4);

  prefs.end();

  if (selectedArea < 0 ||
      selectedArea >= AREA_COUNT) {

    selectedArea = 0;
  }

  // Kontroller gyldig verdi for timer
  if (CHEAPEST_HOURS < 2 ||
    CHEAPEST_HOURS > 6) {

     CHEAPEST_HOURS = 4;
  }

}

void saveSelectedArea() {

  Preferences prefs;

  if (!prefs.begin("settings", false))
    return;

  prefs.putInt("area", selectedArea);

  prefs.end();
}

// =====================================================
// NVS-NAVN FOR VALGT OMRÅDE
// =====================================================

String getWindowNamespace() {

  return String("strom_") +
         PRICE_AREAS[selectedArea];
}

// =====================================================
// CHECKSUM
// =====================================================

uint32_t checksumData(
  const StoredData &data
) {

  const uint8_t* bytes =
    reinterpret_cast<const uint8_t*>(&data);

  uint32_t hash = 2166136261UL;

  size_t length =
    offsetof(StoredData, checksum);

  for (size_t i = 0; i < length; i++) {

    hash ^= bytes[i];
    hash *= 16777619UL;
  }

  return hash;
}

// =====================================================
// KONVERTER LAGREDE VINDUER
// =====================================================

StoredWindow toStored(
  const CheapWindow &w
) {

  StoredWindow s = {};

  s.start = (int64_t)w.start;
  s.end = (int64_t)w.end;
  s.averagePrice = w.averagePrice;
  s.valid = w.valid ? 1 : 0;

  return s;
}

CheapWindow fromStored(
  const StoredWindow &s
) {

  CheapWindow w = {};

  w.start = (time_t)s.start;
  w.end = (time_t)s.end;
  w.averagePrice = s.averagePrice;

  w.valid =
    s.valid == 1 &&
    w.start > 0 &&
    w.end > w.start &&
    isfinite(w.averagePrice) &&
    w.averagePrice >= 0;

  return w;
}

// =====================================================
// SAMMENLIGN VINDUER
// =====================================================

bool sameWindow(
  const CheapWindow &a,
  const CheapWindow &b
) {

  if (a.valid != b.valid)
    return false;

  if (!a.valid)
    return true;

  return
    a.start == b.start &&
    a.end == b.end &&
    fabs(a.averagePrice -
         b.averagePrice) < 0.0001f;
}

// =====================================================
// LAGRE BILLIGPERIODER I FLASH
// =====================================================

bool saveWindows() {

  StoredData data = {};

  data.magic = STORAGE_MAGIC;
  data.version = STORAGE_VERSION;
  data.hours = CHEAPEST_HOURS;

  data.today = toStored(todayWindow);
  data.tomorrow = toStored(tomorrowWindow);

  data.checksum = checksumData(data);

  Preferences prefs;

  String ns = getWindowNamespace();

  if (!prefs.begin(ns.c_str(), false))
    return false;

  // Unngå skriving hvis innholdet er likt.
  StoredData oldData = {};

  if (prefs.getBytesLength("windows") ==
      sizeof(StoredData)) {

    prefs.getBytes(
      "windows",
      &oldData,
      sizeof(oldData)
    );

    if (memcmp(
          &oldData,
          &data,
          sizeof(data)
        ) == 0) {

      prefs.end();
      return true;
    }
  }

  size_t written = prefs.putBytes(
    "windows",
    &data,
    sizeof(data)
  );

  prefs.end();

  if (written == sizeof(data)) {

    Serial.println("Vinduer lagret i flash");
    return true;
  }

  return false;
}

// =====================================================
// LES BILLIGPERIODER FRA FLASH
// =====================================================

bool loadWindows() {

  todayWindow = {};
  tomorrowWindow = {};

  Preferences prefs;

  String ns = getWindowNamespace();

  if (!prefs.begin(ns.c_str(), true))
    return false;

  if (prefs.getBytesLength("windows") !=
      sizeof(StoredData)) {

    prefs.end();
    return false;
  }

  StoredData data = {};

  size_t count = prefs.getBytes(
    "windows",
    &data,
    sizeof(data)
  );

  prefs.end();

  if (count != sizeof(data))
    return false;

  // Sjekk om timer er endret
  if (data.hours != CHEAPEST_HOURS) {

    Serial.println(
      "Antall timer endret. "
      "Gamle billigperioder forkastes."
    );

    todayWindow.valid = false;
    tomorrowWindow.valid = false;

    return false;
  }

  if (data.magic != STORAGE_MAGIC ||
      data.version != STORAGE_VERSION)
    return false;

  if (data.checksum != checksumData(data))
    return false;

  todayWindow = fromStored(data.today);
  tomorrowWindow = fromStored(data.tomorrow);

  Serial.println("Vinduer lest fra flash");

  return true;
}

// =====================================================
// FLYTT PERIODER VED NY DATO
// =====================================================

void rotateWindows() {

  if (!clockValid())
    return;

  time_t now = time(nullptr);

  struct tm t;
  localtime_r(&now, &t);

  int today = dateKey(t);
  int tomorrow = nextDateKey(t);

  CheapWindow newToday = todayWindow;
  CheapWindow newTomorrow = tomorrowWindow;

  if (tomorrowWindow.valid &&
      dateKey(tomorrowWindow.start) == today) {

    newToday = tomorrowWindow;
  }

  if (newToday.valid &&
      dateKey(newToday.start) != today) {

    newToday = {};
  }

  if (newTomorrow.valid &&
      dateKey(newTomorrow.start) != tomorrow) {

    newTomorrow = {};
  }

  bool changed =
    !sameWindow(todayWindow, newToday) ||
    !sameWindow(tomorrowWindow, newTomorrow);

  if (changed) {

    todayWindow = newToday;
    tomorrowWindow = newTomorrow;

    saveWindows();
  }
}

// =====================================================
// SORTER PRISINTERVALLER
// =====================================================

void sortPrices(
  PricePeriod* list,
  int count
) {

  for (int i = 0; i < count - 1; i++) {

    for (int j = i + 1; j < count; j++) {

      if (list[j].start < list[i].start) {

        PricePeriod tmp = list[i];
        list[i] = list[j];
        list[j] = tmp;
      }
    }
  }
}

// =====================================================
// FINN BILLIGSTE SAMMENHENGENDE PERIODE
// =====================================================

// Støtter intervaller med ulik lengde,
// eksempelvis 15 eller 60 minutter.
//
// Krever sammenhengende prisintervaller.
// Vinduet skal vare CHEAPEST_HOURS timer
// og ligge innenfor én lokal kalenderdag.

CheapWindow findCheapest(
  PricePeriod* list,
  int count,
  int targetDate
) {

  CheapWindow best = {};

  double bestCost = INFINITY;

  const time_t duration =
    (time_t)CHEAPEST_HOURS * 3600;

  for (int i = 0; i < count; i++) {

    time_t start = list[i].start;
    time_t targetEnd = start + duration;

    if (dateKey(start) != targetDate)
      continue;

    if (dateKey(targetEnd - 1) != targetDate)
      continue;

    time_t cursor = start;

    double weightedSum = 0;
    bool complete = false;

    for (int j = i; j < count; j++) {

      if (list[j].start != cursor)
        break;

      if (list[j].end <= list[j].start)
        break;

      time_t segmentEnd = list[j].end;

      if (segmentEnd > targetEnd)
        segmentEnd = targetEnd;

      time_t seconds = segmentEnd - cursor;

      if (seconds <= 0)
        break;

      weightedSum +=
        (double)list[j].price *
        (double)seconds;

      cursor = segmentEnd;

      if (cursor == targetEnd) {

        complete = true;
        break;
      }

      if (cursor > targetEnd)
        break;
    }

    if (!complete)
      continue;

    double average =
      weightedSum / (double)duration;

    if (average < bestCost) {

      bestCost = average;

      best.start = start;
      best.end = targetEnd;
      best.averagePrice = (float)average;
      best.valid = true;
    }
  }

  return best;
}

// =====================================================
// LAST NED STRØMPRISER
// =====================================================

bool downloadPrices() {

  if (WiFi.status() != WL_CONNECTED)
    return false;

  if (!clockValid())
    return false;

  time_t now = time(nullptr);

  struct tm t;
  localtime_r(&now, &t);

  int today = dateKey(t);
  int tomorrow = nextDateKey(t);

  String url =
    String(API_BASE) +
    PRICE_AREAS[selectedArea];

  Serial.print("Henter: ");
  Serial.println(url);

  WiFiClientSecure client;

  // Eksempeloppsett:
  // Sertifikatkontroll er deaktivert.
  client.setInsecure();

  HTTPClient https;

  // 20 sekunder lesetimeout - for litt ustabile nettverk
  https.setTimeout(20000);

  // 20 sekunder til etablering av forbindelse - for litt ustabile nettverk
  https.setConnectTimeout(20000);

  if (!https.begin(client, url))
    return false;

  int code = https.GET();

  if (code != HTTP_CODE_OK) {

    Serial.printf("HTTP-feil: %d\n", code);

    https.end();
    return false;
  }

  // ==================================
  // ENKEL NEDLASTING UTEN DATAKONTROLL
  // ==================================
  /*
  JsonDocument doc;

  DeserializationError error =
    deserializeJson(
      doc,
      https.getStream()
    );

  https.end();
  */ 

  // =====================================
  // ROBUST JSON-NEDLASTING
  // =====================================

  // Les hele HTTP-responsen først
  String json = https.getString();

  int expectedLength = https.getSize();

  https.end();

  // Kontroller at vi mottok data
  if (json.length() == 0) {

    Serial.println("JSON-feil: Tom respons");
    return false;
  }

  // Kontroller lengden dersom serveren
  // oppgir Content-Length.
  if (expectedLength > 0 &&
    json.length() != (size_t)expectedLength) {

    Serial.printf(
      "JSON-feil: Mottok %u av %d bytes\n",
      (unsigned int)json.length(),
      expectedLength
    );

    return false;
  }

  Serial.printf(
    "JSON mottatt: %u bytes\n",
    (unsigned int)json.length()
  );

  // Parse JSON fra minnet
  JsonDocument doc;

  DeserializationError error =
    deserializeJson(doc, json);

  if (error) {

    Serial.print("JSON-feil: ");
    Serial.println(error.c_str());

    return false;
  }

  if (error) {

    Serial.print("JSON-feil: ");
    Serial.println(error.c_str());

    return false;
  }

  JsonArray series =
    doc["series"].as<JsonArray>();

  if (series.isNull())
    return false;

  // Midlertidig liste.
  // Gamle data beholdes ved feil.
  PricePeriod temp[MAX_PRICE_PERIODS];

  int count = 0;

  for (JsonObject item : series) {

    if (count >= MAX_PRICE_PERIODS)
      break;

    const char* startStr = item["start"];
    const char* endStr = item["end"];

    if (!startStr || !endStr)
      continue;

    if (item["price_ore"].isNull())
      continue;

    time_t start = parseISO(startStr);
    time_t end = parseISO(endStr);

    if (start <= 0 || end <= start)
      continue;

    float price =
      item["price_ore"].as<float>() / 100.0f;

    if (!isfinite(price) || price < 0)
      continue;

    temp[count].start = start;
    temp[count].end = end;
    temp[count].price = price;

    count++;
  }

  if (count == 0)
    return false;

  sortPrices(temp, count);

  CheapWindow newToday =
    findCheapest(temp, count, today);

  CheapWindow newTomorrow =
    findCheapest(temp, count, tomorrow);

  if (!newToday.valid) {

    Serial.println("Mangler dagens priser");
    return false;
  }

  // Kontroller om data er endret.
  bool changed =
    !sameWindow(todayWindow, newToday) ||
    !sameWindow(tomorrowWindow, newTomorrow);

  // Kopier prislisten.
  for (int i = 0; i < count; i++)
    prices[i] = temp[i];

  priceCount = count;

  todayWindow = newToday;
  tomorrowWindow = newTomorrow;

  lastDownloadDate = today;

  bool afternoon =
    t.tm_hour > 13 ||
    (t.tm_hour == 13 && t.tm_min >= 15);

  if (afternoon && tomorrowWindow.valid)
    lastAfternoonDate = today;

  if (changed)
    saveWindows();

  Serial.println("Strømpriser oppdatert");

  return true;
}

// =====================================================
// GJELDENDE PRIS
// =====================================================

void updateCurrentPrice() {

  currentPriceValid = false;

  if (!clockValid())
    return;

  time_t now = time(nullptr);

  for (int i = 0; i < priceCount; i++) {

    if (now >= prices[i].start &&
        now < prices[i].end) {

      currentPrice = prices[i].price;
      currentPriceValid = true;
      break;
    }
  }
}

// =====================================================
// GPIO-STYRING
// =====================================================

void updateOutput() {

  outputActive = false;

  if (clockValid() && todayWindow.valid) {

    time_t now = time(nullptr);

    if (dateKey(todayWindow.start) ==
        dateKey(now)) {

      if (now >= todayWindow.start &&
          now < todayWindow.end) {

        outputActive = true;
      }
    }
  }

  digitalWrite(
    OUTPUT_PIN,
    outputActive ? HIGH : LOW
  );
}

// =====================================================
// OLED: OPPSETTSMODUS
// =====================================================

void showSetupScreen() {

  if (!oledReady)
    return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("WIFI OPPSETT");

  display.drawLine(
    0, 11, 127, 11,
    SSD1306_WHITE
  );

  display.setCursor(0, 17);
  display.println("Koble til:");

  display.setCursor(0, 28);
  display.println(AP_NAME);

  display.setCursor(0, 40);
  display.print("Pass: ");
  display.println(AP_PASSWORD);

  display.setCursor(0, 53);
  display.println("IP: 192.168.4.1");

  display.display();
}

// =====================================================
// OLED: NORMAL VISNING
// =====================================================

void updateDisplay() {

  if (!oledReady || oledSleeping)
    return;

  if (wm.getConfigPortalActive()) {

    showSetupScreen();
    return;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Overskrift
  display.setCursor(0, 0);
  display.print(PRICE_AREAS[selectedArea]);
  display.print(" ");
  display.print(AREA_NAMES[selectedArea]);

  display.setCursor(98, 0);

  if (clockValid()) {

    time_t now = time(nullptr);

    struct tm t;
    localtime_r(&now, &t);

    display.printf(
      "%02d:%02d",
      t.tm_hour,
      t.tm_min
    );

  } else {

    display.print("--:--");
  }

  display.drawLine(
    0, 11, 127, 11,
    SSD1306_WHITE
  );

  // Gjeldende pris
  display.setCursor(0, 16);
  display.print("PRIS:");

  display.setCursor(38, 16);

  if (currentPriceValid) {

    display.print(currentPrice, 2);
    display.print(" kr/kWh");

  } else {

    display.print("--.-- kr");
  }

  char startText[8];
  char endText[8];

  // I dag
  display.setCursor(0, 32);
  display.print("I DAG:");

  if (todayWindow.valid) {

    formatTime(
      todayWindow.start,
      startText,
      sizeof(startText)
    );

    formatTime(
      todayWindow.end,
      endText,
      sizeof(endText)
    );

    display.setCursor(62, 32);

    display.printf(
      "%s-%s",
      startText,
      endText
    );

  } else {

    display.setCursor(62, 32);
    display.print("UKJENT");
  }

  // I morgen
  display.setCursor(0, 43);
  display.print("I MORGEN:");

  if (tomorrowWindow.valid) {

    formatTime(
      tomorrowWindow.start,
      startText,
      sizeof(startText)
    );

    formatTime(
      tomorrowWindow.end,
      endText,
      sizeof(endText)
    );

    display.setCursor(62, 43);

    display.printf(
      "%s-%s",
      startText,
      endText
    );

  } else {

    display.setCursor(62, 43);
    display.print("VENTER");
  }

  // GPIO
  display.setCursor(0, 55);

  display.print(
    outputActive ? "AKTIV" : "INAKTIV"
  );

  // WiFi
  display.setCursor(103, 55);

  display.print(
    WiFi.status() == WL_CONNECTED
      ? "WiFi"
      : " OFF"
  );

  display.display();
}

// =====================================
// OLED STRØMSPARING
// =====================================

void wakeOLED() {

  lastOledActivity = millis();

  if (!oledReady)
    return;

  if (oledSleeping) {

    display.ssd1306_command(SSD1306_DISPLAYON);

    oledSleeping = false;

    Serial.println("OLED vekket");
  }
}

void checkOLEDSleep() {

  if (!oledReady || oledSleeping)
    return;

  // Hold skjermen på under WiFi-oppsett
  if (wm.getConfigPortalActive())
    return;

  if (millis() - lastOledActivity >=
      OLED_TIMEOUT_MS) {

    display.ssd1306_command(SSD1306_DISPLAYOFF);

    oledSleeping = true;

    Serial.println("OLED skjermsparer aktiv");
  }
}

// =====================================================
// BYGG HTML-NEDTREKKSMENY FOR AREA
// =====================================================

void buildAreaHTML() {

  String html;

  html.reserve(750);

  html +=
    "<br><label for='pricearea'>"
    "Velg prisomrade</label>";

  html +=
    "<select name='pricearea' "
    "id='pricearea'>";

  for (int i = 0; i < AREA_COUNT; i++) {

    html += "<option value='";
    html += PRICE_AREAS[i];
    html += "'";

    if (i == selectedArea)
      html += " selected";

    html += ">";

    html += PRICE_AREAS[i];
    html += " - ";
    html += AREA_NAMES[i];

    html += "</option>";
  }

  html += "</select><br>";

  snprintf(
    areaHtml,
    sizeof(areaHtml),
    "%s",
    html.c_str()
  );
}

// =====================================================
// BYGG HTML-NEDTREKKSMENY FOR TIMER
// =====================================================

void buildHoursHTML() {

  String html;

  html.reserve(400);

  html +=
    "<br><label for='cheaphours'>"
    "Antall billigste timer</label>";

  html +=
    "<select name='cheaphours' "
    "id='cheaphours'>";

  for (int h = 2; h <= 6; h++) {

    html += "<option value='";
    html += String(h);
    html += "'";

    if (h == CHEAPEST_HOURS)
      html += " selected";

    html += ">";

    html += String(h);
    html += " timer";

    html += "</option>";
  }

  html += "</select><br>";

  snprintf(
    hoursHtml,
    sizeof(hoursHtml),
    "%s",
    html.c_str()
  );
}


// =====================================================
// MOTTA PRISOMRÅDE OG TIMER FRA WEBPORTAL
// =====================================================

void onSaveParameters() {

  if (!wm.server)
    return;

  if (wm.server->hasArg("pricearea")) {

    String newArea =
      wm.server->arg("pricearea");

    if (newArea != PRICE_AREAS[selectedArea]) {
  
      for (int i = 0; i < AREA_COUNT; i++) {

        if (newArea == PRICE_AREAS[i]) {

          selectedArea = i;
        }
      }

      saveSelectedArea();

      // Fjern gammel prisliste.
      priceCount = 0;
      currentPriceValid = false;

      todayWindow = {};
      tomorrowWindow = {};

      lastDownloadDate = 0;
      lastAfternoonDate = 0;

      // Les eventuelle lagrede perioder
      // for det nye området.
      loadWindows();
      rotateWindows();

      Serial.print("Nytt prisomrade: ");
      Serial.println(PRICE_AREAS[selectedArea]);

      pendingRestart = true;
    }
  }

  if (wm.server->hasArg("cheaphours")) {

    int selectedHours =
      wm.server->arg("cheaphours").toInt();

    if (selectedHours >= 2 &&
        selectedHours <= 6) {

      if (selectedHours != CHEAPEST_HOURS) {

        CHEAPEST_HOURS = selectedHours;

        Preferences settings;
        settings.begin("settings", false);

        settings.putInt(
          "cheapHours",
          CHEAPEST_HOURS
        );

        settings.end();

        Serial.printf(
          "Nytt antall billigtimer: %d\n",
          CHEAPEST_HOURS
        );

        pendingRestart = true;
      }
    }
  }  
}

// =====================================================
// BRUK NYTT PRISOMRÅDE OG TIMER
// =====================================================

void applyPending() {
  // Gjenstart for å bygge opp
  // WiFiManager-menyen med riktig
  // forhåndsvalgt prisområde og/eller timer.

  if (pendingRestart) {

    // Deaktiver utgangen før omstart.
    digitalWrite(OUTPUT_PIN, LOW);
    outputActive = false;

    pendingRestart = false;

    Serial.println(
      "Endinger er gjort. Starter på nytt."
    );

    delay(200);
    ESP.restart();
  }
}

// =====================================================
// START WIFI-OPPSETT
// =====================================================

void startSetupPortal() {

  // Vekk OLED umiddelbart
  wakeOLED();

  if (wm.getConfigPortalActive())
    return;

  Serial.println();
  Serial.println("Starter WiFi-oppsett");
  Serial.println("Portalen lukkes etter 5 minutter");

  wm.setConfigPortalTimeout(300);

  wm.startConfigPortal(
    AP_NAME,
    AP_PASSWORD
  );
}

/// =====================================
// GPIO32: OLED OG WIFI-OPPSETT
// =====================================

void checkSetupButton() {

  bool pressed =
    digitalRead(SETUP_BUTTON) == LOW;

  if (!pressed) {

    buttonStart = 0;
    buttonHandled = false;
    return;
  }

  // Registrer nytt knappetrykk
  if (buttonStart == 0) {

    buttonStart = millis();

    // Vekk OLED umiddelbart
    wakeOLED();

    // Vis oppdaterte data
    updateDisplay();
  }

  // Hold inne i fem sekunder
  if (!buttonHandled &&
      millis() - buttonStart >=
      BUTTON_HOLD_MS) {

    buttonHandled = true;

    // Åpne WiFiManager
    startSetupPortal();
  }
}

// =====================================================
// WIFI MANAGER-OPPSETT
// =====================================================

void setupWiFiManager() {

  WiFi.mode(WIFI_STA);

  WiFi.setAutoReconnect(true);

  wm.setConfigPortalBlocking(false);

  wm.setConnectTimeout(15);

  // Lukk oppsettsportalen etter 300 sekunder
  wm.setConfigPortalTimeout(300);

  wm.setSaveParamsCallback(
    onSaveParameters
  );

  wm.setAPCallback(
    [](WiFiManager* manager) {

      Serial.println(
        "Konfigurasjonsportal aktiv"
      );
    }
  );

  // Bygg prisområdevelgeren.
  buildAreaHTML();

  new (&areaParameter)
    WiFiManagerParameter(areaHtml);

  wm.addParameter(&areaParameter);

  // Bygg timevelgeren.
  buildHoursHTML();

  new (&hoursParameter)
    WiFiManagerParameter(hoursHtml);

  wm.addParameter(&hoursParameter);

  // Forsøk lagret WiFi.
  // Hvis det ikke finnes eller ikke virker,
  // starter WiFiManager portalen.

  wm.autoConnect(
    AP_NAME,
    AP_PASSWORD
  );
}


// =====================================================
// WIFI-GJENOPPKOBLING
// =====================================================

void checkWiFi() {

  if (WiFi.status() == WL_CONNECTED)
    return;

  if (wm.getConfigPortalActive())
    return;

  if (millis() - lastWifiRetry <
      WIFI_RETRY_MS)
    return;

  lastWifiRetry = millis();

  WiFi.reconnect();
}

// =====================================================
// OPPDATER STRØMPRISER
// =====================================================

void checkPriceUpdates() {

  if (!clockValid())
    return;

  if (WiFi.status() != WL_CONNECTED)
    return;

  if (wm.getConfigPortalActive())
    return;

  time_t now = time(nullptr);

  struct tm t;
  localtime_r(&now, &t);

  int today = dateKey(t);

  bool needDaily =
    lastDownloadDate != today;

  bool afternoon =
    t.tm_hour > 13 ||
    (t.tm_hour == 13 && t.tm_min >= 15);

  bool needTomorrow =
    afternoon &&
    lastAfternoonDate != today;

  if (!needDaily && !needTomorrow)
    return;

  bool retryAllowed =
    !apiAttempted ||
    millis() - lastApiAttempt >=
    API_RETRY_MS;

  if (!retryAllowed)
    return;

  lastApiAttempt = millis();
  apiAttempted = true;

  downloadPrices();
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  // Sikker oppstart av utgangen
  pinMode(OUTPUT_PIN, OUTPUT);
  digitalWrite(OUTPUT_PIN, LOW);

  // Knapp
  pinMode(SETUP_BUTTON, INPUT_PULLUP);

  // OLED
  Wire.begin(OLED_SDA, OLED_SCL);

  oledReady = display.begin(
    SSD1306_SWITCHCAPVCC,
    OLED_ADDRESS
  );

  if (oledReady) {

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.println("ESP32 Stromstyring");
    display.println();
    display.println("Starter...");

    display.display();
  }

  // hold skjermen våken (i OLED_TIMEOUT_MS ms)
  lastOledActivity = millis();

  // Les lagret prisområde
  loadSelectedArea();

  // Les lagrede billigperioder
  loadWindows();

  // Start WiFiManager
  setupWiFiManager();

  // Gi WiFi litt tid til å stabilisere
  delay(3000);

  // NTP
  configTzTime(
    TZ_NORWAY,
    "pool.ntp.org",
    "time.google.com"
  );

  rotateWindows();

  updateCurrentPrice();
  updateOutput();
  updateDisplay();
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  // 1. Håndter WiFiManager.
  // Viktig ved ikke-blokkerende portal.
  wm.process(); 
  // Registrer om WiFi-portalen var aktiv
  bool portalActive = wm.getConfigPortalActive();
  // Portalen har nettopp blitt lukket
  if (portalWasActive && !portalActive) {
    wakeOLED();
    updateDisplay();
  }
  portalWasActive = portalActive;

  // 2. Sjekk knappen.
  checkSetupButton();

  // 3. Behandle eventuelt nytt prisområde.
  applyPending();

  // 4. WiFi-gjenoppkobling.
  checkWiFi();

  // 5. Oppdater dato og lagrede vinduer.
  rotateWindows();

  // 6. Styr GPIO etter lagrede vinduer.
  updateOutput();

  // 7. Hent nye strømpriser ved behov.
  checkPriceUpdates();

  // 8. Oppdater gjeldende pris.
  updateCurrentPrice();

  // 9. Oppdater GPIO igjen etter prisnedlasting.
  updateOutput();

  // 10. OLED.
  if (millis() - lastDisplayUpdate >=
      DISPLAY_UPDATE_MS) {

    lastDisplayUpdate = millis();
    updateDisplay();
  }

  // Kontroller om OLED skal slås av
  checkOLEDSleep();

  delay(20);
}
