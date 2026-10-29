# Task List

Your Home Assistant to-do lists (`todo` entities) on a Tessera screen: Local To-do, Shopping List, Google Tasks,
Todoist, CalDAV and any other integration that makes a to-do list.

- Every task is a row with a round checkbox: tap it to tick the task off, tap again to untick it.
- A task with a due date or a description shows it on a second line. "Overdue" is red, "Today" amber. Not every
  integration keeps due dates (the Shopping List does not).
- The tile shows as many tasks as fit, then "3 more". Tap there, or anywhere below the tasks, to open the whole list on
  a card, and page through it with the arrows at the bottom.
- Done tasks stay out of sight unless you turn on **Show completed tasks**; on the card they always show, at the bottom.

A list shows up to about 25 to 30 tasks: what fits in one answer from Home Assistant to the screen.

## Set up

1. Add the plugin to a screen in the Tessera app.
2. In Layout, place the **Tasks** tile from the plugin library.
3. In the inspector, choose your to-do list as the tile's entity.
4. Optionally, turn on **Show completed tasks**.
5. Build and install the screen.
