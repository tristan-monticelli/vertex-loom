# ADR-0073 — Sélection multiple et verrous effectifs de map

## Décision

`MapSession::translate_instances` applique un déplacement à plusieurs
instances dans une seule commande snapshot. Les identifiants doivent être
uniques, les positions restent finies et le snapping partagé s’applique à
chaque position finale.

Une instance dont le calque est verrouillé ne peut pas être placée, supprimée,
transformée, personnalisée ou déplacée en groupe. La sélection d’interface
reste une liste temporaire d’identifiants ; elle n’est pas persistée dans le
document.

## Conséquences

Les manipulations groupées sont undoables atomiquement et les verrous de
calques ont un effet réel. Les gizmos de sélection multiple, duplication et
réordonnancement restent à ajouter.
