# C4 Component — coquille Asset Studio

```mermaid
C4Component
    title Vertex Loom — première tranche Asset Studio
    Container_Boundary(asset, "Asset Studio") {
        Component(shell, "Desktop shell", "SDL2 / OpenGL / Dear ImGui", "Fenêtre, événements, frames et panneaux de l'atelier")
        Component(project_ui, "Project workspace", "Dear ImGui", "Création, ouverture, état de chargement et diagnostics")
    }
    Container_Boundary(editor, "fabric_editor") {
        Component(session, "ProjectSession", "C++20", "Conserve uniquement le manifeste lu et validé en une opération, ainsi que les erreurs de la dernière ouverture")
    }
    Container(project, "fabric_project", "C++20", "Crée, valide et charge le manifeste partagé")
    ContainerDb(files, "Project Files", "JSON + assets", "Dossier projet local")
    Rel(shell, project_ui, "Affiche")
    Rel(project_ui, session, "Demande la création ou l'ouverture")
    Rel(session, project, "Crée ou charge")
    Rel(project, files, "Lit et écrit")
```

## Contract

- Le chemin peut être fourni au démarrage ou saisi dans l'atelier.
- La création demande un nom, un identifiant de ressource valide et un dossier
  de destination absent ou vide.
- Une ouverture échouée expose les erreurs structurées et ne remplace pas la
  dernière session valide.
- SDL2 possède la fenêtre et les événements ; Dear ImGui possède uniquement
  l'interface d'outil ; OpenGL efface et présente la surface.
- Aucun import ou rendu d'asset n'est introduit dans cette tranche.
