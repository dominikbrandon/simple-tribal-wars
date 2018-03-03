#include <ncurses.h>
#include "settings.h"

#define WIDTH 80

void processKilled(int);
int initGame(char*, int*, struct enemy**);
void keyError(int);
void makeResourcesWindow(WINDOW*);
void updateResourcesWindow(WINDOW*, struct playerStatusStruct);
void clearCommandsWindow(WINDOW*);
void makeCommandsWindow(WINDOW*, int, char**);
int makeWindow(WINDOW*, int, int, char*, int, char**);
void presentBattleDetails(WINDOW*, int, int, char*, char*);

int main(int argc, char *argv[])
{
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	char name[NAME_MAX_LENGTH];
	int myID;
	struct enemy **enemies = NULL;
	int nEnemies;
	int mid;
	if((mid = initGame(name, &nEnemies, enemies)) == -1) {
		return -1;
	}
	// obtain other players' names
	struct stdMsg enemiesNumber;
	struct enemyMsg enemyMsg;
    msgrcv(mid, &enemiesNumber, sizeof(struct stdMsg)-sizeof(long), 2, 0);	// get number of enemies
    sscanf(enemiesNumber.text, "%d %d", &myID, &nEnemies);
    enemies = malloc(nEnemies * sizeof *enemies);
    for(int i = 0; i < nEnemies; ++i) {
    	enemies[i] = malloc(sizeof **enemies);
    	msgrcv(mid, &enemyMsg, sizeof(enemyMsg)-sizeof(long), 3, 0);	// get enemies' details
    	*enemies[i] = enemyMsg.enemy;
    }
	// create semaphore restricting access to writing to terminal
	int writeAccess, key = 1000;
	while((writeAccess = semget(key, 1, IPC_CREAT|IPC_EXCL|0600)) == -1) ++key;
	struct sembuf blockSem = {0, -1, 0};
	struct sembuf freeSem = {0, 1, 0};
	semctl(writeAccess, 0, SETVAL, 1);
	
	printf("\e[1;1H\e[2J");	// clear terminal
	initscr();
	noecho();
	start_color();
	init_pair(1, COLOR_RED, COLOR_BLACK);
	init_pair(2, COLOR_GREEN, COLOR_BLACK);
	init_pair(3, COLOR_MAGENTA, COLOR_BLACK);
	init_pair(4, COLOR_YELLOW, COLOR_BLACK);
	refresh();
	
	int hIncoming = 0, /*hOutcoming = 0,*/ hTraining = 0, /*hBattles = 0,*/ hCommands = 7, hResources = 7;
	WINDOW *resourcesWindow = newwin(hResources, WIDTH / 2, 0, 0);
	WINDOW *commandsWindow = newwin(hCommands, WIDTH / 2, 0, WIDTH / 2);
	WINDOW *incomingWindow = newwin(1, WIDTH / 2, 0, 0);
	WINDOW *outcomingWindow = newwin(1, WIDTH / 2, 0, 0);
	WINDOW *recruitWindow = newwin(1, WIDTH / 2, 0, 0);
	WINDOW *battlesWindow = newwin(1, WIDTH / 2, 0, 0);
	char *commands[] = {
		"1: Hire workers",
		"2: Hire light infantry",
		"3: Hire heavy infantry",
		"4: Hire cavalry",
		"5: Send an attack"
	};
	char *incoming[] = {
    	""
    };
    char *outcoming[] = {
    	""
    };
    char *training[] = {
		""
	};
	char *battles[] = {
		""
	};
	int nCommands = sizeof(commands) / sizeof(char*);
	int nIncoming = sizeof(incoming) / sizeof(char*);
	int nOutcoming = sizeof(outcoming) / sizeof(char*);
	int nTraining = sizeof(training) / sizeof(char*);
	int nBattles = sizeof(battles) / sizeof(char*);
	makeCommandsWindow(commandsWindow, nCommands, commands);
	hIncoming	= makeWindow(incomingWindow, hResources, 0, "Incoming:", nIncoming, incoming);
	/*hOutcoming	=*/ makeWindow(outcomingWindow, hResources + hIncoming, 0, "Outcoming:", nOutcoming, outcoming);
	hTraining	= makeWindow(recruitWindow, hCommands, WIDTH/2, "Training:", nTraining, training);
	/*hBattles	=*/ makeWindow(battlesWindow, hCommands + hTraining, WIDTH/2, "Battles:", nBattles, battles);
	
	// process refreshing windows
	struct serverMsg serverMsg;
	int pidRefresh = fork();
	if(pidRefresh == 0) {
		signal(SIGQUIT, SIG_DFL);
		signal(SIGTERM, SIG_DFL);
		int colorId, enemyId;
		int battleLine = 1;
		char firstLine[WIDTH / 2], secondLine[WIDTH / 2];
		char enemyType[10];
		struct unitsStruct mySurvived, myUnits, enemySurvived, enemyUnits;
		while(1) {
			// get message
			if(msgrcv(mid, &serverMsg, sizeof(serverMsg)-sizeof(long), -MSGTYPE_STATUS, 0) == -1) {
				exit(EXIT_FAILURE);
			}
			// if message includes player's status
			if(serverMsg.type == MSGTYPE_STATUS) {
				semop(writeAccess, &blockSem, 1);
				updateResourcesWindow(resourcesWindow, serverMsg.data.status);
				semop(writeAccess, &freeSem, 1);
			// if message includes battle's details
			} else if(serverMsg.type == MSGTYPE_BATTLE) {
				// green or red
				if(serverMsg.data.battle.wonBy == myID) {
					colorId = 2;
				} else {
					colorId = 1;
				}
				if(serverMsg.data.battle.attackerId == myID) {
					strncpy(enemyType, "defender", 10);
					enemyId			= serverMsg.data.battle.defenderId;
					mySurvived		= serverMsg.data.battle.attackerSurvived;
					myUnits			= serverMsg.data.battle.attackerUnits;
					enemySurvived	= serverMsg.data.battle.defenderSurvived;
					enemyUnits		= serverMsg.data.battle.defenderUnits;
				} else {
					strncpy(enemyType, "attacker", 10);
					enemyId			= serverMsg.data.battle.attackerId;
					mySurvived		= serverMsg.data.battle.defenderSurvived;
					myUnits			= serverMsg.data.battle.defenderUnits;
					enemySurvived	= serverMsg.data.battle.attackerSurvived;
					enemyUnits		= serverMsg.data.battle.attackerUnits;
				}
				// collect data into string
				snprintf(firstLine, sizeof(firstLine), "you: %d(-%d)/%d(-%d)/%d(-%d)/%d(-%d)",
					myUnits.workers, myUnits.workers-mySurvived.workers, myUnits.lightInfantry, myUnits.lightInfantry-mySurvived.lightInfantry,
					myUnits.heavyInfantry, myUnits.heavyInfantry-mySurvived.heavyInfantry, myUnits.cavalry, myUnits.cavalry-mySurvived.cavalry);
				snprintf(secondLine, sizeof(secondLine), "%s %d: %d(-%d)/%d(-%d)/%d(-%d)/%d(-%d)", enemyType, enemyId,
					enemyUnits.workers, enemyUnits.workers-enemySurvived.workers, enemyUnits.lightInfantry, enemyUnits.lightInfantry-enemySurvived.lightInfantry,
					enemyUnits.heavyInfantry, enemyUnits.heavyInfantry-enemySurvived.heavyInfantry, enemyUnits.cavalry, enemyUnits.cavalry-enemySurvived.cavalry);
				semop(writeAccess, &blockSem, 1);
				presentBattleDetails(battlesWindow, battleLine, colorId, firstLine, secondLine);
				semop(writeAccess, &freeSem, 1);
				battleLine += 2;
			// if message is of type endgame
			} else if(serverMsg.type == MSGTYPE_ENDGAME) {
				struct commandMsg command;
				command.type = MSGTYPE_TRAIN;
				command.id = -1;
				msgsnd(mid, &command, sizeof(command) - sizeof(long), 0);
				exit(serverMsg.data.endgame + 10);
			}
		}
	}
    
	// process waiting for a command from user
	int pidControl = fork();
	struct commandMsg command = {MSGTYPE_TRAIN, 1, 0};
	if(pidControl == 0) {
		signal(SIGQUIT, processKilled);
		signal(SIGTERM, processKilled);
		int amount, in;
		struct commandBattle battle;
		struct unitsStruct units = {0, 0, 0, 0};
		battle.type = MSGTYPE_ATTACK;
		while(1) {
			// restore commands window
			makeCommandsWindow(commandsWindow, nCommands, commands);
			// get command number
			in = getch() - '0';
			// restrict access to writing to console
			semop(writeAccess, &blockSem, 1);
			if(in >= 1 && in < nCommands) {
				echo();
				wmove(commandsWindow, in, 0);
				wclrtoeol(commandsWindow);
				mvwprintw(commandsWindow, in, 2, "Amount:");
				mvwscanw(commandsWindow, in, 10, "%d", &amount);
				noecho();
				
				// send command to train
				command.id = in - 1;
				command.amount = amount;
				msgsnd(mid, &command, sizeof(command) - sizeof(long), 0);
			} else if (in == nCommands) {
				// present list of enemies
				clearCommandsWindow(commandsWindow);
				wrefresh(commandsWindow);
				mvwprintw(commandsWindow, 1, 2, "Please select the target:");
				for(int i = 0; i < nEnemies; ++i) {
					mvwprintw(commandsWindow, i + 2, 2, "%d: %s", enemies[i]->id, enemies[i]->name);
				}
				wrefresh(commandsWindow);
				// choose enemy
				in = getch() - '0';
				battle.target = in;
				short choiceValid = 0;
				for(int i = 0; i < nEnemies; ++i) {
					if(enemies[i]->id == in) {
						choiceValid = 1;
						break;
					}
				}
				if(choiceValid) {
					// get number of troops
					clearCommandsWindow(commandsWindow);
					wrefresh(commandsWindow);
					mvwprintw(commandsWindow, 1, 2, "Please enter number of troops:");
					echo();
				    // mvwprintw(commandsWindow, 2, 9, "Workers: ");
				    // wscanw(commandsWindow,			"%d", &units.workers);
				    units.workers = 0;
				    mvwprintw(commandsWindow, 3, 2, "Light Infantry: ");
				    wscanw(commandsWindow,			"%d", &units.lightInfantry);
				    mvwprintw(commandsWindow, 4, 2, "Heavy Infantry: ");
				    wscanw(commandsWindow,			"%d", &units.heavyInfantry);
				    mvwprintw(commandsWindow, 5, 9, "Cavalry: ");
					wscanw(commandsWindow,			"%d", &units.cavalry);
					noecho();
					// inform that you're going to send command
					command.id = NUMBER_OF_UNIT_TYPES;
					command.amount = 0;
					msgsnd(mid, &command, sizeof(command) - sizeof(long), 0);
					// send command
					battle.units = units;
					msgsnd(mid, &battle, sizeof(battle)-sizeof(long), 0);
				}
			} else if(in == 0) {
				// send command to exit
				command.id = -1;
				msgsnd(mid, &command, sizeof(command) - sizeof(long), 0);
				kill(pidRefresh, SIGTERM);
				exit(0);
			}
			// stop writing to console, allow others
			semop(writeAccess, &freeSem, 1);
		}
	}
	
	// wait for subprocesses to terminate and get their exit status
	int refreshStatus, controlStatus;
	waitpid(pidRefresh, &refreshStatus, 0);
	kill(pidControl, SIGTERM);
	waitpid(pidControl, &controlStatus, 0);
	
	// end curses mode
	delwin(resourcesWindow);
	delwin(incomingWindow);
	delwin(outcomingWindow);
	delwin(commandsWindow);
	delwin(recruitWindow);
	delwin(battlesWindow);
	endwin();			/* End curses mode		  */
	
	// delete ipcs and free dynamically allocated memory
	semctl(writeAccess, 0, IPC_RMID, NULL);
	for(int i = 0; i < nEnemies; ++i) {
		free(enemies[i]);
	}
	free(enemies);
	
	// present result
	int gameResult = WEXITSTATUS(refreshStatus);
	if(gameResult == 11) {
		printf("You won!\n");
	} else if(gameResult == 10) {
		printf("You lost!\n");
	} else {
		printf("Undefined: %d\n", gameResult);
	}
	return 0;
}

