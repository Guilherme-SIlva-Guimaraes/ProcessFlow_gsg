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
    char *exec_args[MAX_ARGS]; // exec_args[0] = programa, resto = argumentos, terminado em NULL(ajuda de IA)
} Task;

Task tasks[MAX_TASKS];
int task_count = 0;

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

    // args[2] em diante = programa + argumentos do programa.
    // Usamos strdup porque args[] aponta pra dentro de 'line', que sera
    // reaproveitada na proxima iteracao do loop - sem copiar, os dados
    // dessa tarefa seriam corrompidos assim que o usuario digitasse outra linha(ajude de IA )
    int j = 0;
    for (int i = 2; i < nargs; i++) {
        t->exec_args[j++] = strdup(args[i]);
    }
    t->exec_args[j] = NULL;

    task_count++;
    printf("Tarefa '%s' cadastrada.\n", args[1]);
}

// Executa uma unica tarefa ja cadastrada: fork + execvp + waitpid, imprimindo
// o codigo de saida. Usada tanto pelo 'run <nome>' simples quanto pelo
// 'run sequential' (uma tarefa de cada vez, esperando terminar).
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
        // Processo filho: aqui configuramos o redirecionamento antes do execvp.
        if (modo != NULL && arquivo_redirecionamento != NULL) {
            int fd = -1;
            int destino = STDOUT_FILENO;

            // Aqui o filho abre o arquivo informado pelo usuario.
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

            // Aqui o descritor aberto substitui um descritor padrao do processo.
            dup2(fd, destino);
            close(fd);
        }

        // Aqui o execvp troca o processo filho pelo programa da tarefa.
        execvp(t->exec_args[0], t->exec_args);
        // So chega aqui se o execvp falhou
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
        // Sequencial: chama run_single_task uma vez por tarefa, e como essa
        // funcao ja faz o waitpid antes de retornar, a proxima tarefa da
        // lista so comeca depois que a anterior terminou de verdade.
        for (int i = 2; i < nargs; i++) {
            run_single_task(args[i], NULL, NULL);
        }
        return;
    }

    if (strcmp(args[1], "parallel") == 0) {
        if (nargs < 3) {
            printf("Erro: uso correto e 'run parallel <nome1> [nome2 ...]'\n");
            return;
        }

        // Paralelo: primeiro da fork em TODAS as tarefas (sem esperar
        // nenhuma), guardando os PIDs; so depois que todas ja foram
        // iniciadas e que a gente espera (waitpid) uma por uma.
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

    // run <nome> sozinho ou com redirecionamento simples
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
        } else {
            printf("Comando desconhecido: %s\n", args[0]);
        }
    }

    return 0;
}
