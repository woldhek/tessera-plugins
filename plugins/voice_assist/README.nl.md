# Spraakassistent

Praat vanaf het scherm met Assist van Home Assistant. Het scherm luistert zelf naar een wekzin en stuurt pas daarna wat
je zegt naar Home Assistant; het antwoord komt uit de speaker van het scherm.

Spraakherkenning, het gesprek en spraak zijn wat je Home Assistant voor Assist gebruikt: zijn eigen gespreksagent met
Whisper en Piper, Home Assistant Cloud, of een AI zoals OpenAI of Claude. Het scherm is een spraaksatelliet in Home
Assistant, in de ruimte die je het geeft.

## Wat je nodig hebt

- Een scherm met een microfoon en een speaker: een bord dat ze heeft (de reTerminal D1001) of een plugin die ze brengt.
- Een Assist-pijplijn in Home Assistant (Instellingen, Spraakassistenten) met spraakherkenning en spraak.

## Instellen

1. Open in Tessera **Plugins**, kies **Spraakassistent** en vink je schermen aan. Elk scherm bouwt één keer.
2. Open in Home Assistant het apparaat van het scherm (Instellingen, Apparaten) en kies bij **Assist-pijplijn** de
   pijplijn, als het niet de voorkeurspijplijn is.
3. Zet de tegel **Assistent** op een pagina: hij zegt wat de assistent doet, en een tik stelt een vraag zonder wekzin,
   of stopt er een.
4. Optioneel: zet **Assistent** in de bovenbalk van een pagina (Bovenbalk, Toevoegen, Uit plugins). Hij verschijnt
   terwijl de assistent luistert, nadenkt of antwoordt.

## Op het scherm

Houd de bovenbalk vast, dan **Instellingen**, **Plugins**, **Spraakassistent**:

- **Wekwoord**: of het scherm naar zijn wekzin luistert. Uit stelt een tik op de tegel nog steeds een vraag.
- **Wekzin**: Okay Nabu, Hey Jarvis of Hey Mycroft.
- **Wekgeluid**: een kort geluid als het scherm begint te luisteren, na de wekzin of een tik.
- **Denkgeluid**: een zacht geluid, steeds opnieuw, zolang Home Assistant het antwoord bedenkt.
- **Popup tonen**: een popup over het scherm terwijl het luistert, nadenkt en antwoordt (aan, tenzij je hem uitzet). Zie hieronder.
- **Nu vragen**: stelt meteen een vraag.

De instellingen zijn ook entiteiten van het scherm in Home Assistant ("Wake word", "Wake phrase", "Wake sound",
"Thinking sound"), en de editor toont ze in de details van de plugin op het tabblad Plugins van het scherm. De eigen schakelaar **Microfoon** van het
scherm, op een bord dat er een heeft, zet hem ook voor de assistent uit.

## De popup

Zodra het scherm de wekzin hoort, of je op de tegel tikt, komt er een popup over het scherm, ook als het scherm donker
was of sliep. Terwijl het luistert rimpelen er ringen uit de microfoon, terwijl Home Assistant nadenkt draait er een
boog, en daaronder verschijnt wat het hoorde en daarna het antwoord, woord voor woord zoals de speaker het zegt; van een
lang antwoord zie je de laatste regels. De popup blijft 30 seconden staan na het antwoord, zodat je het nog eens kunt
lezen. De sluitknop, een tik erop of een tik ernaast sluit hem eerder; tijdens een vraag stopt een tik op de popup die
vraag. Hij heeft Tessera met plugin-API 0.18 nodig.

De woorden van het antwoord komen in één keer van Home Assistant, aan het eind van het antwoord: Home Assistant stuurt
een ESPHome-scherm geen antwoord in stukjes terwijl het wordt geschreven. De popup laat ze zien in het tempo van
spreken, gelijk met de speaker.

## De geluiden

Het zijn twee bestanden in `components/voice_assist/sounds/`: `wake.wav` en `thinking.wav` (het scherm speelt het
opnieuw zodra het afgelopen is, dus de lengte bepaalt het ritme). Elk is een WAV-bestand met 16-bit samples, mono, op 48 kHz zoals de luidspreker van het
scherm. Een eigen plugin die je van deze maakt kan ze vervangen; de build zegt het als een bestand niet zo'n WAV is.

## Privacy

De wekzin wordt op het scherm zelf herkend; er verlaat niets het scherm voordat hij gehoord is of je op de tegel tikt.
Daarna gaat de vraag naar je Home Assistant en verder naar de spraak- en gespreksdiensten die zijn pijplijn gebruikt.
