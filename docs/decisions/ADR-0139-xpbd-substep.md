# ADR-0139 — Solveur XPBD par sous-pas quantifié

## Statut

Accepté.

## Décision

Le solveur XPBD commun expose les contraintes de distance, de flexion, d’aire,
de pin et de collision plane unilatérale.
Chaque contrainte conserve son lambda entre les itérations d’un sous-pas,
utilise la compliance et le pas de temps dans le terme XPBD, puis quantifie les
positions à `1/4096` unité par défaut après chaque itération.

Les indices, masses inverses, compliances, lambdas et positions sont validés
avant exécution. Un système invalide ne modifie pas son état.

Le diagnostic commun mesure, sans modifier l'état, le nombre de particules
dynamiques, le nombre de contraintes, l'erreur maximale et RMS des cinq
familles. Il expose aussi l'énergie élastique estimée `0.5 * erreur² /
compliance` pour les contraintes dont la compliance est strictement positive ;
les contraintes dures sont incluses dans les erreurs mais exclues de cette
énergie afin d'éviter une valeur infinie. Ces mesures sont des aides d'auteur,
pas un nouvel état persistant ni une entrée du solveur.
