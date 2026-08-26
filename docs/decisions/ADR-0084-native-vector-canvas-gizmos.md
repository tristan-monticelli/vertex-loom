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

## Conséquences

- Le canvas et l’inspecteur partagent le même contrat `Transform`.
- Aucun état de transformation temporaire n’est écrit directement dans le
  fichier projet.
- Le renderer et les draw packets reçoivent exactement le même transform que
  celui affiché pendant l’édition.
- Les poignées de sommets Bézier sont éditables par leurs coordonnées de
  commande et par le canvas ; les modes de poignées liées, symétriques et
  libres restent des incréments ultérieurs.
