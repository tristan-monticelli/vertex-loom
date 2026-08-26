# Composants — transformation d'entité

```mermaid
flowchart LR
    Form[Asset Studio\nformulaire typé] --> Session[TransformationSession\nundo / autosave / recovery]
    Session --> Contract[EntityTransformation v1]
    Explorer[Resource Explorer\nsource + destination] --> Form
    Behavior[BehaviorGraph\ntransform_entity] --> Contract
    Contract --> Validator[Validation mappings\ncompatibilité + cycles]
    Contract --> Package[Package closure\ndestination + dépendances]
    Runtime[Preview Runtime] --> Candidate[Prépare candidat destination]
    Contract --> Candidate
    Candidate --> Transfer[Applique politique de transfert]
    Transfer --> Swap[Remplacement atomique\nde l'état d'instance]
    Swap --> Render[Animation / physique / rendu\ncohérents dans la même frame]
```

Le candidat est détruit en cas d'erreur et l'instance source reste inchangée.
La source n'est remplacée qu'après résolution complète des ressources de la
destination et validation de chaque mapping.

`TransformationSession` refuse une mutation avant de toucher son historique si
une entité choisie est absente. Elle publie atomiquement le document principal
et conserve l'autosave comme candidat de récupération séparé.
