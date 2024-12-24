#include	"unp.h"
#include  <string.h>
#include  <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>

char id[MAXLINE];

/* the following two functions use ANSI Escape Sequence */
/* refer to https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797 */


void clr_scr() {
	printf("\x1B[2J");
};

void set_scr() {		// set screen to 80 * 25 color mode
	printf("\x1B[=3h");
};

void game_start(FILE *fp, int sockfd){
	int pipe_c_to_py[2]; // parent -> child
	int       stdineof, peer_exit, n;
    char      sendline[MAXLINE], recvline[MAXLINE];
	stdineof = 0;
	peer_exit = 0;

	// scanf("%s", recvline)

    if (pipe(pipe_c_to_py) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        // child exec game
        close(pipe_c_to_py[1]);

        // connect pipe
        dup2(pipe_c_to_py[0], STDIN_FILENO);

        execlp("python3", "python3", "game.py", "--num", "10", NULL);
        perror("execlp");
        exit(EXIT_FAILURE);
    } else {
        // parent
        close(pipe_c_to_py[0]);

        fd_set rset;
        char buffer[MAXLINE];

        while (1) {
            FD_ZERO(&rset);

            FD_SET(STDIN_FILENO, &rset);
			int maxfdp = STDIN_FILENO;
			if (stdineof == 0) {
				FD_SET(fileno(fp), &rset);
				if (fileno(fp) > maxfdp)
					maxfdp = fileno(fp);
			};	
			if (peer_exit == 0) {
				FD_SET(sockfd, &rset);
				if (sockfd > maxfdp)
					maxfdp = sockfd;
			};

            struct timeval timeout = {1, 0};            

            int ready = select(maxfdp + 1, &rset, NULL, NULL, &timeout);
            if (ready == -1) {
                perror("select");
                break;
            } else if (ready == 0) {
                continue;
            }

			if (FD_ISSET(sockfd, &rset)) {  /* socket is readable */
				n = read(sockfd, recvline, MAXLINE);
				if (n == 0) {
					if (stdineof == 1)
						return;         /* normal termination */
					else {
						kill(pid, SIGTERM);
						wait(NULL);
						// printf("(End of input from the peer!)\n");
						peer_exit = 1;
						return;
					};
				}
				else if (n > 0) {
					recvline[n] = '\0';
					printf("\x1B[0;36m%s\x1B[0m", recvline);
					fflush(stdout);
				}
				else { // n < 0
					kill(pid, SIGTERM);
					wait(NULL);
					// printf("(server down)\n");
					return;
				};
			}

            // read from stdin
            if (FD_ISSET(STDIN_FILENO, &rset)) {
                ssize_t bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
					if(strncmp(buffer, "quit", 4) == 0){
						write(pipe_c_to_py[1], buffer, bytes_read); // 将输入发送到子进程
						Writen(sockfd, buffer, bytes_read+1);			
						return;
					}
					else{
						write(pipe_c_to_py[1], buffer, bytes_read); // 将输入发送到子进程
					}                    
                } else if (bytes_read < 0 && errno != EAGAIN) {
                    perror("read stdin");
                }
            }
        }

        close(pipe_c_to_py[1]);
        wait(NULL);
    }
}

void xchg_data(FILE *fp, int sockfd)
{
    int       maxfdp1, stdineof, peer_exit, n;
    fd_set    rset;
    char      sendline[MAXLINE], recvline[MAXLINE];
	
	set_scr();
	clr_scr();
    Writen(sockfd, id, strlen(id));
    printf("sent: %s\n", id);
	readline(sockfd, recvline, MAXLINE);
	printf("recv: %s", recvline);
	readline(sockfd, recvline, MAXLINE);
	printf("recv: %s", recvline);	
    stdineof = 0;
	peer_exit = 0;

    for ( ; ; ) {
		FD_ZERO(&rset);
		maxfdp1 = 0;
        if (stdineof == 0) {
            FD_SET(fileno(fp), &rset);
			maxfdp1 = fileno(fp);
		};	
		if (peer_exit == 0) {
			FD_SET(sockfd, &rset);
			if (sockfd > maxfdp1)
				maxfdp1 = sockfd;
		};
        maxfdp1++;
        Select(maxfdp1, &rset, NULL, NULL, NULL);
		if (FD_ISSET(sockfd, &rset)) {  /* socket is readable */
			n = read(sockfd, recvline, MAXLINE);
			if (n == 0) {
 		   		if (stdineof == 1)
                    return;         /* normal termination */
		   		else {
					printf("\n(End of input from the peer!)\n");
					peer_exit = 1;
					return;
				};
            }
			else if (n > 0) {
				recvline[n] = '\0';
				if(strncmp(recvline, "start", 5) == 0){
					game_start(fp, sockfd);
				}
				else{
					printf("\x1B[0;36m%s\x1B[0m", recvline);
					fflush(stdout);
				}			
			}
			else { // n < 0
			    printf("\n(server down)\n");
				return;
			};
        }
		
        if (FD_ISSET(fileno(fp), &rset)) {  /* input is readable */

            if (Fgets(sendline, MAXLINE, fp) == NULL) {
				if (peer_exit)
					return;
				else {
					printf("(leaving...)\n");
					stdineof = 1;
					Shutdown(sockfd, SHUT_WR);      /* send FIN */
				};
            }
			else {
				n = strlen(sendline);
				sendline[n] = '\n';
				Writen(sockfd, sendline, n+1);
			};
        }
    }
};

int
main(int argc, char **argv)
{
	int					sockfd;
	struct sockaddr_in	servaddr;

	if (argc != 3)
		err_quit("usage: tcpcli <IPaddress> <ID>");

	sockfd = Socket(AF_INET, SOCK_STREAM, 0);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(SERV_PORT);
    printf("%d\n",SERV_PORT);
	Inet_pton(AF_INET, argv[1], &servaddr.sin_addr);
	strcpy(id, argv[2]);

	Connect(sockfd, (SA *) &servaddr, sizeof(servaddr));

	xchg_data(stdin, sockfd);		/* do it all */

	exit(0);
}
