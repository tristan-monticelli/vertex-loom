# ADR-0074 — Duplication et ordre des instances de map

## Décision

`MapSession::duplicate_instance` copie une instance déverrouillée, lui attribue
un identifiant déterministe (`<id>-copy`, puis suffixe numérique), applique un
offset et recalcule son chunk. `reorder_instance` déplace une instance dans
l’ordre stable du document, sans modifier son identifiant ou ses transforms.

Les deux opérations sont des snapshots undoables et refusent les instances
d’un calque verrouillé. Map Studio expose la duplication et le déplacement de
l’instance sélectionnée en tête de liste.

## Conséquences

Les opérations de composition de base sont reproductibles hors interface. Le
réordonnancement par calque/Z explicite et la duplication multi-sélection restent
à compléter.
