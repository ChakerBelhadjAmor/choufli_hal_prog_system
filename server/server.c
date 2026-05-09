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
#define BUF_SIZE    2048
#define NB_DIAG     12
#define NB_DALANDA  6
#define NB_TOUR     5

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

static sem_t             sem_consultation;
static pthread_mutex_t   mutex_file = PTHREAD_MUTEX_INITIALIZER;
static FileAttente       file       = {NULL, NULL, 0};
static int               next_id    = 0;
static int               total      = 0;
static int               server_fd  = -1;

static const char *DALANDA_ATTENTE[NB_DALANDA] = {
    "mar7ba bik monsieur tfadhal erte7 el docteur tw ychoufek chweye akhor\n"
};

static const char *SLIMEN_TOUR[NB_TOUR] = {
    "tfadhal ,ahkili chnuwa t7es\n"
};

static const char *DIAGNOSTICS[NB_DIAG] = {

    "3andek haja nsamiwhe 3ilmiyen b syndrome de sbou3i.\n",

    "enti andek 7anin lil madhi w rabet el sa3ada taeek b hajet mchet\n"
    "ama el sa3ada 7keye fergha marhouna f hkayet sghira hakaya kwayes tey ,chicha,dhohka maa el beji."


};

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
    printf("File [%d] : ", file.taille);
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

static void handle_sigint(int sig) {
    (void)sig;
    char ts[16]; timestamp(ts, sizeof(ts));
    printf("\n[%s]Arrêt du serveur — %d consultation(s) effectuée(s).\n", ts, total);
    fflush(stdout);
    if (server_fd >= 0) close(server_fd);
    sem_destroy(&sem_consultation);
    pthread_mutex_destroy(&mutex_file);
    exit(0);
}

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
    int   en_consultation = 0; 

    snprintf(buf, sizeof(buf),
             "BONJOUR|Bienvenue ! Vous êtes Patient #%d. Quel est votre prénom ?", id);
    if (send(client_fd, buf, strlen(buf), 0) < 0) goto nettoyage;

    snprintf(log_buf, sizeof(log_buf),
             "Nouveau patient #%d connecté", id);
    log_msg("", log_buf);

    int n = recv(client_fd, nom, sizeof(nom) - 1, 0);
    if (n <= 0) goto nettoyage;
    nom[n] = '\0';
    /* strip trailing newline */
    nom[strcspn(nom, "\r\n")] = '\0';

    pthread_mutex_lock(&mutex_file);
    enfiler(id, nom);
    snprintf(log_buf, sizeof(log_buf), "✚ %s (Patient #%d) rejoint la file", nom, id);
    print_queue();
    pthread_mutex_unlock(&mutex_file);
    log_msg("", log_buf);

    snprintf(buf, sizeof(buf),
             "ATTENTE| dalanda zidou 9ahwa — %s (مريض رقم #%d) :\n%s",
             nom, id, DALANDA_ATTENTE[id % NB_DALANDA]);
    if (send(client_fd, buf, strlen(buf), 0) < 0) goto nettoyage;

    snprintf(log_buf, sizeof(log_buf),
             " Patient #%d (%s) attend le médecin…", id, nom);
    log_msg("", log_buf);

    sem_wait(&sem_consultation);
    en_consultation = 1;

    pthread_mutex_lock(&mutex_file);
    time_t arr  = defiler(id);
    total++;
    int wait_sec = (int)(time(NULL) - arr);
    snprintf(log_buf, sizeof(log_buf),
             " Consultation #%d : %s (Patient #%d) — attente: %ds", total, nom, id, wait_sec);
    print_queue();
    pthread_mutex_unlock(&mutex_file);
    log_msg("", log_buf);

    snprintf(buf, sizeof(buf),
             "VOTRE_TOUR| الدكتور سليمان — %s (انتظرت %d ثانية) :\n%s",
             nom, wait_sec, SLIMEN_TOUR[id % NB_TOUR]);
    if (send(client_fd, buf, strlen(buf), 0) < 0) goto nettoyage;

    char symptoms[BUF_SIZE] = {0};
    n = recv(client_fd, symptoms, sizeof(symptoms) - 1, 0);
    if (n <= 0) goto nettoyage;
    symptoms[n] = '\0';
    symptoms[strcspn(symptoms, "\r\n")] = '\0';

    snprintf(log_buf, sizeof(log_buf), " Symptômes de %s (Patient #%d) reçus", nom, id);
    log_msg("", log_buf);

    snprintf(log_buf, sizeof(log_buf),
             "Examen en cours pour %s — Slimen ykhamam", nom);
    log_msg("", log_buf);
    sleep(8);

    const char *diag = DIAGNOSTICS[id % NB_DIAG];
    snprintf(buf, sizeof(buf), "DIAGNOSTIC|Dr. Slimen → %s (Patient #%d) :\n%s", nom, id, diag);
    if (send(client_fd, buf, strlen(buf), 0) < 0) goto nettoyage;

    snprintf(log_buf, sizeof(log_buf),
             "Diagnostic envoyé à %s — kol mochkel andou 7al", nom);
    log_msg("", log_buf);

nettoyage:
    close(client_fd);

    if (en_consultation) {
        pthread_mutex_lock(&mutex_file);
        snprintf(log_buf, sizeof(log_buf),
                 "Médecin disponible — %d consultation(s) au total", total);
        pthread_mutex_unlock(&mutex_file);
        log_msg("", log_buf);
        sem_post(&sem_consultation);
    } else {
        pthread_mutex_lock(&mutex_file);
        defiler(id);
        snprintf(log_buf, sizeof(log_buf),
                 "Patient #%d (%s) parti sans consulter — ", id, nom[0] ? nom : "?");
        print_queue();
        pthread_mutex_unlock(&mutex_file);
        log_msg("", log_buf);
    }

    return NULL;
}

int main(void) {
    signal(SIGINT, handle_sigint);
    if (sem_init(&sem_consultation, 0, 1) != 0) {
        perror("sem_init"); exit(1);
    }
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
    log_msg("", "Serveur thread شوفلي démarré sur le port 8080 — Dr. Slimen est prêt!");
    log_msg("", "Slimen: Sbeh el nour Dalanda");
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t          client_len = sizeof(client_addr);
        int                client_fd  = accept(server_fd,
                                               (struct sockaddr *)&client_addr,
                                               &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) break; 
            perror("accept");
            continue;
        }
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
