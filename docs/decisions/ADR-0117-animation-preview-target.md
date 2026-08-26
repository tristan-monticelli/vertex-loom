# ADR-0117 — Cible explicite de prévisualisation d'animation

- Status: accepted
- Date: 2026-08-26

## Decision

`AnimationClip v2` porte une référence optionnelle `previewEntity` de type
`entity`. Une absence signifie explicitement que le clip est générique ; elle
n'est jamais remplacée par la dernière sélection du Studio. Les clips v1 sont
migrés en mémoire comme clips génériques.

Le prompt de création exige soit une entité choisie dans le Resource Explorer,
soit l'option explicite `Generic clip`. L'inspecteur permet ensuite de changer
ou retirer la cible. À la sélection du clip, Asset Studio charge uniquement
l'entité persistée et signale une cible absente ou invalide.

## Consequences

La prévisualisation est déterministe après reload et indépendante de l'ordre
des clics. La référence rejoint le graphe de ressources et la fermeture des
paquets. Un runtime peut ignorer cette cible d'authoring pour l'association
dynamique d'un clip à une instance.
