// CS415 Project #4: nim.c (client)
// Gavin Cabbage - gavincabbage@gmail.com

// Compile: gcc -o nim nim.c (use Makefile!)
// Invoke: $ nim {-q} {-p password} 

// Exit Codes:
// <0> Successful termination
// <1> Argument error
// <2> Problem locating server address file
// <3> Problem querying server
// <4> No response to server query
// <5> Problem requesting to play
// <6> Problem communicating with match server

#include "nim.h"

// Global variables and function prototypes.
int query_mode = 0;
char password[20];
char handle[20];
char hostname[HOST_NAME_MAX];
char servaddr[HOST_NAME_MAX];
char *query_port, *play_port;
int play_sock;
int first = 0;
char b[28]; 

void get_config(), query_server();
void play_request(), play_game();
void display_board(), win(), loss();
int check_move(int row, int col);
void error(int code);

int main(int argc, char *argv[]) { /////////////////////////////////////////////

	// Process input arguments.
	int i;
	for (i = 1; i < argc; i++) {
		if ( (strcmp(argv[i], "-q") == 0) && (i == 1) ) query_mode = 1;
		else if (strcmp(argv[i], "-p") == 0) {
			i += 1; // next argument is the password string
			if (argv[i] != NULL) strcpy(password, argv[i]);
			else error(1);
		} else error(1);
	}

	// Get server information from config file.
	get_config();
	// Build full server domain name from given hostname.
	sprintf(servaddr, "%s", hostname);

	// Query server or request to play a game.
	if (query_mode) query_server();
	else {                   
		play_request();
	}
	
	// Query or game complete, terminate successfully.
	exit(0);

} // end main //////////////////////////////////////////////////////////////////

// Get server address information from config file.
void get_config() {
	
	FILE *config;
	config = fopen("nim.conf", "r");
	if (config == NULL) { // failure on first attempt, try again in 60 seconds
		fprintf(stderr, "nim: failure accessing server address file: retry in 60s\n");
		sleep(60); config = fopen("nim.conf", "r");
	} if (config == NULL) error(2); // failure on second attempt, exit
	char line[LINE_MAX];
	while (fgets(line, LINE_MAX, config) != NULL) {
		strcpy(hostname, strtok(line, ":"));
		query_port = strtok(NULL, ":");
		play_port = strtok(NULL, "");
	}
	fclose(config);
}

// Send datagram to server.
void query_server() {

	// Get server info for query.
	int query_sock, sent;
	struct sockaddr_in *q_dest;
	struct addrinfo hints, *addrlist;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_NUMERICSERV;
	hints.ai_protocol = 0;
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;
	if (getaddrinfo(servaddr, query_port, &hints, &addrlist) != 0 )
		error(3);
	q_dest = (struct sockaddr_in*) addrlist->ai_addr;
	
	// Initialize socket and send query request to server.
	query_sock = socket(addrlist->ai_family, addrlist->ai_socktype, 0);
	if (query_sock < 0) error(3);
	struct nim_query *query = malloc(sizeof(struct nim_query));
	memset(query, 0, sizeof(struct nim_query));
	if (password != NULL) strcpy(query->password, password);
	sent = sendto(query_sock, query, sizeof(query), 0,
			(struct sockaddr*) q_dest,
			sizeof(struct sockaddr_in));
	if (sent < sizeof(query)) error(3);
	
	// Build select list.
	fd_set socks;
	FD_ZERO(&socks);
	FD_SET(query_sock, &socks);
	int max_sock = query_sock;
	struct timeval timeout;
	timeout.tv_sec = 60; timeout.tv_usec = 0;
	
	// Wait up to 60s for server response, terminate if none.
	int active = select(max_sock+1, &socks, (fd_set*) 0, (fd_set*) 0, &timeout);
	if (active < 0) error(3); // select() error
	if (active == 0) error(4); // did not receive response
	else { // received response
	
		// Receive response.
		socklen_t q_size = sizeof(q_dest);
		struct nim_query_response *response = 
				malloc(sizeof(struct nim_query_response));
		int rec = recvfrom(query_sock, response, sizeof(*response), 0,
				(struct sockaddr*) q_dest, &q_size);
		if (rec < sizeof(*response)) error(3);
		int inprog = ntohl(response->inprog);
		
		// Display information and terminate.
		if (inprog == 1) printf("> There is 1 game in progress\n");
		else printf("> There are %d games in progress\n", inprog);
		if (inprog) { // display any games in progress
			int players = 2 * inprog - 1;
			char current[20];
			strcpy(current, strtok(response->games, ":"));
			printf("%20s vs. ", current);
			while (players) {
				strcpy(current, strtok(NULL, ":"));
				if (players % 2 == 0) printf("%20s vs. ", current);
				else printf("%-20s\n", current);
				players--;
			}
		}
		if (response->waiting[0] != 0) { // display any player waiting
			printf("> %s is waiting to play\n", response->waiting);
		}
		free(response);
		exit(0);
	}
}

