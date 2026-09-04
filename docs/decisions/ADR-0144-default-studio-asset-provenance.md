# ADR-0144 — Provenance des assets Studio par défaut

- Statut : accepté
- Date : 2026-09-02

## Contexte

Asset Studio crée des projets avec trois PNG par défaut. Une archive publique
doit pouvoir prouver leur intégrité et leur droit de redistribution sans
dépendre du dépôt source.

## Décision

Les PNG restent en RGBA8 2048×2048 et sont décrits par un manifeste de
distribution versionné : identifiant, fichier, SHA-256, dimensions, format,
zone alpha attendue, source, titulaire des droits, licence, attribution et
preuve de redistribution. L’installation place les images, le manifeste et
sa notice ensemble.

Asset Studio résout d’abord ce répertoire installé. Le fallback vers le dépôt
source n’est compilé que lorsque `FABRIC_ENABLE_SOURCE_ASSET_FALLBACK` est
actif. La création d’un projet copie les sources sans les modifier.

Le test ordinaire valide le manifeste, le décodage PNG, RGBA8, les dimensions,
l’alpha, les zones attendues et les empreintes. Le gate public active
`VERTEX_LOOM_PUBLIC_RELEASE=1` et refuse toute entrée sans statut `approved` et
fichier de preuve présent.

## Conséquences

- Une installation ne dépend pas de l’arborescence du dépôt.
- Toute modification binaire intentionnelle exige une revue du manifeste.
- La publication publique reste bloquée tant que les trois licences sont
  `UNRESOLVED` et les preuves écrites absentes.
