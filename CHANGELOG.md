# Changelog

## 0.5.0

- Replace the normal attenuation screen with Stacked Hero: scale-4 applied
  digits, full-word source, actual profile lock, A/B badge, step indicators
  and distinct pending, locked and no-latch messages. Preserve the applied
  hero while editing; show UNKNOWN only without a completed transaction.
- Add explicit `label` capability/get/save/status/cancel commands backed by
  a separate append-only journal record. Display/button records and loader,
  mailbox and app-only update boundaries remain compatible. App updates
  reset all saved setup, including the unit label.
- Compare production C rendering and UI transitions against approved pixel
  fixtures, with 1×/4× screenshots and boundary/ownership/unknown tests.

New screen, button and persistence behavior is hardware UNRUN. Host models
and screenshots do not establish OLED usability, flash endurance or RF
accuracy. See [screen behavior and setup](docs/screen.md) and the
[validation record](docs/validation.md).
