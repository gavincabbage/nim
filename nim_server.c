// CS415 Project #4: nim_server.c
// Gavin Cabbage - gavincabbage@gmail.com

// Compile: gcc -o nim_server nim_server.c (use Makefile!)
// Invoke: $ nim_server {password}

// Exit Codes:
// <0> Successful termination
// <1> Argument error
// <2> Problem with getaddrinfo, report details with gai_strerror()
// <3> Problem initializing query socket
// <4> Problem initializing play socket
// <5> Problem with select
// <6> Error processing client query
// <7> Error processing client play request
// <8> Error creating server address file

#include "nim.h"

// Global variables and function prototypes.
char *password;
int err_code;
int query_sock, play_sock; // socket descriptors
struct addrinfo hints, *addrlist;
struct sockaddr_in *q_in, q_from; // for init_query_sock()
struct sockaddr_in *p_in, p_from; // for init_play_sock()
FILE *config; // address file
char waiting[20]; // waiting client's handle
int wait_sock; // waiting client's socket descriptor
int inprog = 0; // number of games in progress
struct nim_game *game_list;
char games[LINE_MAX];

void init_query_sock(), init_play_sock();
void init_addr_file();
void usr1handler();
void usr2handler(); // SIGUSR2 handler
void build_games_string();
void error(int code); // error/exit function

int main(int argc, char *argv[]) { /////////////////////////////////////////////

	// Process input arguments.
	if (argc == 1) ; // no password
	else if (argc == 2) password = argv[1];
	else error(1);
	memset(waiting, 0, 20);
	
	// Initialize query and play sockets, create address file.
	init_query_sock();
	init_play_sock();
	init_addr_file();

	// Main server loop listens and reacts to client requests on both sockets.
	int max_sock, active;
	struct timeval timeout;
	// embed signal handlers
	if (signal(SIGUSR1, usr1handler) == SIG_ERR) error(9);
	if (signal(SIGUSR2, usr2handler) == SIG_ERR) error(9);
	for ( ; ; ) { 
	
		// Update list of games in progress.
		struct nim_game *cur = game_list;
		struct nim_game *prev;
		while (cur != NULL) {
			int status;
			waitpid(cur->match_pid, &status, WNOHANG);
			if (WIFEXITED(status)) {
				if (cur == game_list) game_list = NULL;
				else {
					prev->next = cur->next;
					cur = cur->next;
				} 
				inprog -= 1;
			} else {
				prev = cur;
				cur = cur->next;
			}
		} if (inprog < 0) inprog = 0;

		// Build select list, find max socket descriptor, set timeout.
		fd_set socks;
		FD_ZERO(&socks);
		FD_SET(query_sock, &socks);
		FD_SET(play_sock, &socks);
		max_sock = query_sock > play_sock ? query_sock : play_sock;
		timeout.tv_sec = 1; timeout.tv_usec = 0;
		
		// Hang select and process client requests.
		active = select(max_sock+1, &socks, (fd_set*) 0, (fd_set*) 0, &timeout);
		if (active < 0) error(5);
		else if (active == 0) continue;
		else { // got a client request
		
			// Handle client query request.
			if (FD_ISSET(query_sock, &socks)) {
				
				// Receive client query.
				socklen_t q_size = sizeof(q_from);
				struct nim_query *query = malloc(sizeof(struct nim_query));
				int rec = recvfrom(query_sock, query, sizeof(query), 0,
						(struct sockaddr*) &q_from, &q_size);
				if (rec < sizeof(query)) error(6);
				
				// If password enabled, check password before responding.
				if ( (password == NULL) || (!strcmp(password, query->password)) ) {
				
					// Respond to client query.
					struct nim_query_response *response = 
							malloc(sizeof(struct nim_query_response));
					response->inprog = htonl(inprog);
					strcpy(response->waiting, waiting);
					build_games_string();
					strcpy(response->games, games);
					int sent = sendto(query_sock, response, sizeof(*response), 0,
							(struct sockaddr*) &q_from,
							sizeof(struct sockaddr_in));
					if (sent < sizeof(*response)) error(6);		
					free(response);
				}
				free(query);
			} // end query handling
			
			// Handle client play request.
			if (FD_ISSET(play_sock, &socks)) {

				// Accept client connection.
				char handle[20];
				int new_sock;
				socklen_t p_size = sizeof(p_from);
				new_sock = accept(play_sock,(struct sockaddr*) &p_from, &p_size);
				if (new_sock < 0) error(7);

				// Accept and check password if enabled, process client handle.
				struct nim_msg *request = malloc(sizeof(struct nim_msg));
				if (s_recv(new_sock, (void *) request, sizeof(struct nim_msg)) < 0)
					error(7);
				char rec_pass[20];
				strcpy(rec_pass, request->data);

				if ( (password == NULL) || (!strcmp(password, rec_pass)) ) {
					// correct password, get client handle
					memset(request, 0, sizeof(struct nim_msg));
					request->type = 'H';
					if (s_send(new_sock, (void *) request, sizeof(struct nim_msg)) < 0)
						error(7);
					memset(request, 0, sizeof(struct nim_msg));
					if (s_recv(new_sock, (void *) request, sizeof(struct nim_msg)) < 0)
						error(7);
					strcpy(handle, request->data);
				} else { // incorrect password, notify client and close socket
					memset(request, 0, sizeof(struct nim_msg));
					request->type = 'X';
					if (s_send(new_sock, (void *) request, sizeof(struct nim_msg)) < 0)
						error(7);
					close(new_sock);
				}
				free(request);
				
				// Set client to wait or spawn a new game.
				if (waiting[0] == 0) { // no client waiting
					wait_sock = new_sock;
					strcpy(waiting, handle);
				} else { // another client already waiting
					char handle1[20]; char handle2[20];
					strcpy(handle1, waiting);
					strcpy(handle2, handle);
					int child;
					if ( (child = fork()) < 0 ) error(10);
					else if (child == 0) { // child
						// close server sockets
						close(play_sock);
						close(query_sock);
						// dup socket descriptors to well known values
						if (wait_sock != MATCH_SOCK_1) {
							dup2(wait_sock, MATCH_SOCK_1);
							close(wait_sock);
						}
						if (new_sock != MATCH_SOCK_2) {
							dup2(new_sock, MATCH_SOCK_2);
							close(new_sock);
						}
						// spawn a match server for the game
						char *env[3];
						char envbuf1[23]; char envbuf2[23];
						sprintf(envbuf1, "H1=%s", handle1);
						sprintf(envbuf2, "H2=%s", handle2);
						env[0] = envbuf1;
						env[1] = envbuf2;
						env[2] = NULL;
						char *args[2];
						args[0] = "./nim_match_server";
						args[1] = NULL;
						execve("./nim_match_server", args, env);		
					} else { // parent
						// close player sockets and clear buffers
						close(wait_sock);
						close(new_sock);
						memset(waiting, 0, sizeof(waiting));
						memset(handle, 0, sizeof(handle));
						// add match to games list
						struct nim_game *new = malloc(sizeof(struct nim_game));
						new->match_pid = child;
						strcpy(new->player1, handle1);
						strcpy(new->player2, handle2);
						new->next = game_list;
						game_list = new;
						inprog += 1;
					}
				}
			} // end play handling
		}
	} // end server loop

	exit(0);

} // end main //////////////////////////////////////////////////////////////////

