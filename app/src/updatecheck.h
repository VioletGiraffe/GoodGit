#pragma once

class QWidget;

// Modal check against the project's GitHub releases: reports that there is no update as well as that there is one.
void checkForUpdatesInteractively(QWidget* parent);

// Only checks when enabled in the settings and a day has passed since the last check, manual or automatic.
// The dialog stays hidden until an update is found, and deletes itself when it closes.
void checkForUpdatesIfDue();
