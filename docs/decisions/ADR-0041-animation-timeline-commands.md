# ADR-0041 — Édition de timeline par commandes

## Statut

Accepté.

## Décision

La timeline headless modifie un `AnimationClip` par snapshots validés soumis
au `CommandStack`. L’insertion, le déplacement, la suppression de clés et la
modification de durée/boucle sont donc réversibles par undo/redo et suivent le
même état dirty que les autres mutations de l’éditeur.

Les clés restent triées par temps. Une piste conserve un type de valeur stable,
et une piste ne peut pas être vidée par suppression de sa dernière clé.
L’interface graphique sera ajoutée au-dessus de cette API sans logique de
persistance spécifique.

Le workspace graphique utilise ces mêmes snapshots pour l'auto-key depuis le
gizmo au playhead, la sélection rectangle et l'édition interpolation/easing de
la piste. La vue de courbe échantillonne l'évaluateur partagé ; elle ne possède
pas une seconde fonction d'interpolation.
