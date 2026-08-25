# ADR-0049 — Session Map et commandes d’édition

## Statut

Accepté.

## Décision

Map Studio manipule un `MapDocument` via une session headless qui charge le
manifeste, publie atomiquement la map et soumet chaque placement, suppression
ou transformation d’instance au `CommandStack`. Le chunk d’une instance est
recalculé à partir de sa position avant validation.

La session conserve l’état dirty et expose undo/redo sans dépendre de Dear
ImGui. L’interface de Map Studio sera un client de cette API.
