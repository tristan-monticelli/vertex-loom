# ADR-0065 — Événements d’entrée de zones dans le runtime

## Décision

Le Preview Runtime ajoute un `TriggerRuntime` headless qui évalue la position
du personnage après chaque pas physique. Les zones cercle, capsule et polygone
déclenchent un événement typé `entered` à l’entrée et `exited` à la sortie ; le
maintien dans la zone ne répète aucun événement, et une sortie réarme le trigger.

Le payload est copié depuis `MapEventDefinition`. Les chaînes de collision ne
sont pas des zones et ne déclenchent donc aucun événement. Le routage reste
déterministe, sans script ni état distant.

## Conséquences

Les maps peuvent être inspectées et prévisualisées avec leurs événements sans
introduire de langage de script. Les conditions et actions gameplay restent des
incréments ultérieurs.

Preview Runtime conserve les événements produits pendant le dernier pas fixe et
les expose avec leur identifiant de trigger et leur payload. Le compteur
global reste disponible dans `PreviewRuntimeStats`, tandis que le flux courant
permet aux transitions du runtime jouable de consommer les données sans
réinterroger la géométrie des triggers.

`PreviewRuntimeOptions::gameplay_event_handler` peut consommer chaque événement
dans le pas fixe. Un retour `false` demande une sortie propre de la boucle après
le rendu courant ; `game_runtime` utilise ce point de hand-off pour appliquer une
transition de scène atomique puis reprendre avec la scène cible.
