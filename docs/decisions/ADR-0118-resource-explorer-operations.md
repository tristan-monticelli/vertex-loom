# ADR-0118 — Opérations sûres du Resource Explorer

- Status: accepted
- Date: 2026-08-26

## Decision

Le Resource Explorer présente dans un même index les assets, entités, maps,
scènes, mécaniques et replays du projet. Une ressource est identifiée par son
type, son identifiant stable et son chemin de document relatif au projet.

La duplication est superficielle par défaut : elle attribue un nouvel
identifiant et un nouveau chemin au document, mais conserve ses références vers
les dépendances partagées. Une duplication profonde doit énumérer les
dépendances choisies et ne réécrire que ces références.

Le renommage visible modifie `name` sans changer l'identifiant ni le chemin. Le
changement d'identifiant est une opération distincte qui exige la réécriture
validée des références entrantes.

Avant une suppression, l'explorateur construit la liste des références
entrantes. Une ressource référencée ne peut pas être supprimée sans stratégie
explicite de remplacement ou de cascade. Toute suppression demande une
confirmation finale et ne supprime jamais une source externe ou partagée. Les
opérations de registre récupérables conservent les octets nécessaires à leur
annulation jusqu'à la sauvegarde ou la fermeture du projet.

## Consequences

Les pickers et les Studios consomment le même index typé et recherchable. Les
actions `Copy ID`, `Copy path` et `Reveal` n'altèrent pas le projet. Les
opérations mutables sont validées avant publication et ne laissent jamais un
index partiellement mis à jour.
