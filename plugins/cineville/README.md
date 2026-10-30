# Cineville

> Needs the Cineville integration in Home Assistant first (through HACS, below). The screen shows the integration's
> sensors; it reaches nothing on the internet itself.

What plays in the Cineville theaters of your city, and what comes soon: the art house cinemas of the Cineville pass in
the Netherlands, as on cineville.nl.

- Every film is a row: its title and, when there is room, when and where it shows next ("19:15 Kriterion",
  "Tomorrow 21:00 Eye").
- Cineville's own picks stand out: the **film of the week** and the **tips** of its editors have a coloured bar and
  say so.
- **Today** shows only the films that still play today; a film drops off once its last showtime has begun.
- The **Coming soon** sensor shows the films that open in the coming weeks, with the day they open and how many
  previews your theaters have.
- Tap the tile to see the whole list on a card (on a screen of 4 inches and up with each film's director), and page
  through it with the arrows.

## Set up

1. In Home Assistant, open HACS, choose **Custom repositories** in the menu at the top right, add
   `https://github.com/woldhek/ha-cineville` as type **Integration**, then find **Cineville** in HACS and download it.
   Restart Home Assistant.
2. In Home Assistant, go to **Settings → Devices & services → Add integration**, choose **Cineville**, pick your city
   and, if you like, only some of its theaters. This makes two sensors: **Now showing** and **Coming soon**.
3. Add this plugin to a screen in the Tessera app.
4. In Layout, place the **Films** tile from the plugin library.
5. In the inspector, choose one of the two Cineville sensors as the tile's entity, and optionally **Today** or **Only
   Cineville's picks**.
6. Build and install the screen.

## The integration

The integration lives in its own repository, [woldhek/ha-cineville](https://github.com/woldhek/ha-cineville). The integration asks Cineville every hour for the showtimes of the chosen days (seven by default) in the chosen
theaters. In its options you change the theaters, the number of days, the order (Cineville's picks first, most
showtimes first, soonest showtime first, or by title), how many weeks "Coming soon" looks ahead, and the language of the
descriptions. Each sensor's state is its number of films; its attributes hold up to 16 films for the screen and up to
60 in `films`, with the director, year, showtimes, theaters, poster and a link to the film on cineville.nl, for a
dashboard or an automation.

## Privacy

The screen talks only to Home Assistant. The Cineville integration asks api.cineville.nl, the same public service the
Cineville website reads, which theaters show which films; it sends no account, no key and nothing about you.
