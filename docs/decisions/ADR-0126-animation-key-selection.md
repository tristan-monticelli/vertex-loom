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

Les bindings visuels de base restent explicites dans le clip : `material/color`
et `material/opacity` sont conservés, tandis que l’éditeur expose aussi
`fill/color` et `imageFill/opacity` pour cibler directement les paquets de
rendu correspondants.

## Conséquences

- Les sélections invalidées par une réorganisation de piste sont ignorées au
  collage plutôt que d’écrire dans une autre piste.
- Le clipboard reste local à la session Asset Studio et n’ajoute aucun champ
  au format `AnimationClip`.
- Les tangentes et l’easing restent un contrat distinct à décider avant leur
  persistance.
