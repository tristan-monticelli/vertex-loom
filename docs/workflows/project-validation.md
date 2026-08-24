# Validation et sauvegarde d'un projet

```mermaid
sequenceDiagram
    actor Caller as Éditeur ou CLI
    participant Project as fabric_project
    participant Migration as Registre de migrations
    participant Files as Système de fichiers

    Caller->>Project: Créer un projet
    Project->>Project: Valider le manifeste
    Project->>Files: Vérifier que la destination est absente ou vide
    Project->>Files: Créer la racine et les répertoires déclarés
    Project->>Files: Sauvegarder project.json atomiquement
    Project-->>Caller: Manifeste chargé ou erreur structurée

    Caller->>Project: Charger le dossier projet
    Project->>Files: Lire project.json
    Project->>Migration: Migrer vers le schéma courant
    Migration-->>Project: JSON v1 ou erreur structurée
    Project->>Project: Valider identifiant et chemins
    Project->>Files: Résoudre et vérifier les dossiers
    Project-->>Caller: Manifeste valide ou erreurs

    Caller->>Project: Sauvegarder un manifeste
    Project->>Project: Valider avant écriture
    Project->>Files: Écrire le fichier temporaire adjacent
    Project->>Files: Remplacer atomiquement project.json
    Project-->>Caller: Rapport de sauvegarde
```

Une erreur de parsing, migration, validation ou accès disque arrête le flux et
retourne un `ValidationReport`. La création n'écrase jamais une destination
non vide ; une erreur d'accès peut laisser les seuls répertoires nouvellement
créés, mais aucun nettoyage destructif n'est tenté automatiquement. En mode
`--json`, la CLI transforme chaque erreur en événement JSON Lines avec son code
et son champ source.
