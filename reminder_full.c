/* reminder_final.c
   Personal Task Reminder System — final version
   ✅ One-time reminders
   ✅ Friendly countdown messages with task titles
   ✅ Tasks auto-deleted when reminder starts
   ✅ CSP concepts: File I/O, Signals, Multithreading, Synchronization
   Compile: gcc -o reminder_final reminder_final.c -lpthread
   Run: ./reminder_final
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#define TASK_FILE "tasks.txt"
#define MAX_TASKS 256
#define LINE_BUF 512

typedef struct {
    int id;
    char title[128];
    char category[32];
    int priority;
    time_t deadline;
} task_t;

/* Copy of tasks to pass into reminder thread */
typedef struct {
    task_t *items;
    int count;
} due_copy_t;

task_t tasks[MAX_TASKS];
int task_count = 0;
int next_id = 1;

pthread_mutex_t tasks_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t alarm_fired = 0;

/* --- Signal handler --- */
void sigalrm_handler(int sig) {
    (void)sig;
    alarm_fired = 1;
    write(STDOUT_FILENO, "\n[!] SIGALRM triggered: task reminder due.\n", 43);
}

/* --- Helpers --- */
void format_time(time_t t, char *buf, size_t n) {
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, n, "%Y-%m-%d %H:%M", &tm);
}

/* Load & Save Tasks */
void load_tasks() {
    pthread_mutex_lock(&tasks_mutex);
    FILE *f = fopen(TASK_FILE, "r");
    if (!f) { pthread_mutex_unlock(&tasks_mutex); return; }
    char line[LINE_BUF];
    task_count = 0; next_id = 1;
    while (fgets(line, sizeof(line), f) && task_count < MAX_TASKS) {
        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
        int id; char title[128], cat[32]; int pr; long long dl;
        if (sscanf(line, "%d|%127[^|]|%31[^|]|%d|%lld", &id, title, cat, &pr, &dl) == 5) {
            tasks[task_count].id = id;
            strncpy(tasks[task_count].title, title, sizeof(tasks[task_count].title)-1);
            strncpy(tasks[task_count].category, cat, sizeof(tasks[task_count].category)-1);
            tasks[task_count].priority = pr;
            tasks[task_count].deadline = (time_t)dl;
            task_count++;
            if (id >= next_id) next_id = id + 1;
        }
    }
    fclose(f);
    pthread_mutex_unlock(&tasks_mutex);
}

void save_tasks() {
    pthread_mutex_lock(&tasks_mutex);
    FILE *f = fopen(TASK_FILE, "w");
    if (!f) { perror("save_tasks"); pthread_mutex_unlock(&tasks_mutex); return; }
    for (int i = 0; i < task_count; ++i) {
        fprintf(f, "%d|%s|%s|%d|%lld\n",
                tasks[i].id, tasks[i].title, tasks[i].category,
                tasks[i].priority, (long long)tasks[i].deadline);
    }
    fclose(f);
    pthread_mutex_unlock(&tasks_mutex);
}

/* --- User functions --- */
void add_task() {
    char title[128], category[32], timestr[64];
    int priority;
    printf("Title: ");
    if (!fgets(title, sizeof(title), stdin)) return;
    title[strcspn(title, "\n")] = 0;
    printf("Category (Work/Study/Personal): ");
    if (!fgets(category, sizeof(category), stdin)) return;
    category[strcspn(category, "\n")] = 0;
    printf("Priority (1–5): ");
    if (scanf("%d", &priority) != 1) { while(getchar()!='\n'); return; }
    while(getchar()!='\n');
    printf("Deadline (YYYY-MM-DD HH:MM): ");
    if (!fgets(timestr, sizeof(timestr), stdin)) return;
    timestr[strcspn(timestr, "\n")] = 0;

    struct tm tm = {0};
    if (!strptime(timestr, "%Y-%m-%d %H:%M", &tm)) { printf("Invalid time.\n"); return; }
    time_t dl = mktime(&tm);

    pthread_mutex_lock(&tasks_mutex);
    if (task_count >= MAX_TASKS) { pthread_mutex_unlock(&tasks_mutex); printf("Max tasks reached.\n"); return; }
    task_t *t = &tasks[task_count++];
    t->id = next_id++;
    strncpy(t->title, title, sizeof(t->title)-1);
    strncpy(t->category, category, sizeof(t->category)-1);
    t->priority = priority;
    t->deadline = dl;
    pthread_mutex_unlock(&tasks_mutex);
    save_tasks();
    printf("Task '%s' added.\n", title);
}

void view_tasks() {
    pthread_mutex_lock(&tasks_mutex);
    if (task_count == 0) { printf("No tasks.\n"); pthread_mutex_unlock(&tasks_mutex); return; }
    printf("ID | Deadline           | Pri | Category   | Title\n");
    printf("--------------------------------------------------------------\n");
    for (int i = 0; i < task_count; ++i) {
        char buf[64];
        format_time(tasks[i].deadline, buf, sizeof(buf));
        printf("%2d | %s |  %d  | %-10s | %s\n",
               tasks[i].id, buf, tasks[i].priority, tasks[i].category, tasks[i].title);
    }
    pthread_mutex_unlock(&tasks_mutex);
}

