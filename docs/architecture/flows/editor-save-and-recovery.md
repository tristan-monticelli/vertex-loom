# Flux — sauvegarde et récupération d’un document

```mermaid
sequenceDiagram
    actor Creator as Créateur
    participant UI as Asset/Map Studio
    participant History as CommandStack
    participant Scheduler as AutosaveScheduler
    participant Storage as DocumentStorage
    participant Files as Project Files

    Creator->>UI: Modifie une propriété
    UI->>History: execute(command)
    History->>Scheduler: mark_changed()
    alt sauvegarde explicite
        Creator->>UI: Save
        UI->>Storage: save_document_atomic(validated document)
        Storage->>Files: remplacement atomique du document principal
        UI->>History: mark_clean()
    else 2 s inactives ou 30 s maximum
        Scheduler->>Storage: save_autosave_atomic(validated document)
        Storage->>Files: remplacement atomique du miroir autosave
    end
    Note over Storage,Files: Un autosave ne remplace jamais le document principal
```

```mermaid
flowchart TD
    A[Ouverture du document principal] --> B{Autosave présent ?}
    B -- non --> C[Utiliser le document principal]
    B -- oui --> D{Valide et plus récent ?}
    D -- non --> C
    D -- oui --> E[Proposer la récupération]
    E -- Refuser --> C
    E -- Accepter --> F[Charger l’autosave en mémoire]
    F --> G[Marquer le document dirty]
    G --> H[Sauvegarde principale uniquement sur action Save]
```

## Invariants

- Une commande échouée ne modifie ni l’historique ni son état dirty.
- Undo et redo déplacent le curseur seulement après succès de la commande.
- Une fusion continue conserve la valeur initiale pour undo et la dernière
  valeur pour redo.
- Un document et son autosave passent le même validateur avant écriture ou
  récupération.
- Les chemins absolus, traversants et les parents résolus hors projet sont
  refusés.
- Refuser ou accepter une récupération ne modifie aucun fichier principal.
- Un retour par undo au point propre écrit le contenu principal dans le miroir
  afin de neutraliser un autosave obsolète sans supprimer de fichier.
