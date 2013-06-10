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
#include <time.h>

#define MAXNAMELENGTH 		6
#define MAXNUMPLAYERS 		3

#define RECEIVE_CARDS		0
#define PLAY				1
#define HAND				2
#define DEAL				3

#define WHEN_LENGTH			20
#define WHO_LENGTH			10+MAXNAMELENGTH
#define WHAT_LENGTH			20

#define NUMBER_ARGUMENTS 4

#define SUIT_CARDS 14
#define DECK_SUITS 4
#define DECK_SIZE 52
#define HAND_SIZE 4

typedef struct {
	int rank;
	char suit;
} card;

typedef struct {
	int id;
	char name[MAXNAMELENGTH];
	card hand[HAND_SIZE];
	char fifo[0];
} player;

typedef struct {
	card deck[DECK_SIZE];
} deck;

player me;
int rank = 0, myturn = 0;
char suit = 'x';
pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond;

typedef struct {
	int nplayers;
	player players[MAXNUMPLAYERS];
	int roundnumber;
	int turn;
	card tablecards[MAXNUMPLAYERS];
	deck d;
	int decki;
	time_t time_elapsed;
	pthread_mutex_t mutex;
	pthread_cond_t cond_var;
} shared_mem;

void print_deck(deck d) {
	int i;
	for (i = 0; i < DECK_SIZE; i++) {
		printf(" i - %i, R - %i, S - %c \n", i, d.deck[i].rank, d.deck[i].suit);
	}
}

void suit_init(int i, card* tmp);

card card_init(int i, int k) {

	card tmp;

	tmp.rank = i + 1;

	suit_init(k, &tmp);
	//printf("card_init fim\n");
	//printf("a sair com i - %i, k - %i, rank = %i, suit = %c\n",i,k, tmp.rank, tmp.suit);
	return tmp;
}

void initialize_deck(deck *d) {
	int i, k;
	int deckit = 0;
	for (k = 0; k < DECK_SUITS; k++) {
		for (i = 0; i < SUIT_CARDS; i++) {
			//printf("i - %i\n", i);
			d->deck[deckit] = card_init(i, k);
			//printf("i2 - %i\n", i);
			deckit++;
		}
	}
}

void shuffle_deck(deck *d);

shared_mem* create_shared(char* shm_name, int shm_size, int *created) {
	int shm_fd;
	shared_mem *res;

	shm_fd = shm_open(shm_name, O_RDWR, 0660);

	if (shm_fd < 0) {
		*created = 1;
		shm_fd = shm_open(shm_name, O_RDWR | O_CREAT, 0660);

		if (shm_fd < 0) {
			perror("Failure in shm_open()");
			return NULL ;
		}

	} else
		*created = 0;

	if (*created) {
		if (ftruncate(shm_fd, shm_size) < 0) {
			perror("Failure in ftruncate()");
			return NULL ;
		}
	}

	res = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

	return (shared_mem *) res;
}

void printPlayer(const player player) {
	printf("%d - %s - \n", player.id, player.name);
}

void printPlayers(shared_mem* shm) {
	int i;
	for (i = 0; i < shm->nplayers; i++)
		printPlayer(shm->players[i]);
}

void print_hand(const player p) {
	int i;
	printf("HAND\n");
	for (i = 0; i < 4; i++) {
		if (p.hand[i].rank != -1) {
			switch (rank) {
			case 1:
				printf("R - %c - S - %c\n", 'A', p.hand[i].suit);
				break;
			case 11:
				printf("R - %c - S - %c\n", 'J', p.hand[i].suit);
				break;
			case 12:
				printf("R - %c - S - %c\n", 'Q', p.hand[i].suit);
				break;
			case 13:
				printf("R - %c - S - %c\n", 'K', p.hand[i].suit);
				break;
			default:
				printf("R - %i - S - %c\n", p.hand[i].rank, p.hand[i].suit);
				break;
			}
		} else
			i--;
	}
}

void print_hand2(const player p) {
	int i;
	printf("HAND\n");
	for (i = 0; i < 4; i++) {
		if (p.hand[i].rank != -1) {
			switch (rank) {
			case 1:
				printf("%i - R - %c - S - %c\n", i, 'A', p.hand[i].suit);
				break;
			case 11:
				printf("%i - R - %c - S - %c\n", i, 'J', p.hand[i].suit);
				break;
			case 12:
				printf("%i - R - %c - S - %c\n", i, 'Q', p.hand[i].suit);
				break;
			case 13:
				printf("%i - R - %c - S - %c\n", i, 'K', p.hand[i].suit);
				break;
			default:
				printf("%i - R - %i - S - %c\n", i, p.hand[i].rank, p.hand[i].suit);
				break;
			}
		} else
			i--;
	}
}

