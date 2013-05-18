#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/stat.h> /* For mode constants */
#include <unistd.h>
#include <sys/types.h>
#include <string.h>

#define MAXNAMELENGTH 100
#define MAXNUMPLAYERS 3

typedef struct {
	int rank;
	char suit;
} card;

typedef struct {
	int id;
	char name[MAXNAMELENGTH];
	char fifo[0];
} player;

typedef struct {
	int nplayers;
	player players[MAXNUMPLAYERS];
	int roundnumber;
	int turn;
	card tablecards[MAXNUMPLAYERS];
	pthread_mutex_t mutex;
	pthread_cond_t cond_var;
	int terminate;
} shared_mem;

shared_mem* create_shared(char* shm_name, int shm_size, int *created) {
	int shm_fd;
	shared_mem *res;

	shm_fd=shm_open(shm_name,O_RDWR,0660);

	if(shm_fd<0)
	{
		*created=1;
		shm_fd=shm_open(shm_name,O_RDWR|O_CREAT,0660);

		if(shm_fd<0) {
			perror(	"Failure in shm_open()");
			return	NULL;
		}

	} else *created=0;

	if(*created) {
		if(ftruncate(shm_fd,shm_size) < 0) {
			perror("Failure in ftruncate()");
			return NULL;
		}
	}

	res=mmap(NULL,shm_size,PROT_READ|PROT_WRITE,MAP_SHARED,shm_fd,0);

	return (shared_mem *)res;
}

void destroy_shared(shared_mem* shm, int shm_size, char* shm_name) {

	if(munmap(shm,shm_size) < 0)
	{
		perror(	"Failure in munmap()");
		exit(-1);
	}

	if(shm_unlink(shm_name) < 0)
	{
		perror(	"Failure in shm_unlink()");
		exit(-1);
	}

}

void init_shared(shared_mem* shm) {
	shm->terminate=0;
	shm->nplayers=0;

	int i;
	for(i=0; i<MAXNUMPLAYERS; i++)
		strcpy(shm->players[i].name,"");

	pthread_mutexattr_t mut_attr;
	pthread_mutexattr_init(&mut_attr);
	pthread_mutexattr_setpshared(&mut_attr,PTHREAD_PROCESS_SHARED);

	pthread_condattr_t cond_attr;
	pthread_condattr_init(&cond_attr);
	pthread_condattr_setpshared(&cond_attr,PTHREAD_PROCESS_SHARED);

	pthread_cond_init(&shm->cond_var,&cond_attr);
	pthread_mutex_init(&shm->mutex,&mut_attr);
}

void printPlayer(const player player) {
	printf("%d - %s - \n",player.id,player.name);
}

void printPlayers(shared_mem* shm) {
	int i;
	for(i=0; i<shm->nplayers; i++)
		printPlayer(shm->players[i]);
}

void addPlayer(shared_mem* shm, const char *playername, player *player) {
	pthread_mutex_lock(&shm->mutex);
	shm->nplayers++;
	player->id=shm->nplayers;
	strcpy(player->name,playername);

	int i=0;
	while(strcmp(shm->players[i].name,""))
		i++;

	shm->players[i]=*player;

	pthread_cond_signal(&shm->cond_var);
	pthread_mutex_unlock(&shm->mutex);
}

void waitPlayers(shared_mem* shm, const int nplayers) {
	pthread_mutex_lock(&shm->mutex);

	while(nplayers!=shm->nplayers) {
		printf("...waiting for more players...\n");
		pthread_cond_wait(&shm->cond_var,&shm->mutex);
	}

	pthread_mutex_unlock(&shm->mutex);
}

int main(int argc, char *argv[])
{
	if(argc!=4) {
		printf("Usage: %s [player_name] [table_name] [nr_players]",argv[0]);
		exit(-1);
	}

	int created=0, nplayers=atoi(argv[3]);

	if(nplayers>MAXNUMPLAYERS) {
		printf("Maximum nr of players is %d",MAXNUMPLAYERS);
		exit(-1);
	}

	shared_mem *shm;
	player me;

	shm=create_shared(argv[2],sizeof(shared_mem),&created);

	if(shm==NULL) {
		perror("Failure in create_shared()");
		exit(-1);
	}

	if(created==1)
		init_shared(shm);

	addPlayer(shm,argv[1],&me);
	waitPlayers(shm,nplayers);

	printf("All players here! Dealer is %s\n",shm->players[0].name);

	if(created==1)
		while(!shm->terminate)
			sleep(2);
	else {
		int tmp=0;
		printf("Terminate?\n");
		scanf("%d",&tmp);
		shm->terminate=tmp;
	}

	printf("%d\n",shm->nplayers);

	if(created==1)
		destroy_shared(shm,sizeof(shared_mem),argv[2]);

	return 0;
}
