# ADR-0035 — Backend OpenGL 3 des draw packets vectoriels

- Status: accepted
- Date: 2026-08-25

## Context

Les draw packets headless sont maintenant en espace monde, mais Asset Studio
ne possédait encore aucun composant OpenGL capable de les consommer. Un
renderer séparé doit partager le contrat de géométrie sans exposer de types
OpenGL dans les documents ou les tests de validation.

## Decision

`fabric_render` expose `OpenGLVectorRenderer`. Il charge les fonctions
OpenGL via `SDL_GL_GetProcAddress`, compile un programme OpenGL 3, configure
un VAO avec VBO/IBO et dessine les triangles de fills couleur ou image ainsi
que les contours ouverts ou fermés. Les packets image fournissent les UV
normalisées et la résolution texture est injectée par callback. Le viewport
convertit les unités monde en coordonnées clip ; les statistiques retournent
packets soumis, packets dessinés, triangles et diagnostics.

Les fills image sont refusés explicitement tant qu’un résolveur de textures
local n’est pas passé au backend. Aucun upload ou atlas implicite n’est créé.
Les tests headless couvrent l’état non initialisé. Asset Studio appelle le
backend après le rendu ImGui dans le viewport natif courant ; le smoke-test
avec contexte OpenGL automatisé et le résolveur de textures restent des étapes
dédiées.

## Consequences

Le backend reste utilisable par Asset Studio et Preview Runtime sans modifier
`VectorAsset`. Le chargement dynamique évite une dépendance à un loader
OpenGL externe, mais impose qu’un contexte SDL courant existe avant
`initialize`, `draw` et `shutdown`.