void print_table_cards(shared_mem * shm) {
	int i;
	for (i = 0; i < shm->turn; i++) {
		printf("R - %i - S - %c\n", shm->tablecards[i].rank, shm->tablecards[i].suit);
	}
}

void destroy_shared(shared_mem* shm, int shm_size, char* shm_name) {

	pthread_mutex_lock(&shm->mutex);

	while (shm->nplayers > 1) {
		printf("...%d still playing\n", shm->nplayers - 1);
		pthread_cond_wait(&shm->cond_var, &shm->mutex);
	}

	pthread_mutex_unlock(&shm->mutex);

	printf("Game over!\n");

	if (munmap(shm, shm_size) < 0) {
		perror("Failure in munmap()");
		exit(-1);
	}

	if (shm_unlink(shm_name) < 0) {
		perror("Failure in shm_unlink()");
		exit(-1);
	}

}

void init_shared(shared_mem* shm) {
	shm->roundnumber = 1;
	shm->turn = 1;
	shm->nplayers = 0;
	shm->time_elapsed = 0;
	initialize_deck(&(shm->d));
	shuffle_deck(&(shm->d));
	//print_deck(shm->d);
	shm->decki = 0;

	int i;
	for (i = 0; i < MAXNUMPLAYERS; i++)
		strcpy(shm->players[i].name, "");

	pthread_mutexattr_t mut_attr;
	pthread_mutexattr_init(&mut_attr);
	pthread_mutexattr_setpshared(&mut_attr, PTHREAD_PROCESS_SHARED);

	pthread_condattr_t cond_attr;
	pthread_condattr_init(&cond_attr);
	pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);

	pthread_cond_init(&shm->cond_var, &cond_attr);
	pthread_mutex_init(&shm->mutex, &mut_attr);
}

void addPlayer(shared_mem* shm, const char *playername) {
	pthread_mutex_lock(&shm->mutex);
	shm->nplayers++;
	me.id = shm->nplayers;
	strncpy(me.name, playername, MAXNAMELENGTH);

	int i = 0;
	while (strcmp(shm->players[i].name, ""))
		i++;

	shm->players[i] = me;

	pthread_cond_broadcast(&shm->cond_var);
	pthread_mutex_unlock(&shm->mutex);
}

void removePlayer(shared_mem* shm) {
	pthread_mutex_lock(&shm->mutex);
	shm->nplayers--;

	int i = 0;
	while (!strncmp(shm->players[i].name, me.name, MAXNAMELENGTH))
		i++;

	strcpy(shm->players[i].name, "");

	printf("Removed. %d still playing\n", shm->nplayers - 1);

	pthread_cond_broadcast(&shm->cond_var);
	pthread_mutex_unlock(&shm->mutex);
}

void waitPlayers(shared_mem* shm, const int nplayers) {
	pthread_mutex_lock(&shm->mutex);

	while (nplayers > shm->nplayers) {
		printf("...waiting for more players... %d/%d\n", shm->nplayers, nplayers);
		pthread_cond_wait(&shm->cond_var, &shm->mutex);
	}

	pthread_mutex_unlock(&shm->mutex);
}

void* waitTurn(void* arg) {
	shared_mem *shm = (shared_mem *) arg;
	time_t start, end;

	while (1) {
		pthread_mutex_lock(&shm->mutex);

		while (shm->turn != me.id) {
			printf("%s is playing...\n", shm->players[shm->turn - 1].name);
			pthread_cond_wait(&shm->cond_var, &shm->mutex);
		}

		if (shm->turn == shm->nplayers)
			printf("Round %d - it's your turn! Next is %s\n", shm->roundnumber, shm->players[0].name);
		else
			printf("Round %d - it's your turn! Next is %s\n", shm->roundnumber, shm->players[shm->turn].name);

		time(&start);

		pthread_mutex_lock(&mut);
		myturn = 1;
		while (rank == 0) {
			printf("Play your card.\n");
			pthread_cond_wait(&cond, &mut);
		}

		printf("Played %d of %c\n", rank, suit); //TODO trocar por: meter carta em shm, printCard()
		myturn = 0;
		rank = 0;

		time(&end);

		shm->time_elapsed = end - start;

		if (shm->roundnumber == 3) {

			if (me.id == shm->nplayers) {
				shm->roundnumber++;
				shm->turn = 1;
			} else
				shm->turn++;

			pthread_cond_broadcast(&shm->cond_var);
			pthread_mutex_unlock(&shm->mutex);
			return NULL ; //TODO condicao termio, mudar
		}

		if (me.id == shm->nplayers) {
			shm->roundnumber++;
			shm->turn = 1;
		} else
			shm->turn++;

		pthread_cond_broadcast(&shm->cond_var);
		pthread_mutex_unlock(&shm->mutex);
	}

	return NULL ;
}

