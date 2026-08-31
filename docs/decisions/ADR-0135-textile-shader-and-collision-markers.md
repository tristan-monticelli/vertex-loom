# ADR-0135 — Profils shader textiles et marqueurs de collision

- Status: accepted
- Date: 2026-08-31

## Context

Les paths texturés savent référencer une texture mais ne décrivent pas encore
les profils Thread/Plastic, les deux couleurs d'effet ni les paramètres de
shader animables. Les collisions sont persistées comme formes physiques seules,
ce qui empêche un marquage cohérent dans les studios et le runtime.

## Decision

Ajouter un profil de surface partagé (`Thread`, `Plastic`, `Monochrome` ou
`Custom`) aux paths texturés et aux strokes. Le profil porte la classification
de texture, deux couleurs, brillance, holographie, opacité, intensité,
répétition et déformation ; les valeurs restent des propriétés ordinaires afin
que les tracks d'animation existants puissent les cibler.

Le manifeste du projet porte aussi une référence optionnelle
`defaultStrokeTexture`. La première texture importée initialise cette valeur
si elle est absente ; elle devient la texture de base commune aux nouveaux
strokes et presets. Les autres textures restent des variantes sélectionnables
dans le combo du Studio et ne remplacent pas silencieusement cette valeur.

Ajouter à chaque `MapDocument` une configuration de collision par surface,
avec activation, texture facultative, apparence, orientation, répétition,
offset et visibilité Studio/runtime. Une configuration d'objet optionnelle
remplace la configuration de surface. La géométrie des huit traits est dérivée
à l'affichage et n'est jamais persistée.

## Consequences

Les documents restent déterministes et rétrocompatibles par défaut. Asset
Studio, Map Studio et Preview Runtime consomment les mêmes paramètres. Le
renderer peut ignorer les effets avancés sur un backend legacy, mais conserve
la couleur et l'opacité ; aucune logique de test ne doit injecter la texture
du beam.
