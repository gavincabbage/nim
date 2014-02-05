// CS415 Project #4: nim_match_server.c
// Gavin Cabbage - gavincabbage@gmail.com

// Compile: gcc -o nim_match_server nim_match_server.c (use Makefile!)
// Invoke: $ nim_match_server (intended to be initialized by nim_server only!)

// Exit Codes:
// <0> Successful termination
// <1> Argument error, expects none
// <2> Environment not found
// <3> Problem sending handles to client
// <4> Problem communicating with client

#include "nim.h"

// Global variables and function prototypes.
char handle1[20];
char handle2[20];
char init[] = {'O','X','X','X','X','X','X',
               'O','O','O','X','X','X','X',
			   'O','O','O','O','O','X','X',
			   'O','O','O','O','O','O','O'};
int sock1;
int sock2;
struct nim_board *board;

void update_board(int row, int col);
int game_over();
void error(int code);

int main(int argc, char *argv[]) { /////////////////////////////////////////////

	// Check for erroneous input arguments and get environment variables.
	if (argc != 1) error(1);
	char *env;
	if ( (env = getenv("H1")) == NULL ) error(2);
	else strcpy(handle1, env);
	if ( (env = getenv("H2")) == NULL ) error(2);
	else strcpy(handle2, env);
	sock1 = MATCH_SOCK_1;
	sock2 = MATCH_SOCK_2;

	// Send handles to players to indicate the match has begun.
	struct nim_msg *handle_msg1 = malloc(sizeof(struct nim_msg));
	struct nim_msg *handle_msg2 = malloc(sizeof(struct nim_msg));
	handle_msg1->type = handle_msg2->type = 'R';
	strcpy(handle_msg1->data, handle1);
	strcpy(handle_msg2->data, handle2);
	// send to first player
	if (s_send(sock1, (void *) handle_msg1, sizeof(struct nim_msg)) < 0)
		error(3);
	if (s_send(sock1, (void *) handle_msg2, sizeof(struct nim_msg)) < 0)
		error(3);
	// send to second player
	if (s_send(sock2, (void *) handle_msg1, sizeof(struct nim_msg)) < 0)
		error(3);
	if (s_send(sock2, (void *) handle_msg2, sizeof(struct nim_msg)) < 0)
		error(3);

	// Enter game loop.
	board = malloc(sizeof(struct nim_board));
	strcpy(board->board, init); // set board to initial config
	struct nim_msg *message = malloc(sizeof(struct nim_msg));
	struct nim_move *move = malloc(sizeof(struct nim_move));
	int turn = 1; // if odd, p1's turn; if even: p2's turn
	int resigned = 0; // indicate last player to move resigned
	for ( ; ; ) {

		// Send the current board to both players.
		if (s_send(sock1, (void *) board, sizeof(struct nim_board)) < 0)
			error(4);
		if (s_send(sock2, (void *) board, sizeof(struct nim_board)) < 0)
			error(4);
		
		// Check for a winner, notify clients and break if so.
		if ( (game_over()) || (resigned) ) {
			int winner, loser;
			if (turn % 2 == 1) { loser = sock2; winner = sock1; }
			else { loser = sock1; winner = sock2; }
			// last player to move is loser
			memset(message, 0, sizeof(struct nim_msg));
			message->type = 'L';
			if (s_send(loser, (void *) message, sizeof(struct nim_msg)) < 0)
				error(4);
			// other player is winner
			memset(message, 0, sizeof(struct nim_msg));
			message->type = 'W';
			if (s_send(winner, (void *) message, sizeof(struct nim_msg)) < 0)
				error(4);
			break;
		}
		
		// Request and receive move from appropriate player.
		memset(message, 0, sizeof(struct nim_msg));
		if (turn % 2 == 1) { // player1's turn
			// request move
			message->type = 'A';
			if (s_send(sock1, (void *) message, sizeof(struct nim_msg)) < 0)
				error(4);
			// dummy send to other player
			message->type = 'Z';
			if (s_send(sock2, (void *) message, sizeof(struct nim_msg)) < 0)
				error(4);
			// receive move
			memset(move, 0, sizeof(struct nim_move));
			if (s_recv(sock1, (void *) move, sizeof(struct nim_move)) < 0)
				error(4);
		} else { // player2's turn
			// request move
			message->type = 'A';
			if (s_send(sock2, (void *) message, sizeof(struct nim_msg)) < 0)
				error(4);
			// dummy send to other player
			message->type = 'Z';
			if (s_send(sock1, (void *) message, sizeof(struct nim_msg)) < 0)
				error(4);
			// receive move
			memset(move, 0, sizeof(struct nim_move));
			if (s_recv(sock2, (void *) move, sizeof(struct nim_move)) < 0)
				error(4);
		}

		// Update the board with the given move.
		update_board(move->row - '0', move->col - '0');
		if (move->row == '0' && move->col == '0') resigned = 1;
		turn += 1;
	} // end game loop
	
	close(sock1); close(sock2);
	exit(0);

} // end main //////////////////////////////////////////////////////////////////

// Update the board with given move.
// Note: assumes a valid move, checked by client.
void update_board(int row, int col) {
	int start = (7 * (row - 1)) + col - 1;
	int end = 7 * row;
	int i;
	for (i = start; i < end; i++) {
		board->board[i] = 'X';
	}
}

// Check for a winner, i.e. all stones removed.
int game_over() {
	int i;
	for (i = 0; i < 28; i++) {
		if (board->board[i] == 'O') return 0;
	}
	return 1;
}

// Print appropriate error message and exit.
void error(int code) {
	switch(code) {
	case 1:
		fprintf(stderr, "nim_match_server: argument error: exit 1\n");
		exit(1); break;
	case 2:
		fprintf(stderr, "nim_match_server: environment not found: exit 2\n");
		exit(2); break;
	case 3:
		fprintf(stderr, "nim_match_server: problem sending handles to client: exit 3\n");
		exit(3); break;
	case 4:
		fprintf(stderr, "nim_match_server: problem communicating with client: exit 4\n");
		exit(4); break;
	}
}
