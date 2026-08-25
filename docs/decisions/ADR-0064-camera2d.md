# ADR-0064 — Caméra 2D interpolée

## Décision

`Camera2D` conserve une position et un zoom cibles, interpole les deux à
chaque frame et expose les bounds monde du viewport. `zoom_at` ajuste la cible
de position afin de conserver le point sous le curseur ; les zooms sont bornés
à `[0.05, 32]`.

Le Preview Runtime utilise la caméra pour le culling et traduit la molette SDL
en zoom centré dans les modes interactifs.

## Conséquences

Le renderer reçoit des bounds monde indépendants de la résolution. Les outils
headless peuvent tester pan, zoom et conversion écran/monde sans SDL.
