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

`TexturedPath v1` décrit un chemin ouvert ou fermé, une largeur éventuellement
variable, une texture, un mode `repeat` ou `stretch`, une échelle et un offset
UV, une couleur, une opacité et des raccords. Le renderer dérive un ruban
triangulé et des UV continus depuis le chemin ; cette géométrie est un cache de
rendu et ne remplace pas le contrat éditable.

Le premier exemple est un Beam à deux attaches, courbe optionnelle, largeur,
texture animée, couleur et opacité. Une collision peut le référencer mais n'est
jamais créée implicitement.

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
