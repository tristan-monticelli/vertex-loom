# ADR-0110 — Transitions uniformes des documents Studio

## Statut

Accepté — 2026-08-26.

## Contexte

Asset Studio protège déjà le remplacement du projet et sa fermeture, mais une
sélection ou une création de ressource dirty est encore refusée directement.
Map Studio ferme immédiatement sa map et sa mécanique lorsque SDL demande la
fin de l'application. L'autosave réduit le risque sans remplacer une décision
explicite du créateur.

## Décision

Toutes les coquilles utilisent `SessionTransitionGuard` pour les actions qui
remplacent un projet ou ferment un document actif. La garde reçoit l'intention,
agrège l'état dirty de tous les documents concernés et produit exactement une
des décisions suivantes :

- `Save and continue` sauvegarde chaque document dirty et continue uniquement
  si toutes les sauvegardes réussissent ;
- `Discard` continue sans modifier les fichiers principaux ;
- `Cancel` conserve les documents, leurs historiques et la sélection active.

La modale nomme chaque document dirty et son type. Un échec de sauvegarde garde
la modale ouverte, conserve les états en mémoire et expose les diagnostics de la
session en erreur. Fermer la fenêtre, utiliser le raccourci système, changer de
ressource, créer, importer, ouvrir un projet ou créer un projet empruntent la
même politique.

À l'intérieur d'un même projet, sélectionner, créer ou importer une ressource
utilise `ProjectSession::save_before_document_transition`. La session tente la
sauvegarde du document courant uniquement lorsque la nouvelle intention est
déjà localement valide, continue automatiquement après succès et conserve le
document actif ainsi que ses diagnostics après échec. Cette voie évite une
modale sur chaque navigation normale ; l'interface peut proposer Retry,
Discard ou Cancel si l'échec nécessite une décision humaine.

Map Studio sauvegarde d'abord la mécanique ouverte, puis la map qui peut la
référencer. La fermeture n'est autorisée qu'après les deux succès. Cette
séquence ne constitue pas une transaction multi-fichier : un premier document
déjà sauvegardé reste valide si le second échoue, tandis que le second reste
dirty et la fenêtre ouverte.

## Conséquences

- Aucun événement SDL ne ferme directement une coquille avec un document dirty.
- L'autosave reste un mécanisme de récupération et non une confirmation de
  fermeture.
- Les refus dispersés fondés sur `commands_.dirty()` doivent être remplacés
  progressivement par cette transition avant de fermer le gate UX.
- Les décisions de garde restent testées headless ; les modales nécessitent en
  plus un parcours end-to-end graphique.
