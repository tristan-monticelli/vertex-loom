# ADR-0062 — Mixer PCM WAV

## Décision

Le runtime charge les WAV PCM little-endian signés 16 bits mono ou stéréo via
SDL2, puis effectue le mixage en mémoire dans `PcmAudioMixer`. Les voix
acceptent un gain, sont converties mono/stéréo et saturées dans l’intervalle
`int16_t`.

Les fréquences d’échantillonnage doivent correspondre à celle du mixer ; aucun
rééchantillonnage ou format compressé n’est introduit dans cette tranche.

Le mixer possède un bus implicite `master` et accepte des bus nommés avec gain
non négatif. Une voix référence son bus et un panoramique normalisé de `-1`
(gauche) à `1` (droite). Le gain du bus est lu pendant le mixage, ce qui permet
de modifier un groupe déjà actif ; un bus inconnu ou un panoramique hors domaine
est refusé avant de créer la voix.

Une voix bouclée remplit le buffer sans silence à la frontière du clip. Le
runtime ouvre une sortie stéréo, applique `volume événement × volume bus ×
atténuation`, puis dérive le panoramique de la position horizontale rapportée à
la distance maximale. Le listener initial est l'origine du monde ; un suivi de
listener caméra pourra remplacer ce point sans changer le document audio.

## Conséquences

Le mixage est déterministe et testable sans périphérique audio. L’ouverture
d’un périphérique SDL et la lecture des sons du runtime pourront consommer les
buffers produits sans déplacer la logique de mixage.

Le test caché `[.device]` de `fabric_audio_mixer_tests` envoie un signal stéréo
non silencieux à un périphérique réel. Il est exécuté explicitement sur chaque
plateforme de release, car une sortie audio matérielle ne doit pas rendre la
suite headless standard instable.

`game_runtime --audio <wav>` ouvre désormais le périphérique SDL au format du
clip, met en file les buffers mixés et ferme proprement le sous-système audio.
