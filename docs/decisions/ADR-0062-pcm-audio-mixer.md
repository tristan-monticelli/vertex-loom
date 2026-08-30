# ADR-0062 — Mixer PCM WAV

## Décision

Le runtime charge les WAV PCM little-endian signés 16 bits mono ou stéréo via
SDL2, puis effectue le mixage en mémoire dans `PcmAudioMixer`. Les voix
acceptent un gain, sont converties mono/stéréo et saturées dans l’intervalle
`int16_t`.

Les fréquences d’échantillonnage doivent correspondre à celle du mixer ; aucun
rééchantillonnage ou format compressé n’est introduit dans cette tranche.

## Conséquences

Le mixage est déterministe et testable sans périphérique audio. L’ouverture
d’un périphérique SDL et la lecture des sons du runtime pourront consommer les
buffers produits sans déplacer la logique de mixage.

`game_runtime --audio <wav>` ouvre désormais le périphérique SDL au format du
clip, met en file les buffers mixés et ferme proprement le sous-système audio.
