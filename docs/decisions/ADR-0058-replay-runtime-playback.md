# ADR-0058 — Lecture runtime des replays

## Décision

`ReplayPlayer` avance un `ReplayDocument` par numéro de frame. Il injecte les
actions au niveau logique dans `InputActionMap`, expose les événements apparus
depuis le dernier appel et expose le dernier checkpoint atteint.

Les frames peuvent être sautées pour les outils headless, mais ne peuvent pas
reculer. Le lecteur n’exécute aucune logique d’interface et reste utilisable
par le Preview Runtime et les tests sans fenêtre.

## Conséquences

La même séquence d’actions et d’événements peut alimenter un runtime graphique,
un smoke-test ou un benchmark. Les checkpoints restent disponibles pour une
future correction d’état sans introduire de dépendance au renderer.

Le Preview Runtime peut charger un replay par identifiant avec `--replay`.
Sa consommation est synchronisée sur les pas physiques fixes et ses compteurs
d’événements et de checkpoints sont inclus dans les sorties smoke-test et
benchmark.

`verify_and_correct_checkpoint` compare les états observés après
quantification, signale les divergences et recale les nœuds connus sur les
valeurs du checkpoint. Les nœuds absents sont signalés comme manquants et ne
sont pas inventés automatiquement.
