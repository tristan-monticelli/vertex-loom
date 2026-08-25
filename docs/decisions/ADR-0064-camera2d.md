# ADR-0064 — Caméra 2D interpolée

## Décision

`Camera2D` conserve une position et un zoom cibles, interpole les deux à
chaque frame et expose les bounds monde du viewport. `zoom_at` ajuste la cible
de position afin de conserver le point sous le curseur ; les zooms sont bornés
à `[0.05, 32]`.

Le Preview Runtime utilise la caméra pour le culling et traduit la molette SDL
en zoom centré dans les modes interactifs.

`Camera2D` accepte des limites monde optionnelles et une cible de suivi. Les
limites tiennent compte de la moitié du viewport afin que la caméra ne montre
pas l’extérieur du rectangle ; si le monde est plus petit que le viewport, le
centre du monde est conservé. `PreviewRuntimeOptions` peut activer le suivi du
personnage et fournir ces limites.

`game_runtime` expose ces réglages avec `--follow-character` et
`--camera-limits <x> <y> <width> <height>` ; les dimensions négatives ou les
valeurs non finies sont refusées avant le chargement du runtime.

## Conséquences

Le renderer reçoit des bounds monde indépendants de la résolution. Les outils
headless peuvent tester pan, zoom et conversion écran/monde sans SDL.
