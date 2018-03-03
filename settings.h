#ifndef SETTINGS_H_
#define SETTINGS_H_

#include <stdlib.h>     // malloc
#include <sys/stat.h>   // fifo
#include <fcntl.h>      // open
#include <unistd.h>     // read, close, unlink
#include <string.h>     // strcpy
#include <time.h>       // time, difftime
#include <errno.h>      // errno
#include <math.h>       // floor
#include <signal.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/wait.h>

#define NAME_MAX_LENGTH 20
#define GAME_READY_SEM_KEY 1000
#define FIFO_ACCESS_SEM_KEY 1001
#define STDMSG_MAX_LENGTH 50
#define PLAYER_ATTRIBUTES_NUMBER 6
#define NUMBER_OF_UNIT_TYPES 4
#define TRAINING_QUEUE_CAPACITY 50
#define BATTLES_QUEUE_CAPACITY 20
#define MSGTYPE_ENDGAME 5
#define MSGTYPE_BATTLE  6
#define MSGTYPE_STATUS  7
#define MSGTYPE_TRAIN   10
#define MSGTYPE_ATTACK  11

struct stdMsg {
    long type;
    char text[STDMSG_MAX_LENGTH];
};
struct nameBuf {
	long type;
	char name[NAME_MAX_LENGTH];
};
struct unitsStruct {
    int workers;
    int lightInfantry;
    int heavyInfantry;
    int cavalry;
};
struct enemy {
	int id;
	char name[NAME_MAX_LENGTH];
};
struct enemyMsg {
    long type;
    struct enemy enemy;
};
struct commandMsg {
    long type;
    short id;
    short amount;
};
struct commandBattle {
    long type;
    short target;
    struct unitsStruct units;
};
struct unitDetails {
    unsigned short id;
    unsigned short cost;
    unsigned short attack;
    unsigned short defence;
    unsigned short produceTime;
};
struct battleStruct {
    int wonBy;
    int attackerId;
    struct unitsStruct attackerUnits;
    struct unitsStruct attackerSurvived;
    int defenderId;
    struct unitsStruct defenderUnits;
    struct unitsStruct defenderSurvived;
};
struct playerStruct {
    char name[NAME_MAX_LENGTH];
    int mid;
    unsigned int gold;
    // struct unitsStruct unitsHome;
    int unitsHome[NUMBER_OF_UNIT_TYPES];
    unsigned short attacksWon;
    unsigned short battlesWon;
    unsigned short battlesLost;
};
struct playerStatusStruct {
    // long type;
    unsigned int gold;
    struct unitsStruct unitsHome;
    // int *unitsHome;
    unsigned short attacksWon;
    unsigned short battlesWon;
    unsigned short battlesLost;
};
struct unitsAmountStruct {
    time_t timer;
    int id;
    int amount;
};
struct trainingQueue {
    int head;
    struct unitsAmountStruct array[TRAINING_QUEUE_CAPACITY];
};
struct battlesQueue {
    int head;
    time_t arrivingAt[BATTLES_QUEUE_CAPACITY];
    int from[BATTLES_QUEUE_CAPACITY];
    struct unitsStruct array[BATTLES_QUEUE_CAPACITY];
};
union serverMsgUnion {
    struct playerStatusStruct status;
    int endgame;
    struct battleStruct battle;
};
struct serverMsg {
    long type;
    union serverMsgUnion data;
};

#endif