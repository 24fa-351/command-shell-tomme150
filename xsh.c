#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAX_INPUT 1024
#define MAX_ARGS 100
#define MAX_ENV 100

typedef struct {
    char *name;
    char *value;
} EnvVar;

EnvVar env_vars[MAX_ENV];
int env_count = 0;

void trim_whitespace(char *str) {
    char *end;
    while (*str == ' ') str++; 
    if (*str == 0) return; 
    end = str + strlen(str) - 1;
    while (end > str && *end == ' ') end--; 
    *(end + 1) = '\0';
}

char *replace_env_vars(char *input) {
    static char buffer[MAX_INPUT];
    strcpy(buffer, input);

    for (int i = 0; i < env_count; i++) {
        if (env_vars[i].name) {
            char placeholder[MAX_INPUT];
            sprintf(placeholder, "$%s", env_vars[i].name);
            char *pos = strstr(buffer, placeholder);

            if (pos) {
                char temp[MAX_INPUT];
                strcpy(temp, pos + strlen(placeholder));
                strcpy(pos, env_vars[i].value);
                strcat(buffer, temp);
            }
        }
    }
    return buffer;
}

void execute_command(char *cmd, int run_in_background) {
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    char command[MAX_INPUT];
    strcpy(command, cmd);

    if (!CreateProcess(
            NULL, 
            command, 
            NULL, 
            NULL, 
            FALSE, 
            run_in_background ? CREATE_NO_WINDOW : 0, 
            NULL, 
            NULL, 
            &si, 
            &pi 
    )) {
        fprintf(stderr, "xsh: command not found: %s\n", cmd);
        return;
    }

    if (!run_in_background) {
        WaitForSingleObject(pi.hProcess, INFINITE); // Wait for process to complete
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

void handle_builtin_commands(char *command) {
    if (strncmp(command, "cd ", 3) == 0) {
        char *path = command + 3;
        trim_whitespace(path);
        if (!SetCurrentDirectory(path)) {
            fprintf(stderr, "xsh: cd: %s: No such file or directory\n", path);
        }
        return;
    }

    if (strcmp(command, "pwd") == 0) {
        char cwd[MAX_PATH];
        if (GetCurrentDirectory(MAX_PATH, cwd)) {
            printf("%s\n", cwd);
        } else {
            fprintf(stderr, "xsh: pwd: Failed to get current directory\n");
        }
        return;
    }

    if (strncmp(command, "set ", 4) == 0) {
        char *name = strtok(command + 4, " ");
        char *value = strtok(NULL, "");
        if (name && value) {
            for (int i = 0; i < env_count; i++) {
                if (env_vars[i].name && strcmp(env_vars[i].name, name) == 0) {
                    free(env_vars[i].value);
                    env_vars[i].value = strdup(value);
                    return;
                }
            }
            env_vars[env_count].name = strdup(name);
            env_vars[env_count].value = strdup(value);
            env_count++;
        }
        return;
    }

    if (strncmp(command, "unset ", 6) == 0) {
        char *name = command + 6;
        for (int i = 0; i < env_count; i++) {
            if (env_vars[i].name && strcmp(env_vars[i].name, name) == 0) {
                free(env_vars[i].name);
                free(env_vars[i].value);
                env_vars[i].name = env_vars[i].value = NULL;
            }
        }
        return;
    }
}

void handle_redirection(char *command, char **redirect_in, char **redirect_out, int *run_in_background) {
    *redirect_in = *redirect_out = NULL;
    *run_in_background = 0;

    char *in_pos = strchr(command, '<');
    char *out_pos = strchr(command, '>');
    char *bg_pos = strchr(command, '&');

    if (bg_pos) {
        *run_in_background = 1;
        *bg_pos = '\0';
    }
    if (in_pos) {
        *redirect_in = strtok(in_pos + 1, " ");
        *in_pos = '\0';
    }
    if (out_pos) {
        *redirect_out = strtok(out_pos + 1, " ");
        *out_pos = '\0';
    }
    trim_whitespace(command);
}

void execute_with_redirection(char *command) {
    char *redirect_in, *redirect_out;
    int run_in_background;

    handle_redirection(command, &redirect_in, &redirect_out, &run_in_background);

    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    HANDLE hIn = NULL, hOut = NULL;

    if (redirect_in) {
        hIn = CreateFile(redirect_in, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hIn == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "xsh: Failed to open input file: %s\n", redirect_in);
            return;
        }
        si.hStdInput = hIn;
        si.dwFlags |= STARTF_USESTDHANDLES;
    }

    if (redirect_out) {
        hOut = CreateFile(redirect_out, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hOut == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "xsh: Failed to open output file: %s\n", redirect_out);
            return;
        }
        si.hStdOutput = hOut;
        si.dwFlags |= STARTF_USESTDHANDLES;
    }

    char command_buffer[MAX_INPUT];
    strcpy(command_buffer, command);

    if (!CreateProcess(NULL, command_buffer, NULL, NULL, TRUE, run_in_background ? CREATE_NO_WINDOW : 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "xsh: Failed to execute command: %s\n", command);
        return;
    }

    if (!run_in_background) {
        WaitForSingleObject(pi.hProcess, INFINITE);
    }

    if (hIn) CloseHandle(hIn);
    if (hOut) CloseHandle(hOut);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

int main() {
    char input[MAX_INPUT];

    while (1) {
        printf("xsh# ");
        fflush(stdout);

        if (!fgets(input, MAX_INPUT, stdin)) break;
        input[strcspn(input, "\n")] = '\0';
        trim_whitespace(input);

        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) break;

        char *expanded_input = replace_env_vars(input);

        if (strncmp(expanded_input, "cd", 2) == 0 || strncmp(expanded_input, "pwd", 3) == 0 ||
            strncmp(expanded_input, "set", 3) == 0 || strncmp(expanded_input, "unset", 5) == 0) {
            handle_builtin_commands(expanded_input);
            continue;
        }

        execute_with_redirection(expanded_input);
    }
    return 0;
}
