#include <stdio.h>
#include "settings.h"

void onKill(int);
int initGame(int, struct playerStruct**, int*, int*, int*);
void keyError(int);
int pushTraining(struct trainingQueue*, int, int);
int popTraining(struct trainingQueue*);
int pushBattle(struct battlesQueue*, int, struct unitsStruct);
int popBattle(struct battlesQueue*);

struct unitDetails unitsInfo[4] = {
    {0, 150,  0,  0, 2},
    {1, 100, 10, 12, 2},
    {2, 250, 15, 30, 3},
    {3, 550, 35, 12, 5}
};

int *mid=NULL, *semid, *shmid, *battlesSemid=NULL, *battlesShmid, *trainingSemid, *trainingShmid;

int main() {
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, onKill);
    signal(SIGQUIT, onKill);
    int errorCode;
    int nPlayers;
    struct sembuf blockSem      = {0, -1, 0};   // simple semaphore blocking operation
    struct sembuf freeSem       = {0,  1, 0};   // simple semaphore freeing operation
    struct sembuf startGameSem  = {0,  0, 0};   // increments semaphore resources by nPlayers value (will be changed after reading nPlayers)
    
    // semaphore blocking the game until all players are loaded
    int gameReady = semget(GAME_READY_SEM_KEY, 1, IPC_CREAT|IPC_EXCL|0600);
    if(gameReady == -1) {
        keyError(GAME_READY_SEM_KEY);
        return -1;
    }
    semctl(gameReady, 0, SETVAL, 0);
    
    // get number of players
    printf("Set number of players: ");
    scanf("%d", &nPlayers);
    
    // allocate memory for players' details and ipc IDs
    struct playerStruct **players   = malloc(nPlayers * sizeof *players);    // array of players
    mid                        = malloc(nPlayers * sizeof *mid);
    semid                      = malloc(nPlayers * sizeof *semid);
    shmid                      = malloc(nPlayers * sizeof *shmid);
    
    // initialize game
    initGame(nPlayers, players, mid, semid, shmid);
    struct trainingQueue **trainingQueue    = malloc(nPlayers * sizeof *trainingQueue);
    struct battlesQueue **battlesQueue      = malloc(nPlayers * sizeof *battlesQueue);
    trainingSemid                      = malloc(nPlayers * sizeof *trainingSemid);
    battlesSemid                       = malloc(nPlayers * sizeof *battlesSemid);
    trainingShmid                      = malloc(nPlayers * sizeof *trainingShmid);
    battlesShmid                       = malloc(nPlayers * sizeof *battlesShmid);
    // create semaphore and shared memory record for training and battles queue
    for(int i = 0, key = 1000; i < nPlayers; ++i) {
        while((trainingSemid[i] = semget(key, 1, IPC_CREAT|IPC_EXCL|0600)) == -1) ++key;
        while((battlesSemid[i]  = semget(key, 1, IPC_CREAT|IPC_EXCL|0600)) == -1) ++key;
        while((trainingShmid[i] = shmget(key, sizeof(struct trainingQueue), IPC_CREAT|IPC_EXCL|0600)) == -1) ++key;
        while((battlesShmid[i]  = shmget(key, sizeof(struct battlesQueue), IPC_CREAT|IPC_EXCL|0600)) == -1) ++key;
        // init semaphores with 1 (can access)
        semctl(trainingSemid[i], 0, SETVAL, 1);
        semctl(battlesSemid[i],  0, SETVAL, 1);
        // allocate shared memory sectors
        trainingQueue[i] = shmat(trainingShmid[i], 0, 0);
        battlesQueue[i]  = shmat(battlesShmid[i], 0, 0);
        // initialize values
        trainingQueue[i]->head = 0;
        battlesQueue[i]->head = 0;
        for(int j = 0; j < TRAINING_QUEUE_CAPACITY; ++j) {
            trainingQueue[i]->array[j] = (struct unitsAmountStruct) {0, -1, -1};
        }
        for(int j = 0; j < BATTLES_QUEUE_CAPACITY; ++j) {
            battlesQueue[i]->arrivingAt[j] = 0;
            battlesQueue[i]->from[j] = -1;
            battlesQueue[i]->array[j] = (struct unitsStruct) {-1, -1, -1, -1};
        }
    }
    
    // start the game
    startGameSem.sem_op = nPlayers;
    semop(gameReady, &startGameSem, 1);
    time_t timeGameStarted = time(NULL);
    
    // process handling communication
    int pidCommunication = fork();
    if(pidCommunication == 0) {
        signal(SIGTERM, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        // create listeners and senders for each client
        int *statusSenders      = malloc(nPlayers * sizeof *statusSenders);
        int *commandListeners   = malloc(nPlayers * sizeof *commandListeners);
        int statusClient        = -1;
        int commandClient       = -1;
        for(int i = 0; i < nPlayers; ++i) {
            statusSenders[i] = fork();
            if(statusSenders[i] == 0) {
                statusClient = i;
                break;
            }
            commandListeners[i] = fork();
            if(commandListeners[i] == 0) {
                commandClient = i;
                break;
            }
        }
        // if process is a status sender
        if(statusClient != -1) {
            struct playerStatusStruct playerStatus;
            struct unitsStruct units;
            struct serverMsg serverMsg;
            serverMsg.type = MSGTYPE_STATUS;
            while(1) {
                // get access to player's record in shared memory
                semop(semid[statusClient], &blockSem, 1);
                playerStatus.gold           = players[statusClient]->gold;
                playerStatus.attacksWon     = players[statusClient]->attacksWon;
                playerStatus.battlesWon     = players[statusClient]->battlesWon;
                playerStatus.battlesLost    = players[statusClient]->battlesLost;
                units.workers               = players[statusClient]->unitsHome[0];
                units.lightInfantry         = players[statusClient]->unitsHome[1];
                units.heavyInfantry         = players[statusClient]->unitsHome[2];
                units.cavalry               = players[statusClient]->unitsHome[3];
                semop(semid[statusClient], &freeSem, 1);
                // send status
                playerStatus.unitsHome = units;
                serverMsg.data.status = playerStatus;
                msgsnd(mid[statusClient], &serverMsg, sizeof(serverMsg)-sizeof(long), 0);
                sleep(1);
            }
            exit(EXIT_SUCCESS);     // not used actually
        }
        // if process is a command listener
        if(commandClient != -1) {
            struct commandMsg command;
            struct commandBattle commandBattle;
            int available[NUMBER_OF_UNIT_TYPES];
            int rcvResult, goldNeeded;
            while(1) {
                // listen for a command
                rcvResult = msgrcv(mid[commandClient], &command, sizeof(struct commandMsg) - sizeof(long), MSGTYPE_TRAIN, 0);
                if(command.id < NUMBER_OF_UNIT_TYPES && command.id >= 0) {
                    // if there is enough gold
                    goldNeeded = command.amount * unitsInfo[command.id].cost;
                    semop(semid[commandClient], &blockSem, 1);
                    if(players[commandClient]->gold >= goldNeeded) {
                        players[commandClient]->gold -= goldNeeded;
                    } else {
                        printf("Player %d couldn't start training %d units of %d, because there wasn't enough gold (%d)\n", commandClient, command.amount, command.id, goldNeeded);
                        goldNeeded = -1;
                    }
                    semop(semid[commandClient], &freeSem, 1);
                    // push training
                    if(goldNeeded != -1) {
                        semop(trainingSemid[commandClient], &blockSem, 1);
                        pushTraining(trainingQueue[commandClient], command.id, command.amount);
                        semop(trainingSemid[commandClient], &freeSem, 1);
                        printf("Player %d started training %d units of %d\n", commandClient, command.amount, command.id);
                    }
                } else if(command.id == NUMBER_OF_UNIT_TYPES) {
                    // get attack info
                    rcvResult = msgrcv(mid[commandClient], &commandBattle, sizeof(commandBattle)-sizeof(long), MSGTYPE_ATTACK, 0);
                    // get available units
                    semop(semid[commandClient], &blockSem, 1);
                    for(int i = 0; i < NUMBER_OF_UNIT_TYPES; ++i) {
                        available[i] = players[commandClient]->unitsHome[i];
                    }
                    semop(semid[commandClient], &freeSem, 1);
                    // check if there are enough units
                    short attackValid = 1;
                    if((available[0] < commandBattle.units.workers ||
                        available[1] < commandBattle.units.lightInfantry ||
                        available[2] < commandBattle.units.heavyInfantry ||
                        available[3] < commandBattle.units.cavalry) && (
                        commandBattle.units.workers > 0 ||
                        commandBattle.units.lightInfantry > 0 ||
                        commandBattle.units.heavyInfantry > 0 ||
                        commandBattle.units.cavalry > 0)) {
                            attackValid = 0;
                    }
                    // push attack
                    if(attackValid) {
                        // decrease number of units home
                        semop(semid[commandClient], &blockSem, 1);
                        players[commandClient]->unitsHome[0] -= commandBattle.units.workers;
                        players[commandClient]->unitsHome[1] -= commandBattle.units.lightInfantry;
                        players[commandClient]->unitsHome[2] -= commandBattle.units.heavyInfantry;
                        players[commandClient]->unitsHome[3] -= commandBattle.units.cavalry;
                        semop(semid[commandClient], &freeSem, 1);
                        // push attack
                        semop(battlesSemid[commandBattle.target], &blockSem, 1);
                        pushBattle(battlesQueue[commandBattle.target], commandClient, commandBattle.units);
                        semop(battlesSemid[commandBattle.target], &freeSem, 1);
                        printf("Player %d sent attack against player %d: %d/%d/%d/%d\n", commandClient, commandBattle.target, commandBattle.units.workers, commandBattle.units.lightInfantry, commandBattle.units.heavyInfantry, commandBattle.units.cavalry);
                    }
                    // notify players about an attack
                } else if(command.id == -1) {
                    break;        // TEMPORARY
                } else {
                    printf("Player entered wrong command ID: %d\n", command.id);
                }
                if(rcvResult == -1) {
                    exit(EXIT_FAILURE);
                }
            }
            exit(EXIT_SUCCESS);
        }
        // wait for one listener to terminate
        int exitStatus;
        int pidClosed = wait(&exitStatus);
        printf("Listener process terminated with status %d\n", WEXITSTATUS(exitStatus));
        for(int i = 0; i < nPlayers; ++i) {
            if(pidClosed != commandListeners[i]) {
                kill(commandListeners[i], SIGTERM);
            }
            kill(statusSenders[i], SIGTERM);
        }
        free(commandListeners);
        free(statusSenders);
        exit(WEXITSTATUS(exitStatus));
    }
    
    // create event handlers
    int *eventHandlers      = malloc(nPlayers * sizeof *eventHandlers);
    int handleClient        = -1;
    for(int i = 0; i < nPlayers; ++i) {
        eventHandlers[i] = fork();
        if(eventHandlers[i] == 0) {
            handleClient = i;
            break;
        }
    }
    // if process is an event handler
    if(handleClient != -1) {
        signal(SIGTERM, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        int timeDifference, timeSinceTrainingBegan, timeTrainingAlreadyLasted;
        int unitsAlreadyRecruited, unitsTillNow, unitsToAdd;
        time_t currentMoment, previousMoment = timeGameStarted;
        struct playerStruct *player = players[handleClient];
        struct unitsAmountStruct unitsTraining;
        struct unitsStruct troopsArriving, troopsHome;
        time_t attackAt;
        int attackStrength, defenceStrength, attackerId, attacksWon;
        double strengthRatio;
        struct battleStruct battleInfo;
        struct serverMsg serverMsg;
        while(1) {
            // read current training task
            semop(trainingSemid[handleClient], &blockSem, 1);
            unitsTraining = trainingQueue[handleClient]->array[trainingQueue[handleClient]->head];
            semop(trainingSemid[handleClient], &freeSem, 1);
            // read first battle's arrival time
            semop(battlesSemid[handleClient], &blockSem, 1);
            attackAt = battlesQueue[handleClient]->arrivingAt[battlesQueue[handleClient]->head];
            semop(battlesSemid[handleClient], &freeSem, 1);
            // get current time
            currentMoment = time(NULL);
            if(attackAt != 0 && attackAt <= currentMoment) {
                currentMoment = attackAt;
            }
            timeDifference = (int) floor(difftime(currentMoment, previousMoment));
            // if there is training task in queue
            if(unitsTraining.id != -1) {
                // count times
                timeSinceTrainingBegan = (int) floor(difftime(currentMoment, unitsTraining.timer));
                timeTrainingAlreadyLasted = (int) floor(difftime(previousMoment, unitsTraining.timer));
                // count units that have been already recruited
                if(timeTrainingAlreadyLasted < unitsInfo[unitsTraining.id].produceTime) {
                    unitsAlreadyRecruited = 0;
                } else {
                    unitsAlreadyRecruited = (int) floor(timeTrainingAlreadyLasted / unitsInfo[unitsTraining.id].produceTime);
                }
                // count units that should have been recruited till now
                unitsTillNow = (int) floor(timeSinceTrainingBegan / unitsInfo[unitsTraining.id].produceTime);
                unitsToAdd = unitsTillNow - unitsAlreadyRecruited;
                // if training is completed, pop it from the queue
                if(unitsTillNow == unitsTraining.amount) {
                    semop(trainingSemid[handleClient], &blockSem, 1);
                    popTraining(trainingQueue[handleClient]);
                    semop(trainingSemid[handleClient], &freeSem, 1);
                }
            } else {
                unitsToAdd = 0;
            }
            // increase amount of gold and troops
            semop(semid[handleClient], &blockSem, 1);
            player->gold = player->gold + (50 + player->unitsHome[0] * 5) * timeDifference;
            player->unitsHome[unitsTraining.id] += unitsToAdd;
            semop(semid[handleClient], &freeSem, 1);
            // if there is an attack arriving and it's arriving right now
            if(attackAt != 0 && attackAt <= currentMoment) {
                // get other details about attack
                semop(battlesSemid[handleClient], &blockSem, 1);
                troopsArriving = battlesQueue[handleClient]->array[battlesQueue[handleClient]->head];
                attackerId = battlesQueue[handleClient]->from[battlesQueue[handleClient]->head];
                semop(battlesSemid[handleClient], &freeSem, 1);
                // delete battle from queue
                popBattle(battlesQueue[handleClient]);
                // get number of troops home
                semop(semid[handleClient], &blockSem, 1);
                troopsHome.workers          = player->unitsHome[0];
                troopsHome.lightInfantry    = player->unitsHome[1];
                troopsHome.heavyInfantry    = player->unitsHome[2];
                troopsHome.cavalry          = player->unitsHome[3];
                semop(semid[handleClient], &freeSem, 1);
                // store details about the battle
                battleInfo.attackerId = attackerId;
                battleInfo.defenderId = handleClient;
                battleInfo.attackerUnits = troopsArriving;
                battleInfo.defenderUnits = troopsHome;
                // count attacker's strength
                attackStrength = troopsArriving.workers * unitsInfo[0].attack +
                    troopsArriving.lightInfantry        * unitsInfo[1].attack +
                    troopsArriving.heavyInfantry        * unitsInfo[2].attack +
                    troopsArriving.cavalry              * unitsInfo[3].attack;
                // count defender's strength
                defenceStrength = troopsHome.workers    * unitsInfo[0].defence +
                    troopsHome.lightInfantry            * unitsInfo[1].defence +
                    troopsHome.heavyInfantry            * unitsInfo[2].defence +
                    troopsHome.cavalry                  * unitsInfo[3].defence;
                // if attack successful
                if(attackStrength > defenceStrength) {
                    // count attacker's troops survived
                    strengthRatio = (double) defenceStrength / attackStrength;
                    troopsArriving.workers          -= (int) floor(strengthRatio * troopsArriving.workers);
                    troopsArriving.lightInfantry    -= (int) floor(strengthRatio * troopsArriving.lightInfantry);
                    troopsArriving.heavyInfantry    -= (int) floor(strengthRatio * troopsArriving.heavyInfantry);
                    troopsArriving.cavalry          -= (int) floor(strengthRatio * troopsArriving.cavalry);
                    // store details about the battle
                    battleInfo.wonBy = attackerId;
                    battleInfo.attackerSurvived = troopsArriving;
                    battleInfo.defenderSurvived = (struct unitsStruct) {0, 0, 0, 0};
                    // overwrite defender's troops number
                    semop(semid[handleClient], &blockSem, 1);
                    for(int i = 0; i < NUMBER_OF_UNIT_TYPES; ++i) {
                        player->unitsHome[i] = 0;
                    }
                    player->battlesLost += 1;
                    semop(semid[handleClient], &freeSem, 1);
                    // overwrite attacker's troops number
                    semop(semid[attackerId], &blockSem, 1);
                    players[attackerId]->attacksWon += 1;
                    players[attackerId]->battlesWon += 1;
                    players[attackerId]->unitsHome[0] += troopsArriving.workers;
                    players[attackerId]->unitsHome[1] += troopsArriving.lightInfantry;
                    players[attackerId]->unitsHome[2] += troopsArriving.heavyInfantry;
                    players[attackerId]->unitsHome[3] += troopsArriving.cavalry;
                    attacksWon = players[attackerId]->attacksWon;
                    semop(semid[attackerId], &freeSem, 1);
                } else {
                    strengthRatio = (double) attackStrength / defenceStrength;
                    // count attacker's troops survived
                    troopsArriving.workers          -= (int) floor(strengthRatio * troopsArriving.workers);
                    troopsArriving.lightInfantry    -= (int) floor(strengthRatio * troopsArriving.lightInfantry);
                    troopsArriving.heavyInfantry    -= (int) floor(strengthRatio * troopsArriving.heavyInfantry);
                    troopsArriving.cavalry          -= (int) floor(strengthRatio * troopsArriving.cavalry);
                    // count defender's troops survived
                    troopsHome.workers          -= (int) floor(strengthRatio * troopsHome.workers);
                    troopsHome.lightInfantry    -= (int) floor(strengthRatio * troopsHome.lightInfantry);
                    troopsHome.heavyInfantry    -= (int) floor(strengthRatio * troopsHome.heavyInfantry);
                    troopsHome.cavalry          -= (int) floor(strengthRatio * troopsHome.cavalry);
                    // store details about the battle
                    battleInfo.wonBy = handleClient;
                    battleInfo.attackerSurvived = troopsArriving;
                    battleInfo.defenderSurvived = troopsHome;
                    // overwrite defender's troops number
                    semop(semid[handleClient], &blockSem, 1);
                    player->battlesWon += 1;
                    player->unitsHome[0] = troopsHome.workers;
                    player->unitsHome[1] = troopsHome.lightInfantry;
                    player->unitsHome[2] = troopsHome.heavyInfantry;
                    player->unitsHome[3] = troopsHome.cavalry;
                    semop(semid[handleClient], &freeSem, 1);
                    // overwrite attacker's troops number
                    semop(semid[attackerId], &blockSem, 1);
                    players[attackerId]->battlesLost += 1;
                    players[attackerId]->unitsHome[0] += troopsArriving.workers;
                    players[attackerId]->unitsHome[1] += troopsArriving.lightInfantry;
                    players[attackerId]->unitsHome[2] += troopsArriving.heavyInfantry;
                    players[attackerId]->unitsHome[3] += troopsArriving.cavalry;
                    semop(semid[attackerId], &freeSem, 1);
                }
                printf("Battle between attacker %d and defender %d won by %d\n", attackerId, handleClient, battleInfo.wonBy);
                // notify players about the battle
                serverMsg.type = MSGTYPE_BATTLE;
                serverMsg.data.battle = battleInfo;
                msgsnd(mid[attackerId], &serverMsg, sizeof(serverMsg)-sizeof(long), 0);
                msgsnd(mid[handleClient], &serverMsg, sizeof(serverMsg)-sizeof(long), 0);
                // if there were 5 successful attacks
                if(attacksWon == 5) {
                    printf("Player %d have attacked successfully 5 times and hence won the game\n", attackerId);
                    // end the game
                    struct serverMsg serverMsg;
                    serverMsg.type = MSGTYPE_ENDGAME;
                    // tell attacker that he won
                    serverMsg.data.endgame = 1;
                    msgsnd(mid[attackerId], &serverMsg, sizeof(serverMsg)-sizeof(long), 0);
                    // tell others that they lost
                    serverMsg.data.endgame = 0;
                    for(int i = 0; i < nPlayers; ++i) {
                        if(i != attackerId) {
                            msgsnd(mid[i], &serverMsg, sizeof(serverMsg)-sizeof(long), 0);
                        }
                    }
                }
            }
            // overwrite previous moment
            previousMoment = currentMoment;
        }
    }

    // wait for handling communication to terminate
    wait(&errorCode);
    printf("Process handling communication terminated with status %d\n", WEXITSTATUS(errorCode));
    for(int i = 0; i < nPlayers; ++i) {
        kill(eventHandlers[i], SIGTERM);
    }
    free(eventHandlers);
    
    // delete ipcs
    for(int i = 0; i < nPlayers; ++i) {
        shmdt(players[i]);
        shmdt(trainingQueue[i]);
        shmdt(battlesQueue[i]);
        msgctl(mid[i],              IPC_RMID, NULL);
        semctl(semid[i], 0,         IPC_RMID, NULL);
        semctl(trainingSemid[i], 0, IPC_RMID, NULL);
        semctl(battlesSemid[i], 0,  IPC_RMID, NULL);
        shmctl(shmid[i],            IPC_RMID, NULL);
        shmctl(trainingShmid[i],    IPC_RMID, NULL);
        shmctl(battlesShmid[i],     IPC_RMID, NULL);
    }
    free(players);
    free(trainingQueue);
    free(battlesQueue);
    free(mid);
    free(semid);
    free(trainingSemid);
    free(battlesSemid);
    free(shmid);
    free(trainingShmid);
    free(battlesShmid);
    semctl(gameReady, 0, IPC_RMID, 0);  // remove gameReady semaphore
    return 0;
}

int pushTraining(struct trainingQueue *trainingQueue, int unit, int amount) {
    int node = trainingQueue->head;
    while(trainingQueue->array[node % TRAINING_QUEUE_CAPACITY].amount != -1) {    // field is not empty
        if(node >= trainingQueue->head + TRAINING_QUEUE_CAPACITY) {
            return 1;   // full array error
        }
        node++;
    }
    time_t timer;
    if(node == trainingQueue->head) {
        timer = time(NULL); // if there are no pending trainings, start new training immediately
    } else {                // in other case, start new training right after the last one pending
        struct unitsAmountStruct last = trainingQueue->array[(node - 1) % TRAINING_QUEUE_CAPACITY];
        timer = last.timer + last.amount * unitsInfo[last.id].produceTime;
    }
    trainingQueue->array[node % TRAINING_QUEUE_CAPACITY] = (struct unitsAmountStruct) {timer, unit, amount};
    return 0;
}
int popTraining(struct trainingQueue *trainingQueue) {
    int head = trainingQueue->head;
    trainingQueue->array[head] = (struct unitsAmountStruct) {0, -1, -1};
    trainingQueue->head = (head + 1) % TRAINING_QUEUE_CAPACITY;
    return 0;
}
int pushBattle(struct battlesQueue *battlesQueue, int from, struct unitsStruct units) {
    int node = battlesQueue->head;
    // look for non-empty record
    while(battlesQueue->from[node % BATTLES_QUEUE_CAPACITY] != -1) {
        // if every record was checked
        if(node >= battlesQueue->head + BATTLES_QUEUE_CAPACITY) {
            return 1;   // full array error
        }
        node++;
    }
    // push attack
    battlesQueue->arrivingAt[node % BATTLES_QUEUE_CAPACITY] = time(NULL) + 5;    // attack arrives after 5 seconds
    battlesQueue->from[node % BATTLES_QUEUE_CAPACITY] = from;
    battlesQueue->array[node % BATTLES_QUEUE_CAPACITY] = (struct unitsStruct) {units.workers, units.lightInfantry, units.heavyInfantry, units.cavalry};
    return 0;
}
int popBattle(struct battlesQueue *battlesQueue) {
    int head = battlesQueue->head;
    battlesQueue->arrivingAt[head] = 0;
    battlesQueue->from[head] = -1;
    battlesQueue->array[head] = (struct unitsStruct) {-1, -1, -1, -1};
    battlesQueue->head = (head + 1) % BATTLES_QUEUE_CAPACITY;
    return 0;
}
int initGame(int nPlayers, struct playerStruct **players, int *mid, int *semid, int *shmid) {
    // create fifo
    char buf[NAME_MAX_LENGTH + 5];
    int desk, key = 1000;
    if(mkfifo("init", 0600) == -1) {
        return 1;
    }
    
    // define fifo access
    int fifoAccess = semget(FIFO_ACCESS_SEM_KEY, 2, IPC_CREAT|IPC_EXCL|0600);
    semctl(fifoAccess, 0, SETVAL, 1);   // can write
    semctl(fifoAccess, 1, SETVAL, 0);   // cannot read
    struct sembuf writeMsg = {0, -1, 0};
    struct sembuf allowRead = {1, 1, 0};
    
    // for each player, create msgQueue and send its key through fifo
    for(int i = 0; i < nPlayers; ++i) {
        while((mid[i] = msgget(key, IPC_CREAT|IPC_EXCL|0600)) == -1) ++key;
        snprintf(buf, sizeof buf, "%d", key);
        semop(fifoAccess, &writeMsg, 1);   // wait for write possibility
        semop(fifoAccess, &allowRead, 1);  // allow reading
        desk = open("init", O_WRONLY);
        write(desk, buf, sizeof buf);
        close(desk);
    }
    semctl(fifoAccess, 0, IPC_RMID, 0);
    semctl(fifoAccess, 1, IPC_RMID, 0);
    
    // create semaphore and shared memory record
    key = 1000;
    for(int i = 0; i < nPlayers; ++i) {
        // create semaphores for each attribute a player
        while((semid[i] = semget(key, 1, IPC_CREAT|IPC_EXCL|0600)) == -1) {
            ++key;
        }
        // init semaphores with 1 (can access)
        for(int j = 0; j < 1; ++j) {
            semctl(semid[i], j, SETVAL, 1);
        }
        // allocate shared memory sectors
        key = 1000;
        while((shmid[i] = shmget(key, sizeof(struct playerStruct), IPC_CREAT|IPC_EXCL|0600)) == -1) ++key;
        players[i] = shmat(shmid[i], 0, 0);
    }
    
    // obtain name from each player and initialize one's properties
    struct nameBuf nameMsg;
    for(int i = 0; i < nPlayers; ++i) {
        msgrcv(mid[i], &nameMsg, sizeof(struct nameBuf)-sizeof(long), 1, 0);
        snprintf(players[i]->name, NAME_MAX_LENGTH * sizeof(char), "%s", nameMsg.name);
        printf("Player %s loaded with MID %d\n", players[i]->name, mid[i]);
        // initializing player
        players[i]->mid = mid[i];
        players[i]->gold = 300;
        players[i]->attacksWon = 0;
        players[i]->battlesWon = 0;
        players[i]->battlesLost = 0;
    }
    unlink("init");
    
    // send info about enemies
    struct stdMsg enemiesNumber;
    struct enemyMsg enemyMsg;
    enemyMsg.type = 3;
    enemiesNumber.type = 2;
    for(int i = 0; i < nPlayers; ++i) {
        snprintf(buf, sizeof(buf), "%d %d", i, nPlayers - 1);
        strcpy(enemiesNumber.text, buf);
        if(msgsnd(mid[i], &enemiesNumber, sizeof(struct stdMsg)-sizeof(long), 0) == -1) {
            return -1;
        }
        for(int j = 0; j < nPlayers; ++j) {
            if(j == i) continue;
            enemyMsg.enemy.id = j;
            strcpy(enemyMsg.enemy.name, "");
            strncat(enemyMsg.enemy.name, players[j]->name, NAME_MAX_LENGTH);
            if(msgsnd(mid[i], &enemyMsg, sizeof(enemyMsg)-sizeof(long), 0) == -1) return -1;
        }
    }
    return 0;
}
void keyError(int key) {
    printf("Error creating IPC with key %d\nPress any button", key);
    getchar();
}
void onKill(int a) {
    unlink("init");
    int gameReady = semget(GAME_READY_SEM_KEY, 1, 0);
    semctl(gameReady, 0, IPC_RMID, 0);
    int fifoAccess = semget(FIFO_ACCESS_SEM_KEY, 2, 0);
    semctl(fifoAccess, 0, IPC_RMID, 0);
    for(int i = 0; i < sizeof(mid)/sizeof(*mid); ++i) {
        msgctl(mid[i], IPC_RMID, NULL);
        semctl(semid[i], 0, IPC_RMID, NULL);
        shmctl(shmid[i], IPC_RMID, NULL);
    }
    for(int i = 0; i < sizeof(battlesSemid)/sizeof(*battlesSemid); ++i) {
        semctl(battlesSemid[i], 0, IPC_RMID, NULL);
        semctl(trainingSemid[i], 0, IPC_RMID, NULL);
        shmctl(battlesShmid[i], IPC_RMID, NULL);
        shmctl(trainingShmid[i], IPC_RMID, NULL);
    }
    pid_t groupId = getpgid(0);
    killpg(groupId, SIGTERM);
    exit(EXIT_FAILURE);
}
