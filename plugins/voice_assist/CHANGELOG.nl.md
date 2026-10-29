# Wijzigingen

## 0.4.0 - 2026-10-25
- De spraakassistent wacht zijn beurt af met de andere geluiden van het scherm. Een deurbel stopt het wekwoord zolang er iemand voor de deur staat en laat het daarna weer luisteren; een vraag pauzeert muziek die speelt, en de muziek blijft na het antwoord gepauzeerd, net als na de deurbel.
- Heeft Tessera met plugin-API 0.19 (dev) nodig; werk het scherm daarmee bij.

## 0.3.0 - 2026-10-20
- Een popup over het scherm terwijl het luistert, nadenkt en antwoordt, ook als het scherm donker was of sliep: ringen die rimpelen tijdens het luisteren, een boog die draait tijdens het nadenken, wat het hoorde en het antwoord woord voor woord terwijl het wordt uitgesproken. Hij blijft 30 seconden staan na het antwoord, met een sluitknop. Een nieuwe instelling, Popup tonen, zet hem uit.
- Het denkgeluid stopt zodra een lang antwoord begint te spelen, in plaats van door de eerste woorden heen te gaan.
- Heeft Tessera met plugin-API 0.18 nodig.

## 0.2.3 - 2026-10-10
- Een nieuw geluid voor wakker worden.

## 0.2.2 - 2026-10-10
- Nieuwe geluiden voor wakker worden en nadenken. Het denkgeluid speelt opnieuw zodra het afgelopen is, dus de eigen lengte bepaalt het ritme.

## 0.2.1 - 2026-10-10
- De geluiden spelen via de eigen geluidsspeler van Tessera; op een scherm waarvan de microfoon en de luidspreker een bus delen, valt een geluid weg dat niet kan spelen zolang hij luistert.

## 0.2.0 - 2026-10-10
- Een kort geluid als het scherm begint te luisteren, en een zacht plopje steeds opnieuw zolang Assist nadenkt, elk met een instelling om het uit te zetten (Wekgeluid, Denkgeluid).
- In de bovenbalk en op de tegel een microfoon die oplicht zolang hij luistert, stipjes in amber zolang hij nadenkt en een luidspreker zolang hij antwoordt; het item in de bovenbalk kan alleen zijn icoon tonen.
- Heeft ook de luidspreker van het scherm nodig.

## 0.1.0 - 2026-10-10
- Eerste versie: Assist van Home Assistant op het scherm, met een wekzin die het scherm zelf herkent, een tegel, een item voor de bovenbalk en twee instellingen.
