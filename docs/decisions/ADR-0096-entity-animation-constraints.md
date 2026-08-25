# ADR-0096 — Contraintes d’animation persistées dans l’entité

## Statut

Accepté.

## Contexte

Le moteur possède déjà les primitives de validation et d’ordonnancement des
contraintes `copy_transform`, `limits` et `look_at`, mais `EntityDefinition`
ne les sauvegardait pas. Une entité rechargée perdait donc la description de
son rig logique.

## Décision

`EntityDefinition v1` accepte un tableau optionnel `constraints`. Chaque entrée
porte son identifiant, son type, un nœud source, un nœud cible, un ordre unique
et les trois indicateurs de transformation. Le champ est absent dans les
anciens documents et est interprété comme un tableau vide.

Le parseur sérialise et recharge les contraintes. Le validateur vérifie les
identifiants, les doublons d’ordre, les cycles de dépendances et l’existence des
nœuds source et cible dans la même entité. Cette étape ne résout pas encore la
contrainte pendant le rendu ; elle établit la source persistante nécessaire au
prochain solveur runtime.

## Conséquences

- Les rigs simples survivent au round-trip et à une publication atomique.
- Les documents historiques restent lisibles sans migration destructive.
- L’exécution runtime pourra consommer un ordre déjà validé et déterministe.
- Les déformations maillées et XPBD restent des contrats séparés à introduire.
