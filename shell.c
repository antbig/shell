#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/types.h>

#define BUF_SIZE 128 //Taille du buffer pour les différents tableaux utilisés

#define PRINT(text) write (STDOUT_FILENO, text, strlen(text));
#define PRINT_EXIT(text) write (STDOUT_FILENO, text, strlen(text)); exit(-1);

/**
Structure utilisé pour 
**/
typedef struct backgroundProcess {
    pid_t id;
    long startTime;
	char running; //1 the process is running, 0 the process has ended
	char cmd[BUF_SIZE];
} BACKGROUND_PROCESS;


void processCommand(int nbCmds, char **cmds);
void executeSubPipecommand(int ncmds, char **cmds, int output[2]);
pid_t executePipeline(int nbCmds, char **cmds);

/*
Il est possible de faire pas mal de chose avec printf
 #define PRINT(text) printf("%s vaut %d\n", " ff ->" #text, text);
*/

char commandInput[BUF_SIZE] = "";

/**
L'objectif est de changer la sortie du process actuel pour le remplacer par le pipe
puis d'éxecuter la sous commande suivante
**/
void executeSubPipecommand(int ncmds, char **cmds, int output[2])
{
    /* On change la sortie pour utiliser le pipe*/
    dup2(output[1], 1);
    close(output[0]);
    close(output[1]);
    processCommand(ncmds, cmds);
}

/**
Obetnir le temps d'execution d'un programme executé en arrière
**/
long getProcessTime(struct rusage usage) {
	double second;
	double ns;
	second = usage.ru_utime.tv_sec + usage.ru_stime.tv_sec;
	ns = usage.ru_utime.tv_usec + usage.ru_stime.tv_usec;
	return (long) (second * 1000 + ns / 1000000);
}

/**
Ici on va parser une commande pour regarder si elle change le STDIN ou le STDOUT
Aussi on va regarder si il n'y a pas un pipe à creer pour executer la commande suivante
nbCmds : le nombre de commande à executer
cmds : Tableau de l'ensemble des commandes à executer
**/ 
void processCommand(int nbCmds, char **cmds) {
	char *tmp_args[BUF_SIZE];
	char *token;
	int fin;
	int fout;
	char *file_stdout = NULL;
	char *file_stdin = NULL;
	int will_be_stdout_file = 0;
	int will_be_stdin_file = 0;
	int numArgs = -1;
	char *input = cmds[nbCmds-1];
	pid_t pid;
    int pipeInput[2]; 

	token = strtok(input, " ");//On va découper l'entrée sur chaque espace (on obtient un pointeur vers le premier élément)
	while(token != NULL) {
		if(*token == '>') {
			will_be_stdout_file = 1;
		} else if(will_be_stdout_file == 1) {
			will_be_stdout_file = 0;
			file_stdout = token;
		} else if(*token == '<') {
			will_be_stdin_file = 1;
		} else if(will_be_stdin_file == 1) {
			will_be_stdin_file = 0;
			file_stdin = token;
		} else {
			tmp_args[++numArgs] = token;
		}
		token = strtok(NULL, " ");//On passe à l'élément suivant
	}
	tmp_args[++numArgs] = NULL;//La liste des arguments doit obligatoirement terminer par un NULL
	
	if(nbCmds > 1) {// Il y a une autre commande à executer après
		if (pipe(pipeInput) != 0) {
            PRINT_EXIT("Failed to create pipe\n");
		}
        if ((pid = fork()) < 0) {
            PRINT_EXIT("Failed to fork\n");
		}
        if (pid == 0) {
            executeSubPipecommand(nbCmds-1, cmds, pipeInput);
        }
        /* On change l'input pour utiliser le pipe */
        dup2(pipeInput[0], 0);
        close(pipeInput[0]);
        close(pipeInput[1]);
	}

	// On va remplacer l'entré par la lecture d'un fichier
	if(file_stdin != NULL) {
		fin = open(file_stdin, O_RDWR, 0666);
		if(fin < 0) {
			PRINT_EXIT("No such file\n");
		}
		dup2(fin, STDIN_FILENO);
	}
	//On remplace la sortie par l'écriture dans un fichier
	if(file_stdout != NULL) {
		fout = open(file_stdout, O_RDWR|O_CREAT, 0666);
		dup2(fout, STDOUT_FILENO);
	}
	
	execvp(tmp_args[0], tmp_args);
	PRINT_EXIT("\nAn error occured")
}

/**
Executer une suite de commande en utilisant des pipes
nbCmds : le nombre de commande à executer
cmds : tableau avec la liste des commandes à executer
**/
pid_t executePipeline(int nbCmds, char **cmds) {
	pid_t pid;
    if ((pid = fork()) < 0) {
        PRINT_EXIT("Failed to fork\n");
	}
    if (pid != 0) {
        return pid;//C'est le parent, on ne fait rien
	}
    processCommand(nbCmds, cmds);
	return 0;
}

/**
Termier le programme 
**/
void quit() {
	PRINT("\nBye bye...\n")
}

