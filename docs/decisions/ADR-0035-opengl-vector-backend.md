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

Les packets portant `clipNodeId` utilisent le stencil du framebuffer pour
appliquer un masque de premier niveau à leur fill et leur contour. Les clips
imbriqués sont refusés avec un diagnostic ; un contexte sans stencil refuse
également le packet concerné.

Les fills image sont refusés explicitement tant qu’un résolveur de textures
local n’est pas passé au backend. Asset Studio fournit désormais ce resolver :
il charge le `TextureAsset` et le PNG local à la demande, puis conserve le
handle GPU pendant la session. Aucun atlas implicite n’est créé.
Les tests headless couvrent l’état non initialisé. Asset Studio appelle le
backend dans le viewport natif courant avant de soumettre les commandes ImGui.
Le panneau central n'émet pas de fond opaque lorsqu'il accueille ce rendu :
grille, gizmos, fenêtres et modals restent ainsi des commandes ImGui composées
au-dessus du canvas, sans que le renderer puisse traverser leur ordre Z.
`npm run test:gl`
exécute un smoke-test dédié avec contexte SDL caché, rendu d’un quad et
vérification d’un pixel ; lorsque le contexte possède un stencil, il vérifie
également un masque triangulaire. Il est tolérant à l’absence de contexte en
retournant le code de saut `77`.

## Consequences

Le backend reste utilisable par Asset Studio et Preview Runtime sans modifier
`VectorAsset`. Le chargement dynamique évite une dépendance à un loader
OpenGL externe, mais impose qu’un contexte SDL courant existe avant
`initialize`, `draw` et `shutdown`.
