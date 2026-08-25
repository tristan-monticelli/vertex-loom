# ADR-0065 — Événements d’entrée de zones dans le runtime

## Décision

Le Preview Runtime ajoute un `TriggerRuntime` headless qui évalue la position
du personnage après chaque pas physique. Les zones cercle, capsule et polygone
déclenchent une seule fois l’événement référencé lors de l’entrée ; le maintien
dans la zone ne répète pas l’événement, et une sortie réarme le trigger.

Le payload est copié depuis `MapEventDefinition`. Les chaînes de collision ne
sont pas des zones et ne déclenchent donc aucun événement. Le routage reste
déterministe, sans script ni état distant.

## Conséquences

Les maps peuvent être inspectées et prévisualisées avec leurs événements sans
introduire de langage de script. Les événements de sortie, conditions et
actions gameplay restent des incréments ultérieurs.
