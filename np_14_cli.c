#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <errno.h>
#include <unp.h>

#define CLEAR_SCREEN "\033[2J"
#define MOVE_CURSOR_HOME "\033[H"
#define TEXT_CYAN "\033[36m"
#define TEXT_RESET "\033[0m"
#define INPUT_PROMPT "\033[1;34m>> \033[0m"

char id[MAXLINE];

void clear_screen()
{
    printf(CLEAR_SCREEN MOVE_CURSOR_HOME);
    fflush(stdout);
}

void error_exit(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

void print_highlighted(const char *msg)
{
    printf(TEXT_CYAN "%s" TEXT_RESET, msg);
    fflush(stdout);
}

void interact_with_server(int sockfd)
{
    fd_set rset;
    int maxfd;
    char sendline[MAXLINE], recvline[MAXLINE];

    clear_screen();
    printf("Connecting to the server...\n");

    // Send player ID to server
    if (write(sockfd, id, strlen(id)) < 0)
    {
        error_exit("write error");
    }

    // Wait for server acknowledgment
    int n = read(sockfd, recvline, MAXLINE);
    if (n <= 0)
    {
        error_exit("Server connection error");
    }
    recvline[n] = '\0';
    print_highlighted(recvline);

    for (;;)
    {
        FD_ZERO(&rset);
        FD_SET(STDIN_FILENO, &rset);
        FD_SET(sockfd, &rset);
        maxfd = sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO;

        // Monitor inputs from both server and stdin
        if (select(maxfd + 1, &rset, NULL, NULL, NULL) < 0)
        {
            if (errno == EINTR)
                continue;
            error_exit("select error");
        }

        // Check for server messages
        if (FD_ISSET(sockfd, &rset))
        {
            n = read(sockfd, recvline, MAXLINE);
            if (n <= 0)
            {
                printf("Server disconnected. Exiting...\n");
                break;
            }
            recvline[n] = '\0';
            print_highlighted(recvline);
        }

        // Check for user input
        if (FD_ISSET(STDIN_FILENO, &rset))
        {

            if (fgets(sendline, MAXLINE, stdin) == NULL)
            {
                printf("Exiting...\n");
                break;
            }
            if (write(sockfd, sendline, strlen(sendline)) < 0)
            {
                error_exit("write error");
            }
        }
    }
}

int main(int argc, char **argv)
{
    int sockfd;
    struct sockaddr_in servaddr;

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <IPaddress> <ID>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Copy player ID
    strncpy(id, argv[2], MAXLINE);

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        error_exit("Socket creation failed");
    }

    // Set up server address
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERV_PORT);

    if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0)
    {
        error_exit("Invalid IP address");
    }

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        error_exit("Connection to server failed");
    }

    // Interact with server
    interact_with_server(sockfd);

    // Close connection
    close(sockfd);
    return 0;
}