int main() {
	struct rusage usage;
	char exit_string[] = "exit\n";
	char tmp_write_string[80];
	int readSize = 0;
	int child_status = -1;
	int tmp_child_status = -1;
	long process_time = 0;
	long seconds;
	long ns;
	struct timespec time_res_start;
	struct timespec time_res_end;
	char *token;
	char *commands[BUF_SIZE];
	int command_nb = 0;
	char background = 0;
	pid_t childpid;
	BACKGROUND_PROCESS backProcess[BUF_SIZE];
	int backTaskCount = 0;
	int i; //On aime les variables comme ça

/*
struct timespec {
   time_t   tv_sec;        * seconds *
   long     tv_nsec;       * nanoseconds *
};
*/

	PRINT("Bienvenue dans le Shell.\nPour quitter, tapez 'exit'.\n")
	while(1) {
		command_nb = 0;
		background = 0;
		
		if(backTaskCount > 0) {//Il y a des taches en arrière plan, on va regarder si elle sont terminées
			for(i=0; i< backTaskCount; i++) {
				if(backProcess[i].running == 1 && wait4(backProcess[i].id, &tmp_child_status, WNOHANG, &usage) == -1) {
					backProcess[i].running = 0;
					PRINT("[")
					sprintf(tmp_write_string, "%d", i+1);
					PRINT(tmp_write_string)
					PRINT("]+ Ended: ")
					PRINT(backProcess[i].cmd)
					PRINT("&\n")
					child_status = tmp_child_status;
					process_time = getProcessTime(usage);
				}
				if(backProcess[i].running == 0) {
					background ++;
				}
			}
		}
		if(background == backTaskCount) {
			backTaskCount = 0;
		}
		background = 0;


		PRINT("enseash")
		if(backTaskCount > 0) {
			PRINT(" [")
			sprintf(tmp_write_string, "%d", backTaskCount);
			PRINT(tmp_write_string)
			PRINT("&]")
		} else if(WIFEXITED(child_status)) {
			PRINT(" [exit:")
			sprintf(tmp_write_string, "%d", WEXITSTATUS(child_status));
			PRINT(tmp_write_string)
			PRINT("|")
			sprintf(tmp_write_string, "%ldms", process_time);
			PRINT(tmp_write_string)
			PRINT("]")
		} else if(WIFSIGNALED(child_status)) {
			PRINT(" [signal:")
			sprintf(tmp_write_string, "%d", WTERMSIG(child_status));
			PRINT(tmp_write_string)
			PRINT("|")
			sprintf(tmp_write_string, "%ldms", process_time);
			PRINT(tmp_write_string)
			PRINT("]")
		}
		child_status = -1;

		PRINT(" % ")
		memset(commandInput, 0, sizeof commandInput);//On va remettre à 0 l'ensemble du tableau
		readSize = read(STDIN_FILENO, commandInput, BUF_SIZE);//Lecture de clavier. !! il y a \n à la fin du tableau
		if(readSize < 0) {//Erreur
			PRINT_EXIT("\nUnable to read command")
		} else if(readSize == 0 || strcmp(exit_string, commandInput) == 0) {//CTRL+D ou exit
			quit();
			return 0;
		} else if(readSize > 1) {
			commandInput[readSize-1] = 0;// Le dernier caractere de l'entrée est un \n, on va donc le remplacer par un 0
			if(commandInput[readSize-2] == '&'){
				commandInput[readSize-2] = 0;
				background = 1;
			}
			token = strtok(commandInput, "|");//On va découper l'entrée sur chaque | pour connaitre la liste des commandes (on obtient un pointeur vers le premier élément)

			clock_gettime(CLOCK_REALTIME, &time_res_start);//Obtenir l'heure dans time_res_start
			
			while(token != NULL) {
				commands[command_nb++] = token;
				token = strtok(NULL, "|");
			}

			childpid = executePipeline(command_nb, commands);

			if(background == 0) {
				while (waitpid(childpid, &child_status, 0) != -1);// waitpid bloquant
			} else {
				backProcess[backTaskCount].id = childpid;
				backProcess[backTaskCount].startTime = process_time;
				backProcess[backTaskCount].running = 1;
				strncpy(backProcess[backTaskCount].cmd, commandInput, BUF_SIZE);
				backTaskCount++;
				PRINT("[")
				sprintf(tmp_write_string, "%d", backTaskCount);
				PRINT(tmp_write_string)
				PRINT("] ")
				sprintf(tmp_write_string, "%d", childpid);
				PRINT(tmp_write_string)
				PRINT("\n")
			}

			clock_gettime(CLOCK_REALTIME, &time_res_end);
			seconds = time_res_end.tv_sec - time_res_start.tv_sec; 
			ns = time_res_end.tv_nsec - time_res_start.tv_nsec; 

			if (time_res_start.tv_nsec > time_res_end.tv_nsec) { // clock underflow 
				--seconds; 
				ns += 1000000000; 
			} 
			
			process_time = seconds*1000 + ns/1000000; //On va mettre le temps d'execution en ms au lieu de ns
			
		}
	}
	return 0;
}
