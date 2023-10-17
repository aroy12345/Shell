#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

pid_t pid, original_pgid;
int status = 0;

char *duplicate_string(char *str)
{
    if (str == NULL)
    {
        return NULL;
    }

    int length = 0;
    while (str[length] != '\0')
    {
        length++;
    }

    char *duplicated_str = malloc(length + 1);
    if (!duplicated_str)
    {
        return NULL;
    }

    for (int i = 0; i < length; i++)
    {
        duplicated_str[i] = str[i];
    }
    duplicated_str[length] = '\0';

    return duplicated_str;
}

typedef struct Process
{
    pid_t pid;
    struct Process *next;
} Process;

typedef struct Job
{
    int job_id;
    int status; // 0 = running, 1 = stopped
    int pgid;
    char *command;
    struct Job *next;
    struct Job *prev;
    struct Process *child;
} Job;

Job *first_process = NULL;
Job *last_process = NULL;

void jobs()
{
    Job *current = first_process;
    if (current == NULL)
    {
        printf("No jobs\n");
        return; // no jobs
    }

    while (current != NULL)
    {
        printf("[%d] %s (%s)\n", current->job_id, current->command, current->status == 0 ? "running" : "stopped");
        current = current->prev;
    }
}

void remove_job(int job_id)
{
    Job *current = last_process;

    if (current == NULL)
    {
        return;
    }

    while (current != NULL)
    {
        if (current->job_id == job_id)
        {
            if (current->prev != NULL)
            {
                current->prev->next = current->next;
                if (current->next == NULL)
                {
                    first_process = current->prev;
                }
                else
                {
                    current->next->prev = current->prev;
                }
            }
            else if (last_process == first_process)
            {
                last_process = NULL;
                first_process = NULL;
            }
            else
            {
                last_process = current->next;
                last_process->prev = NULL;
            }

            free(current->command);
            free(current);

            return;
        }
        current = current->next;
    }
}
Job *find_job_by_id(int job_id)
{
    Job *current = last_process;
    while (current != NULL)
    {
        if (current->job_id == job_id)
        {
            return current;
        }
        current = current->next;
    }
    return NULL; // job not found
}

void clear_all_jobs()
{
    Job *current = last_process;
    while (current)
    {
        Job *next = current->next;
        Process *child = current->child;
        while (child)
        {
            Process *next_child = child->next;
            free(child);
            child = next_child;
        }
        free(current->command);
        free(current);
        current = next;
    }
    free(first_process);
    free(last_process);
}

void bg(int job_id)
{
    Job *job;
    if (job_id == -1)
    {
        Job *current = last_process;
        while (current != NULL)
        {
            if (current->status == 1)
            {
                job = current;
            }
            current = current->next;
        }
        if (!job)
            job = first_process;
    }
    else
    {
        job = find_job_by_id(job_id);
    }
    if (!job)
    {
        return;
    }
    if (!job || job->status == 0)
    {
        fprintf(stderr, "Error: Cannot resume job\n");
        return;
    }
    job->status = 0;
    kill(-job->pgid, SIGCONT);
    printf("Running: %s\n", job->command);
}

void fg(int job_id)
{
    Job *job = last_process;
    if (job_id == -1)
    {
        Job *current = last_process;
        while (current != NULL)
        {
            if (current->status == 1)
            {
                job = current;
                job_id = current->job_id;
                break;
            }
            current = current->next;
        }
        if (job == last_process)
        {
            job_id = job->job_id;
        }
    }
    else
    {
        job = find_job_by_id(job_id);
    }
    if (!job)
    {
        return;
    }

    if (job->status == 1)
    {
        job->status = 0;
        printf("Restarting: %s\n", job->command);
        kill(-job->pgid, SIGCONT);
    }
    else
    {
        printf("Running: %s\n", job->command);
    }

    int status;
    tcsetpgrp(STDIN_FILENO, job->pgid);
    waitpid(-job->pgid, &status, WUNTRACED);
    signal(SIGTTOU, SIG_IGN);
    tcsetpgrp(STDIN_FILENO, getpid());

    if (WIFEXITED(status) || WIFSIGNALED(status))
    {
        remove_job(job_id);
    }
    else if (WIFSTOPPED(status))
    {
        printf("Stopped: %s\n", job->command);
        job->status = 1;
    }
}

