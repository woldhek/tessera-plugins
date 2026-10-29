# Schermcamera

Bekijk de eigen camera van het scherm in Home Assistant. Het scherm wordt daar een camera van zijn apparaat: een beeld
op een dashboard, een stream als je hem opent, en alles wat Home Assistant met een camera doet (een momentopname in een
automatisering, een opname).

Het beeld wordt op het scherm zelf in hardware gemaakt, alleen zolang Home Assistant erom vraagt. Zolang de camera in
gebruik is, toont Tessera een stip in de rechterbovenhoek van het scherm, zodat wie ervoor staat het weet; geen
instelling haalt hem weg.

## Wat je nodig hebt

- Een scherm waarvan het bord een camera heeft die Tessera kan aanzetten en bereiken: de reTerminal D1001, de M5Stack
  Tab5, of een Guition ESP32-P4-scherm in de cameraversie ("C" in de modelnaam): de JC8012P4A1 (ook de V2 en V3), de
  JC1060P470 V2 en de JC4880P443. De Guition-schermen zijn nieuw en alleen op de JC8012P4A1 V3 geprobeerd.
- Verder niets in Home Assistant: de camera hoort bij het apparaat van het scherm in de ESPHome-integratie.

## Instellen

1. Open in Tessera **Plugins**, kies **Schermcamera** en vink je schermen aan. Elk scherm wordt een keer gebouwd.
2. Open in Home Assistant het apparaat van het scherm (Instellingen, Apparaten): de camera staat er als **Camera**.
   Zet hem op een dashboard met een kaart die camerabeeld toont (Picture entity of Picture glance).
3. De camera staat vanzelf rechtop, ook op een scherm dat een halve slag gedraaid is. Hangt het scherm andersom, zet
   dan **Camera omgekeerd** aan bij de instellingen van de plugin.
4. Optioneel: zet **Lichtniveau en beweging** aan bij de instellingen van de plugin (het tabblad Plugins van het scherm
   in Tessera, of **Camera light level and motion** bij het apparaat in Home Assistant). Dan volgen **Light level** en
   **Motion** de kamer.

## Video

Het onderdeel **Video** (aan als je de plugin toevoegt, in de details van de plugin op het tabblad Plugins van het
scherm) maakt van de camera live video met geluid in Home Assistant: H.264 uit de eigen video-encoder van het scherm,
de maat van de camera (1280 x 720 op de meeste borden) met 15 beelden per seconde, en de microfoon als Opus, in sync, ongeveer een derde seconde later. Het speelt in
de browserpagina en de app van Home Assistant zoals elke camera.

1. Bouw het scherm met het onderdeel aan.
2. Open in Tessera **Instellingen, Video in Home Assistant**, druk op **Installeren in Home Assistant** en daarna op
   **Home Assistant herstarten**. Dat is één keer voor je hele huis.
3. Vanaf dan heeft elk scherm met het onderdeel een camera **Video** bij zijn apparaat in Home Assistant. Tessera voegt
   hem zelf toe, en niemand typt een wachtwoord: dat wordt aan beide kanten uit de API-sleutel van het scherm berekend.

Met het onderdeel loopt de camera vanaf de start van het scherm, en zijn foto's komen uit de video: de camera **Camera**
verdwijnt van het apparaat van het scherm, en **Video** geeft de stilstaande beelden. Lichtniveau en beweging blijven
werken. De stip is dicht zolang iemand kijkt, en de microfoon luistert alleen dan. De video heeft een ESP32-P4 met een
microfoon nodig; hij is geprobeerd op de reTerminal D1001 en de Guition 10.1-inch.

Op de Guition-schermen kiest de instelling **Camera field of view** hoeveel van de kamer de camera toont: **Full**, de
standaard, alles wat hij ziet op de halve resolutie en 15 beelden per seconde; **Sharp** het midden ervan op volle
resolutie.

