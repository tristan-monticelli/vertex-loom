# Validation d’utilisabilité — Vertex Loom — 2026-09-05

## Objectif

Vérifier qu’un créateur peut produire et corriger un contenu réel dans les
Studios sans connaître les fichiers JSON, les identifiants internes ni les
contrats C++.

Cette checklist complète l’audit mécanique et ne transforme pas les tests
automatisés en preuve d’utilisabilité humaine.

## Protocole

- Recruter au moins cinq personnes : trois débutants d’un moteur 2D et deux
  utilisateurs expérimentés de Godot, Unity, Construct ou GameMaker.
- Fournir le même projet vierge, la même fixture d’assets et la même version
  du binaire.
- Ne pas fournir de tutoriel Vertex Loom ; expliquer uniquement la consigne,
  puis observer sans guider.
- Enregistrer l’écran, les clics, les raccourcis, les changements de panneau,
  les erreurs et les demandes d’aide.
- Réinitialiser le projet entre les participants et conserver la sauvegarde
  produite par chacun.
- Arrêter une tâche après 15 minutes ou après trois erreurs bloquantes.

## Tâches observées

| ID | Consigne donnée à l’utilisateur | Résultat attendu | Preuve à conserver |
| --- | --- | --- | --- |
| U01 | Importer une texture, créer un Beam et conserver ses couleurs d’origine | Beam créé sans teinte implicite, aperçu visible | capture Studio + ressource rechargée |
| U02 | Composer une Entity avec deux visuels, renommer un enfant et le déplacer | arbre, canvas et Inspector restent synchronisés | capture avant/après + fichier sauvegardé |
| U03 | Créer une animation, poser deux clés, lire puis corriger la seconde clé | timeline lisible, correction visible dans Preview | capture timeline + clip rechargé |
| U04 | Placer l’Entity dans une Map, choisir un rail, créer puis retirer l’animation de chemin | rail, curseur, bouton d’attachement et bouton de retrait compréhensibles | capture Map + map rechargée + paquet |
| U05 | Ajouter un comportement, le prévisualiser, publier puis relancer le paquet | comportement observable et paquet autonome | capture Preview + manifeste publié |

## Mesures par tâche

Reporter pour chaque participant et chaque tâche :

- temps jusqu’au premier résultat visible ;
- temps total et nombre d’étapes ;
- erreurs récupérées et erreurs bloquantes ;
- changements de panneau ou recherches infructueuses ;
- demandes d’aide et consultation éventuelle du JSON ;
- réussite après sauvegarde et rechargement ;
- confiance déclarée de 1 à 5.

## Critères d’acceptation

La tâche est acceptée si l’utilisateur :

- la termine en moins de 15 minutes sans aide ;
- ne modifie aucun JSON à la main ;
- obtient le résultat attendu après rechargement ;
- peut annuler puis refaire l’action principale ;
- sait expliquer où il se trouve et quelle est l’action suivante.

Le parcours est accepté quand au moins quatre participants sur cinq réussissent
chaque tâche, qu’aucune tâche ne dépasse deux erreurs bloquantes cumulées et
que le résultat Studio → Preview → paquet reste identique.

## Fiche de session

| Participant | Expérience moteur | U01 | U02 | U03 | U04 | U05 | Aide demandée | JSON consulté | Observations |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| P01 | à remplir | — | — | — | — | — | — | — | — |
| P02 | à remplir | — | — | — | — | — | — | — | — |
| P03 | à remplir | — | — | — | — | — | — | — | — |
| P04 | à remplir | — | — | — | — | — | — | — | — |
| P05 | à remplir | — | — | — | — | — | — | — | — |

## État actuel

- Les tests automatisés couvrent les contrats et les parcours E2E graphiques
  disponibles, mais aucune session humaine n’est encore enregistrée.
- U01, U02, U03 et U05 possèdent déjà des preuves automatisées partielles ou
  complètes.
- U04 dispose maintenant d’un parcours nominal d’attachement et de retrait,
  mais son test utilisateur reste à exécuter.
- Tant que la fiche n’est pas remplie, l’utilisabilité reste `non prouvée` dans
  le tableau maître de l’audit.

## Preuve automatisée disponible

Le sous-ensemble E2E correspondant aux tâches a été exécuté le 5 septembre
2026 : 9/9 tests passent en 45,08 s.

| Tâche | Tests associés | Niveau de preuve |
| --- | --- | --- |
| U01 | `asset_studio_ui_beam_e2e`, `asset_studio_ui_button_e2e` | UI et persistance de fixture |
| U02 | `asset_studio_entity_e2e`, `asset_studio_entity_animation_workflow_e2e` | composition, sélection et rechargement |
| U03 | `asset_studio_animation_e2e`, `asset_studio_entity_animation_workflow_e2e` | timeline, clés et runtime |
| U04 | `map_studio_placement_e2e`, `map_studio_close_e2e` | placement et fermeture/rechargement ; PathFollower reste à exercer par geste dédié |
| U05 | `asset_studio_behavior_e2e`, `package_install_smoke` | comportement et paquet autonome |

Cette exécution confirme la non-régression des parcours automatisés ; elle ne
remplit pas la fiche P01–P05 et ne remplace donc pas les sessions humaines.

## Décision après sessions

Pour chaque échec, classer la cause avant de corriger :

1. libellé ou affordance invisible ;
2. contexte perdu entre panneaux ;
3. modèle mental incompatible avec les moteurs comparés ;
4. validation ou récupération insuffisante ;
5. capacité réellement absente.

Corriger d’abord les erreurs observées par au moins deux participants, puis
rejouer uniquement les tâches concernées et mettre à jour le tableau maître.
