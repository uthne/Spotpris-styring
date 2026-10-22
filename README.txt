********************************************************************************
ESP32 DevKit C – Automatisk strømstyring med spotpris, OLED og permanent lagring
********************************************************************************

Programmet er laget for å kunne stå på kontinuerlig og styre GPIO-pin 26 etter de billigste sammenhengende timene i strømprisområdet. 

Programmet sjekker JSON-filen for oppføringer under "cheapest_windows" for billigste timer. Programmet prøver å et antall sammenhengende timer med billigst strøm. Antallet timer settes i WiFi-Manager. Hvis antallet sammenhengende timer ikke finnes i "cheapest_hours" vil programmet gå igjennom hele prislisten for å finne de sammenhengende billigste timene.

Programmet lagrer billigperiodene i ESP32 sitt permanente flashminne ved hjelp av Preferences (NVS), slik at ESP32 husker dagens og morgendagens billigperioder selv etter strømbrudd eller omstart. Programmet lagrer strømprisene for hvert prisområde hver for seg i eget navne-område (når området aktiveres), slik at prisene holdes separat og ikke blandes sammen.

ESP32 henter prisdata som JSON fra: https://api.strompriseridag.no/v1/***

Hvor områdene (***) er definert som:
NO1 = Østlandet
NO2 = Sørlandet
NO3 = Midt-Norge 
NO4 = Nord-Norge
NO5 = Vestlandet

Ved første oppstart av programmet vil det starte et wifi-nettverk, med en wifi-manager på ip-adressen 192.168.4.1

I wifi-manageren kan du oppgi innlogging til ditt lokale nettverk. Du må også velge hvilket prisområde du skal bruke i wifi-manageren.Det er også en knapp på GPIO-pin 32 for å gå inn i wifi-manageren under drift for å endre nettverk eller prisområde.
Wifi-manageren har en time-out på 5 minutter hvis det ikke er noe aktivitet.

OLED -skjermen vil gå i skjermsparer etter 2 minutter uten trykk på GPIO 32, eller 2 minutter etter wifi-manager startet. Skjermen vil også våkne, når wifi-instillinger lagres fra web-portalen. Strømstyringen fortsetter selvsagt uavbrutt når skjermen går i strømsparing.





1. Slik vil systemet fungere


VANLIG DRIFT

ESP32 kobler seg automatisk til det lagrede WiFi-nettverket og henter strømpriser fra det valgte prisområdet.
OLED-displayet viser for eksempel:

*******OLED SKJERM*******
NO1 Ostlandet   14:25
---------------------
PRIS: 0.94 kr/kWh

I DAG:    13:00-17:00
I MORG:   02:00-06:00
AKTIV            WiFi
*************************

GPIO 26 er HIGH, så den viser "AKTIV" i de fire billigste sammenhengende timene og LOW (INAKTIV) resten av døgnet.




WIFI-OPPSETT

Når du holder knappen på GPIO 32 inne i tre sekunder, åpner ESP32 et eget WiFi-aksesspunkt.

Nettverksnavn: ESP32-Stromstyring
Passord: Styr2026

Nettverksnavn og passord settes i variablene:
const char* AP_NAME = "ESP32-Stromstyring";
const char* AP_PASSWORD = "Styr2026";


Du kobler telefonen til dette nettverket og åpner:
WiFiManager – konfigurasjonsside
Der kan du velge WiFi-nettverk, skrive inn passordet og velge prisområde og sette antall aktive timer.
Eksempel på oppsettssiden:

┌────────────────────────────┐
│                            │
│      ESP32 Stromstyring    │
│                            │
│  WiFi-nettverk             │
│  ┌──────────────────────┐  │
│  │ Mitt hjemmenett    ▼ │  │
│  └──────────────────────┘  │
│                            │
│  WiFi-passord              │
│  ┌──────────────────────┐  │
│  │ ••••••••••••         │  │
│  └──────────────────────┘  │
│                            │
│  Velg prisområde           │
│  ┌──────────────────────┐  │
│  │ NO1 - Ostlandet    ▼ │  │
│  └──────────────────────┘  │
│                            │
│  Antall billigste timer    │
│  ┌──────────────────────┐  │
│  │ 4                  ▼ │  │
│  └──────────────────────┘  │
│                            │
│       [ LAGRE ]            │
│                            │
└────────────────────────────┘

