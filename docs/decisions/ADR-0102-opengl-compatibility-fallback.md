# ADR-0102 — Compatibilité OpenGL du Preview Runtime

## Décision

Le Preview Runtime demande OpenGL 3.x sur macOS et Linux. Sur Windows, il
demande un contexte OpenGL 2.1 de compatibilité afin de fonctionner sur les
environnements qui n’exposent pas OpenGL 3.x.

`OpenGLVectorRenderer` sélectionne GLSL 1.20 et un chemin VBO sans VAO pour un
contexte OpenGL 2. Les contextes OpenGL 3+ conservent GLSL 1.30 ou 1.50 selon
la plateforme et le chemin VAO existant. Si le pilote Windows ne fournit que
OpenGL 1.1, le renderer utilise les tableaux de sommets et le batching du
pipeline fixe. Les deux benchmarks désactivent la VSync après création du
contexte afin de mesurer le coût de rendu réel.

## Conséquences

Le runtime reste utilisable sur les runners Windows dépourvus de pilote
OpenGL 3.x, sans masquer une erreur de création de contexte. Le chemin legacy
réduit les garanties de performance et reste destiné à la compatibilité ; le
gate de 60 FPS p95 demeure obligatoire et est mesuré sans synchronisation
verticale en mode benchmark.
