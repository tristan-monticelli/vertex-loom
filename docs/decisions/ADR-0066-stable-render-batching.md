# ADR-0066 — Batching stable des draw packets OpenGL

## Décision

`OpenGLVectorRenderer` regroupe les packets contigus sans clipping ni stroke
qui partagent la même couleur, ou la même texture et opacité. Les sommets et
indices sont uploadés ensemble et produisent un seul `glDrawElements`.

Les packets avec clipping, stroke, matériau incompatible ou erreur de
résolution restent sur le chemin individuel. L’ordre des packets n’est jamais
réorganisé : le batching est donc stable et conserve le tri par calque/Z fourni
par le runtime.

## Conséquences

Les scènes composées de nombreux sprites ou formes de même matériau réduisent
leurs draw calls sans modifier leur résultat visuel. La cible de 10 000 éléments
et 60 FPS p95 doit encore être mesurée sur la scène de référence et n’est pas
considérée comme acquise par cette optimisation seule.

Le backend mémorise également les uniform locations lors de l’initialisation,
réutilise la capacité des VBO/IBO avec `glBufferSubData` et évite la table
d’indexation des packets lorsqu’aucun clip n’est présent. Ces optimisations
réduisent les allocations et recherches répétées par frame sans changer l’ordre
ou le résultat des draw packets.
