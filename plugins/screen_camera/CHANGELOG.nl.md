# Wijzigingen

## 0.6.13 - 2026-10-29
- **De quantiser van de video staat weer op hoogstens 42.** 0.6.11 liet de encoder het hele bereik van H.264 gebruiken
  om in een druk beeld rond zijn 1 Mbit/s te blijven. Gemeten doet hij dat bij 42 al: op een Guition 10.1 met verse
  ruis over de helft van elk beeld stuurde hij 983 tot 1006 kbit/s bij 42 en 1001 tot 1002 bij 51, en er bleven in
  beide gevallen evenveel beelden achter op de verbinding. Dus de instelling gaat terug naar wat hij was.

## 0.6.12 - 2026-10-29
- **Het scherm vertraagt zijn eigen live tegels zolang iemand zijn microfoon hoort** (vereist Tessera's dev-firmware,
  plugin API 0.23). Vier live tegels op een reTerminal D1001 maakten zijn Wi-Fi zo druk dat het geluid van zijn video in
  bursts een seconde te laat bij de kijker aankwam; zolang een kijker het geluid heeft, houdt het scherm zijn live
  camerategels nu op een kwart van hun beelden per seconde, zodat het gesprek voorgaat.

## 0.6.11 - 2026-10-29
- **De video houdt zich aan zijn bitrate in een druk beeld.** De encoder kon een beeld maar tot een bepaalde grofheid
  maken (quantiser hoogstens 42), dus een beeld met veel detail of beweging werd groter dan de 1 Mbit/s die de video
  hoort te sturen: een reTerminal D1001 stuurde 1,6 tot 2,1 Mbit/s, en 30 tot 70 beelden per minuut kwamen niet aan.
  Hij mag nu het hele bereik van H.264 gebruiken (tot 51), zodat hij rond 1 Mbit/s blijft en een druk moment
  iets zachter oogt in plaats van te haperen.

## 0.6.10 - 2026-10-29
- **Het geluid wacht op een trage verbinding niet meer op het beeld.** Beeld en geluid verlaten het scherm over één
  verbinding, en als de Wi-Fi-uplink van het scherm stokte (een reTerminal D1001 met vier live tegels terwijl zijn camera
  bekeken werd) wachtte een beeld seconden in de verbinding en het geluid erachter: het kwam in bursts een seconde te
  laat bij Home Assistant aan, hoogstens 2,4 s, en het scherm dat het afspeelde knipte er meer dan de helft van weg
  (woorden weg, stukjes dubbel). Na 2 s wachten werd de kijker weggegooid en verbond go2rtc opnieuw. Nu blijft een beeld
  dat de verbinding vol vindt (meer dan de helft van de zendbuffer wacht nog) achter, de kijker krijgt eerst een
  keyframe als er weer ruimte is, en het geluid gaat tussen de pakketten van een lang beeld door. Op de bench, met een
  kijker die elke 6 s 3 s niet las: 0 bytes geluid verloren, de kijker bleef, 232 beelden achtergebleven in 40 s;
  daarvoor werd dezelfde kijker na 2 s weggegooid. De logregel per minuut telt de beelden die achterbleven voor een
  volle verbinding.

## 0.6.9 - 2026-10-28
- **Naar de video kijken laat het wake word niet meer struikelen.** Terwijl iemand de video van het scherm in Home
  Assistant bekeek, kon het wake word vaak per uur de draad kwijtraken (217 keer per uur op een Guition 10.1 met één
  kijker). Het geluid van de video gaat nu voor het wake word opzij: 0 keer in dezelfde test, de video nog steeds op 15
  beelden per seconde. De Sound-regel in het log zegt ook hoe lang het coderen van elk stukje geluid duurt.

## 0.6.8 - 2026-10-28
- **De eigen camera van het scherm op een tegel van dat scherm wordt op het scherm zelf getekend** (vraagt de dev-firmware
  en -app van Tessera, plugin-API 0.22): een live cameratile met de eigen camera van dit scherm krijgt zijn beelden
  rechtstreeks van de camera, zonder de video, Home Assistant of het netwerk ertussen. Het beeld is zo vers als dat van
  de camera, en zolang niemand de video in Home Assistant bekijkt, blijft de video-encoder van het scherm uit. Er is
  niets te doen; de camerastip brandt zolang de tegel op het scherm staat, net als wanneer iemand de video bekijkt.

