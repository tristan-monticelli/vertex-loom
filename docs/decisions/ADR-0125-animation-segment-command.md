# ADR-0125 — Création atomique d’un segment d’animation

## Décision

Asset Studio expose une commande A→B qui reçoit une liaison, deux temps
strictement ordonnés et deux valeurs de même type. La timeline met à jour ou
ajoute les deux clés dans une copie du clip, valide le résultat, puis publie
une seule commande undoable.

Le contrat s’applique aux types de valeur déjà supportés par les animations :
vecteur, scalaire, couleur, booléen et référence de ressource. Les temps
égaux ou inversés sont refusés afin de préserver une interpolation déterministe.

## Conséquences

- Le geste A→B ne laisse pas un clip partiellement modifié si la seconde clé
  est invalide.
- Undo et redo restaurent ou réappliquent le segment comme une unité.
- Les tangentes, le snapping et la sélection multiple restent des évolutions
  distinctes du contrat.