void file_log_deal(char *filename) {

	FILE * file;
	struct tm * timeinfo;
	time_t rawtime;

	file = fopen(filename, "a");
	if (file == NULL ) {
		perror("Failure in fopen()");
	}

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	fprintf(file, "%-*s|", WHEN_LENGTH, "when");
	fprintf(file, "%-*s|", WHO_LENGTH, "who");
	fprintf(file, "%-*s|", WHAT_LENGTH, "what");
	fprintf(file, "%s\n", "result");

	fprintf(file, "%-*s|", WHEN_LENGTH, asctime(timeinfo));
	fprintf(file, "Dealer-%-*s|", WHO_LENGTH - 7, me.name);
	fprintf(file, "%-*s|", WHAT_LENGTH, "deal");
	fprintf(file, "-\n");

	fclose(file);
}

void file_log_hand(char *filename, int receive, player p) {

	FILE * file;
	struct tm * timeinfo;
	time_t rawtime;

	file = fopen(filename, "a");
	if (file == NULL ) {
		perror("Failure in fopen()");
	}

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	fprintf(file, "%-*s|", WHEN_LENGTH, "when");
	fprintf(file, "%-*s|", WHO_LENGTH, "who");
	fprintf(file, "%-*s|", WHAT_LENGTH, "what");
	fprintf(file, "%s\n", "result");

	fprintf(file, "%-*s|", WHEN_LENGTH, asctime(timeinfo));
	fprintf(file, "Player%d-%-*s|", me.id, WHO_LENGTH - 7, me.name);
	if (receive)
		fprintf(file, "%-*s|", WHAT_LENGTH, "receive_card");
	else
		fprintf(file, "%-*s|", WHAT_LENGTH, "hand");
	int i;
	for (i = 0; i < HAND_SIZE; i++) {
		if (p.hand[i].suit == 'h') {
			switch (rank) {
			case 1:
				printf("%c%c-", 'A', p.hand[i].suit);
				break;
			case 11:
				printf("%c%c-", 'J', p.hand[i].suit);
				break;
			case 12:
				printf("%c%c-", 'Q', p.hand[i].suit);
				break;
			case 13:
				printf("%c%c-", 'K', p.hand[i].suit);
				break;
			default:
				printf("%i%c-", p.hand[i].rank, p.hand[i].suit);
				break;
			}
			printf("/");
		}
	}
	for (i = 0; i < HAND_SIZE; i++) {
		if (p.hand[i].suit == 's') {
			switch (rank) {
			case 1:
				printf("%c%c-", 'A', p.hand[i].suit);
				break;
			case 11:
				printf("%c%c-", 'J', p.hand[i].suit);
				break;
			case 12:
				printf("%c%c-", 'Q', p.hand[i].suit);
				break;
			case 13:
				printf("%c%c-", 'K', p.hand[i].suit);
				break;
			default:
				printf("%i%c-", p.hand[i].rank, p.hand[i].suit);
				break;
			}
			printf("/");
		}
	}
	for (i = 0; i < HAND_SIZE; i++) {
		if (p.hand[i].suit == 'd') {
			switch (rank) {
			case 1:
				printf("%c%c-", 'A', p.hand[i].suit);
				break;
			case 11:
				printf("%c%c-", 'J', p.hand[i].suit);
				break;
			case 12:
				printf("%c%c-", 'Q', p.hand[i].suit);
				break;
			case 13:
				printf("%c%c-", 'K', p.hand[i].suit);
				break;
			default:
				printf("%i%c-", p.hand[i].rank, p.hand[i].suit);
				break;
			}
			printf("/");
		}
	}
	for (i = 0; i < HAND_SIZE; i++) {
		if (p.hand[i].suit == 'c') {
			switch (rank) {
			case 1:
				printf("%c%c-", 'A', p.hand[i].suit);
				break;
			case 11:
				printf("%c%c-", 'J', p.hand[i].suit);
				break;
			case 12:
				printf("%c%c-", 'Q', p.hand[i].suit);
				break;
			case 13:
				printf("%c%c-", 'K', p.hand[i].suit);
				break;
			default:
				printf("%i%c-", p.hand[i].rank, p.hand[i].suit);
				break;
			}
			printf("/");
		}
	}
	fclose(file);
}

