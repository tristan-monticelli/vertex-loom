# ADR-0037 — Aperçu headless des draw packets

- Status: accepted
- Date: 2026-08-25
- Supersedes: aucun

## Context

Le renderer natif et le personnalisateur doivent partager exactement le même
payload avant l’arrivée du runtime. Une vérification qui ne passe que par une
fenêtre SDL ne permet pas d’intégrer ce contrat dans les validations de projet.

## Decision

Fournir `fabric_asset_preview <project-directory> <vector-id>`. L’outil charge
et valide un `VectorAsset v2 native`, construit les draw packets via
`fabric_render` et émet un JSON stable contenant nœuds, sommets, indices,
contours, couleurs, références image et UV. Il refuse les documents liés,
invalides ou non triangulables sans créer de fenêtre.

Le script npm `asset:preview` reste un raccourci local ; le binaire demeure la
référence utilisée par CTest et les futurs smoke tests de runtime.

## Consequences

Asset Studio, le runtime et les outils CI peuvent inspecter le même payload
headless. L’outil ne publie et ne modifie aucun fichier du projet.
