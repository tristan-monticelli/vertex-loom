# ADR-0121 — AudioDocument v2

`AudioDocument` is a project resource containing named audio events. Each event
references a source path and stores normalized volume and loop policy. The
document is validated and published atomically under `assets/audio`; runtime
WAV playback remains a separate mixer concern.

Version 2 adds named buses with normalized volume and an implicit `master`
bus. Each event selects a bus and may define a 2D source position with minimum
and maximum attenuation distances. Schema v1 loads with `master`, no spatial
settings, then saves as v2. Sources must be portable relative paths. This keeps
existing projects audible while making grouping and spatial intent explicit.

`AnimationClip v4` peut cibler un événement par le couple typé
`audio document ResourceReference + eventId`. Le validateur résout le document
et l'identifiant imbriqué lors de la validation projet. Les anciens marqueurs
v3 sans cue restent valides et migrent sans son implicite.
