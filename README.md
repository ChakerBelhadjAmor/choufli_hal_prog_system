# شوفلي حل — Choufli Hal Clinic

> Projet Programmation Systèmes & Réseaux — Simulation d'une clinique médicale

---

## Description

**FR** : Ce projet simule une clinique médicale fictive inspirée du sitcom tunisien *شوفلي حل (Choufli Hal)*. Un serveur TCP en C gère une file d'attente FIFO avec un médecin (Dr. Slimen) qui consulte les patients un par un. Les patients se connectent via un client terminal en C ou via une interface web Flask. Le tout tourne sur **une seule machine Fedora** avec plusieurs terminaux.

**EN** : This project simulates a fictional medical clinic themed after the Tunisian sitcom *Choufli Hal*. A C TCP server manages a FIFO queue with one doctor (Dr. Slimen) consulting patients one at a time. Patients connect via a C terminal client or a Flask web interface. Everything runs on **a single Fedora machine** using multiple terminals.

---

## Architecture

```
              Machine locale (Fedora) — 127.0.0.1
  ┌───────────────────────────────────────────────────────┐
  │                                                       │
  │  Terminal 1 — server                                  │
  │  ┌─────────────────────────────────────────────┐     │
  │  │  server.c (C TCP Server) — port 8080        │     │
  │  │     accept() loop                            │     │
  │  │       ├─→ pthread_create(handle_patient)     │     │
  │  │       │       ├─ mutex: enfiler()            │     │
  │  │       │       ├─ sem_wait()  ◄─ BLOCKS       │     │
  │  │       │       ├─ mutex: defiler()            │     │
  │  │       │       ├─ recv symptoms               │     │
  │  │       │       ├─ sleep(8) ← consultation     │     │
  │  │       │       ├─ send DIAGNOSTIC             │     │
  │  │       │       └─ sem_post()                  │     │
  │  └─────────────────────────────────────────────┘     │
  │          ▲                     ▲                      │
  │          │ TCP 127.0.0.1:8080  │ TCP 127.0.0.1:8080  │
  │          │                     │                      │
  │  Terminal 2              Terminal 3 (optionnel)       │
  │  client.c                web_client.py :5000          │
  │  (terminal)              → navigateur :5000           │
  │                                                       │
  └───────────────────────────────────────────────────────┘
```

---

## Compilation & Lancement

### Étape 1 — Compiler

```bash
# Serveur
cd projet_medecin/server && make

# Client terminal
cd projet_medecin/client && make
```

### Étape 2 — Lancer le serveur (Terminal 1)

```bash
cd projet_medecin/server
./server
```

Le serveur écoute sur `0.0.0.0:8080` — laisser ce terminal ouvert.

### Étape 3a — Client terminal (Terminal 2)

```bash
cd projet_medecin/client
./client 127.0.0.1
```

### Étape 3b — Interface web (Terminal 2 ou 3)

```bash
cd projet_medecin/client
pip install flask          # une seule fois
python3 web_client.py
# Ouvrir http://localhost:5000 dans le navigateur
```

`C_SERVER_IP` est déjà réglé sur `127.0.0.1` dans `web_client.py`.

---

## Test avec 2 patients simultanés

Ouvrir **3 terminaux** sur la même machine :

```bash
# Terminal 1 — serveur
cd projet_medecin/server && ./server

# Terminal 2 — patient Alice
cd projet_medecin/client && ./client 127.0.0.1
# → entrer "Alice" → attendre le médecin

# Terminal 3 — patient Bob (simultanément)
cd projet_medecin/client && ./client 127.0.0.1
# → entrer "Bob" → Bob attend qu'Alice finisse (semaphore bloquant)
```

Ou via le web : ouvrir 2 onglets sur `http://localhost:5000` et soumettre les deux formulaires rapidement.

---

## Ajouter les vraies images

Placer les fichiers dans `client/` :

```bash
cp dalanda.gif projet_medecin/client/static/dalanda.gif
cp slimen.gif  projet_medecin/client/static/slimen.gif
```

L'interface web les charge automatiquement si présentes.

---

## Concepts démontrés

| Concept | Implémentation |
|---------|----------------|
| **Sockets TCP** | `socket()` → `bind()` → `listen()` → `accept()` |
| **Threads POSIX** | `pthread_create()` + `PTHREAD_CREATE_DETACHED` |
| **Sémaphore** | `sem_t sem_consultation = 1` → 1 consultation à la fois |
| **Mutex** | `pthread_mutex_t mutex_file` protège file + compteurs |
| **File FIFO** | Liste chaînée `Noeud`, `enfiler()` / `defiler()` |
| **Signaux** | `SIGINT` → arrêt propre via `handle_sigint()` |

---

## Questions / Réponses (Soutenance orale)

**Q : Pourquoi utiliser un sémaphore et pas un mutex pour la consultation ?**
R : Le sémaphore permet de contrôler le nombre de consultations simultanées (valeur = 1). Il peut être utilisé entre threads différents (sem_wait dans un thread, sem_post dans le même). Un mutex serait moins naturel ici.

**Q : Pourquoi le flag `en_consultation` est-il nécessaire ?**
R : Pour le nettoyage (goto nettoyage). Si le patient se déconnecte avant d'avoir atteint `sem_wait()`, on ne doit PAS appeler `sem_post()` (ce serait une erreur grave). Le flag indique si `sem_wait()` a été atteint.

**Q : Que protège le mutex ?**
R : Tous les accès à `file` (tete, queue, taille), `next_id`, et `total`. Sans mutex, plusieurs threads pourraient corrompre la file simultanément.

**Q : Pourquoi `PTHREAD_CREATE_DETACHED` ?**
R : On n'a pas besoin de `pthread_join()`. Le thread libère ses ressources automatiquement à la fin, évitant les fuites mémoire.

**Q : Comment Flask gère plusieurs patients simultanément ?**
R : `app.run(threaded=True)` fait que Flask crée un thread par requête. Chaque thread Flask maintient sa propre connexion TCP vers le serveur C sur `127.0.0.1:8080`.

**Q : Que se passe-t-il si le serveur est arrêté pendant qu'un patient attend ?**
R : Le `recv()` ou `send()` retourne une erreur (< 0 ou 0), le thread va dans `nettoyage`, ferme le socket, et si `en_consultation == 1`, appelle `sem_post()`.

---

## Structure du projet

```
projet_medecin/
├── server/
│   ├── server.c          ← Serveur TCP C (médecin)
│   └── Makefile
├── client/
│   ├── client.c          ← Client terminal C (patient)
│   ├── web_client.py     ← Interface web Flask
│   ├── index.html        ← Page HTML (thème شوفلي حل)
│   ├── static/
│   │   ├── dalanda.gif   ← Image salle d'attente
│   │   └── slimen.gif    ← Image docteur
│   └── Makefile
└── README.md
```
