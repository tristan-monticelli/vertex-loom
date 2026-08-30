# Flux — reprise et sauvegarde de progression runtime

```mermaid
sequenceDiagram
    actor Player as Joueur
    participant CLI as game_runtime
    participant Store as ProgressStore
    participant Scene as SceneRuntimeSession
    participant Runtime as PreviewRuntime
    participant File as ProgressSave

    Player->>CLI: --save-slot slot [--scene bootstrap]
    CLI->>Store: configure(slot)
    alt slot existant
        Store->>File: load et valider
        File-->>CLI: scène et propriétés autoritaires
    else slot absent
        CLI->>CLI: exiger --scene
        CLI->>CLI: créer un état de progression vide
    end
    CLI->>Scene: charger la scène de l'état actif
    CLI->>Runtime: charger scène et propriétés
    Runtime->>Runtime: exécuter le jeu
    alt exécution réussie
        Runtime-->>CLI: scène active et propriétés
        CLI->>Store: save atomique
        Store->>File: remplacer le slot
    else erreur de chargement ou d'exécution
        Runtime-->>CLI: erreur
        Note over Store,File: aucun remplacement
    end
```

## Invariants

- Un slot existant gagne sur `--scene`; cette option amorce seulement un slot
  absent.
- `--save-slot` et `--save-path` utilisent le même flux et sont mutuellement
  exclusifs.
- Une sauvegarde invalide arrête le lancement avant la création d'une fenêtre.
- Une exécution sans mutation explicite conserve toutes les propriétés.
- Seule une exécution réussie peut remplacer atomiquement le slot.
