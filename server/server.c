/*
 * server.c — Choufli Hal Clinic TCP Server
 * عيادة شوفلي حل — Dr. Slimen Labyeth
 * Concepts: sockets, pthreads, semaphore, mutex, FIFO queue
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT        8080
#define BUF_SIZE    1024
#define NB_DIAG     8

/* ─── Linked-list FIFO queue ─────────────────────────────────────────────── */

typedef struct Noeud {
    int         id;
    char        nom[64];
    time_t      heure_arrivee;
    struct Noeud *suivant;
} Noeud;

typedef struct {
    Noeud *tete;
    Noeud *queue;
    int    taille;
} FileAttente;

/* ─── Globals ─────────────────────────────────────────────────────────────── */

static sem_t             sem_consultation;
static pthread_mutex_t   mutex_file = PTHREAD_MUTEX_INITIALIZER;
static FileAttente       file       = {NULL, NULL, 0};
static int               next_id    = 0;
static int               total      = 0;
static int               server_fd  = -1;

/* ─── Sarcastic Tunisian diagnostics (Dr. Slimen Labyeth) ─────────────────── */

static const char *DIAGNOSTICS[NB_DIAG] = {
    "واضح بيّن انّك تعبان من راسك مش من جسمك.\nالعلاج: بطّل تفكّر بزاف وروح نام.",
    "انت تعاني من كسل مزمن مع هلوسة متقدمة.\nالعلاج: قوم من الكنبة وافتح الشباك.",
    "تشخيصي: إدمان على التوتر بدون سبب منطقي.\nالعلاج: شوف حلقة من شوفلي حل وارتاح.",
    "حالتك نادرة — الراس تقيل والجيب خاوي.\nالعلاج: نفسين عميقين وكاسة قهوة بدون سكر.",
    "عندك ما نسميه 'سندروم التفكير الزيادة'.\nالعلاج: بطّل تحسب في كل شيء، الدنيا بخير.",
    "مريض بالنوستالجيا والحنين لأيام ما صارتش.\nالعلاج: تقبّل الواقع وسيبها في ربي.",
    "أعراضك كلاسيكية — إرهاق وجودي من الحياة اليومية.\nالعلاج: تبسّم في وجه الدنيا ولو غلط.",
    "تعاني من 'فوبيا الصحة' — تجي تتشكّى وانت بخير.\nالعلاج: روح دير رياضة وبطّل تطلع في غوغل."
};

/* ─── Helpers ─────────────────────────────────────────────────────────────── */

static void timestamp(char *buf, size_t len) {
    time_t     now = time(NULL);
    struct tm *tm  = localtime(&now);
    strftime(buf, len, "%H:%M:%S", tm);
}

static void log_msg(const char *icon, const char *msg) {
    char ts[16];
    timestamp(ts, sizeof(ts));
    printf("[%s] %s %s\n", ts, icon, msg);
    fflush(stdout);
}

static void print_queue(void) {
    /* caller must hold mutex_file */
    printf("📋 File [%d] : ", file.taille);
    Noeud *n = file.tete;
    while (n) {
        printf("#%d-%s", n->id, n->nom);
        if (n->suivant) printf(" ➜ ");
        n = n->suivant;
    }
    if (file.taille == 0) printf("(vide)");
    printf("\n");
    fflush(stdout);
}

/* ─── Queue operations (always called under mutex) ───────────────────────── */

static void enfiler(int id, const char *nom) {
    Noeud *n = malloc(sizeof(Noeud));
    if (!n) { perror("malloc"); return; }
    n->id           = id;
    n->heure_arrivee = time(NULL);
    n->suivant      = NULL;
    strncpy(n->nom, nom, sizeof(n->nom) - 1);
    n->nom[sizeof(n->nom) - 1] = '\0';

    if (!file.tete) {
        file.tete = file.queue = n;
    } else {
        file.queue->suivant = n;
        file.queue          = n;
    }
    file.taille++;
}

