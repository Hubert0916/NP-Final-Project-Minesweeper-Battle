#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unp.h>

#define BACKLOG 1024     // Maximum number of pending connections
#define BUF_SIZE 1024  // Buffer size for messages
#define MAX_CLIENTS 4 // Maximum number of clients

typedef struct {
    int client_fd;
    char player_name[BUF_SIZE];
} client_info_t;
void *handle_client(void* args);
void broadcast_message(int sender_fd, const char *message, int message_len);
void enter_broadcast(int room_num,int assign_num);
void signal_handler(int sig);

// Shared variables
char name[1000][MAX_CLIENTS][BUF_SIZE];
int client_pool[1000][MAX_CLIENTS]; //room num 000-999, max player per room = 4 
int peek[1000][MAX_CLIENTS];
int pass[1000][MAX_CLIENTS];
int steal[1000][MAX_CLIENTS];
char create_usage[] = "Usage: create {room_num(0-999)}";
char join_usage[] = "Usage: join {room_num(0-999)}";
char used_message[] = "This room number is used, please try another room number or join this room.\n";
char empty_message[] = "This room hasn't been create, please try another room number or create this room.\n";
char full_message[] = "This room is full, please try another room number\n";
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

int main() {
    for(int  i=0;i<1000;i++)
    {
        for(int j=0;j<MAX_CLIENTS;j++)
        {
            strcpy(name[i][j],"");
        }
    }
    int listen_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Set up signal handler for cleaning up zombie processes
    signal(SIGCHLD, signal_handler);

    // Create socket
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Initialize server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(SERV_PORT); // Example port 8080

    // Bind socket to address
    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // Start listening for connections
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("Listen failed");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on port %d...\n", SERV_PORT);

    while (1) {
        client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }

        printf("New client connected from %s:%d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        char cur_name[BUF_SIZE];
        memset(cur_name, 0, BUF_SIZE);
        recv(client_fd, cur_name, BUF_SIZE, 0);
        // Create a thread to handle this client
        client_info_t *info = malloc(sizeof(client_info_t));
        info->client_fd = client_fd;
        strncpy(info->player_name, cur_name, BUF_SIZE);
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, info);
        pthread_detach(tid); // Automatically clean up the thread after it exits
    }

    close(listen_fd);
    return 0;
}

void *handle_client(void *arg) {
    client_info_t *info = (client_info_t *)arg;
    int client_fd = info->client_fd;
    char player_name[BUF_SIZE];
    strncpy(player_name, info->player_name, BUF_SIZE);

    free(info);
    int room_num = -1;
    int nth_player = -1;
    char buffer[BUF_SIZE];
    char buffer_copy[BUF_SIZE]; //for turning recv message into command and args
    char message[BUF_SIZE];
    int empty_flag;
    int full_flag;
    snprintf(buffer,BUF_SIZE,"Welcome to minesweeper battle!\nPlease create or join a room\n");
    send(client_fd, buffer, strlen(buffer), 0);
    while (1) {
        int nbytes = recv(client_fd, buffer, BUF_SIZE, 0);
        if (nbytes <= 0) {
            // Notify the leaving user
            send(client_fd, "Bye!\n", 5, 0);

            pthread_mutex_lock(&client_mutex);
            printf("Client %s disconnected\n", player_name);
            nth_player = find_nth(room_num,client_fd);
            // Close client connection and remove from pool
            close(client_fd);
            if(room_num!=-1)
            {
                leave_broadcast(room_num,nth_player);
                room_num = -1;
                nth_player = -1;
            }
            pthread_mutex_unlock(&client_mutex);
            break;
        }
        buffer[nbytes] = ' ';
        buffer[nbytes+1] = '\0'; // Ensure null-terminated string
        printf("Message from %s: %s", player_name, buffer);
        snprintf(buffer_copy,BUF_SIZE,"%s",buffer);
        char *token = strtok(buffer_copy," \n");
        if(strcmp(token,"create")==0)
        {
            if(room_num!=-1)
            {
                snprintf(message,BUF_SIZE,
                        "You are in room %d now.\nLeave the room before creating new room.\n",
                        room_num);
                send(client_fd,message,strlen(message),0);
            }
            else
            {
                token = strtok(NULL," \n");
                if(token==NULL)
                {
                    send(client_fd,create_usage,strlen(create_usage),0);
                }
                else
                {
                    room_num = str_to_number(token);
                    if(room_num == -1)
                    {
                        send(client_fd,create_usage,strlen(create_usage),0);
                    }
                    else
                    {
                        empty_flag=1;
                        for(int i=0;i<MAX_CLIENTS;i++)
                        {
                            if(client_pool[room_num][i]!=0)
                            {
                                empty_flag=0;
                            }
                        }
                        if(empty_flag==0)
                        {
                            send(client_fd,used_message,strlen(used_message),0);
                            room_num = -1;
                        }
                        else
                        {
                            nth_player=0;
                            client_pool[room_num][0]=client_fd;
                            strcpy(name[room_num][0],player_name);
                            snprintf(message,BUF_SIZE,
                                    "Welcome to room %d.\nYou are the host(#1) of this room.\n",
                                    room_num);
                            send(client_fd,message,strlen(message),0);
                        }
                    }
                }
            }
        }
        else if(strcmp(token,"join")==0)
        {
            if(room_num!=-1)
            {
                snprintf(message, BUF_SIZE,
                        "You are in room %d now.\nLeave the room before joining new room.\n",
                        room_num);
                send(client_fd,message,strlen(message),0);
            }
            else
            {
                token = strtok(NULL," \n");
                if(token==NULL)
                {
                    send(client_fd,join_usage,strlen(join_usage),0);
                }
                else
                {
                    room_num = str_to_number(token);
                    if(room_num == -1)
                    {
                        send(client_fd,join_usage,strlen(join_usage),0);
                    }
                    else
                    {
                        empty_flag=1;
                        full_flag = 1;
                        for(int i=0;i<MAX_CLIENTS;i++)
                        {
                            if(client_pool[room_num][i]!=0)
                            {
                                empty_flag=0;
                            }
                            else
                            {
                                full_flag=0;
                            }
                        }
                        if(empty_flag==1)
                        {
                            send(client_fd,empty_message,strlen(empty_message),0);
                            room_num = -1;
                        }
                        else if(full_flag==1)
                        {
                            send(client_fd,full_message,strlen(full_message),0);
                            room_num = -1;
                        }
                        else
                        {
                            for(int i=MAX_CLIENTS-1;i>=0;i--)
                            {
                                if(client_pool[room_num][i]==0)
                                {
                                    nth_player=i;
                                }
                            }
                            client_pool[room_num][nth_player]=client_fd;
                            strcpy(name[room_num][nth_player],player_name);
                            enter_broadcast(room_num,nth_player);
                            snprintf(message,BUF_SIZE,
                                    "Welcome to room %d.\nYou are player #%d of this room.\n",
                                    room_num,nth_player+1);
                            send(client_fd,message,strlen(message),0);
                        }
                    }
                }
            }
        }
        else if(strcmp(token,"leave")==0)
        {
            if(room_num==-1)
            {
                snprintf(message,BUF_SIZE,"You are not in any room.\n");
                send(client_fd,message,strlen(message),0);
            }
            else
            {
                pthread_mutex_lock(&client_mutex);
                snprintf(message,BUF_SIZE,"You left room #%d.\n",room_num);
                nth_player = find_nth(room_num,client_fd);
                // Close client connection and remove from pool
                leave_broadcast(room_num,nth_player);
                room_num = -1;
                nth_player = -1;
                pthread_mutex_unlock(&client_mutex);
                send(client_fd,message,strlen(message),0);
            }
        }
        else if(strcmp(token,"peek")==0)
        {

        }
        else if(strcmp(token,"pass")==0)
        {

        }
        else if(strcmp(token,"steal")==0)
        {

        }
        else if(strcmp(token,"help")==0)
        {

        }
    }

    return NULL;
}



