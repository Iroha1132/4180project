#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>

#define BUFFER_SIZE 5000

enum Mode
{
    SEND,
    RECV
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
};

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

    while (1)
    {
        pConfig->data = (char *)malloc(pConfig->PktSize);
        *((unsigned long *)pConfig->data) = SeqNum;

        gettimeofday(&TimeForDelay, NULL);

        if (pConfig->eProto == TCP)
        {
            send(pConfig->tcp_socket, pConfig->data, pConfig->PktSize, 0);
        }

        else if (pConfig->eProto == UDP)
        {
            sendto(pConfig->udp_socket, pConfig->data, pConfig->PktSize, 0, (struct sockaddr *)&client, sizeof(client));
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

    struct sockaddr_in servaddr, client;
    memset(&servaddr, 0, sizeof(servaddr));
    memset(&client, 0, sizeof(client));

    int client_len = sizeof(client);

    gettimeofday(&StartTime, NULL);

    printf("Receiving data\n");

    int current_connection = pConfig->connection_info++;
    printf("Connected to %s port %d [%d, %d]", pConfig->rhost, pConfig->rport, current_connection, pConfig->connection_info++);
    printf(" %s, %s, %.2d Bps\n", modeToString(pConfig->eMode), protoToString(pConfig->eProto), pConfig->PktRate);

    while (1)
    {

        if (pConfig->eProto == TCP)
        {
            int n = recv(pConfig->tcp_new_socket, buffer, BUFFER_SIZE - 1, 0);
            if (n > 0)
            {
                buffer[n] = '\0';
            }
        }

        else if (pConfig->eProto == UDP)
        {
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

void *clientHandler(void *arg)
{
    struct Config *clientConfig = (struct Config *)arg;

    if (clientConfig->eMode == SEND)
    {
        SendLoop(clientConfig);
    }
    else if (clientConfig->eMode == RECV)
    {
        RecvLoop(clientConfig);
    }

    close(clientConfig->tcp_socket);
    close(clientConfig->udp_socket);
    close(clientConfig->tcp_new_socket);

    free(clientConfig->data);
    free(clientConfig);
    return NULL;
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

    int count = 0;

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

        clientConfig->eMode = nConfig->eMode ^ 1;
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
                if (connect(clientConfig->tcp_socket, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
                {
                    perror("connection to the server failed");
                    exit(EXIT_FAILURE);
                }
            }

            pthread_t thread_id;
            if (pthread_create(&thread_id, NULL, clientHandler, (void *)clientConfig) != 0)
            {
                perror("Failed to create thread");
                free(clientConfig);
            }

            pthread_detach(thread_id);

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
                if (bind(clientConfig->udp_socket, (struct sockaddr *)&client, sizeof(client)) < 0)
                {
                    perror("bind failed");
                    close(clientConfig->udp_socket);
                    exit(EXIT_FAILURE);
                }
            }

            pthread_t thread_id;
            if (pthread_create(&thread_id, NULL, clientHandler, (void *)clientConfig) != 0)
            {
                perror("Failed to create thread");
                free(clientConfig);
            }

            pthread_detach(thread_id);
        }
    }

    return 0;
}