void set_job_status(int job_id, int status)
{
    Job *job = find_job_by_id(job_id);
    if (job != NULL)
    {
        job->status = status;
    }
}

int add_job(char *command, pid_t child_pid, int status)
{
    static int job_id = 1;
    Job *new_job = malloc(sizeof(Job));
    Process *new_process = malloc(sizeof(Process));
    new_process->pid = child_pid;
    new_job->job_id = job_id++;
    new_job->command = duplicate_string(command);
    new_job->child = new_process;
    new_job->status = status;
    new_job->pgid = child_pid;
    if (first_process == NULL)
    {
        first_process = new_job;
    }
    if (last_process != NULL)
    {
        new_job->next = last_process;
        last_process->prev = new_job;
    }
    last_process = new_job;
    return job_id - 1;
}

int execute_builtin_command(char *command)
{
    char *command_copy = duplicate_string(command);

    char *command_name = NULL;
    int id = -1;
    char *token = strtok(command_copy, " ");
    command_name = token;
    if (command_name)
    {
        if (strcmp(command_name, "jobs") == 0)
        {
            jobs();
            free(command_copy);
            return 1;
        }
        else if (strcmp(command_name, "bg") != 0 && strcmp(command_name, "fg") != 0)
        {
            free(command_copy);
            return 0;
        }
        token = strtok(NULL, " ");
        free(command_copy);
        if (token != NULL)
        {
            if ((id = atoi(token)) == 0)
            {
                perror("invalid argument");
                return 1;
            }
        }

        if (strcmp(command_name, "bg") == 0)
        {
            if (last_process == NULL)
            {
                perror("no jobs");
            }
            else
            {
                bg(id);
            }
            return 1;
        }
        else if (strcmp(command_name, "fg") == 0)
        {
            if (last_process == NULL)
            {
                perror("no jobs");
            }
            else
            {
                fg(id);
            }
            return 1;
        }
        else
        {
            return 0;
        }
    }
    else
    {
        free(command_copy);
        return 0;
    }
}

void add_process(int job_id, pid_t pid)
{
    Process *new_process = malloc(sizeof(Process));
    new_process->pid = pid;
    Job *current = last_process;
    while (current)
    {
        if (current->job_id == job_id)
        {
            break;
        }
        current = current->next;
    }
    new_process->next = current->child;
    current->child = new_process;
}

char *remove_process(pid_t pid)
{
    Job *current = last_process;
    while (current)
    {
        Process *process = current->child;
        Process *prev_process = NULL;
        while (process)
        {
            if (process->pid == pid)
            {
                if (prev_process == NULL)
                {
                    free(process);
                    char *command = duplicate_string(current->command);
                    remove_job(current->job_id);
                    return command;
                }
                prev_process->next = process->next;
                free(process);
                return NULL;
            }
            prev_process = process;
            process = process->next;
        }
        current = current->next;
    }
    return NULL; // If the job wasn't found
}

