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
    // struct sockaddr_in newaddr;
    // socklen_t newaddr_len;

    int socket;
    int new_socket;

    int eTimeElapse;
    double eLossRate;
    int eLossPacket;
    int eSeqNum;
    double eSendRate;
    double eRecvRate;
};

void ParseArguments(struct Config *pConfig, int argc, char **argv)
{
    int c;

    while (1)
    {
        struct option long_options[] = {
            {"send", 0, (int *)&(pConfig->eMode), SEND},
            {"recv", 0, (int *)&(pConfig->eMode), RECV},
            {"proto tcp", 0, (int *)&(pConfig->eProto), TCP},
            {"proto udp", 0, (int *)&(pConfig->eProto), UDP},

            {"stat", 1, 0, 'a'},
            {"rhost", 1, 0, 'b'},
            {"rport", 1, 0, 'c'},
            {"lhost", 1, 0, 'd'},
            {"lport", 1, 0, 'e'},

            {"pktsize", 1, 0, 'f'},
            {"pktrate", 1, 0, 'g'},
            {"pktnum", 1, 0, 'h'},

            {"sbufsize", 1, 0, 'i'},
            {"rbufsize", 1, 0, 'j'},

        };

        int option_index = 0;

        c = getopt_long_only(argc, argv, "",
                             long_options, &option_index);

        if (c == -1)
            break;

        switch (c)
        {
        case 0:

            break;
        case 'a':
            printf("option -stat with value `%s'\n", optarg);
            pConfig->Stat = atoi(optarg);
            break;
        case 'b':
            printf("option -rhost with value `%s'\n", optarg);
            pConfig->rhost = strdup(optarg);
            break;
        case 'c':
            printf("option -rport with value `%s'\n", optarg);
            pConfig->rport = atoi(optarg);
            break;
        case 'd':
            printf("option -lhost with value `%s'\n", optarg);
            pConfig->lhost = strdup(optarg);
            break;
        case 'e':
            printf("option -lport with value `%s'\n", optarg);
            pConfig->lport = atoi(optarg);
            break;
        case 'f':
            printf("option -pktsize with value `%s'\n", optarg);
            pConfig->PktSize = atoi(optarg);
            break;
        case 'g':
            printf("option -pktrate with value `%s'\n", optarg);
            pConfig->PktRate = atoi(optarg);
            break;
        case 'h':
            printf("option -pktnum with value `%s'\n", optarg);
            pConfig->PktNum = atoi(optarg);
            break;
        case 'i':
            printf("option -sbufsize with value `%s'\n", optarg);
            pConfig->sbufsize = atoi(optarg);
            break;
        case 'j':
            printf("option -rbufsize with value `%s'\n", optarg);
            pConfig->rbufsize = atoi(optarg);
            break;

        default:
            printf("unsupported option `%s'\n", argv[optind]);
            break;
        }
    }
}

int SendLoop(struct Config *pConfig)
{
    struct timeval StartTime, SendFinishTime, TimeForDelay;
    unsigned long SeqNum = 0;
    double SendDelay = 0;                 // in milliseconds
    pConfig->SendRate = pConfig->PktRate; // bytes/second

    if (pConfig->PktRate != 0)
        SendDelay = (float)(pConfig->PktSize) * 1000 / pConfig->SendRate;

    gettimeofday(&StartTime, NULL);

    printf("Start sending data\n");

    while (1)
    {
        pConfig->data = (char *)malloc(pConfig->PktSize);
        *((unsigned long *)pConfig->data) = SeqNum;

        gettimeofday(&TimeForDelay , NULL);

        if (pConfig->eProto == TCP)
        {
            send(pConfig->socket, pConfig->data, strlen(pConfig->data), 0);
        }
        /*else if (pConfig->eProto == UDP)
        {
            sendto(pConfig->socket, data, strlen(data), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
        }*/

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
    }
}

