#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <curl/curl.h>

#define BUFFER_SIZE 1024 * 1024

#define MAX_THREADS 10
#define MAX_QUEUE_SIZE 10

enum Mode
{
    SEND,
    RECV,
    HTTP
};

enum Proto
{
    TCP,
    UDP
};

struct Config
{
    enum Mode eMode;
    enum Proto eProto;
    int Stat;
    int PktSize;
    int PktNum;
    int PktRate;
    char *data;
    int SendRate;
    char *rhost;
    int rport;
    char *lhost;
    int lport;
    int sbufsize;
    int rbufsize;

    int connection_info;

    int tcp_socket;
    int tcp_new_socket;
    int udp_socket;

    int eTimeElapse;
    double eLossRate;
    int eLossPacket;
    int eSeqNum;
    double eSendRate;
    double eRecvRate;

    char *urlstr;
};

typedef struct Task
{
    struct Config *clientConfig;
} Task;

Task taskQueue[MAX_QUEUE_SIZE];
int taskCount = 0;
pthread_mutex_t queueMutex;
pthread_cond_t queueCond;

pthread_t *threads;
int poolSize;
int activeThreads = 0;
time_t lastUtilizationCheck = 0;

int TcpThreads = 0;
int UdpThreads = 0;

const char *modeToString(enum Mode mode)
{
    switch (mode)
    {
    case SEND:
        return "SEND";
    case RECV:
        return "RECV";
    default:
        return "UNKNOWN";
    }
}

const char *protoToString(enum Proto proto)
{
    switch (proto)
    {
    case TCP:
        return "TCP";
    case UDP:
        return "UDP";
    default:
        return "UNKNOWN";
    }
}

int DecodeArguments(struct Config *nConfig, char *pBuf, int iBufSize)
{

    int idx = 0;

    nConfig->eMode = ntohs(*((unsigned short *)(pBuf + idx)));
    idx += 2;
    nConfig->eProto = ntohs(*((unsigned short *)(pBuf + idx)));
    idx += 2;
    nConfig->PktSize = ntohl(*((unsigned long *)(pBuf + idx)));
    idx += 4;

    int iLen = ntohs(*((unsigned short *)(pBuf + idx)));
    idx += 2;
    nConfig->lhost = (char *)malloc(iLen + 1);
    memcpy(nConfig->lhost, pBuf + idx, iLen);
    idx += iLen; // Client hostname

    nConfig->lport = ntohs(*((unsigned short *)(pBuf + idx)));
    idx += 2; //  Client portnum

    int rLen = ntohs(*((unsigned short *)(pBuf + idx)));
    idx += 2;
    nConfig->rhost = (char *)malloc(iLen + 1);
    memcpy(nConfig->rhost, pBuf + idx, rLen);
    idx += rLen; // Server hostname

    nConfig->rport = ntohs(*((unsigned short *)(pBuf + idx)));
    idx += 2; //  Server portnum

    return idx;
}

