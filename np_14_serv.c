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
int game_round[1000];
int start_flag[1000]; // 1: started, 0: not yet
int status[1000][MAX_CLIENTS]={0}; // 0: no player, 1: spectator, 2: player, 3: ready, 4: not ready
int row_num[1000];
int col_num[1000];
int mine_num[1000];
char name[1000][MAX_CLIENTS][BUF_SIZE];
int client_pool[1000][MAX_CLIENTS]; //room num 000-999, max player per room = 4 
int peek[1000][MAX_CLIENTS];
int pass[1000][MAX_CLIENTS];
int steal[1000][MAX_CLIENTS];
int main_board[1000][22][22];//0: safe, 1: mine
int player_board[1000][MAX_CLIENTS][22][22];
//-1: unknown, 0~8: 0~8 mines nearby, 10~18: 0~8 mines nearby (peek), 100: mine (step), 200: mine (peek), 20: flag
char create_usage[] = "Usage: create {room_num(0-999)}\n";
char join_usage[] = "Usage: join {room_num(0-999)\n}";
char set_usage[] = "Usage: set {row num(1-20)} {col num(1-20)} {mine num}\n";
char step_usage[] = "Usage: step {row} {col}\n";
char range_message[] = "Out of range!\n";
char used_message[] = "This room number is used, please try another room number or join this room.\n";
char empty_message[] = "This room hasn't been create, please try another room number or create this room.\n";
char full_message[] = "This room is full, please try another room number\n";
char peek_usage[] = "Usage: peek {row num} {col num}\n";
char steal_usage[] = "Usage: steal {player num} {peek/pass/steal}\n";
char flag_usage[] = "Usage: flag {row num} {col num}\n";
char help_message[] = "leave\ncreate {room_num(0-999)}\njoin {room_num(0-999)\nset {row num(1-20)} {col num(1-20)} {mine num}\nready\ncancel\nstart\nstep {row} {col}\nflag {row num} {col num}\npeek {row num} {col num}\npass\nsteal {player num} {peek/pass/steal}\nhelp\n";
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