Dette er en illustrasjon. Den faktiske siden bruker WiFiManagers standarddesign.
WiFiManager støtter egne HTML-felt, inkludert nedtrekksmenyer, og en ikke-blokkerende konfigurasjonsportal. Det er disse funksjonene vi bruker.
https://github.com/tzapu/WiFiManager/blob/master/examples/Advanced/Advanced.ino




2. Tilkobling av ESP32

Du trenger ingen ekstra komponenter bortsett fra én trykknapp og enAdafruit SSD1306 OLED-skjerm.

ESP32:    Tilkobling
=============================================================================
GPIO 21:  OLED SDA
GPIO 22:  OLED SCL
3V3:      OLED VCC
GND:      OLED GND
GPIO 26:  Styreutgang
GPIO 32:  Trykknapp
GND:      Andre siden av trykknappen

Trykknappen kobles altså direkte mellom GPIO 32 og GND.
Programmet aktiverer den interne pull-up-motstanden, så du trenger ingen ekstern motstand.




3. Slik bruker du programmet

FØRSTE OPPSTART

Ved første oppstart har ESP32 ingen lagret WiFi-tilkobling.
Den oppretter derfor aksesspunktet:
ESP32-Stromstyring
OLED viser:

*******OLED SKJERM*******
WIFI OPPSETT
---------------------
Koble til:
ESP32-Stromstyring
Pass: Styr2026
IP: 192.168.4.1
*************************

Åpne WiFi-innstillingene på telefonen og koble til dette nettverket.

Åpne deretter konfigurasjonssiden. På enkelte telefoner åpnes siden automatisk.

Trykk på knappen "Konfigurer WiFi" eller "Configure WiFi", avhengig av språk.

Velg WiFi-nettverk og prisområde, antall aktive timer, og lagre.

Når ESP32 har fått WiFi-tilkobling og riktig klokkeslett, henter den strømprisene.

WiFi-oppsettet lukkes automatisk etter fem minutter uten aktivitet.




ENDRE WIFI SENERE

Hold knappen på GPIO 32 inne i fem sekunder.

ESP32 åpner aksesspunktet på nytt.

Du kan nå endre WiFi-nettverk og passord.

Du trenger ikke slette flashminnet eller laste opp programmet på nytt.




ENDRE PRISOMRÅDE

Hold den samme knappen inne i fem sekunder.
Åpne konfigurasjonssiden og velg ønsket prisområde:

Valg   Område
=============================================================================
NO1:   Østlandet
NO2:   Sørlandet
NO3:   Midt-Norge
NO4:   Nord-Norge
NO5:   Vestlandet

Lagre valget.

ESP32 lagrer prisområdet permanent og starter på nytt dersom området er endret.

Deretter brukes riktig API-adresse automatisk. 




ENDRE ANTALL AKTIVE TIMER

Hold den samme knappen inne i fem sekunder.
Åpne konfigurasjonssiden og velg ønsket antall timer:

4 timer er forhåndsvalgt første gang, og du kan velge 2, 3, 4, 5 eller 6 timer.

Lagre valget.

ESP32 lagrer antall timer permanent og starter på nytt dersom antallet er endret.

Beregnede timer blir forkastet og ny periode blir beregnet. 

Hvis endringen blir gjort i en aktiv periode vil ikke GPIO gå høy (aktiv) før neste hele time etter omstart.




WIFI-MANAGER OPPFØRSEL

Hendelse                                   Resultat
=============================================================================
Hold GPIO 32 inne i 3 sekunder:            WiFi-oppsettet åpnes
Ingen oppsett fullføres innen 5 minutter:  Portalen lukkes
WiFi-oppsett lagres:                       ESP32 bruker nye innstillinger
Portalen lukkes:                           OLED går tilbake til normalvisning
WiFi mangler:                              ESP32 forsøker gjenoppkobling
Gyldige billigperioder finnes:             GPIO 26 følger fortsatt periodene



