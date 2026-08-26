# ADR-0122 — Réglages runtime persistants du projet

## Statut

Accepté.

## Contexte

Le Preview Runtime reçoit encore par options de ligne de commande le spawn du
personnage, son activation, le suivi caméra, les limites caméra et le fichier
audio. Ces réglages ne peuvent donc pas être reproduits depuis Asset Studio,
Map Studio ou un paquet publié.

## Décision

Le manifeste projet porte une section optionnelle `runtime` contenant :

- `character.enabled` et `character.spawn` ;
- `character.actions` pour les trois actions sémantiques ;
- `camera.followCharacter` et `camera.limits` ;
- `audio` comme référence typée vers un `AudioDocument`.

Les valeurs sont validées avant tout chargement runtime. Les chemins audio
restent des références de ressources, jamais des chemins absolus. L’absence de
la section conserve les valeurs par défaut actuelles afin de rester compatible
avec les projets existants.

La migration de manifeste ajoute cette section uniquement lorsqu’elle est
nécessaire ; elle ne fabrique pas de référence audio implicite. Asset Studio
édite ces champs dans Project settings, et Preview Runtime consomme le même
résultat depuis un projet ou un paquet.

## Conséquences

- Le contrat du manifeste passe à une version suivante avec migration atomique.
- Le validateur doit rejeter les vecteurs non finis, les limites négatives et
  les références audio d’un type différent.
- Le runtime ne doit utiliser la CLI que comme surcharge explicite de test ou
  de compatibilité, après résolution de la configuration projet.
- Des tests de round-trip JSON, validation, édition et chargement runtime sont
  requis avant de fermer le lot runtime du checklist.
