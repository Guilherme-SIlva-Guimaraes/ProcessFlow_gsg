# ProcessFlow

ProcessFlow e um mini gerenciador de tarefas feito em C. Ele permite cadastrar comandos como tarefas e executa-los usando chamadas de sistema POSIX, como fork, execvp, waitpid, pipe e dup2.

## Funcionalidades

- Cadastro de tarefas com nome, programa e argumentos.
- Execucao de uma tarefa individual.
- Execucao sequencial de varias tarefas.
- Execucao paralela de varias tarefas.
- Execucao de tarefas encadeadas por pipe.
- Redirecionamento de entrada e saida.
- Execucao de tarefas em background.
- Listagem e espera de jobs em background.
- Alteracao do diretorio de trabalho.
- Execucao interativa ou por arquivo de workflow.

## Pre-requisitos

- Linux ou WSL no Windows (o projeto foi desenvolvido e testado no Windows com WSL2, distribuicao Ubuntu).
- gcc.
- make.

O programa usa chamadas de sistema POSIX, entao nao deve ser executado diretamente no Windows sem WSL ou ambiente equivalente.

## Como compilar

Use o make:

```bash
make
```

Isso gera o executavel processflow.

Tambem e possivel compilar diretamente com:

```bash
gcc -Wall -Wextra -g main.c -o processflow
```

Para limpar o executavel gerado:

```bash
make clean
```

## Como executar

Modo interativo:

```bash
./processflow
```

Modo workflow, usando um arquivo .pf:

```bash
./processflow teste.pf
```

Para encerrar o programa, use:

```text
exit
```

## Comandos disponiveis

- task nome programa argumentos: cadastra uma tarefa.
- run nome: executa uma tarefa.
- run nome output arquivo: redireciona a saida para um arquivo.
- run nome append arquivo: adiciona a saida ao final de um arquivo.
- run nome input arquivo: usa um arquivo como entrada.
- run sequential nome1 nome2: executa tarefas em sequencia.
- run parallel nome1 nome2: executa tarefas em paralelo.
- run pipe nome1 nome2: liga tarefas usando pipe.
- start nome: executa uma tarefa em background.
- jobs: lista os jobs em background.
- wait jobId: espera um job terminar.
- workdir diretorio: altera o diretorio de trabalho.
- exit: encerra o programa.

## Exemplo de uso

No modo interativo:

```text
processflow> task listar /bin/ls
Tarefa 'listar' cadastrada.
processflow> task contar /usr/bin/wc -l
Tarefa 'contar' cadastrada.
processflow> run pipe listar contar
Tarefa 'listar' (pid 1234) terminou com codigo de saida 0
Tarefa 'contar' (pid 1235) terminou com codigo de saida 0
processflow> exit
```

Usando o arquivo teste.pf:

```bash
./processflow teste.pf
```

Conteudo do exemplo:

```text
task ola /bin/echo ola workflow
run ola
task listar /bin/ls
task contar /usr/bin/wc -l
run pipe listar contar
exit
```

## Como testar

Para validar rapidamente todas as funcionalidades principais de uma vez, use o arquivo de workflow de exemplo:

```bash
./processflow teste.pf
```

Para testar casos de erro (tarefa inexistente, diretorio inexistente em workdir, run sem argumentos suficientes, arquivo de workflow que nao existe, etc.), rode o modo interativo (`./processflow`) e digite os comandos manualmente.

As sessoes de teste reais, incluindo os casos de sucesso e de erro, ficam registradas em `evidencias.log`, gravado com `script -a evidencias.log`.

## Estrutura do projeto

```text
main.c
makefile
teste.pf
evidencias.log
README.md
```

- main.c: codigo-fonte principal do ProcessFlow.
- makefile: automatiza compilacao, execucao e limpeza.
- teste.pf: exemplo de arquivo de workflow.
- evidencias.log: registro de testes feitos no terminal.

