# ADR-0116 — État authorable des nœuds d'entité

- Status: accepted
- Date: 2026-08-26

## Decision

`EntityDefinition v4` persiste `visible` et `locked` sur chaque nœud.
`visible` contrôle la production de draw packets dans Asset Studio, Map Studio
et Preview Runtime. `locked` protège les gestes d'édition sans modifier la
simulation. Les documents v1, v2 et v3 sont migrés en mémoire avec
`visible = true` et `locked = false`.

L'inspecteur autorise le changement de kind, de ressource, de matériau et de
configuration d'instance du composant. Un changement de kind conserve les
références compatibles et efface seulement les champs incompatibles après une
action explicite. Reparentage et ordre restent des mutations atomiques du
document complet afin que validation de cycles et undo couvrent le geste.

## Consequences

Un drawable n'est plus figé au moment de la création. La visibilité produit le
même rendu dans les trois consommateurs. Le verrouillage demeure un état
d'authoring portable et n'empêche pas le runtime d'évaluer la pose du nœud.
