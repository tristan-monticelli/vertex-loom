# ADR-0042 — Graphe d’états d’animation déterministe

## Statut

Accepté.

## Décision

Les états référencent des clips d’animation par `ResourceReference`. Les
transitions portent un état source, une destination, une priorité, une liste
de conditions et un `exitTime` optionnel exprimé dans l’intervalle normalisé
`[0, 1]`.

Les conditions acceptent des paramètres booléens ou numériques. Toutes les
conditions d’une transition doivent être vraies. En cas de concurrence, la
priorité la plus élevée gagne ; à priorité égale, l’ordre de déclaration est
conservé. Les graphes invalides sont refusés avant exécution.
