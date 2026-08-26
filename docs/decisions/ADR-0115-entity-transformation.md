# ADR-0115 — EntityTransformation v1 atomique

## Statut

Accepté — 2026-08-26.

## Contexte

Un BehaviorGraph peut demander une transformation, mais remplacer directement
une référence d'entité ne précise pas quels états survivent. Une mutation en
plusieurs temps produirait aussi une frame sans visuel, collision ou logique
valide.

## Décision

`EntityTransformation v1` est une ressource réutilisable référencée par l'action
`transform_entity`. Elle déclare explicitement une entité source et une entité
destination ainsi qu'une politique pour transform monde, identifiant
d'instance, couche/Z, vitesse physique, propriétés compatibles, paramètres de
comportement, animation/temps, cooldowns/timers et suivi caméra.

Chaque domaine choisit `preserve`, `reset`, `mapping` ou `error` parmi les
valeurs autorisées pour ce domaine. `mapping` exige des couples source/cible
uniques. Une valeur incompatible applique la stratégie `reset` ou provoque une
erreur avant mutation ; elle n'est jamais convertie implicitement.

Le runtime prépare d'abord l'entité destination, ses ressources, son
BehaviorGraph, son état visuel et sa physique dans un candidat hors monde. Il
applique ensuite la politique et remplace l'état de l'instance en une seule
validation/affectation. L'identifiant peut être préservé ou régénéré de manière
déterministe. Il n'existe donc aucune frame intermédiaire sans entité valide.

La référence source sert de précondition de validation, mais n'est pas une
dépendance de packaging sortante : la source est nécessairement déjà dans la
fermeture qui atteint la transformation. La destination est une dépendance.
Cela évite le faux cycle structurel source → behavior → transformation → source.
Les vrais cycles de chaînes A→B→A restent détectés par la fermeture via les
behaviors des destinations et sont refusés en v1.

## Conséquences

- Les transformations réversibles doivent être deux ressources distinctes ;
  elles ne peuvent pas former un cycle automatique en v1.
- Collision et mécanique sont reconstruites depuis la destination lorsque la
  conservation n'est pas explicitement compatible.
- Replay enregistre la demande sémantique ; le résultat reste déterministe car
  politique, ordre des mappings et identifiants sont persistés.
- Asset Studio possède le formulaire et les pickers source/destination ; Map
  Studio prévisualise l'opération sur l'instance sélectionnée.