## 0.6.7 - 2026-10-28
- **Geeft mee op een druk scherm** (vraagt de dev-firmware van Tessera, plugin-API 0.21): zolang een vinger op het
  scherm ligt of het scherm tekenen van drukte geeft, stuurt de video om het andere beeld of één op de vier, en worden
  lichtniveau en beweging half of een kwart zo vaak gemeten. Daarna vanzelf weer volledig. Er is niets te doen.

## 0.6.6 - 2026-10-28
- **Minder werk terwijl niemand kijkt** op de camera van de Guition-schermen met het beeldveld Full: het scherm halveert
  de regels van een beeld alleen nog terwijl iemand de video bekijkt. Lichtniveau en beweging lezen het beeld zoals de
  camera het maakt.

## 0.6.5 - 2026-10-27
- **Full is het standaard beeldveld** van de camera van de Guition-schermen (OV02C10): alles wat hij ziet, op de halve
  resolutie, 15 beelden per seconde. De twee Full-standen zijn er nu één: de sensor middelt telkens twee kolommen en
  het scherm halveert de regels, wat loopt waar het halveren van de regels door de sensor zelf na één beeld stopte.
  Sharp, het midden op volle resolutie en 30 per seconde, blijft beschikbaar. Een scherm waarvan de instelling Sharp
  toont, houdt die: kies één keer Full om de hele kamer te zien.
- **Een helderder beeld in een schemerige kamer**: de automatische belichting kan nu ook de digitale versterking van
  de sensor gebruiken, tot 63x in plaats van 15,5x. En sinds 0.6.0 zette een wissel van beeldveld de belichting en
  versterking terug naar hun begin terwijl de automatische belichting haar keuze hield, zodat het beeld donker kon
  blijven; nu blijven ze waar de automatische belichting ze zette.
- **De eigen beeldafstelling van Guition** voor de OV02C10 (kleuren, witbalans, zwartniveau), voor beide
  chiprevisies van de ESP32-P4.
- **De camera rust als niemand kijkt**: voor het lichtniveau en beweging, of met het onderdeel Video en verder niets,
  maakt hij 5 beelden per seconde (de OV02C10 van de Guition-schermen en de SC202CS van de reTerminal D1001), en hij
  gaat meteen terug naar volle snelheid als iemand de video opent of Home Assistant om een beeld vraagt, en tien
  seconden na de laatste daarvan weer langzamer.
- **De camerastip maakt het verschil zichtbaar**: dicht zolang het beeld van de camera ergens heen gaat, een ring als
  hij alleen licht en beweging meet. Dit vraagt de dev-firmware van Tessera (plugin-API 0.20), dus deze versie staat
  op de dev-branch tot de volgende release van Tessera.
- De video houdt 15 beelden per seconde als de camera er 15 maakt; eerder kon hij er telkens één overslaan.

## 0.6.4 - 2026-10-26
- Beeldveld camera op de Guition: met de regels gehalveerd stopte de sensor ook bij 960 x 544 na één beeld, en de P4
  zei waarom: een beeld dat de sensor begon en nooit afsloot. Met hoeveel gehalveerde regels de beeldverwerking van
  de sensor een beeld afsluit staat niet in de datasheet, dus een Full-stand zoekt het nu zelf uit: hij probeert een
  rij vensters (de hoogte en de offsets) na elkaar, elk tot het loopt of stopt, houdt het eerste dat loopt en begint
  er de volgende keer mee. Loopt er geen, dan gaat hij terug naar Sharp zoals voorheen. De log noemt elk geprobeerd
  venster en welk liep, en het rapport van de P4 komt in twee regels die niet meer worden afgekapt. Testers: zet elke
  Full-stand één keer aan, wacht ongeveer een minuut, en stuur alle logregels met "Field of view" en een screenshot.

## 0.6.3 - 2026-10-26
- Beeldveld camera op de Guition 10.1-inch: Jeroen Peters vond met zijn testbuilds dat de Full-standen na één beeld
  stilvallen zodra de regels gehalveerd worden, terwijl alleen de kolommen halveren gewoon blijft lopen. Beide
  Full-standen vragen nu om elke gehalveerde regel (960 x 544 in plaats van 960 x 528). Valt een Full-stand toch
  stil, dan zegt de log wat de camera-ingang van de P4 zag (uit zijn testbuild). Testers: probeer beide Full-standen
  opnieuw en stuur de logregels met "Field of view" en een screenshot.

