# ADR-0057 — Document de replay déterministe

## Décision

`ReplayDocument v1` est stocké sous `assets/replays/<id>.replay.json`.
Il contient le build, la seed, les entrées et événements ordonnés par frame,
ainsi que des checkpoints d’états quantifiés.

Les positions sont quantifiées à `1/4096` d’unité et les rotations à
`1/65536` de tour. Le document est validé avant sauvegarde atomique et ses
références de scène sont ajoutées au registre headless.

## Conséquences

Le replay compare un état observable stable entre plateformes sans exiger
l’égalité brute des flottants internes. Les checkpoints doivent être
strictement croissants et chaque nœud ne peut apparaître qu’une fois par
checkpoint.
