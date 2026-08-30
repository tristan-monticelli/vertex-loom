# ADR-0105 — Composants paramétriques et chemins texturés

## Statut

Accepté — 2026-08-25.

## Contexte

L'esthétique textile demande des éléments récurrents comme les yeux, boutons,
fermetures, coutures, fils et bordures. Les construire comme des images
aplaties duplique les assets et empêche leur animation ou leur adaptation à
une forme.

## Décision

`VisualComponent v1` décrit un composant paramétrique réutilisable : variante,
drawables, points d'ancrage, propriétés typées, propriétés animables et bounds
de preview. Une instance conserve ses paramètres, son ancrage, son transform et
son ordre Z. Les premiers presets sont œil, bouton, fermeture éclair et couture.
Une fermeture éclair compose deux rails `TexturedPath`, une répétition de dents
et un curseur ancré à une progression commune ; elle ne devient pas une forme
vectorielle monolithique.

Le document est stocké sous
`assets/components/<id>.component.json` et référence une
`VisualComposition v1` interne qui reste l'unique description de ses
drawables. Ses bounds sont un `Rect` en unités locales. Chaque ancrage possède
un identifiant stable et une position locale finie.

Un paramètre déclare un type parmi scalaire, angle, entier, booléen, texte,
`Vec2`, couleur et référence de ressource, une valeur par défaut, un binding
cible dans la composition interne et son caractère animable. Entier et texte
restent éditables mais non animables dans v1, car `AnimationValue v1` ne sait
pas les interpoler. Les autres paramètres animables sont exposés au
`PropertyDescriptorRegistry` ; aucune piste spéciale n'est ajoutée.
Le registre étend donc ses genres de valeur avec `integer` et `text`, tout en
les excluant de `animatable()` pour ce contrat v1.

Une variante est un ensemble nommé d'overrides typés. Une instance de composant
dans un calque `VisualComposition` choisit éventuellement une variante, un
ancrage du composant et ses propres overrides. L'ordre de résolution est valeur
par défaut, variante, puis instance. La validation headless vérifie la
composition interne, les bindings de calques, les types d'override, les
variantes, les ancrages et les cycles du graphe de ressources.

`TexturedPath v1` décrit un chemin ouvert ou fermé, une largeur éventuellement
variable, une texture, un mode `repeat` ou `stretch`, une échelle et un offset
UV, une couleur, une opacité et des raccords. Le renderer dérive un ruban
triangulé et des UV continus depuis le chemin ; cette géométrie est un cache de
rendu et ne remplace pas le contrat éditable.

Le document est stocké sous
`assets/paths/<id>.textured-path.json`. Son chemin commence par un unique
`move`, suivi de commandes `line` ou `cubic`; la fermeture est un booléen du
document. La largeur de base est positive. Un profil optionnel contient des
clés strictement croissantes sur la distance normalisée `[0,1]`, avec une clé
aux deux extrémités et des largeurs positives.

Les modes UV sont `repeat` et `stretch`. Échelle UV, offset, couleur, opacité,
limite de miter, raccord `miter|round|bevel` et terminaison
`butt|round|square` sont persistés et validés. La seule dépendance visuelle
obligatoire est un `TextureAsset`. Aucun champ de collision ni aucun mesh
triangulé n'est persisté dans `TexturedPath v1`.

La tessellation aplatit les cubiques avec une tolérance explicite, calcule la
distance cumulée sur la ligne centrale, interpole le profil de largeur et émet
deux sommets par section, complétés par des éventails pour les caps ronds. Les
UV en répétition utilisent cette distance ;
les UV en étirement utilisent sa normalisation globale. `uvScale.x` multiplie
la coordonnée longitudinale et `uvScale.y` la coordonnée transversale ; le draw
packet demande `GL_REPEAT` uniquement sur l'axe longitudinal en mode repeat.
Pour un chemin fermé, la couture UV reste continue jusqu'à une paire de sommets
dupliquée à la fin. La couleur est une teinte du fragment texturé et l'opacité
reste un multiplicateur séparé.

Le premier exemple est un Beam à deux attaches, courbe optionnelle, largeur,
texture animée, couleur et opacité. Une collision peut le référencer mais n'est
jamais créée implicitement.

La bibliothèque initiale de presets est une factory d'authoring déterministe.
Œil et bouton assemblent des formes vectorielles natives. Couture assemble un
rail `TexturedPath`. Fermeture assemble deux rails, des calques répétés d'une
même dent vectorielle et un curseur vectoriel. La position du curseur est un
paramètre `Vec2` de transform ; le suivi scalaire d'un rail ne sera ajouté que
par une contrainte de composition générique et non dans le renderer.

## Alternatives

Un mesh persisté dupliquerait une donnée dérivable et compliquerait l'édition
de la courbe. Une texture aplatie empêcherait les variations de longueur,
l'animation UV et les attaches. Le chemin éditable et le ruban dérivé restent
donc séparés.

## Conséquences

- Asset Studio édite les composants et le chemin avec preview réelle.
- Map Studio instancie et paramètre les mêmes composants.
- Les propriétés déclarées animables passent par le registre existant.
- Coutures, cordes, câbles, rails et effets lumineux réutilisent le même
  pipeline de chemin au lieu d'ajouter un renderer par effet.
