# ADR-0092 — Première édition des clips d’animation

- Statut : accepté
- Date : 2026-08-25

## Décision

Asset Studio édite les `AnimationClip v1` par binding générique
`node/component/property`. La première tranche expose la durée, la boucle et
l’insertion de clés `Vec2` avec interpolation step, linear ou cubic. Les
tracks sont créées à la première clé et doivent conserver le type de valeur du
track. Une clé existante peut être supprimée depuis la liste de tracks, sauf si
elle est la dernière clé de la track, ou déplacée dans la durée du clip. Les
déplacements continus fusionnent leurs commandes ; le scrubber évalue le clip
sans modifier le document.

`ProjectSession` orchestre `AnimationTimeline` avec le même `CommandStack`,
dirty state, autosave miroir, récupération validée et publication atomique que
les documents vectoriels et d’entité.

## Conséquences

- Un clip créé peut recevoir des données d’animation sans modifier son JSON à
  la main.
- Les bindings restent génériques et ne dépendent pas encore d’un type précis
  d’entité ou de matériau.
- Le scrubbing visuel, l’édition de tangentes et les pistes booléennes/couleur/
  référence restent des incréments de timeline ultérieurs.
