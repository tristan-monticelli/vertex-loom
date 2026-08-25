# ADR-0047 — MapDocument v1 et indexation par chunks

## Statut

Accepté.

## Décision

`MapDocument v1` est stocké sous `maps/<id>.map.json`. Il sépare calques
visuels, tiles, instances, collisions, triggers et gameplay. Les instances
portent leur chunk calculé dans une grille fixe de `64 × 64` unités ; une
coordonnée incohérente est refusée par le validateur.

Les collisions supportent cercle, capsule, polygone, chaîne et capteur. Les
triggers référencent une forme et un événement stable. Les propriétés
personnalisées sont limitées à booléen, entier, réel, texte, `Vec2` et
référence de ressource.

L’index runtime `MapChunkIndex` trie les instances par chunk puis identifiant,
utilise `floor` pour les coordonnées négatives et retourne uniquement les
instances dont la position tombe dans le viewport demandé.
