all: nim_server nim_match_server nim

nim_server: nim_server.c nim.h
	$ gcc -Wall -o nim_server nim_server.c 

nim_match_server: nim_match_server.c nim.h
	$ gcc -Wall -o nim_match_server nim_match_server.c

nim: nim.c nim.h
	$ gcc -Wall -o nim nim.c

