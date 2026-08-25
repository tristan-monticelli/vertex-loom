# ADR-0050 — Édition des événements de map

## Statut

Accepté.

## Décision

`MapSession` expose les opérations headless de déclaration et suppression
d’événements, ainsi que l’ajout et la suppression de triggers. Chaque
opération produit une commande snapshot réversible et est validée avant
publication.

Un événement référencé par un trigger ne peut pas être supprimé. Cette règle
maintient le graphe local de la map cohérent pendant l’édition et garantit que
le runtime ne rencontre pas de référence d’événement orpheline.