void enter_broadcast(int room_num,int assign_num) 
{
    char sendline[BUF_SIZE];
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (i != assign_num && client_pool[room_num][i] != 0) {
            snprintf(sendline, BUF_SIZE, "(Player #%d %s enters)\n",
                     assign_num + 1, name[room_num][assign_num]);
            send(client_pool[room_num][i], sendline, strlen(sendline), 0);
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

void leave_broadcast(int room_num,int nth)
{
    char message[BUF_SIZE];
    char leave_name[BUF_SIZE];
    strcpy(leave_name,name[room_num][nth]);
    if(nth == 0)
    {
        if(client_pool[room_num][1]!=0)
        {
            snprintf(message,BUF_SIZE,"The host(%s) left the room.\nYou are the new host(#1) now.\n",leave_name);
            send(client_pool[room_num][1],message,strlen(message),0);
        }
        client_pool[room_num][0] = client_pool[room_num][1];
        strcpy(name[room_num][0],name[room_num][1]);
        for(int i=2;i<MAX_CLIENTS;i++)
        {
            if(client_pool[room_num][i]!=0)
            {
                snprintf(message,BUF_SIZE,"The host(%s) left the room.\n%s is the new host.\nYou are player #%d now.\n",leave_name,name[room_num][0],i);
                send(client_pool[room_num][i],message,strlen(message),0);
            }
            client_pool[room_num][i-1]=client_pool[room_num][i];
            strcpy(name[room_num][i-1],name[room_num][i]);
        }
        client_pool[room_num][MAX_CLIENTS-1]=0;
        strcpy(name[room_num][MAX_CLIENTS-1],"");
    }
    else
    {
        for(int i=0;i<nth;i++)
        {
            snprintf(message,BUF_SIZE,"Player #%d(%s) left the room.\n",nth+1,leave_name);
            send(client_pool[room_num][i],message,strlen(message),0);
        }
        for(int i=nth+1;i<MAX_CLIENTS;i++)
        {
            if(client_pool[room_num][i]!=0)
            {
                snprintf(message,BUF_SIZE,"Player #%d(%s) left the room.\nYou are player #%d now.\n",nth+1,leave_name,i);
                send(client_pool[room_num][i],message,strlen(message),0);
            }
            client_pool[room_num][i-1]=client_pool[room_num][i];
            strcpy(name[room_num][i-1],name[room_num][i]);
        }
        client_pool[room_num][MAX_CLIENTS-1]=0;
        strcpy(name[room_num][MAX_CLIENTS-1],"");
    }
}
void signal_handler(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}
int str_to_number(const char *s) {
    char *endptr;
    long num = strtol(s, &endptr, 10);
    if (*endptr != '\0' && !isspace(*endptr)) {
        return -1;
    }
    if (num < 0 || num > 999) {
        return -1;
    }
    return (int)num;
}

int find_nth(int room_num,int client_fd)
{
    if(room_num == -1)
    {
        return -1;
    }
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        if(client_pool[room_num][i]==client_fd)
        {
            return i;
        }
    }
    return -1;
}
