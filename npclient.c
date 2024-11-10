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

    int tcp_socket;
    int tcp_new_socket;
    int udp_socket;

    int eTimeElapse;
    double eLossRate;
    int eLossPacket;
    int eSeqNum;
    double eSendRate;
    double eRecvRate;

    double eJitter;
};

void ParseArguments(struct Config *pConfig, int argc, char **argv)
{
    int c;

    while (1)
    {
        struct option long_options[] = {
            {"send", 0, (int *)&(pConfig->eMode), SEND},
            {"recv", 0, (int *)&(pConfig->eMode), RECV},

            {"proto", 1, 0, 0},

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
            if (strcmp(long_options[option_index].name, "proto") == 0)
            {
                if (strcmp(optarg, "tcp") == 0)
                {
                    pConfig->eProto = TCP;
                }
                else if (strcmp(optarg, "udp") == 0)
                {
                    pConfig->eProto = UDP;
                }
                else
                {
                    fprintf(stderr, "Unsupported protocol: %s\n", optarg);
                }
            }
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

    struct sockaddr_in client;
    memset(&client, 0, sizeof(client));

    client.sin_family = AF_INET;
    client.sin_port = htons(pConfig->rport);
    inet_pton(AF_INET, pConfig->rhost, &client.sin_addr);

    if (pConfig->PktRate != 0)
        SendDelay = (float)(pConfig->PktSize) * 1000 / pConfig->SendRate;

    gettimeofday(&StartTime, NULL);

    printf("Start sending data\n");

    int testfd;

    //create tcp testfd
    if (pConfig->eProto == UDP)
    {

        if ((testfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in testaddr;
        int testport = 10;
        testport += pConfig->rport;

        testaddr.sin_family = AF_INET;
        testaddr.sin_port = htons(testport);
        inet_pton(AF_INET, pConfig->rhost, &testaddr.sin_addr);

        while (connect(testfd, (struct sockaddr *)&testaddr, sizeof(testaddr)) < 0)
        {
            perror("connection to the server failed: try again");
            sleep(1);
        }
    }
    //end

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
    }
}

int RecvLoop(struct Config *pConfig)
{
    struct timeval StartTime, RecvFinishTime;

    struct timeval last_recv_time, recv_time;
    double mean_recv_interval = 0.0;
    double mean_jitter = 0.0;
    int num_intervals = 0;


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

    int testfd, new_testfd;

    //udp test
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
        testport += pConfig->lport;
        //use port + 10

        testaddr.sin_family = AF_INET;
        testaddr.sin_port = htons(testport);
        inet_pton(AF_INET, pConfig->lhost, &testaddr.sin_addr);

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
    //end

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
            int m = recv(new_testfd, testbuffer, BUFFER_SIZE - 1, 0);
            if (m > 0)
            {
                testbuffer[m] = '\0';
            } else if (m == 0)
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
        gettimeofday(&recv_time, NULL);

        double TimeElapse = (double)(RecvFinishTime.tv_sec - StartTime.tv_sec) * 1000 +
                            (double)(RecvFinishTime.tv_usec - StartTime.tv_usec) / 1000;
        
        if (num_intervals > 0)
        {
            double recv_interval = (double)(recv_time.tv_sec - last_recv_time.tv_sec) * 1000 +
                                   (double)(recv_time.tv_usec - last_recv_time.tv_usec) / 1000;

            double jitter = recv_interval - mean_recv_interval;
            if (jitter < 0) jitter = -jitter;
            mean_jitter = (mean_jitter * num_intervals + jitter) / (num_intervals + 1);

            mean_recv_interval = (mean_recv_interval * num_intervals + recv_interval) / (num_intervals + 1);
        }

        pConfig->eJitter = mean_jitter;
        last_recv_time = recv_time;
        num_intervals++;

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
        printf("Elapsed [%dms] Pkts [%d] Lost [%d, %f%%] Rate [%fMbps] Jitter [%fms]\n", pConfig->eTimeElapse, pConfig->eSeqNum, pConfig->eLossPacket, pConfig->eLossRate, pConfig->eRecvRate, pConfig->eJitter);
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

int EncodeArguments(struct Config *pConfig, char *Buf, int iBufSize)
{

    int idx = 0;
    *((unsigned short *)(Buf + idx)) = htons(pConfig->eMode);
    idx += 2; // Requested mode of operation
    *((unsigned short *)(Buf + idx)) = htons(pConfig->eProto);
    idx += 2; // Protocol to be used
    *((unsigned long *)(Buf + idx)) = htonl(pConfig->PktSize);
    idx += 4; // Packet size

    int iLen = strlen(pConfig->lhost) + 1;
    *((unsigned short *)(Buf + idx)) = htons(iLen);
    idx += 2; // Length of client hostname
    memcpy(Buf + idx, pConfig->lhost, iLen);
    idx += iLen; // Client hostname

    *((unsigned short *)(Buf + idx)) = htons(pConfig->lport);
    idx += 2; // Client portnum

    int rLen = strlen(pConfig->rhost) + 1;
    *((unsigned short *)(Buf + idx)) = htons(rLen);
    idx += 2; // Length of server hostname
    memcpy(Buf + idx, pConfig->rhost, rLen);
    idx += rLen; // Server hostname

    *((unsigned short *)(Buf + idx)) = htons(pConfig->rport);
    idx += 2; // Server portnum

    return idx;
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
    pConfig->PktNum = 0;
    pConfig->eProto = UDP;
    pConfig->rhost = "127.0.0.1";
    pConfig->rport = 4180;
    pConfig->lhost = "0.0.0.0";
    pConfig->lport = 4180;
    pConfig->data = malloc(pConfig->PktSize * sizeof(char));
    memset(pConfig->data, 'A', sizeof(char) * pConfig->PktSize);
    pConfig->data[pConfig->PktSize - 1] = '\0';
    pConfig->PktRate = 1000; // bytes/second

    nConfig->rhost = "127.0.0.1";
    nConfig->rport = 4180;

    ParseArguments(pConfig, argc, argv);

    int infofd, sockfd, new_sockfd;
    struct sockaddr_in servaddr, client;
    memset(&servaddr, 0, sizeof(servaddr));
    memset(&client, 0, sizeof(client));
    int client_len = sizeof(client);

    // Encode configure information and send

    char info[BUFFER_SIZE];
    int iArgSize = EncodeArguments(pConfig, info, BUFFER_SIZE);

    if ((infofd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket creation failed");

        exit(EXIT_FAILURE);
    }

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(nConfig->rport);
    inet_pton(AF_INET, nConfig->rhost, &servaddr.sin_addr);

    if (connect(infofd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("connection to the server failed");

        exit(EXIT_FAILURE);
    }

    send(infofd, info, iArgSize, 0);
    printf("Configure information has been encoded and sent\n");
    close(infofd);

    // End configure information sending

    if (pConfig->eProto == TCP)
    {
        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            perror("socket creation failed");

            exit(EXIT_FAILURE);
        }

        pConfig->tcp_socket = sockfd;
    }

    else if (pConfig->eProto == UDP)
    {
        if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        {
            perror("socket creation failed");

            exit(EXIT_FAILURE);
        }

        pConfig->udp_socket = sockfd;
    }

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
            if (connect(pConfig->tcp_socket, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
            {
                perror("connection to the server failed");

                exit(EXIT_FAILURE);
            }
        }

        SendLoop(pConfig);
    }

    else if (pConfig->eMode == RECV)
    {
        // receiving mode

        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(pConfig->lport);
        inet_pton(AF_INET, pConfig->lhost, &servaddr.sin_addr);

        client.sin_family = AF_INET;
        client.sin_port = htons(pConfig->lport);
        inet_pton(AF_INET, pConfig->lhost, &client.sin_addr);

        if (pConfig->eProto == TCP)
        {
            if (bind(pConfig->tcp_socket, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
            {
                perror("bind failed");
                close(pConfig->tcp_socket);
                exit(EXIT_FAILURE);
            }

            listen(pConfig->tcp_socket, 5);

            new_sockfd = accept(pConfig->tcp_socket, (struct sockaddr *)&client, &client_len);

            if (new_sockfd < 0)
            {
                perror("accept failed");
                close(pConfig->tcp_socket);
                exit(EXIT_FAILURE);
            }

            pConfig->tcp_new_socket = new_sockfd;
        }

        if (pConfig->eProto == UDP)
        {
            if (bind(pConfig->udp_socket, (struct sockaddr *)&client, sizeof(client)) < 0)
            {
                perror("bind failed");
                close(pConfig->udp_socket);
                exit(EXIT_FAILURE);
            }
        }

        RecvLoop(pConfig);
    }

    return 0;
}