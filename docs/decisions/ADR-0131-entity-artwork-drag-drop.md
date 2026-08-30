# ADR-0131 — Glisser-déposer des artworks dans la hiérarchie d’entité

- Statut : accepté
- Date : 2026-08-26

## Contexte

L’explorateur de ressources et la hiérarchie d’entité étaient séparés : un
artwork devait être choisi dans l’inspecteur avant de pouvoir être affecté à un
nœud. Le parcours attendu permet de déposer une texture, un vecteur ou un
composant visuel sur un nœud existant, ou de créer directement un nœud racine ou
enfant.

## Décision

`Asset Studio` publie un payload ImGui `VERTEX_LOOM_RESOURCE` contenant le type
typé de ressource et son identifiant borné. L’inspecteur propose trois cibles :
le nœud existant, la création d’une racine et la création d’un enfant. Toutes
les mutations passent par `ProjectSession`.

Un drop sur un composant visuel avec des overrides est refusé sans modifier le
document. Il devra réutiliser la confirmation « Discard incompatible overrides? »
avant d’être autorisé.

## Conséquences

- Le geste est disponible sans dupliquer la logique de sélection de ressource.
- Les ressources non graphiques sont rejetées par le contrat de payload.
- Le test headless `entity artwork destinations cover existing root and new root
  or child nodes` vérifie les mutations vers un nœud existant, une racine et un
  enfant. La preuve reproductible du geste UI depuis le Resource Explorer reste
  requise avant de fermer la gate UX E2E.
