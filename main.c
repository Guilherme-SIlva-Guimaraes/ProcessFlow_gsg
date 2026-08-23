#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

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

void cmd_run(char *args[], int nargs) {
    if (nargs < 2) {
        printf("Erro: uso correto e 'run <nome>'\n");
        return;
    }

    Task *t = find_task(args[1]);
    if (t == NULL) {
        printf("Erro: tarefa '%s' nao encontrada\n", args[1]);
        return;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("Erro ao criar processo (fork)");
        return;
    } else if (pid == 0) {
        execvp(t->exec_args[0], t->exec_args);
        // So chega aqui se o execvp falhou
        fprintf(stderr, "Erro: nao foi possivel executar '%s': ", t->exec_args[0]);
        perror("");
        exit(1);
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            printf("Tarefa '%s' terminou com codigo de saida %d\n", args[1], WEXITSTATUS(status));
        }
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