docs/CAMERA.md van Tessera, "The screen's own camera as video in Home Assistant", zegt hoe het werkt en wat het vraagt.

## Lichtniveau en beweging

Uit tot je het aanzet. Zolang het aan staat blijft de camera draaien en bepaalt het scherm er twee dingen uit:

- **Light level**, een schatting in lux uit de helderheid van het beeld, de belichtingstijd en de versterking van de
  sensor. Hij klopt in verhouding (twee keer zoveel licht is ongeveer twee keer zoveel lux), niet precies: kies de
  drempels van je automatiseringen door te kijken wat hij in jouw kamer meet. Hij wordt elke tien seconden bijgewerkt, en
  meteen als er een lamp aan- of uitgaat.
- **Motion**, uit het beeld dat een paar keer per seconde in een grof raster wordt vergeleken; aan als een deel ervan
  verandert, en daarna nog **Beweging blijft aan** lang (eerst tien seconden, van vijf seconden tot tien minuten). Een lamp die aan- of uitgaat (het grootste deel van het beeld tegelijk lichter of
  donkerder) is geen beweging: drie seconden daarna, terwijl de camera zich aanpast, wordt beweging niet beoordeeld.

Hiervoor wordt geen beeld gemaakt of verstuurd: alleen de twee waarden verlaten het scherm. De stip in de
rechterbovenhoek is een ring zolang het draait, en dicht zodra iemand kijkt. Zolang niemand kijkt maakt de camera 5
beelden per seconde, genoeg voor allebei, en hij gaat meteen terug naar volle snelheid voor de video of een foto. Met
**Beweging wekt het scherm** aan (eerst uit) wekt iemand die voor het scherm beweegt het scherm, zoals een aanraking.

## Privacy

Er verlaat niets het scherm tot Home Assistant om een beeld vraagt, en elk beeld gaat alleen naar je eigen Home
Assistant. Met lichtniveau en beweging aan kijkt de camera steeds, maar wat hij ziet blijft op het scherm. Iedereen die het apparaat van het scherm of een dashboard met de camera kan openen, kan erdoor kijken. Om dat
even te stoppen schakel je de camera-entiteit in Home Assistant uit; voorgoed haal je de plugin van het scherm.

## Voor makers

De camera is ESPHome's camera met id `ts_camera` (de feature `camera`), zodat een andere plugin erop kan bouwen. Het
camerastuurprogramma is `esp_video_camera` (github.com/n-IA-hane/esphome-esp-video-camera, uit ESPHome pull request
esphome/esphome#16944), ongewijzigd overgenomen in `components/esp_video_camera` onder de ESPHome-licentie, met zijn
NOTICE. De OmniVision OV02C10 van de Guition-schermen, waarvoor Espressifs camerabibliotheek geen stuurprogramma heeft,
is een eigen component ernaast, `components/ov02c10`: de sensordriver en zijn beeldafstemming (een voor rev3-chips, een
voor oudere), met een NOTICE die zegt waar ze vandaan komen. De SmartSens SC202CS (SC2356) van de Tab5 en de reTerminal
heeft ook een kleine eigen component, `components/sc202cs`: Espressifs driver start hem met een zwartniveau van 16 van
255 dat de ISP eraf moet halen, wat een ESP32-P4 van voor rev3 niet kan, dus verlaagt de component het eigen
zwartniveau van de sensor nadat de camera hem heeft gestart. De beelden van de camera gaan naar een verdeler van Screen
camera (`components/screen_camera/frame_hub.h`) die ze doorgeeft aan lichtniveau en beweging en aan de video, en de
sensoren die de camera zelf vindt staan in de tabel `components/screen_camera/sensors.py`. Wat de driver van een
sensor kan naast zijn formaat, een lager beeldtempo als de camera alleen meet en een stand over zijn hele beeldveld,
vertelt hij de camera via `components/esp_video_camera/camera_sensor.h`, op welk bord hij ook zit.