// Initialize datagram socket to listen and respond to client quaries.
void init_query_sock() {

	// Set hints struct and get address info.
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_NUMERICSERV | AI_PASSIVE;
	hints.ai_protocol = 0;
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;
	if ( (err_code = getaddrinfo(NULL, "4201", &hints, &addrlist)) != 0 ) 
		error(2);
	q_in = (struct sockaddr_in*) addrlist->ai_addr;

	// Initialize socket and bind to port.
	query_sock = socket(addrlist->ai_family, addrlist->ai_socktype, 0);
	if (query_sock < 0) error(3);
	if ( bind(query_sock, (struct sockaddr*) q_in, sizeof(struct sockaddr_in)) < 0 )
		error(3);
}

// Initialize stream socket to listen and repond to client play requests.
void init_play_sock() {

	// Set hints struct and get address info.
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICSERV | AI_PASSIVE;
	hints.ai_protocol = 0;
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;
	if ( (err_code = getaddrinfo(NULL, "4202", &hints, &addrlist) ) != 0 )
		error(2);
	p_in = (struct sockaddr_in*) addrlist->ai_addr;
	
	// Initialize socket, bind to port and set to listen.
	play_sock = socket(addrlist->ai_family, addrlist->ai_socktype, 0);
	if (play_sock < 0) error(4);
	if ( bind(play_sock, (struct sockaddr*) p_in, sizeof(struct sockaddr_in)) < 0 )
		error(4);
	if ( listen(play_sock, 5) < 0 )
		error(4);
}

// Create address file with local symbolic host and port numbers of
// query and play sockets, seperated with colons.
void init_addr_file() {

	// Get local hostname.
	char hostname[HOST_NAME_MAX];
	hostname[HOST_NAME_MAX-1] = '\0';
	gethostname(hostname, HOST_NAME_MAX);
	
	// Write to address file: hostname:query_port:play_port
	config = fopen("nim.conf", "w");
	if (config == NULL) error(8);
	fprintf(config, "%s:%d:%d", hostname, 4201, 4202); 
	fclose(config);
}

// Signal handler for SIGUSR2 induced clean termination.
// NOTE: Games in progress allowed to finish, per preliminary grading rubric.
void usr2handler() {

	// remove config file
	remove("nim.conf");
	// terminate normally
	exit(0);
}

// Signal handler for SIGUSR1 updates games in progress list.
void usr1handler() {

}

// Build string of games in progress from games list.
void build_games_string() {

	// Iterate through list of games and append to a string, handles seperated
	// by colons to be parsed by client.
	memset(games, 0, LINE_MAX);
	struct nim_game *cur = game_list;
	while(cur != NULL) {
		char cur_game[41];
		sprintf(cur_game, "%s:%s:", cur->player1, cur->player2);
		strcat(games, cur_game);
		cur = cur->next;
	}
}

// Print appropriate error message and exit.
void error(int code) {
	switch(code) {
	case 1:
		fprintf(stderr, "nim_server: argument error: exit 1\n");
		exit(1); break;
	case 2:
		fprintf(stderr, "nim_server: getaddrinfo: %s: exit 2\n", gai_strerror(err_code));
		exit(2); break;
	case 3:
		fprintf(stderr, "nim_server: problem initializing query socket: exit 3\n");
		exit(3); break;
	case 4:
		fprintf(stderr, "nim_server: problem initializing play socket: exit 4\n");
		exit(4); break;
	case 5:
		fprintf(stderr, "nim_server: problem with select: exit 5\n");
		exit(5); break;
	case 6:
		fprintf(stderr, "nim_server: error processing client query: exit 6\n");
		exit(6); break;
	case 7:
		fprintf(stderr, "nim_server: error processing client play request: exit 7\n");
		exit(7); break;
	case 8:
		fprintf(stderr, "nim_server: error creating server address file: exit 8\n");
		exit(8); break;
	case 9:
		fprintf(stderr, "nim_server: signal error: exit 9\n");
		exit(9); break;
	case 10:
		fprintf(stderr, "nim_server: fork error: exit 10\n");
		exit(10); break;
	}
}
