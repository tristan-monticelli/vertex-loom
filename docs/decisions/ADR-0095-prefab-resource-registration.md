# ADR-0095 — Enregistrement des prefabs dans le graphe de ressources

## Statut

Accepté.

## Contexte

Les prefabs sont définis inline dans `MapDocument v1`, mais les instances les
référencent comme des ressources de type `prefab`. Sans entrée correspondante,
le validateur global signale à tort une référence manquante et le runtime ne
peut pas charger une map utilisant un prefab.

## Décision

Lors du chargement headless du projet, chaque `PrefabDefinition` de chaque map
est enregistré comme une entrée logique de type `prefab`, avec l’identifiant du
prefab, le chemin de la map comme provenance et une référence vers son entité.
Les overrides sont validés par `MapDocument`, et les références d’instances
vers ces prefabs sont ensuite résolues par `ResourceRegistry`.

Le runtime fusionne les overrides du prefab avec les propriétés locales de
l’instance, ces dernières étant prioritaires. La propriété réservée `animation`
est ainsi transmissible par prefab sans dupliquer la référence sur chaque
instance.

## Conséquences

- Les références prefab sont vérifiées comme les autres ressources du projet.
- Les identifiants de prefab doivent être uniques dans le graphe chargé.
- Les propriétés locales conservent la priorité sur les overrides hérités.
- Aucun fichier prefab supplémentaire n’est créé sur disque dans cette version.