## 0.6.2 - 2026-10-25
- Beeldveld camera op de Guition 4.3-inch (GitHub issue #326): beide Full-standen gaven daar geen beeld. Ze beginnen
  nu vanuit de tabel die Sharp gebruikt, die het starten van de camera aan het scherm laat; de vorige zette de
  camera halverwege het instellen al aan. Dezelfde lanesnelheid en timing als voorheen.
- Een Full-stand waarvan het beeld in de eerste tien seconden stopt, of waar de camera niet van herstelt, gaat nu
  vanzelf terug naar Sharp, wat de beelden ook afneemt (de video, een foto, lichtniveau en beweging); eerder kon hij
  zonder beeld op Full blijven staan tot een herstart. De log zegt dan wat de sensor van de stand maakte.

## 0.6.1 - 2026-10-25
- Beeldveld camera op de Guition: de eerste test toonde meer van de kamer, maar naar rechtsboven verschoven en met
  strepen. Beide Full-standen houden de offsets van hun venster nu even, en op hun plek als het beeld gespiegeld of
  gedraaid wordt, en tien seconden na een wissel zegt de log wat de sensor ervan maakt: zijn binning, draaien en
  spiegelen, het deel van de sensor dat hij leest, en de maat en plek van zijn uitvoer. Testers: probeer beide
  Full-standen opnieuw, en stuur die logregel met een screenshot van elk.

## 0.6.0 - 2026-10-25
- Een nieuwe instelling, **Beeldveld camera**, voor de camera van de Guition ESP32-P4-schermen (OV02C10), om te
  proberen. **Sharp** is het oude gedrag, het midden van het camerabeeld, en de standaard. **Full (binning)** en
  **Full (skip)** tonen alles wat de camera ziet op de halve resolutie (960 x 528), zoals de datasheet van
  OmniVision het beschrijft: binning middelt telkens twee pixels en regels, skip leest er één op de twee, dus
  fijne patronen (jaloezieën, tegels) kunnen bij skip trillen. Geen van beide is al bewezen op een Guition.
  Guition-eigenaren met een camera en het onderdeel Video: schakel terug naar Sharp als iets vreemd is, en meld bij
  Full welke van de twee werkte, hoeveel van de kamer je ziet vergeleken met Sharp, de kleuren, de beelden per
  seconde (de log zegt het tien seconden na de wissel, en of de sensor de binning aannam), en of het een paar minuten
  stabiel blijft. Een stand die geen beeld geeft, gaat vanzelf terug naar Sharp. Elke andere camera blijft op Sharp
  (de sensor van de M5Stack Tab5 en de reTerminal D1001 heeft voor zover we weten zo'n stand niet). Een wissel start
  de camera en de encoder van de video opnieuw op de nieuwe maat.

## 0.5.1 - 2026-10-25
- De server van de video telt zijn verbindingen (zijn luisterpoort en zijn drie kijkers) mee in wat de firmware van het
  scherm toestaat. Met vier live camerategels op het scherm naast de kijkers van de video raakten ze op, en vond het
  plaatje van een vijfde camerategel geen verbinding. Werk bij als het wordt aangeboden; het scherm wordt opnieuw gebouwd.

## 0.5.0 - 2026-10-25
- De camera staat vanzelf rechtop: op een scherm waarvan het bord hem op zijn kop monteert (de M5Stack Tab5) en op
  een scherm dat een halve slag gedraaid is. De sensor draait zijn eigen beeld, dus de plaatjes in Home Assistant, de
  video en de lichtsterkte en beweging volgen allemaal, zonder kosten. Een nieuwe instelling, **Camera omgekeerd**,
  draait hem nog een keer voor een scherm dat andersom hangt. Vraagt Tessera's firmware met plugin-API 0.19 (dev);
  werk het scherm daarmee bij.
- De regel van de video in het log zegt hoe lang de encoder over een beeld doet en hoeveel intern geheugen hij vasthoudt.
- Tab5-bezitters: kijk of de camera rechtop staat in Home Assistant, en meld het.

## 0.4.3 - 2026-10-24
- Er verandert niets op het scherm of in Home Assistant: de code van de camera is anders ingedeeld voor wat komt. De
  beelden van de camera gaan via een eigen verdeler naar lichtniveau en beweging en naar de video, in plaats van via
  de video, de sensoren die de camera zelf vindt staan in een tabel van de plugin, en de encoder, de server en het
  geluid van de video hebben elk een eigen bestand. Werk bij als het wordt aangeboden; het scherm bouwt opnieuw.

## 0.4.2 - 2026-10-22
- Bouwt op een ESPHome-bouwserver onder Windows. De kleurafstelling van de camera ging als Windows-pad naar de bouw,
  dat onderweg zijn backslashes kwijtraakte, dus de bouw stopte bij een bestand `sc202cs_ipa.json` dat niet te vinden
  was.

## 0.4.1 - 2026-10-22
- Video op de Guition ESP32-P4-schermen. Het vroeg precies 1280 x 720, en hun camera (OV02C10) gaf dat niet
  (1288 x 728), dus daar liet Video niets zien. Die camera heeft nu een modus van 1280 x 720, zijn eerste, en Video
  neemt op elk bord de maat van de camera zelf: een maat die niet in hele macroblokken van 16 past wordt onderweg tot
  zijn midden geknipt (dat kost beelden per seconde, het log zegt het), en een camera boven 1280 x 800 vraagt om een
  kleinere modus in de YAML van het onderdeel. Geen bord noemt een maat. Nog niet geprobeerd op een Guition-scherm:
  zeg wat je ziet.

## 0.4.0 - 2026-10-21
- **Video**, een onderdeel dat aan staat als je de plugin toevoegt: de camera als live video met geluid in Home
  Assistant, een camera **Video** bij het apparaat van het scherm die speelt in de browser en de app, ongeveer een derde
  seconde later. H.264 uit de video-encoder van het scherm (1280 x 720, 15 beelden per seconde) en de microfoon als Opus,
  in sync. Installeer eenmalig Video in Home Assistant onder Instellingen van Tessera (Tessera met die kaart nodig);
  Tessera voegt daarna elk scherm zelf toe. Met het onderdeel loopt de camera vanaf de start en verdwijnt **Camera** van
  het apparaat: **Video** geeft de stilstaande beelden. Lichtniveau en beweging blijven werken. Een scherm dat de plugin
  al heeft houdt hem zoals hij is tot je Video aanvinkt.

## 0.3.2 - 2026-10-20
- Echtere kleuren op de M5Stack Tab5 en de reTerminal D1001: zwart is weer zwart. Hun camera gaf zwart door als een
  donkergrijs dat de chip van het scherm er niet af kon halen, zodat donkere delen van het beeld roze waren en het hele
  beeld flets. De plugin zet nu het zwartniveau van de sensor zelf.

## 0.3.1 - 2026-10-20
- **Beweging wekt het scherm** (eerst uit) en **Beweging blijft aan** (5 seconden tot 10 minuten, eerst 10) zijn ook
  instellingen.
- De eigen stip van Tessera in de rechterbovenhoek van het scherm staat er nu zolang de camera in gebruik is, ook bij
  het meten van licht en beweging; de bovenbalk-items **Camera in gebruik** en **Camera ziet beweging** van de plugin
  zijn weg. Werkt met Tessera met plugin-API 0.17.

## 0.3.0 - 2026-10-20
- Scherpere beelden: ze werden veel te sterk gecomprimeerd (JPEG-kwaliteit 12 van 100, nu 80). Elk beeld is groter,
  ongeveer 150 tot 300 KB.
- Ook de camera van de Guition ESP32-P4-schermen (de OmniVision OV02C10 van de JC8012P4A1, JC1060P470 V2 en
  JC4880P443 in hun cameraversie), op een Tessera waarvan het bord hem noemt. Geprobeerd op de JC8012P4A1 V3.
- Nieuw, uit tot je het aanzet: **Light level** (lux, een schatting) en **Motion** bij het apparaat van het scherm in
  Home Assistant, bepaald door de camera op het scherm zelf: hiervoor gaat geen beeld van het scherm af. Zet
  **Lichtniveau en beweging** aan bij de instellingen van de plugin; de camera blijft dan draaien. Uitgezet is het
  lichtniveau onbekend.

## 0.2.0 - 2026-10-10
- Ook op een bord dat zijn camera vanaf een pin van klok voorziet, zoals de M5Stack Tab5.

## 0.1.0 - 2026-10-10
- Eerste versie: de eigen camera van het scherm als camera in Home Assistant, en een camera in de bovenbalk zolang er iemand kijkt.
