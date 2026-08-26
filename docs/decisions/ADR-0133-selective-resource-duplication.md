# ADR-0133 — Duplication sélective des dépendances

- Statut : accepté
- Date : 2026-08-26

## Contexte

La duplication générique créait un nouveau document, mais conservait toutes ses
références vers les mêmes ressources. Cela ne permettait pas de choisir quelles
dépendances devaient devenir indépendantes.

## Décision

`ProjectSession::duplicate_resource` reçoit une liste optionnelle de
`ResourceDuplicationDependency`. Chaque entrée décrit le type, l’identifiant
source, l’identifiant destination et le nom de la copie. Les dépendances sont
dupliquées avant le document principal. Les documents `EntityDefinition` et
`VectorAsset` réécrivent leurs références structurées ; les autres documents
passant par le duplicateur générique utilisent le même fallback JSON typé. Dans
les deux cas, seules les références sélectionnées et de même `expected_type`
sont réécrites.

Une dépendance invalide ou cyclique avec la ressource principale est rejetée
avant la publication de la copie principale ; les erreurs de publication sont
retournées par le même contrat que les duplications classiques.

## Conséquences

- La copie superficielle reste le comportement par défaut.
- Une copie profonde partielle est déterministe et utilise le même garde de
  transition que les duplications classiques.
- L’interface propose la sélection visuelle des dépendances détectées dans le
  document JSON ; les types sans référence affichent explicitement qu’il n’y a
  rien à cloner.
