*************************************************************************************************
ESP32 DevKit C – Automatisk strømstyring med spotpris, Real Time Clock, OLED og permanent lagring
*************************************************************************************************

Dette er en versjon av Spotpris-styring-esp32-wifi med DS3231 Real Time Clock modul
Se også README.txt

Hvis strømstyringen brukes i miljøre hvor strømmen av en eller annen grunn faller bort eller kobles til- og fra kan det være hensiksmessig å bruke en sanntidsklokke sammen med ESP32 kontrolleren. Da vil kontrolleren kunne fortsette å kjøre selv om klokkeslett ikke kan hentes fra WiFi-nettverket under oppstart.

En sanntidsklokke kan for eksempel være en DS3231 enhet. Den fortsetter å holde tiden når ESP32 er avslått, ved hjelp av et reservebatteri. Når ESP32 starter igjen, kan den hente riktig klokkeslett fra DS3231 uten å vente på internett. 

Dette vil fungere fordi billigperiodene allerede lagres permanent i flashminnet.




1. Tilkobling til ESP32 DevKit C

DS3231 bruker I²C og kan dele de samme tilkoblingene som OLED-displayet. Målet er at ESP32 skal kunne starte etter et strømbrudd og bruke de lagrede billigperiodene selv om WiFi eller internett ikke er tilgjengelig.

DS3231  ESP32
=============================================================================
VCC:	3V3
GND:	GND
SDA:	GPIO 21
SCL:	GPIO 22




2. Ekstra bibliotek

Vi bruker RTClib fra Adafruit for å håndtere sanntidsklokken over I2C, og sys/time.h for å lese og stille selve systemklokken, med mikrosekundoppløsning. I tillegg bruker vi esp_sntp.h for synkronisering mot NTP server.

Bibliotek    Oppgave
=============================================================================
RTClib.h:    Kommunisere med DS3231 modulen via I2C
time.h:      Behandler dato, klokkeslett og tidssoner
sys/time.h:  Leser og stiller ESP32 sin systemklokke
esp_sntp.h:  Styrer og overvåker synkronisering mot NTP-servere





3. Funksjonelle endringer i programkoden

Programmet leser nå UTC-tiden fra DS3231 og stiller ESP32 sin interne klokke. ESP32 bruker fortsatt den eksisterende tidssonen for norge og dermed håndteres sommer- og vintertid automatisk.

Vi ønsker også at DS3231 skal korrigeres automatisk når internett er tilgjengelig. Det er imidlertid en viktig detalj her: clockValid() er ikke tilstrekkelig til å bekrefte NTP-synkronisering, fordi systemklokken allerede kan være satt fra DS3231.

Derfor lar vi ESP32 registrere når en faktisk NTP-synkronisering er gjennomført og RTC oppdateres bare etter at NTP faktisk har synkronisert systemklokken.

I koden  brukes lastRtcSync som en tidsbegrensning. Men etter en omstart er den null, og dersom NTP-synkroniseringen ikke lykkes, skal RTC selvfølgelig ikke overskrives. Dette ivaretas av ntpSynchronized.

Det er også lurt å sørge for at DS3231 ikke skrives unødvendig ofte, så en gang per døgn er tilstrekkelig for denne typen strømstyring.





4. Hva skjer nå ved strømbrudd?

Situasjon	Resultat
Normal drift med WiFi:                      ESP32 bruker NTP-tid
Internett forsvinner:                       Systemklokken fortsetter
ESP32 mister strømmen:                      DS3231 fortsetter på batteri
ESP32 starter uten WiFi:                    Tiden hentes fra DS3231
Lagrede billigperioder finnes:              GPIO 26 kan følge periodene
Internett kommer tilbake:                   NTP korrigerer klokken og DS3231
DS3231 mangler eller har mistet tiden:      GPIO forblir LOW inntil gyldig tid er tilgjengelig

En viktig forutsetning: De lagrede billigperiodene må fortsatt gjelde dagens dato. DS3231 gjør det mulig å vite hva klokken er, men kan ikke erstatte nedlasting av fremtidige strømpriser.





5. Visning av klokke-kilde
Programmet viser nå også kilde for klokken nederst i høyre hjørne før WiFi-status.
Det vil vises NTP hvis kilden er Network Time Protocol (WiFi), RTC hvis kilden er Real Time Clock og ––– hvis tid mangler.
Hvis WiFi er tilkoblet men RTC blir stående er det en feil i nettverksforbindelsen til Network Time Protocol serveren (pool.ntp.org og time.google.com).
Ved feil på Real Time Clock modulen vil ERR vises i stedet for NTP.

*******OLED SKJERM*******
NO1 Ostlandet   14:25
---------------------
PRIS: 0.94 kr/kWh

I DAG:    13:00-17:00
I MORG:   02:00-06:00
AKTIV      NTP | WiFi
*************************