int SendLoop(struct Config *pConfig)
{
    struct timeval StartTime, SendFinishTime, TimeForDelay;
    unsigned long SeqNum = 0;
    double SendDelay = 0;                 // in milliseconds
    pConfig->SendRate = pConfig->PktRate; // bytes/second

    struct sockaddr_in client;
    memset(&client, 0, sizeof(client));

    client.sin_family = AF_INET;
    client.sin_port = htons(pConfig->lport);
    inet_pton(AF_INET, pConfig->lhost, &client.sin_addr);

    if (pConfig->PktRate != 0)
        SendDelay = (float)(pConfig->PktSize) * 1000 / pConfig->SendRate;

    gettimeofday(&StartTime, NULL);

    printf("Start sending data\n");

    int current_connection = pConfig->connection_info++;
    printf("Connected to %s port %d [%d, %d]", pConfig->lhost, pConfig->lport, current_connection, pConfig->connection_info++);
    printf(" %s, %s, %.2d Bps\n", modeToString(pConfig->eMode), protoToString(pConfig->eProto), pConfig->PktRate);

    int testfd;

    // create tcp testfd
    if (pConfig->eProto == UDP)
    {

        if ((testfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in testaddr;
        int testport = 10;
        testport += pConfig->lport;

        testaddr.sin_family = AF_INET;
        testaddr.sin_port = htons(testport);
        inet_pton(AF_INET, pConfig->lhost, &testaddr.sin_addr);

        while (connect(testfd, (struct sockaddr *)&testaddr, sizeof(testaddr)) < 0)
        {
            perror("connection to the server failed: try again");
            sleep(1);
        }
    }
    // end

    while (1)
    {
        pConfig->data = (char *)malloc(pConfig->PktSize);
        *((unsigned long *)pConfig->data) = SeqNum;

        gettimeofday(&TimeForDelay, NULL);

        if (pConfig->eProto == TCP)
        {
            if (send(pConfig->tcp_socket, pConfig->data, pConfig->PktSize, MSG_NOSIGNAL) <= 0)
            {
                break;
            }
        }

        else if (pConfig->eProto == UDP)
        {
            sendto(pConfig->udp_socket, pConfig->data, pConfig->PktSize, 0, (struct sockaddr *)&client, sizeof(client));

            if (send(testfd, pConfig->data, pConfig->PktSize, MSG_NOSIGNAL) <= 0)
            {
                break;
            }
        }

        ++SeqNum;
        gettimeofday(&SendFinishTime, NULL);

        double TimeElapse = (double)(SendFinishTime.tv_sec - StartTime.tv_sec) * 1000 +
                            (double)(SendFinishTime.tv_usec - StartTime.tv_usec) / 1000;
        double TimeElapseForDelay = (double)(SendFinishTime.tv_sec - TimeForDelay.tv_sec) * 1000 +
                                    (double)(SendFinishTime.tv_usec - TimeForDelay.tv_usec) / 1000;

        double SendRate = SeqNum / TimeElapse;

        if (TimeElapseForDelay < SendDelay)
            usleep((int)((SendDelay - TimeElapseForDelay) * 1000));

        pConfig->eSendRate = SendRate;
        pConfig->eTimeElapse = TimeElapse;

        free(pConfig->data);
    }
}

int RecvLoop(struct Config *pConfig)
{
    struct timeval StartTime, RecvFinishTime;

    unsigned long SeqNum = 0;
    int PktLost = 0;
    char buffer[BUFFER_SIZE];
    char testbuffer[BUFFER_SIZE];

    struct sockaddr_in servaddr, client;
    memset(&servaddr, 0, sizeof(servaddr));
    memset(&client, 0, sizeof(client));

    int client_len = sizeof(client);

    gettimeofday(&StartTime, NULL);

    printf("Receiving data\n");

    int current_connection = pConfig->connection_info++;
    printf("Connected to %s port %d [%d, %d]", pConfig->rhost, pConfig->rport, current_connection, pConfig->connection_info++);
    printf(" %s, %s, %.2d Bps\n", modeToString(pConfig->eMode), protoToString(pConfig->eProto), pConfig->PktRate);

    int testfd, new_testfd;

    // udp test
    if (pConfig->eProto == UDP)
    {
        struct sockaddr_in testaddr, testclient;
        memset(&testaddr, 0, sizeof(testaddr));
        memset(&testclient, 0, sizeof(testclient));
        int testclient_len = sizeof(testclient);

        if ((testfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            perror("socket creation failed");

            exit(EXIT_FAILURE);
        }

        int testport = 10;
        testport += pConfig->rport;
        // use port + 10

        testaddr.sin_family = AF_INET;
        testaddr.sin_port = htons(testport);
        inet_pton(AF_INET, pConfig->rhost, &testaddr.sin_addr);

        if (bind(testfd, (struct sockaddr *)&testaddr, sizeof(testaddr)) < 0)
        {
            perror("bind failed");
            close(testfd);
            exit(EXIT_FAILURE);
        }

        listen(testfd, 5);

        new_testfd = accept(testfd, (struct sockaddr *)&testclient, &testclient_len);

        if (new_testfd < 0)
        {
            perror("accept failed");
            close(testfd);
            exit(EXIT_FAILURE);
        }
    }
    // end

    while (1)
    {

        if (pConfig->eProto == TCP)
        {
            int n = recv(pConfig->tcp_new_socket, buffer, BUFFER_SIZE - 1, 0);
            if (n > 0)
            {
                buffer[n] = '\0';
            }
            else if (n == 0)
            {
                break;
            }
        }

        else if (pConfig->eProto == UDP)
        {

            int m = recv(new_testfd, testbuffer, BUFFER_SIZE - 1, 0);
            if (m > 0)
            {
                testbuffer[m] = '\0';
            }
            else if (m == 0)
            {
                break;
            }

            int n = recvfrom(pConfig->udp_socket, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)&client, &client_len);
            if (n > 0)
            {
                buffer[n] = '\0';
            }
        }

        unsigned long PktSN = *((unsigned long *)buffer);

        if (PktSN > SeqNum)
        {
            PktLost += PktSN - SeqNum;
            SeqNum = PktSN;
        }

        ++SeqNum;

        gettimeofday(&RecvFinishTime, NULL);
        double TimeElapse = (double)(RecvFinishTime.tv_sec - StartTime.tv_sec) * 1000 +
                            (double)(RecvFinishTime.tv_usec - StartTime.tv_usec) / 1000;

        pConfig->eTimeElapse = TimeElapse;

        int RecvSeqNum = SeqNum - PktLost;
        double LossRate = (double)PktLost / (double)SeqNum;
        double RecvRate = (double)RecvSeqNum / (double)TimeElapse;

        pConfig->eRecvRate = RecvRate;
        pConfig->eLossRate = LossRate;
        pConfig->eLossPacket = PktLost;
        pConfig->eSeqNum = RecvSeqNum;
    }
}

void handle_server(struct Config *pConfig)
{
    struct sockaddr_in servaddr, client;
    memset(&servaddr, 0, sizeof(servaddr));
    memset(&client, 0, sizeof(client));
    int client_len = sizeof(client);

    // Socket setup
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(pConfig->rport);              // Use rport for listening
    inet_pton(AF_INET, pConfig->rhost, &servaddr.sin_addr); // Use rhost for listening

    int server_fd;
    // Create socket based on the protocol
    if ((server_fd = socket(AF_INET, (pConfig->eProto == UDP) ? SOCK_DGRAM : SOCK_STREAM, 0)) < 0)
    {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Bind the socket
    if (bind(server_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (pConfig->eProto == TCP)
    {
        // Listen for incoming TCP connections
        listen(server_fd, 5);
        printf("Server listening on %s:%d (TCP)\n", pConfig->rhost, pConfig->rport);

        while (1)
        {
            int client_fd = accept(server_fd, (struct sockaddr *)&client, &client_len);
            if (client_fd < 0)
            {
                perror("accept failed");
                continue;
            }

            // Handle incoming requests
            char buffer[BUFFER_SIZE];
            int nread = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
            if (nread > 0)
            {
                buffer[nread] = '\0';
                printf("Received request:\n%s\n", buffer);

                if (strncmp(buffer, "GET ", 4) == 0)
                {
                    char *file_path = strtok(buffer + 4, " ");
                    if (file_path)
                    {
                        char command[BUFFER_SIZE];
                        snprintf(command, sizeof(command), "curl -s %s", file_path);
                        FILE *file = popen(command, "r");

                        const char *response_header = "HTTP/1.1 200 OK\r\n"
                                                      "Content-Type: text/html\r\n"
                                                      "Connection: close\r\n\r\n";
                        send(client_fd, response_header, strlen(response_header), MSG_NOSIGNAL);

                        if (file)
                        {
                            char file_buffer[BUFFER_SIZE];
                            while (fgets(file_buffer, sizeof(file_buffer), file) != NULL)
                            {
                                send(client_fd, file_buffer, strlen(file_buffer), MSG_NOSIGNAL);
                            }
                            pclose(file);
                        }
                        else
                        {
                            const char *not_found_response = "HTTP/1.1 404 Not Found\r\n"
                                                             "Content-Type: text/plain\r\n"
                                                             "Connection: close\r\n\r\n"
                                                             "404 Not Found";
                            send(client_fd, not_found_response, strlen(not_found_response), MSG_NOSIGNAL);
                        }
                    }
                }
            }

            close(client_fd);
        }
    }
    else if (pConfig->eProto == UDP)
    {
        // Handle UDP requests (acting like a simple HTTP server)
        printf("Server listening on %s:%d (UDP)\n", pConfig->rhost, pConfig->rport);

        while (1)
        {
            char buffer[BUFFER_SIZE];
            // Receive data from clients
            int nread = recvfrom(server_fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&client, &client_len);
            if (nread > 0)
            {
                buffer[nread] = '\0'; // Null-terminate the buffer
                printf("Received UDP request:\n%s\n", buffer);

                // Check if it's a GET request
                if (strncmp(buffer, "GET ", 4) == 0)
                {
                    char *file_path = strtok(buffer + 4, " ");
                    if (file_path)
                    {
                        char command[BUFFER_SIZE];
                        snprintf(command, sizeof(command), "curl -s %s", file_path);
                        FILE *file = popen(command, "r");

                        const char *response_header = "HTTP/1.1 200 OK\r\n"
                                                      "Content-Type: text/html\r\n"
                                                      "Connection: close\r\n\r\n";
                        sendto(server_fd, response_header, strlen(response_header), 0, (struct sockaddr *)&client, client_len);

                        if (file)
                        {
                            char file_buffer[BUFFER_SIZE];
                            while (fgets(file_buffer, sizeof(file_buffer), file) != NULL)
                            {
                                sendto(server_fd, file_buffer, strlen(file_buffer), 0, (struct sockaddr *)&client, client_len);
                            }

                            // Send END_OF_FILE message
                            const char *end_of_file_msg = "END_OF_FILE\n"; // Add newline for clarity
                            sendto(server_fd, end_of_file_msg, strlen(end_of_file_msg), 0, (struct sockaddr *)&client, client_len);

                            pclose(file);
                        }
                        else
                        {
                            const char *not_found_response = "HTTP/1.1 404 Not Found\r\n"
                                                             "Content-Type: text/plain\r\n"
                                                             "Connection: close\r\n\r\n"
                                                             "404 Not Found";
                            sendto(server_fd, not_found_response, strlen(not_found_response), 0, (struct sockaddr *)&client, client_len);
                        }
                    }
                    break; // Break after handling the first GET request
                }
            }
        }
    }

    close(server_fd);
}

void *workerThread(void *arg)
{
    struct Config *clientConfig = (struct Config *)arg;

    while (1)
    {
        pthread_mutex_lock(&queueMutex);
        while (taskCount == 0)
        {
            pthread_cond_wait(&queueCond, &queueMutex);
        }

        Task task = taskQueue[--taskCount];
        activeThreads++;
        if (task.clientConfig->eProto == TCP)
        {
            TcpThreads++;
        }
        else
        {
            UdpThreads++;
        }
        pthread_mutex_unlock(&queueMutex);

        if (task.clientConfig->eMode == HTTP)
        {
            handle_server(task.clientConfig);
        }
        else if (task.clientConfig->eMode == SEND)
        {
            SendLoop(task.clientConfig);
        }
        else
        {
            RecvLoop(task.clientConfig);
        }

        close(task.clientConfig->tcp_socket);
        close(task.clientConfig->udp_socket);
        close(task.clientConfig->tcp_new_socket);
        free(task.clientConfig->data);

        pthread_mutex_lock(&queueMutex);
        activeThreads--;
        if (task.clientConfig->eProto == TCP)
        {
            TcpThreads--;
        }
        else
        {
            UdpThreads--;
        }
        pthread_mutex_unlock(&queueMutex);

        free(task.clientConfig);
    }

    return NULL;
}

void addTask(struct Config *clientConfig)
{
    pthread_mutex_lock(&queueMutex);
    taskQueue[taskCount++] = (Task){clientConfig};
    pthread_cond_signal(&queueCond);
    pthread_mutex_unlock(&queueMutex);
}

void resizePool(int newSize)
{
    pthread_mutex_lock(&queueMutex);
    threads = realloc(threads, newSize * sizeof(pthread_t));
    for (int i = poolSize; i < newSize; i++)
    {
        pthread_create(&threads[i], NULL, workerThread, NULL);
    }
    poolSize = newSize;
    pthread_mutex_unlock(&queueMutex);
}

void checkUtilization()
{
    time_t now = time(NULL);
    double utilization = (double)activeThreads / poolSize;
    printf("ActiveThreads: %d\n", activeThreads);

    if (utilization == 1)
    {
        resizePool(poolSize * 2);
        printf("ActiveThreads: %d\n", activeThreads);
        printf("Pool size has been DOUBLE\n");
    }

    if (now - lastUtilizationCheck >= 60)
    {
        if (utilization < 0.5)
        {
            resizePool(poolSize / 2);
            printf("ActiveThreads: %d\n", activeThreads);
            printf("Pool size has been HALF\n");
        }
        lastUtilizationCheck = now;
    }
}

void *checkUtilizationThread(void *arg)
{
    while (1)
    {
        checkUtilization();
        sleep(10);
    }
}

int main(int argc, char **argv)
{
    // default settings of Config
    struct Config *pConfig;
    struct Config *nConfig;

    pConfig = malloc(sizeof(struct Config));
    memset(pConfig, 0, sizeof(struct Config));

    nConfig = malloc(sizeof(struct Config));
    memset(nConfig, 0, sizeof(struct Config));

    pConfig->Stat = 500;
    pConfig->PktSize = 1000;
    pConfig->eProto = UDP;
    pConfig->PktNum = 0;
    pConfig->rhost = "127.0.0.1";
    pConfig->rport = 4180;
    pConfig->lhost = "0.0.0.0";
    pConfig->lport = 4180;
    pConfig->data = malloc(pConfig->PktSize * sizeof(char));
    memset(pConfig->data, 'A', sizeof(char) * pConfig->PktSize);
    pConfig->data[pConfig->PktSize - 1] = '\0';
    pConfig->PktRate = 1000; // bytes/second
    poolSize = 8;

    int count = 0;

    pthread_mutex_init(&queueMutex, NULL);
    pthread_cond_init(&queueCond, NULL);
    threads = malloc(poolSize * sizeof(pthread_t));

    for (int i = 0; i < poolSize; i++)
    {
        pthread_create(&threads[i], NULL, workerThread, NULL);
    }

    lastUtilizationCheck = time(NULL);
    pthread_t utilizationThread;
    pthread_create(&utilizationThread, NULL, checkUtilizationThread, NULL);

    while (1)
    {
        struct Config *clientConfig;
        clientConfig = malloc(sizeof(struct Config));
        memset(clientConfig, 0, sizeof(struct Config));

        // Receive and decode configure information
        int infofd, new_infofd;
        struct sockaddr_in servaddr, client;
        memset(&servaddr, 0, sizeof(servaddr));
        memset(&client, 0, sizeof(client));
        int client_len = sizeof(client);

        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(pConfig->lport);
        inet_pton(AF_INET, pConfig->lhost, &servaddr.sin_addr);

        if ((infofd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            perror("socket creation failed");

            exit(EXIT_FAILURE);
        }

        if (bind(infofd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
        {
            perror("bind failed");
            close(infofd);
            exit(EXIT_FAILURE);
        }

        listen(infofd, 5);

        new_infofd = accept(infofd, (struct sockaddr *)&client, &client_len);

        if (new_infofd < 0)
        {
            perror("accept failed");
            close(infofd);
            exit(EXIT_FAILURE);
        }

        printf("Receiving infomation data...\n");

        while (1)
        {
            char info[BUFFER_SIZE];
            int nread = recv(new_infofd, info, BUFFER_SIZE - 1, 0);
            if (nread > 0)
            {
                info[nread] = '\0';
            }

            printf("Received message size: %d\n", nread);

            DecodeArguments(nConfig, info, nread);

            printf("Mode: %d, Proto: %d, Pktsize: %d", nConfig->eMode, nConfig->eProto, nConfig->PktSize);
            printf(", lhost: %s, lport: %d", nConfig->lhost, nConfig->lport);
            printf(", rhost: %s, rport: %d\n", nConfig->rhost, nConfig->rport);
            break;
        }

        close(infofd);
        close(new_infofd);

        if (nConfig->eMode == 0 || nConfig->eMode == 1)
        {
            clientConfig->eMode = nConfig->eMode ^ 1;
        }
        else
        {
            clientConfig->eMode = nConfig->eMode;
        }
        clientConfig->eProto = nConfig->eProto;
        clientConfig->PktSize = nConfig->PktSize;
        clientConfig->lhost = strdup(nConfig->rhost);
        clientConfig->lport = nConfig->lport;
        clientConfig->rhost = strdup(nConfig->rhost);
        clientConfig->rport = nConfig->rport;

        clientConfig->PktRate = pConfig->PktRate;
        clientConfig->Stat = pConfig->Stat;
        clientConfig->PktNum = pConfig->PktNum;

        clientConfig->data = realloc(clientConfig->data, clientConfig->PktSize * sizeof(char));
        memset(clientConfig->data, 'A', clientConfig->PktSize);
        clientConfig->data[clientConfig->PktSize - 1] = '\0';

        clientConfig->connection_info = count++;

        // Start sending data or receiving data
        int sockfd, new_sockfd;

        if (clientConfig->eProto == TCP)
        {
            if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
            {
                perror("socket creation failed");
                exit(EXIT_FAILURE);
            }

            clientConfig->tcp_socket = sockfd;
        }
        else if (clientConfig->eProto == UDP)
        {
            if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
            {
                perror("socket creation failed");
                exit(EXIT_FAILURE);
            }

            clientConfig->udp_socket = sockfd;
        }

        if (clientConfig->eMode == SEND)
        {
            // Sending mode
            servaddr.sin_family = AF_INET;
            servaddr.sin_port = htons(clientConfig->lport);
            inet_pton(AF_INET, clientConfig->lhost, &servaddr.sin_addr);

            if (clientConfig->eProto == TCP)
            {
                while (connect(clientConfig->tcp_socket, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
                {
                    perror("connection to the server failed: try again");
                    sleep(1);
                }
            }

            addTask(clientConfig);
        }
        else if (clientConfig->eMode == RECV)
        {
            // Receiving mode
            servaddr.sin_family = AF_INET;
            servaddr.sin_port = htons(clientConfig->rport);
            inet_pton(AF_INET, clientConfig->rhost, &servaddr.sin_addr);

            if (clientConfig->eProto == TCP)
            {
                if (bind(clientConfig->tcp_socket, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
                {
                    perror("bind failed");
                    close(clientConfig->tcp_socket);
                    exit(EXIT_FAILURE);
                }

                listen(clientConfig->tcp_socket, 5);
                new_sockfd = accept(clientConfig->tcp_socket, (struct sockaddr *)&client, &client_len);

                if (new_sockfd < 0)
                {
                    perror("accept failed");
                    close(clientConfig->tcp_socket);
                    exit(EXIT_FAILURE);
                }

                clientConfig->tcp_new_socket = new_sockfd;
            }

            if (clientConfig->eProto == UDP)
            {
                if (bind(clientConfig->udp_socket, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
                {
                    perror("bind failed");
                    close(clientConfig->udp_socket);
                    exit(EXIT_FAILURE);
                }
            }

            addTask(clientConfig);
        }

        else if (clientConfig->eMode == HTTP)
        {
            addTask(clientConfig);
        }
    }

    for (int i = 0; i < poolSize; i++)
    {
        pthread_join(threads[i], NULL);
    }
    free(threads);
    pthread_mutex_destroy(&queueMutex);
    pthread_cond_destroy(&queueCond);

    return 0;
}
