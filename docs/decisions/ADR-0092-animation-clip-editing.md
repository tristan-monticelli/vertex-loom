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
sans modifier le document. Les markers peuvent aussi être ajoutés ou supprimés
depuis l’inspecteur.

L’action `Set key` est idempotente pour un binding et un temps donnés : elle
crée la clé si elle n’existe pas et remplace sa valeur si elle existe déjà.
Cette opération passe par le `CommandStack`, donc un remplacement de clé est
annulable en une seule commande.

Chaque track porte aussi une composition `replace` ou `additive`, persistée
dans le JSON. Les documents v1 qui n’ont pas ce champ restent `replace` par
défaut. Le runtime applique les tracks additives comme des offsets sur les
transformations de base (position, rotation et échelle), dans l’ordre stable
des tracks. Les propriétés matériau `color` et `opacity` suivent la même règle
de composition lors du rendu des draw packets. Les tracks
`transform/rotationDegrees` suivent toujours le chemin angulaire le plus
court, y compris lorsqu’elles franchissent 0°/360°.

Asset Studio propose un mode `Auto-key at scrub time`. Lorsqu’il est actif,
toute modification de la valeur de clé dans l’inspecteur appelle `Set key` au
temps de scrubbing courant ; le mode reste désactivable et le geste conserve
les mêmes garanties d’undo/redo et de validation.

`ProjectSession` orchestre `AnimationTimeline` avec le même `CommandStack`,
dirty state, autosave miroir, récupération validée et publication atomique que
les documents vectoriels et d’entité.

## Conséquences

- Un clip créé peut recevoir des données d’animation sans modifier son JSON à
  la main.
- Le même geste peut être rejoué pendant une édition continue sans créer de
  clés concurrentes au même instant.
- Les offsets d’animation sont explicites et ne sont jamais déduits de la
  valeur de la clé.
- Les bindings restent génériques et ne dépendent pas encore d’un type précis
  d’entité ou de matériau.
- Le scrubbing visuel, l’édition de tangentes et les pistes booléennes/couleur/
  référence restent des incréments de timeline ultérieurs.
