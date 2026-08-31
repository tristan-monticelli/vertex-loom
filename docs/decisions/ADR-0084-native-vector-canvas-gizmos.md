# ADR-0084 — Gizmos du canvas vectoriel natif

- Statut : accepté
- Date : 2026-08-25

## Contexte

Le canvas natif d’Asset Studio affichait les formes et permettait déjà le
déplacement d’un nœud, mais la rotation, l’échelle et le pivot n’étaient
accessibles que par l’inspecteur. Cela rendait le personnalisateur difficile à
utiliser pour des ajustements visuels continus.

## Décision

Le canvas expose quatre outils explicites : déplacement, rotation, échelle et
pivot. Les poignées visibles de la sélection permettent aussi de passer
directement à la rotation, à l’échelle ou au pivot. Chaque mouvement appelle la
mutation de nœud existante ; `CommandStack` fusionne les valeurs successives en
un seul geste undoable. Les nœuds verrouillés n’affichent aucune poignée et ne
peuvent pas être transformés.

Le déplacement du pivot conserve la géométrie visuelle en compensant la
position du nœud. Les calculs utilisent les unités monde du canvas et sont
validés par `ProjectSession` avant publication dans le document.

Les segments `line` et `cubic` d’un path natif peuvent être convertis dans
l’inspecteur ou les commandes de plume. Une conversion ligne→courbe conserve
les extrémités et initialise des poignées colinéaires aux tiers ; une
conversion courbe→ligne conserve l’extrémité et supprime les poignées.
Le domaine peut aussi fermer un contour par une commande `close` vers le
premier point, ou retirer cette commande pour le rouvrir ; ces opérations
refusent les paths sans tête `move` valide ou les états déjà dans la cible.

Une sélection multiple d’ancres applique translation, rotation et échelle
autour de son centroïde. Les poignées des segments cubic sélectionnés suivent
la même transformation afin de conserver la forme locale du segment.

Les poignées Bézier proposent trois modes d’édition : `linked` conserve la
longueur de la poignée opposée tout en alignant sa direction, `symmetric`
miroite sa position autour de l’ancre et `free` ne modifie que la poignée
éditée. Cette règle est centralisée dans `fabric_editor` et partagée par le
canvas et l’inspecteur.

Le canvas natif expose également un outil `Pen`. Un clic ajoute une commande
`line` en fin de contour ou l’insère avant le segment visé ; un chemin vide
commence par une commande `move`. Le cliquer-glisser du nouveau point convertit
ce segment en `cubic` et initialise ses deux poignées afin que la courbe soit
visible et éditable immédiatement. Un clic sur une ancre ou une poignée déjà
présente en mode `Pen` l’édite au lieu d’insérer un nouveau point ; `Delete` et
`Backspace` retirent les ancres sélectionnées en conservant la tête `move`. Le
clic droit sur une ancre retire également la commande selon les invariants du
domaine ; la suppression multiple est centralisée dans `fabric_editor` et
testée avec conservation de la tête `move`. L’outil `Move` conserve le
déplacement direct des ancres.

## Conséquences

- Le canvas et l’inspecteur partagent le même contrat `Transform`.
- Aucun état de transformation temporaire n’est écrit directement dans le
  fichier projet.
- Le renderer et les draw packets reçoivent exactement le même transform que
  celui affiché pendant l’édition.
- Les poignées de sommets Bézier sont éditables par leurs coordonnées de
  commande et par le canvas, avec les modes liés, symétriques et libres.
