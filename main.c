#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_ARGS 64
#define MAX_TASKS 64
#define MAX_NAME 64
#define MAX_LINE 1024

typedef struct {
    char name[MAX_NAME];
    char *exec_args[MAX_ARGS];
} Task;

Task tasks[MAX_TASKS];
int task_count = 0;

typedef struct {
    int job_id;
    pid_t pid;
    char name[MAX_NAME];
    int finished;
    int exit_code;
} Job;

Job jobs[MAX_TASKS];
int job_count = 0;

int parse_line(char *line, char *args[]) {
    int count = 0;
    char *token = strtok(line, " ");
    while (token != NULL && count < MAX_ARGS - 1) {
        args[count++] = token;
        token = strtok(NULL, " ");
    }
    args[count] = NULL;
    return count;
}

Task *find_task(const char *name) {
    for (int i = 0; i < task_count; i++) {
        if (strcmp(tasks[i].name, name) == 0) {
            return &tasks[i];
        }
    }
    return NULL;
}

void cmd_task(char *args[], int nargs) {
    if (nargs < 3) {
        printf("Erro: uso correto e 'task <nome> <programa> [argumentos...]'\n");
        return;
    }
    if (task_count >= MAX_TASKS) {
        printf("Erro: limite de tarefas cadastradas atingido\n");
        return;
    }

    Task *t = &tasks[task_count];
    strncpy(t->name, args[1], MAX_NAME - 1);
    t->name[MAX_NAME - 1] = '\0';

    int j = 0;
    for (int i = 2; i < nargs; i++) {
        t->exec_args[j++] = strdup(args[i]);
    }
    t->exec_args[j] = NULL;

    task_count++;
    printf("Tarefa '%s' cadastrada.\n", args[1]);
}

void run_single_task(const char *nome, char *modo, char *arquivo_redirecionamento) {
    Task *t = find_task(nome);
    if (t == NULL) {
        printf("Erro: tarefa '%s' nao encontrada\n", nome);
        return;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("Erro ao criar processo (fork)");
        return;
    } else if (pid == 0) {
        if (modo != NULL && arquivo_redirecionamento != NULL) {
            int fd = -1;
            int destino = STDOUT_FILENO;

            if (strcmp(modo, "output") == 0) {
                fd = open(arquivo_redirecionamento, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            } else if (strcmp(modo, "append") == 0) {
                fd = open(arquivo_redirecionamento, O_WRONLY | O_CREAT | O_APPEND, 0644);
            } else if (strcmp(modo, "input") == 0) {
                fd = open(arquivo_redirecionamento, O_RDONLY);
                destino = STDIN_FILENO;
            }

            if (fd == -1) {
                perror("Erro ao abrir arquivo para redirecionamento");
                exit(1);
            }

            dup2(fd, destino);
            close(fd);
        }

        execvp(t->exec_args[0], t->exec_args);
        fprintf(stderr, "Erro: nao foi possivel executar '%s': ", t->exec_args[0]);
        perror("");
        exit(1);
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            printf("Tarefa '%s' terminou com codigo de saida %d\n", nome, WEXITSTATUS(status));
        }
    }
}

