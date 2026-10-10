#pragma once

// Ends the tray application from an explicit request (tray menu "Quit",
// SIGINT/SIGTERM). QCoreApplication::quit() first asks every window to
// close and is cancelled when one refuses; the Dashboard and Settings refuse
// on purpose (closing only hides them), so with both visible quit() would be
// ignored. exit(0) leaves the event loop unconditionally.
void requestApplicationQuit();
