# ADR-0026 — Espace de travail centré sur la ressource

- Status: accepted
- Date: 2026-08-25

## Context

Asset Studio publie des textures et des artworks mais sa coquille ne liste que
les dossiers du manifeste. Le canvas montre le dernier import en mémoire et
l'inspecteur mélange projet, import et création. Changer de projet ou quitter
peut aussi abandonner une modification dirty sans décision explicite.

## Decision

Introduire dans `fabric_editor` un index local des ressources connues et une
sélection stable par identifiant et type. L'interface liste cet index dans un
navigateur, charge la ressource sélectionnée, puis dérive le canvas et
l'inspecteur de cette seule sélection.

Décoder les sources PNG et SVG dans un état temporaire avant publication afin
que les dialogues d'import montrent un aperçu réel. Sélectionner une texture
existante par son nom et sa vignette pour les fills image ; son identifiant
reste un détail de stockage.

Protéger tout remplacement ou fermeture d'une session dirty par une décision
`Save`, `Discard` ou `Cancel`. Utiliser `Cmd` sur macOS et `Ctrl` ailleurs.

La première édition native intégrée couvre la sélection d'un nœud, son nom, sa
visibilité, son transform et son fill couleur ou image. Toute mutation après
publication passe par `CommandStack`, dirty, autosave et récupération.

## Alternatives

Continuer à conserver uniquement le dernier import empêche de reprendre un
travail après redémarrage. Lire directement les dossiers depuis les widgets
couple l'interface au stockage et duplique la validation. Publier avant aperçu
rend l'annulation trompeuse puisque les imports sont immuables.

## Consequences

La session possède davantage d'état de présentation sans dépendre de Dear
ImGui. L'index doit ignorer les documents invalides et exposer leurs diagnostics.
Les tests headless couvrent indexation, sélection, prévisualisation temporaire,
mutations réversibles et protection de session ; le shell conserve un smoke
test visuel manuel jusqu'à l'ajout d'un environnement graphique automatisé.