// Connect to the server to play a game.
void play_request() {

	// Get server info for play request.
	struct sockaddr_in *p_dest;
	struct addrinfo hints, *addrlist;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICSERV | AI_PASSIVE;
	hints.ai_protocol = 0;
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;
	if (getaddrinfo(servaddr, play_port, &hints, &addrlist) != 0)
		error(5);
	p_dest = (struct sockaddr_in*) addrlist->ai_addr;
	
	// Initialize socket and connect to server play port.
	play_sock = socket(addrlist->ai_family, addrlist->ai_socktype, 0);
	if (play_sock < 0) error(5);
	if (connect(play_sock, (struct sockaddr*) p_dest, sizeof(struct sockaddr_in)) < 0)
		exit(5);
	
	// Send password if enabled and respond to server handle request.
	struct nim_msg *request = malloc(sizeof(struct nim_msg));
	request->type = 'P';
	strcpy(request->data, password);
	if (s_send(play_sock, (void *) request, sizeof(struct nim_msg)) < 0)
		error(5);
	memset(request, 0, sizeof(struct nim_msg));
	if (s_recv(play_sock, (void *) request, sizeof(struct nim_msg)) < 0)
		error(5);
	if (request->type != 'H') error(5);
	printf("Enter a handle to play: "); // get handle from user
	scanf("%s", handle);
	memset(request, 0, sizeof(struct nim_msg));
	strcpy(request->data, handle);
	request->type = 'R';
	if (s_send(play_sock, (void *) request, sizeof(struct nim_msg)) < 0)
		error(5);
	free(request);

	play_game();
}

// Play a game of nim through a match server.
void play_game() {

	// Receive handles from server and display.
	struct nim_msg *handle_msg = malloc(sizeof(struct nim_msg));
	if (s_recv(play_sock, (void *) handle_msg, sizeof(struct nim_msg)) < 0)
		error(4);
	printf("\nTHE GAME HAS BEGUN!\n");
	printf("Player 1: %s\n", handle_msg->data);
	if (!strcmp(handle_msg->data, handle)) first = 1;
	memset(handle_msg, 0, sizeof(struct nim_msg));
	if (s_recv(play_sock, (void *) handle_msg, sizeof(struct nim_msg)) < 0)
		error(4);
	printf("Player 2: %s\n", handle_msg->data);
	free(handle_msg);

	// Enter game loop. 
	int turn = first;
	struct nim_board *board = malloc(sizeof(struct nim_board));
	struct nim_msg *message = malloc(sizeof(struct nim_msg));
	struct nim_move *move = malloc(sizeof(struct nim_move));
	for ( ; ; ) {
	
		// Receive board config from server and display.
		memset(board, 0, sizeof(struct nim_board));
		if (s_recv(play_sock, (void *) board, sizeof(struct nim_board)) < 0)
			error(6);
		strcpy(b, board->board);
		display_board();
		
		// Receive and respond to move request if appropriate.
		memset(message, 0, sizeof(struct nim_msg));
		if (s_recv(play_sock, (void *) message, sizeof(struct nim_msg)) < 0)
			error(6);
		if (message->type == 'W') { win(); break; }
		else if (message->type == 'L') { loss(); break; }
		else if ( (message->type == 'A') && (turn % 2 == 1) ) { // respond to move request
			memset(move, 0, sizeof(struct nim_move));
			int valid_move = 0;
			char in[4]; int i; char cur;
			int row = -1; int col = -1;
			printf("\nEnter move: ");
			while (valid_move == 0) {
				fflush(stdin);
				row = -1; col = -1;
				fgets(in, sizeof(in), stdin);
				if (in[0] == '\n') continue;
				for (i = 0; i < strlen(in); i++) {
					cur = in[i];
					if (isspace(cur)) continue;
					else if (row < 0) row = cur - '0';
					else if (col < 0) col = cur - '0';
				}
				valid_move = check_move(row, col);
				if (!valid_move) printf("\nInvalid move, try again: ");
			}
			move->row = row + '0';
			move->col = col + '0';

			if (s_send(play_sock, (void *) move, sizeof(struct nim_move)) < 0)
				error(6);
		} else { // not client's turn
			printf("\nWaiting for opponent's move...\n");
		}
		turn += 1;
	} // end play loop
	free(board); free(message); free(move);
}

// Check a given move for validity.
int check_move(int row, int col) {
	if (row == 0 && col == 0) return -1;
	if (row < 1 || row > 4) return 0;
	if (col < 1 || col > 7) return 0;
	int ndx = (7 * (row - 1)) + col - 1;
	if (b[ndx] != 'O') return 0;
	return 1;
}

void display_board() {
	int i;
	printf("\nrow\n1|"); 
	for (i = 0; i < 7; i++) {
		if (b[i] == 'O') printf(" %c", b[i]);
		else printf("  ");
	} printf("\n2|"); 
	for (i = 7; i < 14; i++) {
		if (b[i] == 'O') printf(" %c", b[i]);
		else printf("  ");
	} printf("\n3|"); 
	for (i = 14; i < 21; i++) {
		if (b[i] == 'O') printf(" %c", b[i]);
		else printf("  ");
	} printf("\n4|"); 
	for (i = 21; i < 28; i++) {
		if (b[i] == 'O') printf(" %c", b[i]);
		else printf("  ");
	} printf("\n +-----------------\n   1 2 3 4 5 6 7 col\n");  
}

void win() {
	printf("\nGame over: you WIN!\n");
}

void loss() {
	printf("\nGame over: you LOSE!\n");
}

// Print appropriate error message and exit.
void error(int code) {
	switch(code) {
	case 1:
		fprintf(stderr, "nim: argument error: exit 1\n");
		exit(1); break;
	case 2:
		fprintf(stderr, "nim: unable to access server address file: exit 2\n");
		exit(2); break;
	case 3:
		fprintf(stderr, "nim: problem querying server: exit 3\n");
		exit(3); break;
	case 4:
		fprintf(stderr, "nim: no query response from server: exit 4\n");
		exit(4); break;
	case 5:
		fprintf(stderr, "nim: problem requesting to play: exit 5\n");
		exit(5); break;
	case 6:
		fprintf(stderr, "nim: problem communicating with match server: exit 6\n");
		exit(6); break;
	}
}
