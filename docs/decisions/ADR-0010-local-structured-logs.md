# ADR-0010 — Journaux locaux structurés

- Status: accepted
- Date: 2026-08-24

## Context

Les outils desktop et les utilitaires headless doivent produire des diagnostics
exploitables hors ligne, sans service de télémétrie ni dépendance de logging.

## Decision

Fournir dans `fabric_core` un logger JSON Lines thread-safe écrivant vers un
flux standard C++. Chaque événement contient un timestamp Unix en
millisecondes, un niveau, une catégorie, un message et des champs texte. Le
validateur headless expose cette sortie avec l'option `--json`.

## Alternatives

Un format libre est plus lisible ponctuellement mais difficile à traiter par
les outils. Une bibliothèque de logging externe serait prématurée pour les
besoins actuels et ajouterait un contrat de dépendance supplémentaire.

## Consequences

Les journaux restent locaux et facilement redirigeables vers un fichier. Les
consommateurs choisissent le flux et sa durée de vie. Une politique de rotation
sera ajoutée seulement lorsqu'une application écrira des fichiers persistants.