OLED-SKJERM OG GPIO32-KNAPP

Handling                                  Resultat
=============================================================================
Kort trykk                                Vekker OLED
Hold i 5 sekunder                         Åpner WiFiManager
Ingen aktivitet i 2 minutter              OLED slår seg av
Lagre instillinger fra web-portal         Vekker OLED



6. Hvordan permanent lagring fungerer

Programmet bruker to typer lagring.

WIFI OG PRISOMRÅDE

WiFiManager og ESP32 lagrer WiFi-opplysningene, mens programmet lagrer valgt prisområde med Preferences.

Eksempel:
WiFi:       MittHjemmenett
Prisområde: NO3




BILLIGPERIODER

Programmet bruker separate NVS-navneområder:

Prisområde  Flashminne
=============================================================================
NO1:        strom_NO1
NO2:        strom_NO2
NO3:        strom_NO3
NO4:        strom_NO4
NO5:        strom_NO5

Det betyr at ESP32 ikke blander strømpriser mellom landsdelene.

Hvis du bytter fra NO1 til NO3, vil programmet først lese eventuelle lagrede perioder for NO3. Deretter forsøker det å hente oppdaterte priser.

Viktig: Lagrede perioder brukes bare dersom datoen stemmer. Gamle perioder fra tidligere dager blir forkastet.




7. Hva skjer ved WiFi-brudd?

Dette er spesielt viktig når GPIO 26 brukes til strømstyring.

Hendelse                      Resultat
=============================================================================
WiFi forsvinner:              ESP32 fortsetter å kjøre
Internett er utilgjengelig:   Lagrede billigperioder brukes
WiFi kommer tilbake:          ESP32 forsøker å hente nye priser
ESP32 starter på nytt:        Lagrede perioder leses fra flash
Klokken er ikke synkronisert: GPIO 26 forblir LOW
Prisområdet endres:           GPIO settes LOW under omstart


Merk: Programmet er ikke-blokkerende når WiFiManager-portalen er åpen, men selve HTTPS-nedlastingen kan fremdeles blokkere hovedløkken i flere sekunder. Derfor er dette ennå ikke en hard sanntidsstyring. For styring av utstyr som krever garantert utkobling på sekundet, bør GPIO-styringen flyttes til en separat FreeRTOS-oppgave eller en dedikert timer.

Det er også to andre forhold som bør testes før permanent drift: prisberegningen velger tidsvinduer innenfor én kalenderdag, ikke på tvers av midnatt, og client.setInsecure() deaktiverer HTTPS-sertifikatkontroll. Det siste bør erstattes med sertifikatvalidering før løsningen brukes til automatisk styring av elektrisk utstyr.





8. Komplett programkode

Programkoden er skrevet i C++ for Arduino IDE. Koden nedenfor inkluderer permanent lagring, gjenoppretting etter omstart, daglig oppdatering, separat lagring av dagens og morgendagens vinduer for valgt prisområde,wifi-innlogging, OLED og GPIO-styring.

8a. Biblioteker
Installer disse i Arduino IDE:

Bibliotek              Utgiver
=============================================================================
WiFiManager:           tzapu
ArduinoJson:           Benoît Blanchon
Adafruit SSD1306:      Adafruit
Adafruit GFX Library:  Adafruit

Preferences, WiFi, HTTPClient og Wire følger med ESP32 Arduino-kjernen.
WiFiManager finnes på: https://github.com/tzapu/WiFiManager.




9. Bruk av sanntidsklokke (Real Time Clock)

Hvis strømstyringen brukes i miljøre hvor strømmen av en eller annen grunn faller bort eller kobles til- og fra kan det være hensiksmessig å bruke en sanntidsklokke sammen med ESP32 kontrolleren. Da vil kontrolleren kunne fortsette å kjøre selv om klokkeslett ikke kan hentes fra WiFi-nettverket under oppstart.

Kildekoden i dokumentet "Spotpris-styring-eps32-wifi-RTC.ino"er beregnet på bruk sammen med en DS3231 Real Time Clock modul.

