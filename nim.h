// CS415 Project #4: nim.h (header)
// Gavin Cabbage - gavincabbage@gmail.com

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <ctype.h>

#define MATCH_SOCK_1 3
#define MATCH_SOCK_2 4


// Safe send and receive functions for TCP communication.
int s_send(int sock, void *buffer, int size) {
	int to_send = size; int sent = 0; int num;
	while (to_send > 0) {
		if ( (num = write(sock, buffer+sent, to_send)) < 0 )
			return -1; // error
		else { to_send -= num; sent += num; }
	} return 0;
}
int s_recv(int sock, void *buffer, int size) {
	int to_rec = size; int rec = 0; int num;
	while (to_rec > 0) {
		if ( (num = read(sock, buffer+rec, to_rec)) < 0 )
			return -1; // error
		else { to_rec -= num; rec += num; }
	} return 0;
}


// Games in progress linked list for server.
struct nim_game {
	int match_pid;
	char player1[20];
	char player2[20];
	struct nim_game *next;
};


// Nim Messaging Protocol
// ======================

 // General purpose message.
struct nim_msg {
	char type;
		// <A> move request - match -> nim
		// <H> handle request - server -> nim
		// <L> loss notification - match -> nim
		// <P> password submit - nim -> server
		// <R> handle response - nim -> server | match -> nim
		// <W> win notification - match -> nim
		// <X> incorrect password - server -> nim
	char data[20];
};

 // Client query.
 // nim -> server
struct nim_query {
	char password[20];
		// limit password to 20 characters
};

 // Server query response.
 // server -> nim
struct nim_query_response {
	int inprog; 
		// number of games in progress
	char waiting[20]; 
		// waiting user's handle
	char games[LINE_MAX]; 
		// list of games in progress
};

 // Match server board config.
 // match -> nim
struct nim_board {
	char board[28];
		// row order format:
		// [ row1 row2 row3 row4 ]
};

 // Client move response.
 // nim -> match
struct nim_move {
	char row;
	char col;
};
