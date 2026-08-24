# ADR-0020 — Historique d’édition et récupération locale

- Status: accepted
- Date: 2026-08-24

## Context

Les futurs documents d’entité, d’animation et de map doivent partager un
historique réversible et survivre à une interruption sans confondre sauvegarde
explicite et récupération de secours. Les imports PNG et SVG ont un autre
contrat : ils publient une nouvelle ressource et ne remplacent jamais un asset
existant.

## Decision

`fabric_editor` fournit un `CommandStack` sans dépendance graphique. Une
commande applique, annule et réapplique une modification. Elle peut absorber
la commande continue suivante lorsqu’elles ciblent la même propriété. La pile
conserve un point propre explicite ; toute branche qui abandonne un redo
invalide ce point lorsqu’il n’est plus atteignable.

`fabric_project` fournit une écriture atomique générique pour les documents
éditables déjà validés par leur parseur. Le fichier temporaire est adjacent à
la destination et le remplace seulement après une écriture complète. La
publication d’import reste séparée et sans remplacement.

`fabric_editor` planifie un autosave après deux secondes sans modification et
au plus trente secondes après la première modification non sauvegardée. Les
autosaves utilisent le même chemin relatif sous
`.vertex-loom/autosave/` et le même validateur que le document principal.

À l’ouverture, une récupération est disponible uniquement si l’autosave est
valide et strictement plus récent que le document principal. L’accepter charge
son contenu en mémoire et marque le document dirty. La refuser conserve le
document principal. Aucune décision ne remplace automatiquement le fichier
principal.

## Alternatives

Un historique basé sur des snapshots complets simplifierait l’annulation mais
augmenterait rapidement la mémoire des maps et maillages. Écrire l’autosave à
côté du document principal mélangerait données de secours et ressources
versionnées. Restaurer automatiquement risquerait de remplacer une sauvegarde
intentionnelle.

## Consequences

Chaque mutation d’un document éditable doit être exprimée par une commande et
testée dans les deux sens. Chaque type de document fournit son validateur à la
sauvegarde et à la récupération. Les anciens autosaves deviennent simplement
inactifs après une sauvegarde principale plus récente ; leur nettoyage sera
une action utilisateur explicite ultérieure.
