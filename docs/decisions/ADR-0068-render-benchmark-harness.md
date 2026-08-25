# ADR-0068 — Harness de benchmark OpenGL dense

## Décision

`fabric_render_benchmark` génère par défaut 10 000 quads visibles dans une
fenêtre SDL2/OpenGL cachée et mesure 600 frames sans synchronisation verticale.
Il rapporte le nombre de packets dessinés, draw calls, triangles, durée totale,
p95 frame time et FPS dérivé du p95. Les paramètres `--packets` et `--frames`
permettent les comparaisons contrôlées.

Le benchmark est un outil manuel/release et ne devient pas un test PR
fluctuant. L’absence de contexte OpenGL est une erreur explicite ; aucun
résultat de performance n’est alors déclaré.

## Conséquences

Le projet possède maintenant une mesure reproductible du chemin renderer et du
batching. La cible de 60 FPS p95 doit être vérifiée sur l’environnement GPU de
référence et ne peut pas être déduite de la compilation ou des tests headless.