void poll_background_jobs()
{
    pid_t wpid;
    int status;
    char *command;
    while ((wpid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0)
    {
        if (WIFSTOPPED(status))
        {
            pid_t pgid = getpgid(wpid);
            Job *current = last_process;
            while (current)
            {
                if (current->pgid == pgid)
                {
                    if (current->status == 1)
                    {
                        break;
                    }
                    current->status = 1;
                    printf("Stopped: %s\n", current->command);
                    break;
                }
                current = current->next;
            }
        }
        else
        {
            command = remove_process(wpid);
            if (command)
            {
                printf("Finished: %d\n", wpid);
            }
            free(command);
        }
    }
}

int countPipes(char *input)
{
    int count = 0;
    while (*input)
    {
        if (*input == '|')
        {
            count++;
        }
        input++;
    }
    return count;
}

int fileExists(char *filename)
{
    int fd = open(filename, O_RDONLY);
    if (fd != -1)
        return 1;
    else
        return 0;
}

int isEmpty(char *input)
{
    while (*input)
    {
        if (*input != ' ' && *input != '\t' && *input != '\n')
        {
            return 0;
        }
        input++;
    }
    return 1;
}

void writePrompt()
{
    if (isatty(STDIN_FILENO))
        write(STDERR_FILENO, PROMPT, strlen(PROMPT));
}

void parent_handler(int signo)
{
    printf("\n");
    if (signo == SIGQUIT)
    {
        poll_background_jobs();
        clear_all_jobs();
        exit(EXIT_SUCCESS);
    }
    if (pid != 0)
    {
        if (signo == SIGINT)
        {
            if (kill(-getpgid(pid), SIGKILL) == -1)
            {
                perror("kill");
                exit(EXIT_FAILURE);
            }
        }
        else if (signo == SIGTSTP)
        {
            if (kill(-getpgid(pid), SIGSTOP) == -1)
            {
                perror("kill");
                exit(EXIT_FAILURE);
            }
        }
        else if (signo == SIGTTOU)
        {
            if (kill(-getpgid(pid), SIGSTOP) == -1)
            {
                perror("kill");
                exit(EXIT_FAILURE);
            }
        }
    }
    writePrompt();
}

void child_handler(int signo)
{
    printf("\n");
    if (signo == SIGINT)
    {
        if (kill(getpid(), SIGKILL) == -1)
        {
            exit(EXIT_FAILURE);
        }
    }
    else if (signo == SIGTSTP)
    {
        if (kill(getpid(), SIGSTOP) == -1)
        {
            exit(EXIT_FAILURE);
        }
    }
    else if (signo == SIGTTIN)
    {
        if (kill(-getpgrp(), SIGSTOP) == -1)
        {
            perror("kill");
            exit(EXIT_FAILURE);
        }
    }
    else if (signo == SIGTTOU)
    {
        if (kill(-getpgrp(), SIGSTOP) == -1)
        {
            perror("kill");
            exit(EXIT_FAILURE);
        }
    }
    else if (signo == SIGQUIT)
    {
        poll_background_jobs();
        clear_all_jobs();
        exit(EXIT_SUCCESS);
    }
}

char **reallocate(char **input, int numChar)
{
    char **ret = malloc(sizeof(char *) * numChar);
    for (int i = 0; i < numChar - 1; i++)
    {
        ret[i] = input[i];
    }
    free(input);
    return ret;
}

char **getArgs(char *input)
{
    int numArgs = 1;
    char **ret = NULL;
    char *word = strtok(input, "\t ");
    while (word != NULL)
    {
        ret = reallocate(ret, numArgs);
        ret[numArgs++ - 1] = word;
        word = strtok(NULL, "\t ");
    }
    ret = reallocate(ret, numArgs);
    ret[numArgs - 1] = NULL;
    return ret;
}

void execute(char *input, int hasInput, int hasOutput)
{
    char **args = getArgs(input);
    char *input_file = NULL;
    char *output_file = NULL;

    int appendOutput = 0;
    for (int i = 0; args[i]; i++)
    {
        if (strcmp(args[i], "<") == 0)
        {
            if (hasInput)
            {
                perror("Invalid: more than one input redirection into a process.\n");
                exit(EXIT_FAILURE);
            }
            args[i] = NULL;
            input_file = args[++i];
            int fd = open(input_file, O_RDONLY);
            if (fd == -1)
            {
                perror("Input file doesn't exist.\n");
                exit(EXIT_FAILURE);
            }
            hasInput = 1;
        }
        else if (strcmp(args[i], ">") == 0)
        {
            if (hasOutput)
            {
                perror("Invalid: more than one output redirection into a process.\n");
                exit(EXIT_FAILURE);
            }
            args[i] = NULL;
            output_file = args[++i];
            hasOutput = 1;
        }
        else if (strcmp(args[i], ">>") == 0)
        {
            if (hasOutput)
            {
                perror("Invalid: more than one output redirection into a process.\n");
                exit(EXIT_FAILURE);
            }
            args[i] = NULL;
            output_file = args[++i];
            if (fileExists(output_file))
            {
                appendOutput = 1;
            }
            hasOutput = 1;
        }
    }
    if (strlen(input) != 0)
    {
        if (input_file)
        {
            int fd = open(input_file, O_RDONLY);
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        if (output_file && appendOutput == 0)
        {
            int fd2;
            if (fileExists(output_file))
            {
                fd2 = open(output_file, O_WRONLY | O_TRUNC);
            }
            else
            {
                fd2 = open(output_file, O_WRONLY | O_CREAT, 0644);
            }

            dup2(fd2, STDOUT_FILENO);
            close(fd2);
        }

        if (output_file && appendOutput == 1)
        {
            int fd2 = open(output_file, O_WRONLY | O_APPEND);
            dup2(fd2, STDOUT_FILENO);
            close(fd2);
        }
        pid = fork();
        if (pid < 0)
        {
            perror("Fork error.\n");
            exit(EXIT_FAILURE);
        }
        if (pid == 0)
        {

            if (args[0] && args)
            {
                if (execvp(args[0], args) == -1)
                {
                    perror("Invalid executable.\n");
                    exit(EXIT_FAILURE);
                }
            } else {
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            wait(NULL);
        }
    }
    free(args);
}

int main(int argc, char *argv[])
{

    if (argc != 3 && argc != 1)
    {
        write(STDERR_FILENO, "Invalid number of arguments (only 0 or 2 accepted).\n", strlen("Invalid number of arguments (only 0 or 2 accepted).\n"));
        exit(EXIT_FAILURE);
    }

    if (signal(SIGINT, parent_handler) == SIG_ERR)
    {
        perror("signal\n");
        exit(EXIT_FAILURE);
    }
    if (signal(SIGTSTP, parent_handler) == SIG_ERR)
    {
        perror("signal\n");
        exit(EXIT_FAILURE);
    }
    if (signal(SIGQUIT, parent_handler) == SIG_ERR)
    {
        perror("signal\n");
        exit(EXIT_FAILURE);
    }

    while (1)
    {
        char input[4096] = "";
        poll_background_jobs();
        original_pgid = getpgid(getpid());
        int n = 0;
        writePrompt();
        // if (isatty(STDIN_FILENO))
        // {
        n = read(STDIN_FILENO, input, sizeof(input) - 1);
        // } else {
        //     n = getline(&input, &n, STDIN_FILENO);
        // }
        if (n < 0)
        {
            poll_background_jobs();
            clear_all_jobs();
            write(STDERR_FILENO, "\n", strlen("\n"));
            exit(EXIT_FAILURE);
        }

        else if (n == 0)
        {
            poll_background_jobs();
            clear_all_jobs();

            write(STDERR_FILENO, "\n", strlen("\n"));
            exit(EXIT_SUCCESS);
        }

        else
        {
            if (isEmpty(input))
                continue;
            if (input[n - 1] == '\n')
            {
                input[n - 1] = '\0';
            }
            else
            {
                input[n] = '\0';
            }
        }
        if (execute_builtin_command(input) == 1)
        {
            continue;
        }
        int numPipes = countPipes(input);
        int initialPipes = numPipes;
        int prev_pipe_read = STDIN_FILENO;

        int background = (input[strlen(input) - 1] == '&');
        if (background)
        {
            input[strlen(input) - 1] = '\0';
        }

        int numChildren = 0;
        char *input_copy = duplicate_string(input);
        char *token = strtok(input_copy, "|");
        while (token != NULL)
        {
            numChildren++;
            token = strtok(NULL, "|");
        }
        free(input_copy);
        input_copy = duplicate_string(input);
        char *command = strtok(input, "|");
        pid_t childPIDs[numChildren];
        int count = 0;
        int job_id = -1;
        while (command != NULL)
        {
            int pipes[2];
            if (numPipes-- > 0)
            {
                if (pipe(pipes) == -1)
                {
                    perror("Pipe error.\n");
                    exit(EXIT_FAILURE);
                }
            }

            pid = fork();
            if (pid < 0)
            {
                perror("Fork error.\n");
                exit(EXIT_FAILURE);
            }

            if (pid == 0)
            {
                if (signal(SIGINT, child_handler) == SIG_ERR)
                {
                    perror("signal\n");
                    exit(EXIT_FAILURE);
                }
                if (signal(SIGTSTP, child_handler) == SIG_ERR)
                {
                    perror("signal\n");
                    exit(EXIT_FAILURE);
                }
                if (signal(SIGTTIN, child_handler) == SIG_ERR)
                {
                    perror("signal\n");
                    exit(EXIT_FAILURE);
                }
                if (signal(SIGTTOU, child_handler) == SIG_ERR)
                {
                    perror("signal\n");
                    exit(EXIT_FAILURE);
                }
                if (signal(SIGQUIT, child_handler) == SIG_ERR)
                {
                    perror("signal\n");
                    exit(EXIT_FAILURE);
                }
                if (count == 0)
                { // This is the first child in the pipeline
                    setpgid(getpid(), getpid());
                    if (!background)
                    {
                        tcsetpgrp(STDIN_FILENO, getpid());
                    }
                }
                else
                {
                    setpgid(getpid(), childPIDs[0]); // Set its PGID to the first child's PID
                }

                if (numPipes >= 0)
                {
                    close(pipes[0]); // close reading end, we're going to write to this pipe
                    dup2(pipes[1], STDOUT_FILENO);
                    close(pipes[1]);
                }

                if (prev_pipe_read != STDIN_FILENO)
                {
                    dup2(prev_pipe_read, STDIN_FILENO);
                    close(prev_pipe_read); // close the descriptor after duplicating
                }
                int hasInput = 1;
                int hasOutput = 1;
                if (initialPipes == numPipes + 1)
                    hasInput = 0;
                if (numPipes == -1)
                    hasOutput = 0;
                execute(command, hasInput, hasOutput);
                exit(EXIT_FAILURE);
            }
            else
            {

                if (count == 0)
                {
                    setpgid(pid, pid);
                    if (!background)
                    {
                        tcsetpgrp(STDIN_FILENO, pid);
                    }
                }
                else
                {
                    setpgid(pid, childPIDs[0]);
                }

                if (background)
                {
                    if (job_id == -1)
                    {
                        job_id = add_job(input_copy, pid, 0);
                        printf("Running: %s\n", input_copy); // 1. Added "Running:" message here
                    }
                    else
                    {
                        add_process(job_id, pid);
                    }
                }
                childPIDs[count++] = pid;

                if (prev_pipe_read != STDIN_FILENO)
                {
                    close(prev_pipe_read);
                }
                if (numPipes >= 0)
                {
                    close(pipes[1]);
                    prev_pipe_read = pipes[0];
                }
                if (numPipes < 0 && prev_pipe_read != STDIN_FILENO)
                {

                    close(prev_pipe_read);
                }
            }
            command = strtok(NULL, "|");
        }

        count = 0;
        if (!background)
        {
            int wpid, status;
            while (count != numChildren && (wpid = waitpid(-1 * childPIDs[0], &status, WUNTRACED)) > 0)
            {
                count++;
            }
            if (WIFSTOPPED(status))
            {
                printf("Stopped: %s\n", input_copy);
                job_id = add_job(input_copy, childPIDs[0], 1);
                for (int i = 1; i < numChildren; i++)
                {
                    add_process(job_id, childPIDs[i]);
                }
            }
            else
            {
            }
        }
        signal(SIGTTOU, SIG_IGN);
        if (tcsetpgrp(STDIN_FILENO, original_pgid) == -1)
        {
            perror("tcsetpgrp error.\n");
            exit(EXIT_FAILURE);
        }
        if (!isatty(STDIN_FILENO))
        {
            // input_token = strtok(NULL, "\n");
        }
        free(input_copy);
    }

    poll_background_jobs();
    clear_all_jobs();
    return (0);
}