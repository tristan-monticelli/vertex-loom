# ADR-0087 — Prompt de création des entités

- Statut : accepté
- Date : 2026-08-25

## Décision

Asset Studio expose `New entity...` comme une création distincte. Le prompt
produit un `EntityDefinition v2` avec un nœud racine stable, son transform, sa
profondeur, un drawable `none`, `vector`, `texture` ou `visualComponent`, ainsi
qu’une référence de matériau optionnelle. Le parcours guidé `Composed Entity`
ajoute explicitement une liste de blocs visuels enfants ; chaque bloc possède
son propre type, sa référence, son transform et son ordre.

Les références sont vérifiées contre les documents locaux déjà enregistrés.
La confirmation valide puis publie atomiquement le document dans
`entities/<id>.entity.json`. L’entité est réindexée et sélectionnable après la
publication ; aucun import ni bitmap intermédiaire n’est créé.

## Conséquences

- Les entités créées ont un document persistant avant toute composition avancée
  de hiérarchie.
- Une Entity composée ne peut être publiée qu’avec au moins un bloc explicitement
  choisi ; aucun drawable n’est deviné et aucun bloc n’est synthétisé.
- Les cycles et transforms non finis restent refusés par le validateur du
  contrat.
- L’édition multi-nœuds, les overrides et les gizmos d’entité seront ajoutés
  par incréments séparés.
