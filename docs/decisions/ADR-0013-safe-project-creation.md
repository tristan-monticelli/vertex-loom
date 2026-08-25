# ADR-0013 — Création de projet sans écrasement

- Status: accepted
- Date: 2026-08-24

## Context

Asset Studio doit créer l'arborescence partagée sans risquer d'écraser les
fichiers d'un dossier choisi par erreur. Une création peut aussi échouer après
la création de certains répertoires à cause des permissions ou du stockage.

## Decision

Valider entièrement `ProjectManifest` avant toute écriture. Dans l'interface,
faire choisir un dossier parent existant puis calculer la racine projet comme
`<parent>/<identifiant-généré>`. Le parent peut être non vide ; accepter
uniquement la racine calculée si elle est inexistante ou vide. Créer les
répertoires déclarés, puis écrire `project.json` avec la sauvegarde atomique
existante.

Refuser une destination non vide avec l'erreur structurée
`directory_not_empty`. Ne supprimer automatiquement aucun répertoire après un
échec : le rapport décrit l'erreur et les éventuels répertoires vides restent
récupérables et visibles.

## Alternatives

Écrire dans un dossier non vide avec confirmation risquerait des collisions de
noms et compliquerait le contrat headless. Supprimer toute la racine lors d'un
échec pourrait détruire un dossier créé simultanément par un autre processus.

## Consequences

La création est prévisible, n'exige pas de dossier vide choisi manuellement et
reste sans écrasement. Un échec d'accès peut laisser
une arborescence partielle vide que l'utilisateur peut inspecter et nettoyer
explicitement avant une nouvelle tentative.