void file_log_play(char *filename, card card) {

	FILE * file;
	struct tm * timeinfo;
	time_t rawtime;

	file = fopen(filename, "a");
	if (file == NULL ) {
		perror("Failure in fopen()");
	}

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	fprintf(file, "%-*s|", WHEN_LENGTH, "when");
	fprintf(file, "%-*s|", WHO_LENGTH, "who");
	fprintf(file, "%-*s|", WHAT_LENGTH, "what");
	fprintf(file, "%s\n", "result");

	fprintf(file, "%-*s|", WHEN_LENGTH, asctime(timeinfo));
	fprintf(file, "Player%d-%-*s|", me.id, WHO_LENGTH - 7, me.name);
	fprintf(file, "%-*s|", WHAT_LENGTH, "hand");
	fprintf(file, "%c%c", card.rank, card.suit);

	fclose(file);
}

card get_card(shared_mem * shm);

void play_card(player * p, shared_mem * shm) {
	printf("Choose card\n");
	print_hand2(*p);
	int cardi;
	scanf("%i", &cardi);
	card played1;
	played1 = p->hand[cardi];
	shm->tablecards[shm->turn - 1] = played1;
	p->hand[cardi] = get_card(shm);
}

void *keyboard(void *arg) {
	shared_mem *shm = (shared_mem *) arg;
	int command = 0, candidate_rank = 0;
	char input[100], candidate_suit = 'x';
	printf("\n1 - Play Card\n2 - Show cards on table\n3 - Show hand\n4 - See time\n");

	while (1) {
		scanf("%s", input);
		fflush(stdin);
		command = atoi(&input[0]);

		if (command != 1 && command != 2 && command != 3) {
			printf("Invalid command\n");
			continue;
		} else {
			switch (command) {
			case 1: {
				if (myturn != 1) {
					printf("It's not your turn!\n");
					command = 0;
					continue;
				}

				candidate_rank = 0;
				candidate_suit = 'x';

//TODO trocar isto -DAQUI- por verificar se tem a carta na mao
				/*
				 while (candidate_rank < 1 || candidate_rank > 13) {
				 printf("Rank %d?\n", candidate_rank);
				 scanf("%s", &input);
				 candidate_rank = atoi(&input[0]);
				 }
				 while (candidate_suit != 'c' && candidate_suit != 'd' && candidate_suit != 'h' && candidate_suit != 's') {
				 printf("Suit %c?\n", candidate_suit);
				 scanf("%s", &input);

				 if (strlen(input) > 1)
				 continue;

				 candidate_suit = input[0];
				 }
				 */

//-ATE AQUI-
				pthread_mutex_lock(&mut);
				play_card(&(shm->players[shm->turn - 1]), shm);
//printf("Vai jogar, antes tinha %d\n", rank);
//rank = candidate_rank;
//suit = candidate_suit;
				command = 0;
				pthread_cond_signal(&cond);
				pthread_mutex_unlock(&mut);
				break;
			}
			case 2:
				print_table_cards(shm);
				break;
			case 3:
				print_hand(shm->players[shm->turn - 1]);
				break;
			}
		}
	}

	return NULL ;
}

void suit_init(int i, card* tmp) {
	if (i == 0) {
		tmp->suit = 's';
	} else if (i == 1) {
		tmp->suit = 'h';
	} else if (i == 2) {
		tmp->suit = 'd';
	} else {
		tmp->suit = 'c';
	}
}

void shuffle_deck(deck *d) {
	int i;
	for (i = 0; i < DECK_SIZE; i++) {
		int r = rand() % DECK_SIZE;
		card tmp = d->deck[i];
		d->deck[i] = d->deck[r];
		d->deck[r] = tmp;
	}
}

card get_card(shared_mem * shm) {
	if (shm->decki < DECK_SIZE) {
		card tmp = shm->d.deck[shm->decki];
		(shm->decki)++;
		return tmp;
	}
	card tmp;
	tmp.rank = -1;
	tmp.suit = 'E';
	return tmp;
}

