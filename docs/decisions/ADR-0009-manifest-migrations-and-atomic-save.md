# ADR-0009 — Migrations et sauvegarde atomique du manifeste

- Status: accepted
- Date: 2026-08-24
- Note: le contrat courant v1 est étendu à v2 par ADR-0018.

## Context

Les éditeurs doivent pouvoir ouvrir les anciens projets et remplacer
`project.json` sans laisser un fichier partiel après une interruption.

## Decision

Lire la version avant de construire `ProjectManifest`, puis appliquer dans
l'ordre une migration explicite par version. Le premier chemin pris en charge
convertit le prototype `v0` vers le contrat courant `v1`. Les versions futures
et les versions sans chemin de migration sont refusées.

Écrire le manifeste validé dans un fichier temporaire adjacent, fermer ce
fichier, puis le renommer sur `project.json`. Le renommage POSIX et
`MoveFileExW` avec remplacement sous Windows assurent que la destination se
trouve toujours dans un état complet.

## Alternatives

Modifier les données pendant la désérialisation masquerait les migrations et
rendrait leur test isolé difficile. Écrire directement dans `project.json`
exposerait les projets à une troncature.

## Consequences

Chaque future version exige une fonction de migration et un test dédié. Un
échec d'écriture conserve le manifeste précédent et peut laisser uniquement un
fichier temporaire, que l'opération tente de nettoyer.
