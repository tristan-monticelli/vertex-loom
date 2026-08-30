# ADR-0068 — Harness de benchmark OpenGL dense

## Décision

`fabric_render_benchmark` génère par défaut 10 000 quads visibles dans une
fenêtre SDL2/OpenGL cachée et mesure 600 frames sans synchronisation verticale.
Il rapporte le nombre de packets dessinés, draw calls, triangles, durée totale,
p95 frame time et FPS dérivé du p95. Les paramètres `--packets`, `--frames`,
`--min-fps` et `--report` permettent respectivement les comparaisons contrôlées,
un seuil explicite et l’archivage JSON des métriques.

Le benchmark est un outil manuel/release et ne devient pas un test PR
fluctuant. L’absence de contexte OpenGL est une erreur explicite ; aucun
résultat de performance n’est alors déclaré.

## Conséquences

Le projet possède maintenant une mesure reproductible du chemin renderer et du
batching. La cible de 60 FPS p95 doit être vérifiée sur l’environnement GPU de
référence et ne peut pas être déduite de la compilation ou des tests headless.

`fabric_runtime_benchmark` complète ce harness au niveau Preview Runtime : il
fabrique un projet temporaire valide avec une map dense, passe par le chargement
réel des ressources et mesure le culling, les draw calls et le p95. Le projet
temporaire est supprimé après la mesure et ne modifie pas le workspace.
L’option `--min-fps` transforme le seuil de performance en assertion explicite ;
le workflow multiplateforme l’utilise avec `60` FPS.
L’option `--report <path>` écrit également un rapport JSON structuré avec la
configuration, les métriques et le résultat du seuil, afin que les workflows
release puissent archiver et comparer les mesures sans parser la sortie texte.

Sur macOS, la scène générée de 10 000 instances visibles en 1440 × 900 donne
`7,070 ms` p95, soit `141,4 FPS p95` sur 600 frames. Cette mesure couvre le
chargement réel, l’index de chunks, le culling, le chemin statique du runtime
et le renderer ; la validation Windows/Linux reste nécessaire avant de fermer
le gate multiplateforme.
