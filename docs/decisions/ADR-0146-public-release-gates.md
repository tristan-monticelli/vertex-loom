# ADR-0146 — Gates et artefacts de release publique

- Statut : accepté
- Date : 2026-09-02

## Contexte

Les builds de branche ne garantissaient ni les tests Node, ni l’absence de
tests graphiques ignorés, ni la provenance des dépendances et assets. CPack
contenait aussi un contact factice.

## Décision

Chaque pull request exécute `npm ci`, `npm run validate`, puis la recette
Studio complète sur macOS, Windows et Linux. CTest écrit un rapport JUnit et la
gate échoue pour tout test `SKIP` ou `notrun`. macOS utilise son GPU natif,
Linux Xvfb/Mesa, et Windows logiciel reste complété avant tag par un runner GPU
auto-hébergé obligatoire.

Les sources CMake téléchargées et les actions GitHub sont épinglées à des
commits. Une release de tag part d’un checkout propre, désactive le fallback
vers les sources, exige le contact public, les preuves de redistribution et
les secrets de signature, puis produit archives, checksums, SBOM et licences.

## Conséquences

- Un test graphique ignoré est visible comme `SKIP` mais ne ferme aucun gate.
- La création d’un tag échoue tant que les données juridiques et de signature
  ne sont pas configurées.
- Le contact CPack local peut rester vide; aucune valeur factice n’est publiée.
