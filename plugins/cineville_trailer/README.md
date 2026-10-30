# Cineville trailer (experimental)

> Experimental, and only on Tessera's dev firmware: it needs the Cineville integration's trailer camera (branch
> `experimental-trailer`, pre-release 0.3.0b1) and a screen that streams (the ESP32-P4 boards).

The trailer you chose on the Cineville card (its **Trailer** key) or in Home Assistant, live on a tile, with its sound
on a screen that has a speaker. Before and after it plays, the tile shows the film's poster.

A Tessera screen cannot play an MP4 itself; it shows a camera live. So the Cineville integration offers the trailer as
a camera, which Home Assistant plays through its go2rtc and the Tessera app sends on to the screen. Whether that plays
well is what this plugin tries out.

## Set up

1. In HACS, install the Cineville integration's pre-release 0.3.0b1 (turn on **Show beta versions** for it, or
   **Redownload** and pick the version), and restart Home Assistant.
2. In the Cineville integration's options, turn on **Trailer camera (experimental)**. Each city gets a camera
   **Trailer** and a select **Trailer film**.
3. Add this plugin to a screen with Tessera's dev firmware in the Tessera app.
4. In Layout, place the **Trailer** tile and choose the Cineville trailer camera as its entity.
5. Build and install the screen. Choose a film with **Trailer** on the Cineville card, or with **Trailer film**.

## Roll back

Remove the plugin from the screen, and turn the option **Trailer camera (experimental)** off (the camera and the
select go with it), or reinstall the integration's release 0.2.0 in HACS.

## Privacy

The screen talks only to Home Assistant. Home Assistant fetches the trailer and the poster from Cineville's picture
service (culturekit-assets.imgix.net), as the Cineville website does.