void cmd_run(char *args[], int nargs) {
    if (nargs < 2) {
        printf("Erro: uso correto e 'run <nome>' ou 'run sequential|parallel <nome1> [nome2 ...]'\n");
        return;
    }

    if (strcmp(args[1], "sequential") == 0) {
        if (nargs < 3) {
            printf("Erro: uso correto e 'run sequential <nome1> [nome2 ...]'\n");
            return;
        }
        for (int i = 2; i < nargs; i++) {
            run_single_task(args[i], NULL, NULL);
        }
        return;
    }

    if (strcmp(args[1], "pipe") == 0) {
        if (nargs < 4) {
            printf("Erro: uso correto e 'run pipe <nome1> <nome2> [nome3 ...]'\n");
            return;
        }

        int n = nargs - 2;
        Task *pipeline_tasks[MAX_ARGS];
        pid_t pids[MAX_ARGS];
        char *names[MAX_ARGS];
        int pipefds[2 * (MAX_ARGS - 1)];

        for (int i = 0; i < n; i++) {
            Task *t = find_task(args[i + 2]);
            if (t == NULL) {
                printf("Erro: tarefa '%s' nao encontrada\n", args[i + 2]);
                return;
            }
            pipeline_tasks[i] = t;
            names[i] = args[i + 2];
        }

        for (int i = 0; i < n - 1; i++) {
            if (pipe(pipefds + i * 2) < 0) {
                perror("Erro ao criar pipe");
                for (int j = 0; j < i * 2; j++) {
                    close(pipefds[j]);
                }
                return;
            }
        }

        int total = 0;
        for (int i = 0; i < n; i++) {
            pid_t pid = fork();

            if (pid < 0) {
                perror("Erro ao criar processo (fork)");
                continue;
            } else if (pid == 0) {
                if (i > 0) {
                    dup2(pipefds[(i - 1) * 2], STDIN_FILENO);
                }

                if (i < n - 1) {
                    dup2(pipefds[i * 2 + 1], STDOUT_FILENO);
                }

                for (int j = 0; j < 2 * (n - 1); j++) {
                    close(pipefds[j]);
                }

                execvp(pipeline_tasks[i]->exec_args[0], pipeline_tasks[i]->exec_args);
                fprintf(stderr, "Erro: nao foi possivel executar '%s': ", pipeline_tasks[i]->exec_args[0]);
                perror("");
                exit(1);
            } else {
                pids[total] = pid;
                total++;
            }
        }

        //fechar so os indices pares (i += 2) fechava apenas as pontas de leitura dos pipes, deixando as pontas de escrita abertas no processo pai. Isso travava o programa, porque o ultimo processo do pipeline nunca recebia o EOF (so recebe quando TODAS as copias da ponta de escrita, em todos os processos, forem fechadas). A correcao fecha todos os indices, pares e impares.
        for (int i = 0; i < 2 * (n - 1); i++) {
            close(pipefds[i]);
        }

        for (int i = 0; i < total; i++) {
            int status;
            waitpid(pids[i], &status, 0);
            if (WIFEXITED(status)) {
                printf("Tarefa '%s' (pid %d) terminou com codigo de saida %d\n", names[i], pids[i], WEXITSTATUS(status));
            }
        }
        return;
    }

    if (strcmp(args[1], "parallel") == 0) {
        if (nargs < 3) {
            printf("Erro: uso correto e 'run parallel <nome1> [nome2 ...]'\n");
            return;
        }

        pid_t pids[MAX_ARGS];
        char *names[MAX_ARGS];
        int total = 0;

        for (int i = 2; i < nargs; i++) {
            Task *t = find_task(args[i]);
            if (t == NULL) {
                printf("Erro: tarefa '%s' nao encontrada\n", args[i]);
                continue;
            }

            pid_t pid = fork();

            if (pid < 0) {
                perror("Erro ao criar processo (fork)");
                continue;
            } else if (pid == 0) {
                execvp(t->exec_args[0], t->exec_args);
                fprintf(stderr, "Erro: nao foi possivel executar '%s': ", t->exec_args[0]);
                perror("");
                exit(1);
            } else {
                pids[total] = pid;
                names[total] = args[i];
                total++;
            }
        }

        for (int i = 0; i < total; i++) {
            int status;
            waitpid(pids[i], &status, 0);
            if (WIFEXITED(status)) {
                printf("Tarefa '%s' (pid %d) terminou com codigo de saida %d\n", names[i], pids[i], WEXITSTATUS(status));
            }
        }
        return;
    }

    if (nargs >= 3 &&
        (strcmp(args[2], "output") == 0 ||
         strcmp(args[2], "append") == 0 ||
         strcmp(args[2], "input") == 0)) {
        if (nargs < 4) {
            printf("Erro: uso correto e 'run <nome> output|append|input <arquivo>'\n");
            return;
        }
        run_single_task(args[1], args[2], args[3]);
    } else {
        run_single_task(args[1], NULL, NULL);
    }
}

