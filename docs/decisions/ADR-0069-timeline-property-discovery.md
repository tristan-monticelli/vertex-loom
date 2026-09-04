# ADR-0069 — Découverte des propriétés animables dans le timeline

## Décision

`AnimationTimeline::animatable_bindings` construit les bindings stables d’un
nœud à partir de `PropertyDescriptorRegistry::animatable()`. L’ordre du registre
est conservé et les descripteurs non lisibles ou non inscriptibles ne sont pas
exposés. Un nœud vide ne produit aucun binding.

Le timeline ne connaît donc pas les propriétés concrètes de transform ou de
matériau ; elles sont déclarées par le registre et ciblées par
`nodeId + componentId + propertyId`.

## Conséquences

Les widgets alimentent automatiquement leur liste de pistes à partir des
composants disponibles. Le nœud racine expose notamment le binding
`pathFollower.progress`, réservé aux instances de map qui possèdent un
PathFollower ; le Preview Runtime l'évalue sans modifier la géométrie du chemin.
La lecture/écriture effective des propriétés et la prévisualisation
multi-propriétés restent des étapes ultérieures pour les composants qui ne sont
pas encore rendus par le runtime.