int RecvLoop(struct Config *pConfig)
{
    struct timeval StartTime, RecvFinishTime;

    unsigned long SeqNum = 0;
    int PktLost = 0;
    char buffer[BUFFER_SIZE];

    gettimeofday(&StartTime, NULL);

    printf("Receiving data\n");

    while (1)
    {

        if (pConfig->eProto == TCP)
        {
            int n = recv(pConfig->new_socket, buffer, BUFFER_SIZE - 1, 0);
            if (n > 0)
            {
                buffer[n] = '\0';
            }
            else
            {
                break;
            }
        }

        /*else if (pConfig->eProto == UDP)
        {
            recvfrom(pConfig->socket, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)&pConfig->newaddr, &pConfig->newaddr_len);
        }*/

        unsigned long PktSN = *((unsigned long *) buffer);

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

void displayStatistics(struct Config *pConfig)
{
    if (pConfig->eMode == SEND)
    {
        printf("Elapsed [%dms] Rate [%fMbps]\n", pConfig->eTimeElapse, pConfig->eSendRate);
    }
    else if (pConfig->eMode == RECV)
    {
        printf("Elapsed [%dms] Pkts [%d] Lost [%d, %f%%] Rate [%fMbps]\n", pConfig->eTimeElapse, pConfig->eSeqNum, pConfig->eLossPacket, pConfig->eLossRate, pConfig->eRecvRate);
    }
}

void *displayThreadFunc(void *arg)
{
    struct Config *pConfig = (struct Config *)arg;
    while (1)
    {
        displayStatistics(pConfig);
        usleep(pConfig->Stat * 1000);
    }
    return NULL;
}

int main(int argc, char **argv)
{

    // default settings of Config
    struct Config *pConfig;
    pConfig = malloc(sizeof(struct Config));
    memset(pConfig, 0, sizeof(struct Config));
    pConfig->Stat = 500;
    pConfig->PktSize = 1000;
    pConfig->eProto = TCP;
    pConfig->PktNum = 0;
    pConfig->rhost = "127.0.0.1";
    pConfig->rport = 4180;
    pConfig->lhost = "0.0.0.0";
    pConfig->lport = 4180;
    pConfig->data = malloc(BUFFER_SIZE * sizeof(char));
    memset(pConfig->data, 'A', sizeof(char) * 1000);
    pConfig->data[1000] = '\0';
    pConfig->PktRate = 1000; // bytes/second

    ParseArguments(pConfig, argc, argv);

    int sockfd, new_sockfd;
    struct sockaddr_in servaddr, new_servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    memset(&new_servaddr, 0, sizeof(new_servaddr));
    socklen_t addr_len = sizeof(new_servaddr);

    // pConfig->newaddr = new_servaddr;
    // pConfig->newaddr_len = addr_len;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket creation failed");

        exit(EXIT_FAILURE);
    }

    pConfig->socket = sockfd;

    pthread_t displayThread;

    if (pthread_create(&displayThread, NULL, displayThreadFunc, (void *)pConfig) != 0)
    {
        perror("Failed to create display thread");
        return EXIT_FAILURE;
    }

    if (pConfig->eMode == SEND)
    {
        // sending mode

        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(pConfig->rport);
        inet_pton(AF_INET, pConfig->rhost, &servaddr.sin_addr);

        if (pConfig->eProto == TCP)
        {
            if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
            {
                perror("connection to the server failed");

                exit(EXIT_FAILURE);
            }
        }

        SendLoop(pConfig);

        close(sockfd);
    }

    else if (pConfig->eMode == RECV)
    {
        // receiving mode

        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(pConfig->lport);
        inet_pton(AF_INET, pConfig->lhost, &servaddr.sin_addr);

        if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
        {
            perror("bind failed");
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        if (pConfig->eProto == TCP)
        {
            listen(sockfd, 5);

            new_sockfd = accept(sockfd, (struct sockaddr *)&new_servaddr, &addr_len);

            if (new_sockfd < 0)
            {
                perror("accept failed");
                close(sockfd);
                exit(EXIT_FAILURE);
            }

            pConfig->new_socket = new_sockfd;
        }

        RecvLoop(pConfig);

        close(sockfd);
        close(new_sockfd);
    }

    return 0;
}