card read_card(card c, int fd) {
//printf("read card\n");
	char str[5];
	card tmp;

	int n = -1;
	do {
		n = read(fd, str, 1);
		if (!strcmp(str, "R")) {
//printf("entrou r %s , %d\n", str, n);
			str[0] = '\0';
			n = read(fd, str, 1);
//printf("entrou r 2 %s, %d\n", str, n);
			tmp.rank = atoi(str);
		}
		if (!strcmp(str, "S")) {
//printf("entrou s %s, %d\n", str, n);
			str[0] = '\0';
			n = read(fd, str, 1);
//printf("entrou s 2 %s, %d\n", str, n);
			tmp.suit = str[0];
//printf("\nSAIU com carta R - %i, S - %c\n", tmp.rank, tmp.suit);
			return tmp;
		}
	} while (n > 0);
//printf("\nSAIU com carta R - %i, S - %i\n", tmp.rank, tmp.suit);
	return tmp;
}

void write_card(card c, int fd) {
//printf("write card\n");
	int msglen;
	char msg[10];
	sprintf(msg, "R%dS%c\n", c.rank, c.suit);
	msglen = strlen(msg) + 1;
	if (write(fd, msg, msglen) == 0)
		perror("Error in write:");
}

void close_fifos(shared_mem* shm, int fd, int fdw[]) {
	int i;
	close(fd);
	for (i = 1; i < shm->nplayers; i++) {
		close(fdw[i]);
	}
}

int main(int argc, char *argv[]) {
	srand(time(NULL ));
	if (argc != 4) {
		printf("Usage: %s [player_name] [table_name] [nr_players]", argv[0]);
		exit(-1);
	}

	int created = 0, nplayers = atoi(argv[3]);

	if (nplayers > MAXNUMPLAYERS) {
		printf("Maximum nr of players is %d", MAXNUMPLAYERS);
		exit(-1);
	}

	shared_mem *shm;
	shm = create_shared(argv[2], sizeof(shared_mem), &created);

	if (shm == NULL ) {
		perror("Failure in create_shared()");
		exit(-1);
	}

	if (created == 1)
		init_shared(shm);

	addPlayer(shm, argv[1]);
	waitPlayers(shm, nplayers);
	printPlayers(shm);
	printf("All players here! Dealer is %s\n", shm->players[0].name);

	int fdw[shm->nplayers];
	int fd, i, j;

//OPENING/CREATING FIFO for reading
	if (strcmp(argv[1], shm->players[0].name)) {
		for (i = 0; i < shm->nplayers; i++) {
//printf("Comparing - %s Player - %s\n", argv[1], shm->players[i].name);
			if (!strcmp(shm->players[i].name, argv[1])) {
				mkfifo(shm->players[i].name, 0666);
//printf("\nOpening FIFO for reading - %d Player - %s\n", i, shm->players[i].name);
				if ((fd = open(shm->players[i].name, O_RDONLY)) == -1) {
					perror("Unable to open FIFO:");
					return EXIT_FAILURE;
				}
				//printf("PLAYER %i\n", i);
				for (j = 0; j < 4; j++) {
					card c;
					shm->players[i].hand[j] = read_card(c, fd);
				}
//print_hand(shm->players[i]);
			}
		}
	}

//OPENING FIFO for writing
	if (!strcmp(argv[1], shm->players[0].name)) {
		//printf("DEALER\n");
		for (i = 0; i < 4; i++) {
			shm->players[0].hand[i] = get_card(shm);
		}
		//print_hand(shm->players[0]);
		for (i = 1; i < shm->nplayers; i++) {
//printf("\nOpening FIFO for writing - %s \n", shm->players[i].name);
			mkfifo(shm->players[i].name, 0666);
			if ((fdw[i] = open(shm->players[i].name, O_WRONLY)) == -1) {
				perror("Unable to open FIFO 1:");
				return EXIT_FAILURE;
			}
			for (j = 0; j < 4; j++) {
				write_card(get_card(shm), fdw[i]);
			}
		}
	}

	pthread_t waitthread, keyboardthread;
	pthread_create(&keyboardthread, NULL, keyboard, shm);
	pthread_create(&waitthread, NULL, waitTurn, shm);
	pthread_join(waitthread, NULL );

	if (created == 1) {
		destroy_shared(shm, sizeof(shared_mem), argv[2]);
	} else {
		sleep(2);
		removePlayer(shm);
	}
	if (!strcmp(argv[1], shm->players[0].name))
		close_fifos(shm, fd, fdw);

	return 0;
}