/* Returns arrival time of the removed node, or 0 if not found. */
static time_t defiler(int id) {
    Noeud *prev = NULL, *cur = file.tete;
    while (cur && cur->id != id) {
        prev = cur;
        cur  = cur->suivant;
    }
    if (!cur) return 0;

    time_t arr = cur->heure_arrivee;
    if (prev) prev->suivant = cur->suivant;
    else       file.tete    = cur->suivant;
    if (cur == file.queue)
        file.queue = prev;
    file.taille--;
    free(cur);
    return arr;
}

/* ─── Signal handler ─────────────────────────────────────────────────────── */

static void handle_sigint(int sig) {
    (void)sig;
    char ts[16]; timestamp(ts, sizeof(ts));
    printf("\n[%s] ✘ Arrêt du serveur — %d consultation(s) effectuée(s).\n"
           "       كيما Slah كي يقول: 'اليوم انتهى، نروحو!'\n", ts, total);
    fflush(stdout);
    if (server_fd >= 0) close(server_fd);
    sem_destroy(&sem_consultation);
    pthread_mutex_destroy(&mutex_file);
    exit(0);
}

/* ─── Patient thread ──────────────────────────────────────────────────────── */

typedef struct {
    int patient_id;
    int client_fd;
} PatientArgs;

static void *handle_patient(void *arg) {
    PatientArgs *pa        = (PatientArgs *)arg;
    int          client_fd = pa->client_fd;
    int          id        = pa->patient_id;
    free(pa);

    char  buf[BUF_SIZE];
    char  nom[64]       = {0};
    char  log_buf[256];
    int   en_consultation = 0; /* tracks whether sem_wait() was reached */

    /* ── Step 1: Send BONJOUR ─────────────────────────────────────────────── */
    snprintf(buf, sizeof(buf),
             "BONJOUR|Bienvenue ! Vous êtes Patient #%d. Quel est votre prénom ?", id);
    if (send(client_fd, buf, strlen(buf), 0) < 0) goto nettoyage;

    snprintf(log_buf, sizeof(log_buf),
             "⟶ Nouveau patient #%d connecté — مرحبا بيك في عيادة شوفلي حل!", id);
    log_msg("", log_buf);

    /* ── Step 2: Receive name ─────────────────────────────────────────────── */
    int n = recv(client_fd, nom, sizeof(nom) - 1, 0);
    if (n <= 0) goto nettoyage;
    nom[n] = '\0';
    /* strip trailing newline */
    nom[strcspn(nom, "\r\n")] = '\0';

    /* ── Step 3: Enqueue ──────────────────────────────────────────────────── */
    pthread_mutex_lock(&mutex_file);
    enfiler(id, nom);
    snprintf(log_buf, sizeof(log_buf), "✚ %s (Patient #%d) rejoint la file", nom, id);
    print_queue();
    pthread_mutex_unlock(&mutex_file);
    log_msg("", log_buf);

    /* ── Step 4: Send ATTENTE ─────────────────────────────────────────────── */
    snprintf(buf, sizeof(buf),
             "ATTENTE|Bonjour %s ! Vous êtes Patient #%d. Patientez… "
             "📋 Dalenda قاعدة تستنى كيف العادة… نفس الصبر متاع الحلقة 12", nom, id);
    if (send(client_fd, buf, strlen(buf), 0) < 0) goto nettoyage;

    /* ── Step 5: Wait for doctor (semaphore) ──────────────────────────────── */
    snprintf(log_buf, sizeof(log_buf),
             "⏳ Patient #%d (%s) attend le médecin…", id, nom);
    log_msg("", log_buf);

    sem_wait(&sem_consultation);
    en_consultation = 1;

    /* ── Step 6: Dequeue + compute wait time ──────────────────────────────── */
    pthread_mutex_lock(&mutex_file);
    time_t arr  = defiler(id);
    total++;
    int wait_sec = (int)(time(NULL) - arr);
    snprintf(log_buf, sizeof(log_buf),
             "▶ Consultation #%d : %s (Patient #%d) — attente: %ds", total, nom, id, wait_sec);
    print_queue();
    pthread_mutex_unlock(&mutex_file);
    log_msg("", log_buf);

    /* ── Step 7: Send VOTRE_TOUR ──────────────────────────────────────────── */
    snprintf(buf, sizeof(buf),
             "VOTRE_TOUR|C'est votre tour %s ! (attente: %ds) "
             "Décrivez vos symptômes. 🩺 Slimen: هيا اقعد وقلي شنوه المشكلة 😅", nom, wait_sec);
    if (send(client_fd, buf, strlen(buf), 0) < 0) goto nettoyage;

    /* ── Step 8: Receive symptoms ─────────────────────────────────────────── */
    char symptoms[BUF_SIZE] = {0};
    n = recv(client_fd, symptoms, sizeof(symptoms) - 1, 0);
    if (n <= 0) goto nettoyage;
    symptoms[n] = '\0';
    symptoms[strcspn(symptoms, "\r\n")] = '\0';

    snprintf(log_buf, sizeof(log_buf), "✉ Symptômes de %s (Patient #%d) reçus", nom, id);
    log_msg("", log_buf);

    /* ── Step 9: Simulate consultation (8 seconds) ────────────────────────── */
    snprintf(log_buf, sizeof(log_buf),
             "🩺 Examen en cours pour %s — Slimen يفكر بجدية متاع الحلقة الأخيرة…", nom);
    log_msg("", log_buf);
    sleep(8);

    /* ── Step 10: Send DIAGNOSTIC ─────────────────────────────────────────── */
    const char *diag = DIAGNOSTICS[id % NB_DIAG];
    snprintf(buf, sizeof(buf), "DIAGNOSTIC|Dr. Slimen → %s (Patient #%d) :\n%s", nom, id, diag);
    if (send(client_fd, buf, strlen(buf), 0) < 0) goto nettoyage;

    snprintf(log_buf, sizeof(log_buf),
             "✔ Diagnostic envoyé à %s — ✔ كيما في Choufli Hal، كل مشكلة عندها حل!", nom);
    log_msg("", log_buf);

nettoyage:
    close(client_fd);

    if (en_consultation) {
        pthread_mutex_lock(&mutex_file);
        snprintf(log_buf, sizeof(log_buf),
                 "◀ Médecin disponible — %d consultation(s) au total", total);
        pthread_mutex_unlock(&mutex_file);
        log_msg("", log_buf);
        sem_post(&sem_consultation);
    } else {
        /* patient disconnected before being seen */
        pthread_mutex_lock(&mutex_file);
        defiler(id);
        snprintf(log_buf, sizeof(log_buf),
                 "✘ Patient #%d (%s) parti sans consulter — "
                 "مشى بلا موعد كيما Slah كي يهرب من العيادة!", id, nom[0] ? nom : "?");
        print_queue();
        pthread_mutex_unlock(&mutex_file);
        log_msg("", log_buf);
    }

    return NULL;
}

