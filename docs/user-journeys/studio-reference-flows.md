# Parcours de référence du Studio

Chaque parcours conserve le document actif tant que la transition n'est pas
résolue. Une erreur est affichée au champ concerné ; `Cancel` conserve document
et historique, `Retry` recommence l'écriture, `Discard` exige une confirmation.

| Parcours | Entrée | Sortie observable | Échecs et annulation | Succès |
| --- | --- | --- | --- | --- |
| Sélection interne | Référence typée + recherche | Ressource choisie et ouvrable dans l'explorateur | type absent, ressource manquante, Cancel | aucun ID saisi manuellement |
| Créer | document actif clean/dirty | nouvelle ressource sélectionnée | validation/écriture ; Retry, Discard, Cancel | ancien document sauvegardé atomiquement |
| Dupliquer | ressource + dépendances choisies | nouvel ID et chemin, références choisies réécrites | collision, cycle, écriture ; annulation sans publication | copie rechargeable et originale intacte |
| Supprimer | ressource + analyse entrante | suppression ou remplacement explicite | références bloquantes ; Cancel par défaut | aucune référence cassée, source partagée conservée |
| Entité multi-artworks | entité + ressources visuelles | arbre de nœuds combinant texture/vector/component | incompatibilité signalée avant mutation | Add/Replace/Clear restent éditables après création |
| Animation ciblée | entité explicite ou choix générique | clip prévisualisé sur la cible choisie | cible/binding absent, réparation proposée | aucune dépendance à l'ancienne sélection |
| Joueur et monstre | BehaviorGraph + signal action/IA | mêmes actions runtime et même trace | port/type/source invalide | aucun code dédié au rôle de l'instance |
| Transformation | instance source + politique A→B | instance B valide dans la même frame | destination absente, mapping invalide, cycle interdit | état transféré exactement selon la politique |
| Vectoriel natif | arbre + outils de points/fill/stroke | draw packet identique dans les trois surfaces | géométrie/fill invalide annulable | édition complète sans JSON ou SVG externe |
| Beam ou Button | PNG choisi + `Source intacte` par défaut | source inchangée dans Studio, Preview et runtime | recoloration uniquement après choix explicite | reset neutre, intensités nulles, ancien JSON inchangé |

Vocabulaire normatif : `Input bindings` désigne uniquement les périphériques ;
`Behavior` désigne la logique d'une entité humaine ou non ; `Transformation`
désigne son remplacement avec transfert d'état explicite.
