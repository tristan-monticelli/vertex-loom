# ADR-0100 — Interpolation de l’état XPBD au rendu

## Décision

Le Preview Runtime conserve les positions XPBD avant et après le dernier pas
de simulation fixe de `1/60` seconde. En mode interactif, le rendu interpole
ces positions avec la fraction restante de l’accumulateur ; les modes smoke et
benchmark rendent l’état courant pour rester reproductibles frame par frame.

L’état simulé, les checkpoints et les événements restent attachés aux pas fixes.
L’interpolation est uniquement une vue de rendu et ne modifie jamais les
particules persistées ni les résultats du solveur.

## Conséquences

Les éléments textiles évitent les sauts visuels lorsque la cadence de rendu
diffère de 60 Hz. Le rendu peut afficher une position intermédiaire sans
introduire de flottants supplémentaires dans le replay ou la validation.
