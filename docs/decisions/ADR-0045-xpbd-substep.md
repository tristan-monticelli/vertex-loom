# ADR-0045 — Solveur XPBD par sous-pas quantifié

## Statut

Accepté.

## Décision

Le solveur XPBD commun commence par les contraintes de distance et de pin.
Chaque contrainte conserve son lambda entre les itérations d’un sous-pas,
utilise la compliance et le pas de temps dans le terme XPBD, puis quantifie les
positions à `1/4096` unité par défaut après chaque itération.

Les indices, masses inverses, compliances, lambdas et positions sont validés
avant exécution. Un système invalide ne modifie pas son état.