void cmd_start(char *args[], int nargs) {
    if (nargs < 2) {
        printf("Erro: uso correto e 'start <nome>'\n");
        return;
    }

    Task *t = find_task(args[1]);
    if (t == NULL) {
        printf("Erro: tarefa '%s' nao encontrada\n", args[1]);
        return;
    }

    if (job_count >= MAX_TASKS) {
        printf("Erro: limite de jobs em background atingido\n");
        return;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("Erro ao criar processo (fork)");
        return;
    } else if (pid == 0) {
        execvp(t->exec_args[0], t->exec_args);
        fprintf(stderr, "Erro: nao foi possivel executar '%s': ", t->exec_args[0]);
        perror("");
        exit(1);
    } else {
        int id = job_count + 1;
        jobs[job_count].job_id = id;
        jobs[job_count].pid = pid;
        strncpy(jobs[job_count].name, args[1], MAX_NAME - 1);
        jobs[job_count].name[MAX_NAME - 1] = '\0';
        jobs[job_count].finished = 0;
        jobs[job_count].exit_code = 0;
        job_count++;
        printf("[%d] %d\n", id, (int)pid);
    }
}

void cmd_jobs(void) {
    if (job_count == 0) {
        printf("Nenhum job em background\n");
        return;
    }

    for (int i = 0; i < job_count; i++) {
        if (!jobs[i].finished) {
            int status;
            pid_t r = waitpid(jobs[i].pid, &status, WNOHANG);
            if (r == jobs[i].pid) {
                jobs[i].finished = 1;
                if (WIFEXITED(status)) {
                    jobs[i].exit_code = WEXITSTATUS(status);
                }
            }
        }

        if (jobs[i].finished) {
            printf("[%d] %d %s Concluido codigo %d\n", jobs[i].job_id, (int)jobs[i].pid, jobs[i].name, jobs[i].exit_code);
        } else {
            printf("[%d] %d %s Rodando\n", jobs[i].job_id, (int)jobs[i].pid, jobs[i].name);
        }
    }
}

void cmd_wait(char *args[], int nargs) {
    if (nargs < 2) {
        printf("Erro: uso correto e 'wait <jobId>'\n");
        return;
    }

    int id = atoi(args[1]);

    for (int i = 0; i < job_count; i++) {
        if (jobs[i].job_id == id) {
            if (jobs[i].finished) {
                printf("Job %d (%s) ja tinha terminado, codigo de saida %d\n", id, jobs[i].name, jobs[i].exit_code);
                return;
            }

            int status;
            waitpid(jobs[i].pid, &status, 0);
            jobs[i].finished = 1;
            if (WIFEXITED(status)) {
                jobs[i].exit_code = WEXITSTATUS(status);
            }
            printf("Job %d (%s) terminou com codigo de saida %d\n", id, jobs[i].name, jobs[i].exit_code);
            return;
        }
    }

    printf("Erro: job %d nao encontrado\n", id);
}

void cmd_workdir(char *args[], int nargs) {
    if (nargs < 2) {
        printf("Erro: uso correto e 'workdir <diretorio>'\n");
        return;
    }

    int resultado = chdir(args[1]);
    if (resultado == 0) {
        printf("Diretorio de trabalho alterado para '%s'\n", args[1]);
    } else {
        perror("Erro ao alterar diretorio");
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    char line[MAX_LINE];

    while (1) {
        printf("processflow> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        line[strcspn(line, "\n")] = '\0';

        if (strlen(line) == 0) {
            continue;
        }

        char *args[MAX_ARGS];
        int nargs = parse_line(line, args);

        if (nargs == 0) {
            continue;
        }

        if (strcmp(args[0], "exit") == 0) {
            break;
        } else if (strcmp(args[0], "task") == 0) {
            cmd_task(args, nargs);
        } else if (strcmp(args[0], "run") == 0) {
            cmd_run(args, nargs);
        } else if (strcmp(args[0], "workdir") == 0) {
            cmd_workdir(args, nargs);
        } else if (strcmp(args[0], "start") == 0) {
            cmd_start(args, nargs);
        } else if (strcmp(args[0], "jobs") == 0) {
            cmd_jobs();
        } else if (strcmp(args[0], "wait") == 0) {
            cmd_wait(args, nargs);
        } else {
            printf("Comando desconhecido: %s\n", args[0]);
        }
    }

    return 0;
}
