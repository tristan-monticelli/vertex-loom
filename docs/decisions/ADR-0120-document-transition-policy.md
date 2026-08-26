# ADR-0120 — Politique de transition du document actif

## Statut

Accepté.

## Décision

Toute opération qui change le document actif passe par la sauvegarde atomique
du document courant (`ProjectSession::save_before_document_transition`). Une
validation échouée ou une écriture impossible laisse la sélection et
`CommandStack` inchangés. Les opérations récupérables restent annulables.

## Portée

La politique couvre sélection, création, import, duplication et ouverture de
ressources dans Asset Studio, ainsi que les sessions Map/Mechanic/Scene.