int initGame(char* name, int* nEnemies, struct enemy **enemies) {
	int mkey, mid;
	char buf[10];
	
	// obtain user's name
	printf("Enter your name: ");
	fgets(name, NAME_MAX_LENGTH, stdin);

	// get key of messages queue from fifo queue
	int fifoAccess = semget(FIFO_ACCESS_SEM_KEY, 2, 0);
	struct sembuf readMkey = {1, -1, 0};
	struct sembuf allowWrite = {0, 1, 0};
	semop(fifoAccess, &readMkey, 1);	// wait for read possibility
	int fifo = open("init", O_RDONLY);
	if(fifo == -1) {
		return -1;
	}
	read(fifo, &buf, 10);
	close(fifo);
	semop(fifoAccess, &allowWrite, 1);	// allow writing
	sscanf(buf, "%d", &mkey);
	
	// send user's name via messages queue
	mid = msgget(mkey, 0);
	struct nameBuf nameMsg;
	nameMsg.type = 1;
	strcpy(nameMsg.name, name);
	if(msgsnd(mid, &nameMsg, sizeof(struct nameBuf)-sizeof(long), 0) == -1) {
		return -1;
	}
	
	// wait for other players
	int gameReady = semget(GAME_READY_SEM_KEY, 1, 0);
	if(gameReady == -1) {
		keyError(GAME_READY_SEM_KEY);
		return -1;
	}
	struct sembuf wait = {0, -1, 0};
	printf("Please wait for other players to join\n");
	semop(gameReady, &wait, 1);
	
	
	return mid;
}
void keyError(int key) {
    printf("Error creating IPC with key %d\nPress enter", key);
    getchar();
}
// void makeResourcesWindow(WINDOW *win) {
// 	werase(win);
// 	wattron(win, A_BOLD | COLOR_PAIR(4));
// 	mvwprintw(win, 1, 11, "Gold:");
//     mvwprintw(win, 2, 8, "Workers:");
//     mvwprintw(win, 3, 1, "Light Infantry:");
//     mvwprintw(win, 4, 1, "Heavy Infantry:");
//     mvwprintw(win, 5, 8, "Cavalry:");
//     wattroff(win, A_BOLD | COLOR_PAIR(4));
// 	wrefresh(win);
// 	move(0, 0);
// }
void updateResourcesWindow(WINDOW *win, struct playerStatusStruct status) {
	werase(win);
	wrefresh(win);
	mvwprintw(win, 0, 2, "Battles ratio: %d(%d)/%d", status.battlesWon, status.attacksWon, status.battlesLost);
	wattron(win, A_BOLD | COLOR_PAIR(4));
	mvwprintw(win, 1, 11, "Gold: %d", status.gold);
    mvwprintw(win, 2, 8, "Workers: %d", status.unitsHome.workers);
    mvwprintw(win, 3, 1, "Light Infantry: %d", status.unitsHome.lightInfantry);
    mvwprintw(win, 4, 1, "Heavy Infantry: %d", status.unitsHome.heavyInfantry);
    mvwprintw(win, 5, 8, "Cavalry: %d", status.unitsHome.cavalry);
    wattroff(win, A_BOLD | COLOR_PAIR(4));
    wmove(win, 0, 0);
	wrefresh(win);
	// move(0, 0);
}
void clearCommandsWindow(WINDOW *win) {
	werase(win);
	wborder(win, '|', '|', '-', '-', '+', '+', '+', '+');
}
void makeCommandsWindow(WINDOW *win, int nCommands, char** commands) {
	werase(win);
	wborder(win, '|', '|', '-', '-', '+', '+', '+', '+');
    for(int i = 0; i < nCommands; ++i) {
    	mvwprintw(win, i + 1, 2, commands[i]);
    }
    wrefresh(win);
    move(0, 0);
    refresh();
}
int makeWindow(WINDOW *win, int yPos, int xPos, char *title, int nData, char **data) {
	mvwin(win, yPos, xPos);
	werase(win);
	wresize(win, nData + 1, WIDTH / 2);
	wattron(win, A_BOLD | COLOR_PAIR(3));
	mvwprintw(win, 0, 1, title);
	wattroff(win, A_BOLD | COLOR_PAIR(3));
	for(int i = 0; i < nData; ++i) {
		mvwprintw(win, i + 1, 1, data[i]);
	}
	wrefresh(win);
	move(0, 0);
	return nData + 2;	// window height (+2 for title and spacing)
}
void presentBattleDetails(WINDOW *win, int nLine, int colorId, char *myInfo, char *enemyInfo) {
	wresize(win, nLine + 2, WIDTH / 2);
	wattron(win, COLOR_PAIR(colorId));
	mvwprintw(win, nLine, 1, myInfo);
	mvwprintw(win, nLine+1, 1, enemyInfo);
	wattroff(win, COLOR_PAIR(colorId));
	wrefresh(win);
}
void processKilled(int a) {
	pid_t groupId = getpgid(0);
    killpg(groupId, SIGTERM);
    exit(EXIT_FAILURE);
}
