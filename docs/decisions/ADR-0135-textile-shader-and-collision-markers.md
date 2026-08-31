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

Le renderer doit interpréter le profil, pas seulement le sérialiser. `Thread`
recolore selon la luminance tout en conservant les détails de l'image et ajoute
une bande iridescente dépendante des UV. `Plastic` conserve la couleur source
avec un reflet concentré, `Monochrome` applique la couleur principale à la
luminance et `Custom` mélange les deux couleurs selon les UV et la luminance.
La brillance est un reflet localisé ; elle ne peut pas être une addition blanche
uniforme. Une holographie à `1` ne doit jamais remplacer toute la texture par
une couleur constante.

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

Le smoke OpenGL rend un motif asymétrique avec le profil Thread, compare deux
pixels issus de texels différents et écrit `fabric-render-shader-smoke.ppm`.
Cette preuve échoue si l'holographie aplatit de nouveau l'image.