/* ─── Main ────────────────────────────────────────────────────────────────── */

int main(void) {
    signal(SIGINT, handle_sigint);

    if (sem_init(&sem_consultation, 0, 1) != 0) {
        perror("sem_init"); exit(1);
    }

    /* Create TCP socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(PORT),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen"); exit(1);
    }

    log_msg("🏥", "Serveur شوفلي حل démarré sur le port 8080 — Dr. Slimen est prêt!");
    log_msg("", "🩺 Slimen: يبدولي اليوم رانا نستقبلو بزاف مرضى… الله يعين 😅");

    /* Infinite accept loop */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t          client_len = sizeof(client_addr);
        int                client_fd  = accept(server_fd,
                                               (struct sockaddr *)&client_addr,
                                               &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) break; /* SIGINT received */
            perror("accept");
            continue;
        }

        /* Allocate args — freed inside handle_patient */
        PatientArgs *pa = malloc(sizeof(PatientArgs));
        if (!pa) { close(client_fd); continue; }

        pthread_mutex_lock(&mutex_file);
        pa->patient_id = next_id++;
        pthread_mutex_unlock(&mutex_file);
        pa->client_fd = client_fd;

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&tid, &attr, handle_patient, pa) != 0) {
            perror("pthread_create");
            free(pa);
            close(client_fd);
        }
        pthread_attr_destroy(&attr);
    }

    return 0;
}