void delete_task() {
    int id;
    printf("Enter id to delete: ");
    if (scanf("%d", &id) != 1) { while(getchar()!='\n'); return; }
    while(getchar()!='\n');
    pthread_mutex_lock(&tasks_mutex);
    int idx = -1;
    for (int i = 0; i < task_count; ++i) if (tasks[i].id == id) { idx = i; break; }
    if (idx != -1) {
        for (int i = idx; i < task_count - 1; ++i) tasks[i] = tasks[i+1];
        task_count--;
        printf("Task %d deleted.\n", id);
    } else printf("Not found.\n");
    pthread_mutex_unlock(&tasks_mutex);
    save_tasks();
}

/* --- Utility --- */
time_t next_deadline() {
    pthread_mutex_lock(&tasks_mutex);
    time_t now = time(NULL);
    time_t best = 0;
    for (int i = 0; i < task_count; ++i) {
        if (tasks[i].deadline <= now) { best = now; break; }
        if (best == 0 || tasks[i].deadline < best) best = tasks[i].deadline;
    }
    pthread_mutex_unlock(&tasks_mutex);
    return best;
}

/* --- Reminder Thread --- */
void *reminder_thread_fn(void *arg) {
    due_copy_t *dc = (due_copy_t *)arg;
    if (!dc || dc->count <= 0) return NULL;

    printf("\n====== REMINDER: %d task(s) due ======\n", dc->count);
    for (int i = 0; i < dc->count; ++i) {
        char buf[64];
        format_time(dc->items[i].deadline, buf, sizeof(buf));
        printf("  - [%s] %s (priority %d) due at %s\n",
               dc->items[i].category, dc->items[i].title,
               dc->items[i].priority, buf);
    }

    /* Countdown intervals (60s total) */
    int intervals[] = {30, 10, 15, 4, 1};
    int announce_secs[] = {30, 20, 5, 1, 0};

    for (int k = 0; k < 5; ++k) {
        sleep(intervals[k]);
        for (int i = 0; i < dc->count; ++i) {
            if (announce_secs[k] > 0)
                printf("Reminder: \"%s\" is closing in %d seconds...\n",
                       dc->items[i].title, announce_secs[k]);
            else
                printf("Final reminder: \"%s\" deadline reached! Clearing now.\n",
                       dc->items[i].title);
        }
    }

    free(dc->items);
    free(dc);
    printf("Reminder finished.\n");
    return NULL;
}

/* --- Scheduler Thread --- */
void *scheduler_thread_fn(void *arg) {
    (void)arg;
    while (1) {
        time_t nd = next_deadline();
        time_t now = time(NULL);

        if (nd == 0) { sleep(2); continue; }

        int seconds = (int)difftime(nd, now);
        if (seconds <= 0) alarm_fired = 1;
        else alarm(seconds);

        while (!alarm_fired) sleep(1);
        alarm_fired = 0;

        pthread_mutex_lock(&tasks_mutex);
        time_t tnow = time(NULL);
        int due_count = 0;
        for (int i = 0; i < task_count; ++i)
            if (tasks[i].deadline <= tnow) due_count++;
        if (due_count == 0) { pthread_mutex_unlock(&tasks_mutex); continue; }

        task_t *copies = malloc(sizeof(task_t) * due_count);
        int ci = 0, write_idx = 0;
        for (int i = 0; i < task_count; ++i) {
            if (tasks[i].deadline <= tnow)
                copies[ci++] = tasks[i];
            else
                tasks[write_idx++] = tasks[i];
        }
        task_count = write_idx;
        pthread_mutex_unlock(&tasks_mutex);

        save_tasks();

        due_copy_t *dc = malloc(sizeof(due_copy_t));
        dc->items = copies;
        dc->count = due_count;

        pthread_t rt;
        if (pthread_create(&rt, NULL, reminder_thread_fn, dc) == 0)
            pthread_detach(rt);
        else {
            perror("pthread_create reminder");
            free(copies);
            free(dc);
        }
    }
    return NULL;
}

/* --- main --- */
int main(void) {
    struct sigaction sa;
    sa.sa_handler = sigalrm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);

    load_tasks();

    pthread_t scheduler;
    pthread_create(&scheduler, NULL, scheduler_thread_fn, NULL);

    while (1) {
        printf("\n=== Personal Task Reminder ===\n");
        printf("1) View tasks\n2) Add task\n3) Delete task\n4) Save & Exit\nChoice: ");
        int c;
        if (scanf("%d", &c) != 1) { while(getchar()!='\n'); continue; }
        while(getchar()!='\n');
        switch (c) {
            case 1: view_tasks(); break;
            case 2: add_task(); break;
            case 3: delete_task(); break;
            case 4:
                save_tasks();
                printf("Exiting...\n");
                _exit(0);
            default: printf("Invalid.\n");
        }
    }
}
