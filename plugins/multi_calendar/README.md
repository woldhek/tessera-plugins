# Multi calendar

The coming appointments of one day, from several Home Assistant calendars in one tile, the way an agenda app combines
your calendars.

- The tile shows one day: today as long as something is left, else the next day within a few days that has
  appointments. The day's name ("Today", "Tomorrow", "Fri 9 Oct") is at the top.
- Every appointment is a row with its time and its title. All-day events come first, as "All day".
- Every calendar has a dot in its own colour (blue, green, amber, purple), so you see where an appointment comes from.
- An appointment that is over goes from the tile by itself. When more appointments are left than fit, the last row says
  "3 more".
- Tap the tile to ask the calendars again now.

## Set up

1. Add the plugin to a screen in the Tessera app and build the screen.
2. In Layout, place the **Agenda** tile from the plugin library, at least 2x2.
3. In the inspector, choose the first calendar as the tile's entity.
4. Under **More calendars**, add up to three more, as entity ids such as `calendar.work` (find them in Home Assistant
   under Settings, Devices and services, Entities; they all start with `calendar.`).
5. Optionally, change **Days to look ahead** (1 to 7, default 3).

## Good to know

- The calendars are asked every five minutes, and at once when the tile's own calendar changes.
- Times are written as 24-hour times, as Home Assistant gives them.
- A calendar shows at most the first 12 appointments of the days it looks ahead.
- The dot colours follow the order of the calendars: the tile's own calendar is blue, the first extra one green.
