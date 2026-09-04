# ADR-0126 — Sélection, collage et snapping des clés d’animation

## Statut

Accepté — 2026-08-26

## Décision

Asset Studio conserve la sélection des clés dans l’état d’interface, en
référençant chaque clé par son `PropertyBinding` et son index de piste. Une
copie conserve la valeur, le binding, l’interpolation et la composition de
chaque clé sélectionnée.

Le collage aligne la clé la plus ancienne sur le temps demandé, conserve les
écarts relatifs entre clés et repasse chaque écriture par
`ProjectSession::set_selected_animation_key`, donc par validation,
`CommandStack`, dirty state et autosave. Les temps peuvent être arrondis à un
intervalle positif configurable avant l’écriture.

Le déplacement d'un groupe applique un delta temporel commun à toutes les clés
sélectionnées dans une seule commande. L'opération est refusée en entier si une
référence est périmée, dupliquée ou si une clé sortirait de la durée du clip ;
undo/redo ne peut donc jamais laisser un groupe partiellement déplacé.

La mise à l'échelle temporelle d'un groupe suit la même règle atomique. Elle
utilise le playhead comme pivot et un facteur strictement positif ; chaque
temps devient `pivot + (temps - pivot) × facteur`. L'opération entière est
refusée si le pivot ou le facteur est invalide, si une référence est périmée
ou si une clé sortirait du clip. Dans la timeline, `Alt` + glisser une clé d'une
sélection calcule ce facteur depuis la clé manipulée ; le glisser sans
modificateur conserve la translation commune.
Un clic dans la règle déplace le playhead sans vider la sélection, afin que le
pivot puisse être posé après un box-select sans reconstruire le groupe.

Les bindings visuels de base restent explicites dans le clip : `material/color`
et `material/opacity` sont conservés, tandis que l’éditeur expose aussi
`fill/color` et `imageFill/opacity` pour cibler directement les paquets de
rendu correspondants.

## Conséquences

- Les sélections invalidées par une réorganisation de piste sont ignorées au
  collage plutôt que d’écrire dans une autre piste.
- Le clipboard reste local à la session Asset Studio et n’ajoute aucun champ
  au format `AnimationClip`.
- Le schéma d’animation v3 persiste un easing par piste et des tangentes
  entrante et sortante optionnelles par clé. Les tangentes sont des valeurs du
  même type que la clé et ne sont évaluées que pour une interpolation cubique ;
  sans tangentes, le comportement cubique historique est conservé.
