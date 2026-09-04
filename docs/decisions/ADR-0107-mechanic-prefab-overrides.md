# ADR-0107 — Mécaniques paramétrées par prefab

## Statut

Accepté — 2026-08-26.

## Contexte

Un `MechanicGraph` peut déclarer des paramètres typés, mais un prefab de map
ne conserve actuellement qu'une entité visuelle et des propriétés génériques.
La plateforme tournante ne peut donc pas être instanciée avec des vitesses,
limites ou zones de présence différentes sans dupliquer ou modifier son graphe.

## Décision

`PrefabDefinition` conserve une référence `mechanic` optionnelle de type
`mechanic` et une liste `mechanicOverrides` distincte. Chaque override cible
l'identifiant stable d'un `MechanicParameterDefinition` et porte exactement un
`MechanicValue`. Les anciens prefabs sans ces deux champs restent des prefabs
visuels MapDocument v1 valides ; le sérialiseur omet les champs mécaniques
lorsqu'ils sont absents.

La validation locale refuse identifiants dupliqués, valeurs non finies,
références mal typées et overrides sans mécanique. La validation du projet
charge le graphe référencé puis refuse tout paramètre inconnu ou valeur dont le
type diffère du paramètre. Les références contenues dans les valeurs rejoignent
le graphe global de ressources.

La compilation d'une instance copie le graphe, remplace les valeurs par défaut
des paramètres ciblés, puis applique le `Transform` de l'instance au plan
physique compilé. Translation, rotation et échelle uniforme positive déplacent
ensemble corps, pivots et capteurs ; les échelles non uniformes ou négatives
sont refusées car elles introduiraient du cisaillement ou une réflexion que les
formes physiques v1 ne représentent pas. Cette résolution ne modifie ni le
document `MechanicGraph` ni son fichier. Map Studio crée le prefab, édite les
overrides par commandes undoables et ouvre la preview de son instance avec les
valeurs effectives du prefab.

Le canvas Map peut manipuler directement une forme de l'overlay uniquement
lorsque la propriété spatiale correspondante (`position`, `center`, `size` ou
`rotation`) est publiée comme paramètre du graphe. Le geste convertit la valeur
monde vers l'espace local de l'instance, écrit un `mechanicOverride` du prefab
via le `CommandStack` de `MapSession`, puis recharge la preview de cette même
instance. L'interface nomme explicitement la portée prefab : la modification
s'applique à toutes ses instances. Une propriété non paramétrée reste en lecture
seule dans la Map et s'édite dans le document Mechanics ; le canvas ne modifie
jamais implicitement le graphe partagé.
À chaque frame Map, `MechanicSession` compare aussi le transform et les
overrides mémorisés au document courant. Elle ne recompile que s'ils ont changé,
ce qui garde l'overlay aligné après Undo/Redo ou déplacement d'instance sans
relire le graphe ni perdre un authoring Mechanics non sauvegardé.
La Map est une surface d'authoring : si la preview avait avancé dans Mechanics,
elle est pausée et remise au pas zéro avant d'exposer ses poignées. La pose
physique live reste observable dans Mechanics et Preview, mais ne devient jamais
accidentellement une nouvelle valeur persistée.

Un double-clic sur une forme, paramétrée ou protégée, demande au shell d'ouvrir
le document Mechanics déjà résolu et de sélectionner son nœud exact. Le shell
enregistre d'abord le contexte Map dans `EditorContext`; Retour restaure donc
la même map et la même instance sans nouvelle recherche.

## Conséquences

- Un même graphe mécanique peut servir à plusieurs prefabs configurés.
- Le visuel, la collision et la mécanique restent trois données distinctes.
- Les overrides invalides sont refusés avant toute création Box2D.
- Preview Runtime utilise la même résolution d'overrides et le même transform
  d'instance que la preview de Map Studio.
- Undo/Redo Map couvre les manipulations d'overlay paramétrées sans mélanger
  l'historique du document Mechanics.
