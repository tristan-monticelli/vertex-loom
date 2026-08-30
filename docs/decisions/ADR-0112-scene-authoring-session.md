# ADR-0112 — Authoring des scènes dans Map Studio

## Statut

Accepté — 2026-08-26.

## Contexte

`SceneDocument v1` et son runtime existent, mais aucune interface ne permet de
créer ou modifier ses maps montées et ses transitions. Modifier directement le
JSON contourne validation, undo, autosave et récupération.

## Décision

`fabric_editor` fournit `SceneSession`, une session de document analogue à
`MapSession`. Chaque mutation construit un snapshot complet, valide le
`SceneDocument`, puis passe par `CommandStack`. La création et l'ouverture
sauvegardent d'abord une scène dirty valide ; une cible invalide ne remplace
jamais la session active.

Map Studio intègre le Scene Studio dans une fenêtre dédiée. L'utilisateur peut
créer ou ouvrir une scène, ajouter et retirer des maps avec un identifiant de
montage, choisir la map d'entrée, puis ajouter, modifier ou retirer une
transition avec scène cible, point d'entrée et événement optionnel. Les
références utilisent les pickers du projet et restent validées avant écriture.

La fermeture de Map Studio agrège map, mécanique et scène dans la même décision
Save/Discard/Cancel. Une scène peut être validée et publiée directement comme
`ScenePackageManifest v1` depuis ce panneau.

## Conséquences

- L'authoring ne dépend pas d'un état de sélection caché.
- Undo, redo, dirty, autosave, récupération et sauvegarde atomique couvrent le
  même document consommé par Preview Runtime.
- Les tests headless couvrent chaque mutation et le round-trip ; le harnais
  graphique ouvre et modifie une scène réelle avant publication.
