# C4 Component — coquille Asset Studio

```mermaid
C4Component
    title Vertex Loom — première tranche Asset Studio
    Container_Boundary(asset, "Asset Studio") {
        Component(shell, "Desktop shell", "SDL2 / OpenGL / Dear ImGui", "Fenêtre, événements, frames et panneaux de l'atelier")
        Component(project_ui, "Project workspace", "Dear ImGui + NFD", "Création, ouverture, imports PNG/SVG par dialogues natifs, état et diagnostics")
    }
    Container_Boundary(editor, "fabric_editor") {
        Component(session, "ProjectSession", "C++20", "Conserve le manifeste validé, orchestre les imports PNG/SVG et expose les erreurs de la dernière opération")
        Component(history, "CommandStack", "C++20", "Exécute, fusionne, annule et réapplique les modifications réversibles")
        Component(scheduler, "AutosaveScheduler", "C++20", "Déclenche après 2 s d’inactivité ou 30 s au maximum")
    }
    Container(project, "fabric_project", "C++20", "Crée, valide et charge le manifeste partagé")
    System_Ext(dialogs, "Dialogues natifs", "Cocoa, Win32 ou GTK via NFD Extended")
    ContainerDb(files, "Project Files", "JSON + assets", "Dossier projet local")
    Rel(shell, project_ui, "Affiche")
    Rel(project_ui, session, "Demande la création ou l'ouverture")
    Rel(project_ui, dialogs, "Sélectionne dossiers et fichiers")
    Rel(session, project, "Crée ou charge")
    Rel(session, history, "Porte les mutations éditables")
    Rel(session, scheduler, "Signale les modifications")
    Rel(scheduler, project, "Demande un autosave validé")
    Rel(project, files, "Lit et écrit")
```

## Contract

- Le chemin peut être fourni au démarrage ; les actions interactives utilisent
  le sélecteur natif de dossier ou de fichier.
- La création demande un nom, un identifiant de ressource valide et un dossier
  de destination absent ou vide.
- Une ouverture échouée expose les erreurs structurées et ne remplace pas la
  dernière session valide.
- SDL2 possède la fenêtre et les événements ; Dear ImGui possède uniquement
  l'interface d'outil ; OpenGL efface et présente la surface.
- Un import réussi conserve le `TextureAsset` et les pixels décodés pour
  l'aperçu ; un échec conserve le dernier import réussi.
- Un import SVG réussi conserve le `VectorAsset` et son aperçu RGBA8 borné ;
  un échec conserve le dernier import vectoriel réussi.
- Toute mutation d’un document éditable passe par `CommandStack`. Les imports,
  qui créent des ressources immuables sans remplacement, restent hors de cet
  historique de document.
- La session expose undo, redo et dirty et ne marque clean qu’après une
  sauvegarde principale réussie.
- `CommandStack::execute` accepte une commande possédée par la pile ;
  `can_undo`, `can_redo`, `undo`, `redo`, `mark_clean` et `dirty` exposent son
  état sans dépendance à Dear ImGui.
- Une récupération plus récente est proposée à l’ouverture ; accepter charge
  son contenu en mémoire, refuser conserve le principal, sans écriture implicite.
