# ADR-0088 — Prompt de création des animations

- Statut : accepté
- Date : 2026-08-25

## Décision

Asset Studio expose `New animation...` comme une création distincte. Le prompt
produit un `AnimationClip v1` avec durée positive, boucle et marker optionnel.
Les pistes restent ajoutées par le timeline editor et ne sont pas inventées
dans le prompt.

La confirmation valide puis publie atomiquement le clip dans
`assets/animations/<id>.animation.json`. Le document est ensuite indexé et
sélectionnable par Asset Studio.

## Conséquences

- Les clips disposent d’un propriétaire et d’un chemin persistants avant
  l’édition de leurs bindings.
- Les markers hors durée et identifiants invalides sont refusés avant écriture.
- Le prompt reste générique : aucune propriété d’entité n’est codée dans sa
  création.
