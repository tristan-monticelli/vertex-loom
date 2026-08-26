# ADR-0121 — AudioDocument v1

`AudioDocument` is a project resource containing named audio events. Each event
references a source path and stores normalized volume and loop policy. The
document is validated and published atomically under `assets/audio`; runtime
WAV playback remains a separate mixer concern.
