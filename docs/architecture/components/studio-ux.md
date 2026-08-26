# C4 Component — parcours Studio et propriété des paramètres

```mermaid
C4Component
    title Vertex Loom — boucle UX d'authoring
    Container_Boundary(studio, "Studio") {
        Component(explorer, "Resource Explorer", "Asset Studio / Map Studio", "Recherche, filtre, sélection, duplication et actions contextuelles")
        Component(create, "Creation Prompt", "Dear ImGui", "Valeurs initiales et références typées")
        Component(inspector, "Inspector", "Dear ImGui", "Édition complète des paramètres persistés")
        Component(preview, "Preview Surface", "fabric_render / Preview Runtime", "Effet immédiat, erreurs et comparaison")
        Component(transition, "Document Transition", "SessionTransitionGuard", "Save, Discard, Cancel, Retry et atomicité")
        Component(validate, "Field Validation", "fabric_project", "Erreurs attachées au champ et diagnostics structurés")
    }
    ContainerDb(files, "Project Files", "JSON + assets", "Documents versionnés")
    Rel(explorer, transition, "Sélectionne ou administre")
    Rel(create, transition, "Crée sans perdre le document actif")
    Rel(inspector, transition, "Mutations undo/autosave")
    Rel(create, validate, "Valide les valeurs initiales")
    Rel(inspector, validate, "Valide les paramètres")
    Rel(validate, preview, "Autorise ou bloque la preview")
    Rel(preview, inspector, "Ramène vers le champ fautif")
    Rel(transition, files, "Publie atomiquement")
```

## Règle de propriété des paramètres

| Surface | Responsabilité obligatoire |
| --- | --- |
| Creation Prompt | Valeur initiale, référence typée, défaut documenté |
| Resource Explorer | Découverte, contexte, chemin, type, dépendances, actions |
| Inspector | Toutes les propriétés persistées, y compris celles ajoutées après création |
| Preview | Résultat visuel/runtime et état invalide explicite |
| Validator | Erreur par champ, chemin et suggestion de correction |
| CommandStack | Undo/redo, dirty, autosave et récupération |

## Parcours nominal

1. Ouvrir/créer le projet ; afficher le rail de ressources et l'état dirty.
2. Rechercher une ressource par nom, ID ou type ; sélectionner sans saisie libre.
3. Créer ou ouvrir ; sauvegarder automatiquement le document précédent s'il est valide.
4. Régler les paramètres dans l'inspecteur ; chaque changement passe par une commande.
5. Prévisualiser immédiatement dans la même surface et corriger les erreurs au champ.
6. Composer la map/scène avec les références existantes et publier après validation.
7. Lancer Preview Runtime ; toute divergence renvoie à la ressource et au paramètre concernés.

## Parcours d'échec

Une référence absente, une valeur hors domaine ou une écriture disque échouée ne
doit jamais fermer le document courant. L'interface affiche la ressource, le
champ et la cause, puis propose `Retry`, `Discard` ou `Cancel`.