int main() {
    srand(time(NULL));
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
                            start_flag[room_num]=0;
                            status[room_num][0]=4;
                            client_pool[room_num][0]=client_fd;
                            strcpy(name[room_num][0],player_name);
                            snprintf(message,BUF_SIZE,
                                    "Welcome to room %d.\nYou are the host(#1) of this room.\n",
                                    room_num);
                            send(client_fd,message,strlen(message),0);
                            row_num[room_num]=6;
                            col_num[room_num]=6;
                            mine_num[room_num]=9;
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
                            if(start_flag[room_num]==0)
                            {
                                status[room_num][nth_player] = 4;
                            }
                            else
                            {
                                status[room_num][nth_player] = 1;
                                snprintf(message,BUF_SIZE,
                                        "The host has started the game.\nYou are the spectator now.\n");
                                send(client_fd,message,strlen(message),0);
                            }
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
                int temp_room_num = room_num;
                // Close client connection and remove from pool
                leave_broadcast(room_num,nth_player);
                room_num = -1;
                nth_player = -1;
                pthread_mutex_unlock(&client_mutex);
                send(client_fd,message,strlen(message),0);
                int winner = check_winner(temp_room_num);
                if(winner!=-1)
                {
                    for(int i=0;i<MAX_CLIENTS;i++)
                    {
                        if(status[temp_room_num][i]>0)
                        {
                            status[temp_room_num][i]=4;
                            if(i==winner)
                            {
                                snprintf(message,BUF_SIZE,"You are the last survivor.\nYou win!\n");
                                send(client_pool[temp_room_num][i],message,strlen(message),0);
                                start_flag[temp_room_num]=0;
                            }
                            else
                            {
                                snprintf(message,BUF_SIZE,"Player #%d (%s) win!\n",winner+1,name[temp_room_num][winner]);
                                send(client_pool[temp_room_num][i],message,strlen(message),0);
                            }
                        }
                    }
                    reset_game(temp_room_num);
                }
            }
        }
        else if(strcmp(token,"ready")==0)
        {
            nth_player = find_nth(room_num,client_fd);
            if(room_num==-1)
            {
                snprintf(message,BUF_SIZE,"You are not in any room.\nCreate or join a room to become a player.\n");
                send(client_fd,message,strlen(message),0);
            }
            else
            {
                if(start_flag[room_num]==1)
                {
                    snprintf(message,BUF_SIZE,"The host has started the game.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else
                {
                    status[room_num][nth_player]=3;
                }
            }
        }
        else if(strcmp(token,"cancel")==0)
        {
            nth_player = find_nth(room_num,client_fd);
            if(room_num==-1)
            {
                snprintf(message,BUF_SIZE,"You are not in any room.\nCreate or join a room to become a player.\n");
                send(client_fd,message,strlen(message),0);
            }
            else
            {
                if(start_flag[room_num]==1)
                {
                    snprintf(message,BUF_SIZE,"The host has started the game.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else
                {
                    status[room_num][nth_player]=4;
                }
            }
        }
        else if(strcmp(token,"start")==0)
        {
            for(int i=0;i<MAX_CLIENTS;i++)
            {
                printf("Player %d status: %d\n",i,status[room_num][i]);
            }
            nth_player = find_nth(room_num,client_fd);
            if(room_num==-1)
            {
                snprintf(message,BUF_SIZE,"You are not in any room.\nCreate a room to become a host.\n");
                send(client_fd,message,strlen(message),0);
            }
            else
            {
                if(nth_player != 0)
                {
                    snprintf(message,BUF_SIZE,"Only the host can start the game.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else if(start_flag[room_num]==1)
                {
                    snprintf(message,BUF_SIZE,"You have started the game.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else if(check_ready(room_num)<2)
                {
                    printf("ready: %d\n",check_ready(room_num));
                    snprintf(message,BUF_SIZE,"A game requires at least 2 players.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else
                {
                    start_flag[room_num]=1;
                    board_initialize(room_num,row_num[room_num],col_num[room_num],mine_num[room_num]);
                    for(int i=0;i<MAX_CLIENTS;i++)
                    {
                        if(status[room_num][i]==3)
                        {
                            status[room_num][i]=2;
                            snprintf(message,BUF_SIZE,"GAME START!\nYou are a player.\n");
                            send(client_pool[room_num][i],message,strlen(message),0);
                            peek[room_num][i]=0;
                            steal[room_num][i]=0;
                            pass[room_num][i]=0;
                        }
                        else if(status[room_num][i]==4)
                        {
                            status[room_num][i]=1;
                            snprintf(message,BUF_SIZE,"GAME START!\nYou are a spectator.\n");
                            send(client_pool[room_num][i],message,strlen(message),0);
                        }
                    }
                    game_round[room_num] = 0;
                    while(status[room_num][game_round[room_num]]!=2)
                    {
                        game_round[room_num]++;
                    }
                    for(int i=0;i<MAX_CLIENTS;i++)
                    {
                        if(status[room_num][i]!=0)
                        {
                            print_board(room_num,i);
                        }
                    }
                }
            }
        }
        else if(strcmp(token,"set")==0)
        {
            nth_player = find_nth(room_num,client_fd);
            if(room_num==-1)
            {
                snprintf(message,BUF_SIZE,"You are not in any room.\nCreate a room to become a host.\n");
                send(client_fd,message,strlen(message),0);
            }
            else if(nth_player!=0)
            {
                snprintf(message,BUF_SIZE,"Only the host can set the board.\n");
                send(client_fd,message,strlen(message),0);
            }
            else
            {
                token = strtok(NULL," \n");
                if(token==NULL)
                {
                    send(client_fd,set_usage,strlen(set_usage),0);
                }
                else
                {
                    int temp_row = str_to_number(token);
                    if(temp_row<1||temp_row>20)
                    {
                        send(client_fd,set_usage,strlen(set_usage),0);
                    }
                    else
                    {
                        token = strtok(NULL," \n");
                        if(token==NULL)
                        {
                            send(client_fd,set_usage,strlen(set_usage),0);
                        }
                        else
                        {
                            int temp_col = str_to_number(token);
                            if(temp_col<1||temp_col>20)
                            {
                                send(client_fd,set_usage,strlen(set_usage),0);
                            }
                            else
                            {
                                token = strtok(NULL," \n");
                                if(token==NULL)
                                {
                                    send(client_fd,set_usage,strlen(set_usage),0);
                                }
                                else
                                {
                                    int temp_mine = str_to_number(token);
                                    if(temp_mine==-1)
                                    {
                                        send(client_fd,set_usage,strlen(set_usage),0);
                                    }
                                    else if(temp_mine>temp_row*temp_col)
                                    {
                                        snprintf(message,BUF_SIZE,"Too many mines!\n");
                                        send(client_fd,message,strlen(message),0);
                                    }
                                    else
                                    {
                                        row_num[room_num] = temp_row;
                                        col_num[room_num] = temp_col;
                                        mine_num[room_num] = temp_mine;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        else if(strcmp(token,"step")==0)
        {
             if(room_num==-1)
             {
                snprintf(message,BUF_SIZE,"You are not in any room.\nCreate or join a room to become a player.\n");
                send(client_fd,message,strlen(message),0);
             }
             else
             {
                nth_player = find_nth(room_num,client_fd);
                if(status[room_num][nth_player]==3||status[room_num][nth_player]==4)
                {
                    snprintf(message,BUF_SIZE,"The game hasn't started yet.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else if(status[room_num][nth_player]!=2)
                {
                    snprintf(message,BUF_SIZE,"You are not the player in this game.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else if(nth_player!=game_round[room_num])
                {
                    snprintf(message,BUF_SIZE,"It's not your turn yet.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else
                {
                    token = strtok(NULL," \n");
                    if(token == NULL)
                    {
                        send(client_fd,step_usage,strlen(step_usage),0);
                    }
                    else
                    {
                        int step_row = str_to_number(token);
                        if(step_row<1||step_row>row_num[room_num])
                        {
                            send(client_fd,range_message,strlen(range_message),0);
                        }
                        else
                        {
                            token = strtok(NULL," \n");
                            if(token == NULL)
                            {
                                send(client_fd,step_usage,strlen(step_usage),0);
                            }
                            else
                            {
                                int step_col = str_to_number(token);
                                if(step_col<1||step_col>col_num[room_num])
                                {
                                    send(client_fd,range_message,strlen(range_message),0);
                                }
                                else
                                {
                                    if((player_board[room_num][nth_player][step_row][step_col]>=0&&
                                        player_board[room_num][nth_player][step_row][step_col]<=8)||
                                        player_board[room_num][nth_player][step_row][step_col]==100)
                                    {
                                        snprintf(message,BUF_SIZE,"This block is stepped.\n");
                                        send(client_fd,message,strlen(message),0);
                                    }
                                    else
                                    {
                                        int rand_num = rand()%20;
                                        if(rand_num<3)
                                        {
                                            get_item_broadcast(room_num,nth_player,rand_num);
                                        }
                                        step_broadcast(room_num,nth_player,step_row,step_col);
                                        game_round[room_num]++;
                                        game_round[room_num]%=MAX_CLIENTS;
                                        while(status[room_num][game_round[room_num]]<2)
                                        {
                                            game_round[room_num]++;
                                            game_round[room_num]%=MAX_CLIENTS;
                                        }
                                        for(int k=0;k<MAX_CLIENTS;k++)
                                        {
                                            if(status[room_num][k]!=0)
                                            {
                                                print_board(room_num,k);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
             }
        }
        else if(strcmp(token,"peek")==0)
        {
             if(room_num==-1)
             {
                snprintf(message,BUF_SIZE,"You are not in any room.\nCreate or join a room to become a player.\n");
                send(client_fd,message,strlen(message),0);
             }
             else
             {
                nth_player = find_nth(room_num,client_fd);
                if(status[room_num][nth_player]==3||status[room_num][nth_player]==4)
                {
                    snprintf(message,BUF_SIZE,"The game hasn't started yet.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else if(status[room_num][nth_player]!=2)
                {
                    snprintf(message,BUF_SIZE,"You are not the player in this game.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else if(nth_player!=game_round[room_num])
                {
                    snprintf(message,BUF_SIZE,"It's not your turn yet.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else if(peek[room_num][nth_player]==0)
                {
                    snprintf(message,BUF_SIZE,"You don't have any peek item.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else
                {
                    token = strtok(NULL," \n");
                    if(token == NULL)
                    {
                        send(client_fd,peek_usage,strlen(peek_usage),0);
                    }
                    else
                    {
                        int peek_row = str_to_number(token);
                        if(peek_row<1||peek_row>row_num[room_num])
                        {
                            send(client_fd,range_message,strlen(range_message),0);
                        }
                        else
                        {
                            token = strtok(NULL," \n");
                            if(token == NULL)
                            {
                                send(client_fd,peek_usage,strlen(peek_usage),0);
                            }
                            else
                            {
                                int peek_col = str_to_number(token);
                                if(peek_col<1||peek_col>col_num[room_num])
                                {
                                    send(client_fd,range_message,strlen(range_message),0);
                                }
                                else
                                {
                                    if((player_board[room_num][nth_player][peek_row][peek_col]>=0&&
                                        player_board[room_num][nth_player][peek_row][peek_col]<=8)||
                                        (player_board[room_num][nth_player][peek_row][peek_col]>=10&&
                                        player_board[room_num][nth_player][peek_row][peek_col]<=18)||
                                        player_board[room_num][nth_player][peek_row][peek_col]==100||
                                        player_board[room_num][nth_player][peek_row][peek_col]==200)
                                    {
                                        snprintf(message,BUF_SIZE,"This block is not unknown.\n");
                                        send(client_fd,message,strlen(message),0);
                                    }
                                    else
                                    {
                                        peek_broadcast(room_num,nth_player,peek_row,peek_col);
                                        peek[room_num][nth_player]--;
                                        print_board(room_num,nth_player);
                                    }
                                }
                            }
                        }
                    }
                }
             }
        }
        else if(strcmp(token,"pass")==0)
        {
             if(room_num==-1)
             {
                snprintf(message,BUF_SIZE,"You are not in any room.\nCreate or join a room to become a player.\n");
                send(client_fd,message,strlen(message),0);
             }
             else
             {
                nth_player = find_nth(room_num,client_fd);
                if(status[room_num][nth_player]==3||status[room_num][nth_player]==4)
                {
                    snprintf(message,BUF_SIZE,"The game hasn't started yet.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else if(status[room_num][nth_player]!=2)
                {
                    snprintf(message,BUF_SIZE,"You are not the player in this game.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else if(nth_player!=game_round[room_num])
                {
                    snprintf(message,BUF_SIZE,"It's not your turn yet.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else if(pass[room_num][nth_player]==0)
                {
                    snprintf(message,BUF_SIZE,"You don't have any pass item.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else
                {
                    pass_broadcast(room_num,nth_player);
                    pass[room_num][nth_player]--;
                    game_round[room_num]++;
                    game_round[room_num]%=MAX_CLIENTS;
                    while(status[room_num][game_round[room_num]]<2)
                    {
                        game_round[room_num]++;
                        game_round[room_num]%=MAX_CLIENTS;
                    }
                    for(int k=0;k<MAX_CLIENTS;k++)
                    {
                        if(status[room_num][k]!=0)
                        {
                            print_board(room_num,k);
                        }
                    }
                }
             }
        }
        else if(strcmp(token,"steal")==0)
        {
            if(room_num==-1)
             {
                snprintf(message,BUF_SIZE,"You are not in any room.\nCreate or join a room to become a player.\n");
                send(client_fd,message,strlen(message),0);
             }
             else
             {
                nth_player = find_nth(room_num,client_fd);
                if(status[room_num][nth_player]==3||status[room_num][nth_player]==4)
                {
                    snprintf(message,BUF_SIZE,"The game hasn't started yet.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else if(status[room_num][nth_player]!=2)
                {
                    snprintf(message,BUF_SIZE,"You are not the player in this game.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else if(nth_player!=game_round[room_num])
                {
                    snprintf(message,BUF_SIZE,"It's not your turn yet.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else if(steal[room_num][nth_player]==0)
                {
                    snprintf(message,BUF_SIZE,"You don't have any steal item.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else
                {
                    token = strtok(NULL," \n");
                    if(token==NULL)
                    {
                        send(client_fd,steal_usage,strlen(steal_usage),0);
                    }
                    int target_player = str_to_number(token);
                    target_player--;
                    if(target_player<0||target_player>=MAX_CLIENTS)
                    {
                        send(client_fd,steal_usage,strlen(steal_usage),0);
                    }
                    else
                    {
                        token = strtok(NULL," \n");
                        if(token == NULL)
                        {
                            send(client_fd,steal_usage,strlen(steal_usage),0);
                        }
                        else
                        {
                            if(strcmp(token,"peek")==0)
                            {
                                if(peek[room_num][target_player]==0)
                                {
                                    send(client_fd,"That player doesn't have peek item.\n",BUF_SIZE,0);
                                }
                                else
                                {
                                    steal_broadcast(room_num,nth_player,target_player,0);
                                }
                            }
                            else if(strcmp(token,"pass")==0)
                            {
                                if(peek[room_num][target_player]==0)
                                {
                                    send(client_fd,"That player doesn't have pass item.\n",BUF_SIZE,0);
                                }
                                else
                                {
                                    steal_broadcast(room_num,nth_player,target_player,1);
                                }
                            }
                            else if(strcmp(token,"steal")==0)
                            {
                                if(peek[room_num][target_player]==0)
                                {
                                    send(client_fd,"That player doesn't have steal item.\n",BUF_SIZE,0);
                                }
                                else
                                {
                                    steal_broadcast(room_num,nth_player,target_player,2);
                                }
                            }
                            else
                            {
                                send(client_fd,steal_usage,strlen(steal_usage),0);
                            }
                        }
                    }
                }
             }
        }
        else if(strcmp(token,"flag")==0)
        {
             if(room_num==-1)
             {
                snprintf(message,BUF_SIZE,"You are not in any room.\nCreate or join a room to become a player.\n");
                send(client_fd,message,strlen(message),0);
             }
             else
             {
                nth_player = find_nth(room_num,client_fd);
                if(status[room_num][nth_player]==3||status[room_num][nth_player]==4)
                {
                    snprintf(message,BUF_SIZE,"The game hasn't started yet.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else if(status[room_num][nth_player]!=2)
                {
                    snprintf(message,BUF_SIZE,"You are not the player in this game.\n");
                    send(client_fd,message,strlen(message),0);
                }
                else
                {
                    token = strtok(NULL," \n");
                    if(token == NULL)
                    {
                        send(client_fd,step_usage,strlen(step_usage),0);
                    }
                    else
                    {
                        int flag_row = str_to_number(token);
                        if(flag_row<1||flag_row>row_num[room_num])
                        {
                            send(client_fd,range_message,strlen(range_message),0);
                        }
                        else
                        {
                            token = strtok(NULL," \n");
                            if(token == NULL)
                            {
                                send(client_fd,step_usage,strlen(step_usage),0);
                            }
                            else
                            {
                                int flag_col = str_to_number(token);
                                if(flag_col<1||flag_col>col_num[room_num])
                                {
                                    send(client_fd,range_message,strlen(range_message),0);
                                }
                                else
                                {
                                    if(player_board[room_num][nth_player][flag_row][flag_col]==-1)
                                    {
                                        player_board[room_num][nth_player][flag_row][flag_col]=20;
                                        print_board(room_num,nth_player);
                                    }
                                    else if(player_board[room_num][nth_player][flag_row][flag_col]==20)
                                    {
                                        player_board[room_num][nth_player][flag_row][flag_col]=-1;
                                        print_board(room_num,nth_player);
                                    }
                                    else
                                    {
                                        send(client_fd,"You can't put the falg on the known block.\n",BUF_SIZE,0);
                                    }
                                }
                            }
                        }
                    }
                }
             }
        }
        else if(strcmp(token,"help")==0)
        {
            send(client_fd,help_message,strlen(help_message),0);
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
        status[room_num][0] = status[room_num][1];
        strcpy(name[room_num][0],name[room_num][1]);
        for(int i=2;i<MAX_CLIENTS;i++)
        {
            if(client_pool[room_num][i]!=0)
            {
                snprintf(message,BUF_SIZE,"The host(%s) left the room.\n%s is the new host.\nYou are player #%d now.\n",leave_name,name[room_num][0],i);
                send(client_pool[room_num][i],message,strlen(message),0);
            }
            client_pool[room_num][i-1]=client_pool[room_num][i];
            status[room_num][i-1] = status[room_num][i];
            strcpy(name[room_num][i-1],name[room_num][i]);
        }
        client_pool[room_num][MAX_CLIENTS-1]=0;
        status[room_num][MAX_CLIENTS-1]=0;
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
            status[room_num][i-1]=status[room_num][i];
            strcpy(name[room_num][i-1],name[room_num][i]);
        }
        client_pool[room_num][MAX_CLIENTS-1]=0;
        status[room_num][MAX_CLIENTS-1]=0;
        strcpy(name[room_num][MAX_CLIENTS-1],"");
    }
}
void board_initialize(int room, int row, int col, int mine) {
    for (int i = 1; i <= row; i++) {
        for (int j = 1; j <= col; j++) {
            main_board[room][i][j] = 0;
        }
    }

    int total_cells = row * col;
    int *positions = (int *)malloc(total_cells * sizeof(int));
    for (int i = 0; i < total_cells; i++) {
        positions[i] = i;
    }

    for (int i = total_cells - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = positions[i];
        positions[i] = positions[j];
        positions[j] = temp;
    }

    for (int i = 0; i < mine; i++) {
        int pos = positions[i];
        int rand_row = pos / col + 1;
        int rand_col = pos % col + 1;
        main_board[room][rand_row][rand_col] = 1;
    }
    free(positions);
    for(int t=0;t<MAX_CLIENTS;t++)
    {
        for(int i=1;i<=row;i++)
        {
            for(int j=0;j<=col;j++)
            {
                player_board[room][t][i][j]=-1;
                peek[room][t]=0;
                pass[room][t]=0;
                steal[room][t]=0;
            }
        }
    }
}
int check_winner(int room_num)
{
    int survivor = 0;
    int winner = -1;
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        if(status[room_num][i]==2)
        {
            winner = i;
            survivor++;
        }
    }
    if(survivor<2)
    {
        return winner;
    }
    else
    {
        return -1;
    }
}
int check_ready(int room_num)
{
    int res=0;
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        if(status[room_num][i]==3)
        {
            res++;
        }
    }
    return res;
}
void reset_game(int room_num)
{
    for(int i=0;i<21;i++)
    {
        for(int j=0;j<21;j++)
        {
            main_board[room_num][i][j]=0;
        }
    }
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        for(int j=0;j<21;j++)
        {
            for(int k=0;k<21;k++)
            {
                player_board[room_num][i][j][k] = -1;
            }
        }
    }
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        if(status[room_num][i]!=0)
        {
            status[room_num][i]=4;
        }
    }
}
void step_broadcast(int room_num,int nth, int row,int col)
{
    char message[BUF_SIZE];
    snprintf(message,BUF_SIZE,"Player #%d (%s) steps %d %d.\n",nth+1,name[room_num][nth],row,col);
    if(main_board[room_num][row][col]==1)
    {
        status[room_num][nth] = 1; // loser become spectator
        for(int i=0;i<MAX_CLIENTS;i++)
        {
            player_board[room_num][i][row][col] = 100;
        }
        for(int i=0;i<MAX_CLIENTS;i++)
        {
            if(status[room_num][i]>0)
            {
                if(i==nth)
                {
                    snprintf(message,BUF_SIZE,"You step on the mine!\n");
                    send(client_pool[room_num][i],message,strlen(message),0);
                }
                else
                {
                    snprintf(message,BUF_SIZE,"Player #%d (%s) steps on the mine!\n",nth+1,name[room_num][nth]);
                    send(client_pool[room_num][i],message,strlen(message),0);
                }
            }
        }
        int winner = check_winner(room_num);
        if(winner!=-1)
        {
            for(int i=0;i<MAX_CLIENTS;i++)
            {
                if(status[room_num][i]>0)
                {
                    status[room_num][i]=4;
                    if(i==winner)
                    {
                        snprintf(message,BUF_SIZE,"You are the last survivor.\nYou win!\n");
                        send(client_pool[room_num][i],message,strlen(message),0);
                        start_flag[room_num]=0;
                    }
                    else
                    {
                        snprintf(message,BUF_SIZE,"Player #%d (%s) win!\n",winner+1,name[room_num][winner]);
                        send(client_pool[room_num][i],message,strlen(message),0);
                    }
                }
            }
            reset_game(room_num);
        }
    }
    else
    {
        int neighbor_mine = 0;
        for(int i=row-1;i<=row+1;i++)
        {
            for(int j=col-1;j<=col+1;j++)
            {
                if(main_board[room_num][i][j]==1)
                {
                    neighbor_mine++;
                }
            }
        }
        for(int i=0;i<MAX_CLIENTS;i++)
        {
            player_board[room_num][i][row][col] = neighbor_mine;
        }
    }
}
void peek_broadcast(int room_num,int nth,int row,int col)
{
    char message[BUF_SIZE];
    snprintf(message,BUF_SIZE,"Player %d (%s) uses peek.\n",nth+1,name[room_num][nth]);
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        if(status[room_num][i]!=0&&i!=nth)
        {
            send(client_pool[room_num][i],message,strlen(message),0);
        }
    }
    if(main_board[room_num][row][col]==1)
    {
        player_board[room_num][nth][row][col]=200;
    }
    else
    {
        int mine_num = 0;
        for(int i=row-1;i<=row+1;i++)
        {
            for(int j = col-1;j<=col+1;j++)
            {
                if(main_board[room_num][i][j]==1)
                {
                    mine_num++;
                }
            }
        }
        player_board[room_num][nth][row][col] = mine_num+10;
    }
}
void pass_broadcast(int room_num,int nth,int row,int col)
{
    char message[BUF_SIZE];
    snprintf(message,BUF_SIZE,"Player %d (%s) uses pass.\n",nth+1,name[room_num][nth]);
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        if(status[room_num][i]!=0&&i!=nth)
        {
            send(client_pool[room_num][i],message,strlen(message),0);
        }
    }
}
void steal_broadcast(int room_num,int nth,int target,int item_no)
{
    char message[BUF_SIZE];
    char target_message[BUF_SIZE];
    steal[room_num][nth]--;
    if(item_no==0)
    {
        snprintf(message,BUF_SIZE,"Player %d (%s) steals player %d (%s)'s %s item.\n",nth+1,name[room_num][nth],target+1,name[room_num][target],"peek");
        snprintf(target_message,BUF_SIZE,"Your %s item is stolen by player %d (%s).\n","peek",nth+1,name[room_num][nth]);
        peek[room_num][target]--;
        peek[room_num][nth]++;
    }
    if(item_no==1)
    {
        snprintf(message,BUF_SIZE,"Player %d (%s) stole player %d (%s)'s %s item.\n",nth+1,name[room_num][nth],target+1,name[room_num][target],"pass");
        snprintf(target_message,BUF_SIZE,"Your %s item is stolen by player %d (%s).\n","pass",nth+1,name[room_num][nth]);
        pass[room_num][target]--;
        pass[room_num][nth]++;
    }
    if(item_no==2)
    {
        snprintf(message,BUF_SIZE,"Player %d (%s) stole player %d (%s)'s %s item.\n",nth+1,name[room_num][nth],target+1,name[room_num][target],"steal");
        snprintf(target_message,BUF_SIZE,"Your %s item is stolen by player %d (%s).\n","steal",nth+1,name[room_num][nth]);
        steal[room_num][target]--;
        steal[room_num][nth]++;
    }
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        if(i==target)
        {
            send(client_pool[room_num][i],target_message,strlen(target_message),0);
        }
        else if(status[room_num][i]!=0&&i!=nth)
        {
            send(client_pool[room_num][i],message,strlen(message),0);
        }
    }
}
void get_item_broadcast(int room_num,int nth,int item_no)
{
    char message[BUF_SIZE];
    if(item_no==0)
    {
        snprintf(message,BUF_SIZE,"Player %d (%s) gets %s item.\n",nth+1,name[room_num][nth],"peek");
        peek[room_num][nth]++;
    }
    if(item_no==1)
    {
        snprintf(message,BUF_SIZE,"Player %d (%s) gets %s item.\n",nth+1,name[room_num][nth],"pass");
        pass[room_num][nth]++;
    }
    if(item_no==2)
    {
        snprintf(message,BUF_SIZE,"Player %d (%s) gets %s item.\n",nth+1,name[room_num][nth],"steal");
        steal[room_num][nth]++;
    }
    printf("%s",message);
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        if(status[room_num][i]!=0)
        {
            send(client_pool[room_num][i],message,strlen(message),0);
        }
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
//-1: unknown, 0~8: 0~8 mines nearby, 10~18: 0~8 mines nearby (peek), 100: mine (step), 200: mine (peek), 20: flag
void print_board(int room_num,int nth)
{
    char message[BUF_SIZE] = "\n";
    for(int i=1;i<=row_num[room_num];i++)
    {
        for(int j=1;j<=col_num[room_num];j++)
        {
            if(player_board[room_num][nth][i][j]==-1)
            {
                strcat(message,"⬜");
            }
            else if(player_board[room_num][nth][i][j]== 0)
            {
                strcat(message,"\xEF\xBC\x90");
            }
            else if(player_board[room_num][nth][i][j]== 1)
            {
                strcat(message,"\xEF\xBC\x91");
            }
            else if(player_board[room_num][nth][i][j]== 2)
            {
                strcat(message,"\xEF\xBC\x92");
            }
            else if(player_board[room_num][nth][i][j]== 3)
            {
                strcat(message,"\xEF\xBC\x93");
            }
            else if(player_board[room_num][nth][i][j]== 4)
            {
                strcat(message,"\xEF\xBC\x94");
            }
            else if(player_board[room_num][nth][i][j]== 5)
            {
                strcat(message,"\xEF\xBC\x95");
            }
            else if(player_board[room_num][nth][i][j]== 6)
            {
                strcat(message,"\xEF\xBC\x96");
            }
            else if(player_board[room_num][nth][i][j]== 7)
            {
                strcat(message,"\xEF\xBC\x97");
            }
            else if(player_board[room_num][nth][i][j]== 8)
            {
                strcat(message,"\xEF\xBC\x98");
            }
            else if(player_board[room_num][nth][i][j]== 10)
            {
                strcat(message,"\x1B[33m\xEF\xBC\x90\x1B[0m");
            }
            else if(player_board[room_num][nth][i][j]== 11)
            {
                strcat(message,"\x1B[33m\xEF\xBC\x91\x1B[0m");
            }
            else if(player_board[room_num][nth][i][j]== 12)
            {
                strcat(message,"\x1B[33m\xEF\xBC\x92\x1B[0m");
            }
            else if(player_board[room_num][nth][i][j]== 13)
            {
                strcat(message,"\x1B[33m\xEF\xBC\x93\x1B[0m");
            }
            else if(player_board[room_num][nth][i][j]== 14)
            {
                strcat(message,"\x1B[33m\xEF\xBC\x94\x1B[0m");
            }
            else if(player_board[room_num][nth][i][j]== 15)
            {
                strcat(message,"\x1B[33m\xEF\xBC\x95\x1B[0m");
            }
            else if(player_board[room_num][nth][i][j]== 16)
            {
                strcat(message,"\x1B[33m\xEF\xBC\x96\x1B[0m");
            }
            else if(player_board[room_num][nth][i][j]== 17)
            {
                strcat(message,"\x1B[33m\xEF\xBC\x97\x1B[0m");
            }
            else if(player_board[room_num][nth][i][j]== 18)
            {
                strcat(message,"\x1B[33m\xEF\xBC\x98\x1B[0m");
            }
            else if(player_board[room_num][nth][i][j]== 20)
            {
                strcat(message,"\xEF\xBC\xA6");
            }
            else if(player_board[room_num][nth][i][j]== 100)
            {
                strcat(message,"\x1B[31m\xEF\xBC\xA2\x1B[0m");
            }
            else
            {
                strcat(message,"\x1B[35m\xEF\xBC\xA2\x1B[0m");
            }
        }
        strcat(message,"\n");
        send(client_pool[room_num][nth],message,strlen(message),0);
        snprintf(message,BUF_SIZE,"");
    }
    snprintf(message,BUF_SIZE,"\n");
    send(client_pool[room_num][nth],message,strlen(message),0);
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        if(game_round[room_num] == i)
        {
            snprintf(message,BUF_SIZE,"Player #%d (%s): peek: %d, pass: %d, steal: %d <-\n",i,name[room_num][i],peek[room_num][i],pass[room_num][i],steal[room_num][i]);
        }
        else if(status[room_num][i]==1)
        {
            snprintf(message,BUF_SIZE,"Player #%d (%s): peek: %d, pass: %d, steal: %d (spectator)\n",i,name[room_num][i],peek[room_num][i],pass[room_num][i],steal[room_num][i]);
        }
        else if(status[room_num][i]!=0)
        {
            snprintf(message,BUF_SIZE,"Player #%d (%s): peek: %d, pass: %d, steal: %d\n",i,name[room_num][i],peek[room_num][i],pass[room_num][i],steal[room_num][i]);        
        }
        if(status[room_num][i]!=0)
        {
            send(client_pool[room_num][nth],message,strlen(message),0);
        }
    }
}
