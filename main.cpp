#include "List.h"

#include <SFML/Network.hpp>
#include <iostream>
#include <cstdlib>
#include <cstring>

#define DEFAULT_PORT 63000
#define DEFAULT_BUFFER_SIZE 8192
#define DEFAULT_QUIET 0
#define DEFAULT_MAXCLIENT 2


struct Buffer{
    unsigned char * pointer;
    int size;
};

class Client{
public: 
    sf::TcpSocket * socket;
    bool isReady = false;
};

class AnswerDataSet //only one argument per thread, packs everything for the answerClient thread
{
public:
    AnswerDataSet(List<Client*>* newList, sf::Mutex * newListMutex, int newPort, int newMaxClient)
    {
        list = newList;
        listMutex = newListMutex;
        port = newPort;
        maxClient = newMaxClient;
    }
    
    List<Client*> * list;
    sf::Mutex * listMutex;
    int port;
    int maxClient;
};
    

class ConnectDataSet //only one argument per thread, packs everything for the ConnectClient thread
{
public:
    ConnectDataSet(List<Client*>* newList, sf::Mutex * newListMutex, int newClientId)
    {
        list = newList;
        listMutex = newListMutex;
        clientId = newClientId;
    }
    
    List<Client*> * list;
    sf::Mutex * listMutex;
    int clientId;
    
};

class Logger{
private:
    bool beQuiet = false;
    bool debugLog = false;
public:
    void printLog(const char * text)
    {
        if(!beQuiet || debugLog)
        {
            printf("%s\n",text);
        }
    }
    void printError(const char * text)
    {
        printf("ERROR: %s\n",text);
    }
    void printDebug(const char * text)
    {
        if(debugLog)
        {
            printf("DEBUG: %s\n",text);
        }
    }
    void setQuiet(bool newQuiet)
    {
        beQuiet = newQuiet;
    }
    void setDebug(bool newDebugLog)
    {
        debugLog = newDebugLog;
    }
};

static Logger logger;  //global logger


void checkArgCount(int count, const char * arg, int requiered)
{
    if(count < requiered)
    {
        printf("Bad argument: \"%s\" requires %d arguments (%d provided) !\n",arg,requiered, count);
        exit(1);
    }
}

void helpArgs()
{
    printf("USAGE: calculib_server [ARGS]\n");
    printf(" -p, --port    Select the port to be used                              Default: %d\n",DEFAULT_PORT);
    printf(" -b, --buffer Size of the exchange buffer                             Default: %d\n",DEFAULT_BUFFER_SIZE);
    printf(" -q, --quiet   Prints only errors                                           Default: %d\n",DEFAULT_QUIET);
    printf(" -m, --max   Maximum number of simultaneous client         Default: %d\n",DEFAULT_MAXCLIENT);
    printf(" -h, --help    Prints this help\n");
}

void readArgs(int argc, const char * argv[], int * port, int * bufferSize, bool * quiet, int * maxClient)
{
    for(int i = 1 ; i < argc ; i++)
    {
        if(strcmp(argv[i],"--port") || strcmp(argv[i],"-p"))
        {
            checkArgCount(i-argc-1, argv[i], 1);
            
            *port = strtol(argv[i+1],NULL,10);
        }
        else if(strcmp(argv[i],"--buffer") || strcmp(argv[i],"-b"))
        {
            checkArgCount(i-argc-1, argv[i], 1);
     
            *bufferSize = strtol(argv[i+1],NULL,10);
        }
        else if(strcmp(argv[i],"--quiet") || strcmp(argv[i],"-q"))
        {
            *quiet = true;
        }
        else if(strcmp(argv[i],"--max") || strcmp(argv[i],"-m"))
        {
            checkArgCount(i-argc-1, argv[i], 1);
     
            *maxClient = strtol(argv[i+1],NULL,10);
        }
        else
        {
            if(! (strcmp(argv[i],"--help") || strcmp(argv[i],"-h")) )
            {
                printf("Invalid argument ! \"%s\"\n",argv[i]);
            }
            helpArgs();
        }
    }
}



void printInfo(int port, int BufferSize)
{
    std::cout << "Calculib server V2.0 from the NESSCASDK project" << std::endl;
    std::cout << "By Nessotrin for the Casio community, 2016" << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "Buffer size: " << BufferSize << "o" << std::endl;
}


bool sendBuffer(sf::TcpSocket * socket, Buffer * toSend)
{
    std::size_t totalSent = 0;
    
    int workSize = toSend->size;
    unsigned char * workPointer = toSend->pointer;
    
    while(workSize > 0)
    {
        sf::Socket::Status result = socket->send(workPointer, workSize, totalSent);
        if(result == sf::Socket::Error || result ==  sf::Socket::Disconnected)
        {
            return true;
        }
        if(result == sf::Socket::Done)
        {
            workSize -= totalSent;
            workPointer += totalSent;
        }
    }
    return false;    
}

//1 = critical error
bool receiveBuffer(sf::TcpSocket * socket, Buffer * toFill, int max)
{
    std::size_t received;
    //printf("SOCKET- %lu\n",(unsigned long)socket);
    sf::Socket::Status result = socket->receive(toFill->pointer, max, received);
    toFill->size = received;
    
    if(result == sf::Socket::Error || result == sf::Socket::Disconnected)
    {
        if(result == sf::Socket::Disconnected)
        printf("ERROR: %d\n",result);
        return true;
    }

    return false;
}

bool receiveBufferWithTimeout(sf::TcpSocket * socket, Buffer * toFill, int max, int timeout)
{
    toFill->size = 0;
    sf::Clock timer;
    while(timer.getElapsedTime().asMilliseconds() < timeout)
    {
        if(receiveBuffer(socket,toFill,max))
        {
            return true;
        }
        
        if(toFill->size > 0)
        {
            break;
        }
        
        sf::sleep(sf::milliseconds(10));
    }
    return false;
}


void sendFullMessage(sf::TcpSocket * socket)
{
    Buffer toSend;
    toSend.pointer = (unsigned char *) "CALCULIB2_SERVER_FULL";
    toSend.size = 21;
    sendBuffer(socket, &toSend);
    logger.printLog("Full: Refused a Client !");
}

void killClient(List<Client*> * list, int id)
{
    logger.printLog("Client disconnected !");
    printf("ID %d\n",id);
    
    list->get(id)->socket->disconnect();
    delete(list->get(id)->socket);
    
    delete(list->get(id));
    list->remove(id);
}

void killClientMutexed(List<Client*> * list, sf::Mutex * listMutex, int id)
{
    listMutex->lock();
    killClient(list,id);
    listMutex->unlock();
}

//1 = error
void connectProtocol(ConnectDataSet data)// data: List<Client *> list ; sf:Mutex listMutex ; int id
{
    unsigned char inputData[17];
    Buffer inputBuffer;
    inputBuffer.pointer = inputData;
    
    logger.printDebug("Starting protocol connect");
    data.listMutex->lock();
    sf::TcpSocket * socket = data.list->get(data.clientId)->socket;
    data.listMutex->unlock();
    
    bool failed = false;
    
    if(receiveBufferWithTimeout(socket,&inputBuffer,17,500))
    {
        logger.printDebug("Protocol abort, receive error");
        killClientMutexed(data.list, data.listMutex, data.clientId);
        return;
    }
    
    if(inputBuffer.size == 17 && memcmp("CALCULIB2_CONNECT",inputBuffer.pointer,17) == 0)
    {
        logger.printDebug("Received connection marker");
        Buffer toSendBuffer;
        toSendBuffer.size = 18;
        toSendBuffer.pointer = (unsigned char *) "CALCULIB2_ACCEPTED";
        if(sendBuffer(socket,&toSendBuffer))
        {
            logger.printDebug("Protocol abort, send error");
            killClientMutexed(data.list, data.listMutex, data.clientId);
            return;
        }
    }
    else
    {
        logger.printLog("Unknown connection refused !");
        killClientMutexed(data.list, data.listMutex, data.clientId);
        return;
    }

    data.listMutex->lock();
    data.list->get(data.clientId)->isReady = true;
    data.listMutex->unlock();

    //KILLs THE THREAD
}

void setupListener(sf::TcpListener * listener, int port)
{
    // bind the listener to a port
    if (listener->listen(port) != sf::Socket::Done)
    {
        logger.printError("Could not bind to the port !");
        logger.printError("Tip: Change the port or wait a couple seconds");
        exit(1);
    }
    listener->setBlocking(false);
}

//return 1 if accepted
bool listenerAccept(sf::TcpListener * listener, sf::TcpSocket * socket)
{
    
    int result = listener->accept(*socket);
    if(result == sf::Socket::Error)
    {
        logger.printError("LISTENER ACCEPT ERROR !");
    }
    else if(result == sf::Socket::Done)
    {
        logger.printLog("Client requesting connection ...");
        
        std::size_t read;
        if(socket->receive(NULL, 0, read) == sf::Socket::Disconnected)
        {
            logger.printLog("Fast disconnected !");
            return 0;
        }
        
        return 1;
    }
    
    return 0;
}




sf::Thread * acceptThread;

sf::TcpSocket * acceptSocket; // don't recreate it every time

void setupAcceptSocket()
{
    acceptSocket = new sf::TcpSocket;
    if(acceptSocket == NULL)
    {
        printf("ERROR: can't alloc new socket !\n");
        exit(1);
    }
    acceptSocket->setBlocking(false);
}

#define MAX_CLIENT 2

void answerToClient(AnswerDataSet data) //AnswerDataSet = List<Client*> * list, sf::Mutex * listMutex, int port
{
    sf::TcpListener listener;
    setupListener(&listener, data.port);
    
    setupAcceptSocket();

    while(1)
    {
        data.listMutex->lock();
        bool isFull = (data.list->getSize() == MAX_CLIENT);
        data.listMutex->unlock();
        if(isFull)
        {
            while(listenerAccept(&listener,acceptSocket))
            {
                logger.printDebug("Refusing");
                sendFullMessage(acceptSocket);
                acceptSocket->disconnect();
            }
        }
        else
        {
            while(data.list->getSize() < MAX_CLIENT && listenerAccept(&listener,acceptSocket))
            {
                //create the new Client
                Client * newClient = new Client;
                if(newClient == NULL)
                {
                    printf("Failed to allocate Client\n");
                    exit(1);
                }
                newClient->socket = acceptSocket;
                data.listMutex->lock();
                int ClientId = data.list->add(newClient);
                data.listMutex->unlock();
                
                //create and launch a protocol connect thread
                sf::Thread * newConnectThread = new sf::Thread(&connectProtocol,ConnectDataSet(data.list,data.listMutex,ClientId));
                if(newConnectThread == NULL)
                {
                    printf("Failed to allocate thread\n");
                    exit(1);                    
                }
                newConnectThread->launch();

                setupAcceptSocket(); //reset for the next Client
                

                data.listMutex->lock();
                isFull = (data.list->getSize() == MAX_CLIENT);
                data.listMutex->unlock();
                if(isFull) // stops accepting if full
                {
                    break;
                }
            }
            
        }    
        
        sf::sleep(sf::milliseconds(10));
    }
}


void dispatchMessage(List<Client*> * list, Buffer * message, int senderId)
{
    for(int i = 0 ; i < list->getSize() ; i++)
    {
        if(i != senderId)
        {
            logger.printDebug("dispatching ...");
            printf("To %d\n",i);
            if(sendBuffer(list->get(i)->socket,message))
            {
                killClient(list,i);
            }
        }
    }
}

void checkForInputs(List<Client*> * list, sf::Mutex * listMutex, Buffer * exchangeBuffer)
{
    Buffer workExchangeBuffer; // use a copy to keep the size
    workExchangeBuffer.pointer = exchangeBuffer->pointer;
    
    listMutex->lock();
    for(int i = 0 ; i < list->getSize() ; i++)
    {
        if(list->get(i)->isReady)
        {
            if(receiveBuffer(list->get(i)->socket,&workExchangeBuffer,exchangeBuffer->size))
            {
                killClient(list,i);
            }
            if(workExchangeBuffer.size > 0)
            {
                logger.printDebug("Got data, dispatching");
                printf("From %d\n",i);
                dispatchMessage(list,&workExchangeBuffer,i);                
            }
        }
    }
    listMutex->unlock();
}


void setupExchangeBuffer(Buffer * exchangeBuffer, int bufferSize)
{
    exchangeBuffer->pointer = (unsigned char *) malloc(bufferSize);
    if(exchangeBuffer->pointer == NULL)
    {
        printf("ERROR, can't alloc the exchange Buffer !\n");
        exit(1);
    }
    exchangeBuffer->size = bufferSize;
}


int main (int argc, const char * argv[]) 
{
    int port = DEFAULT_PORT;    
    int bufferSize = DEFAULT_BUFFER_SIZE;
    bool isQuiet = DEFAULT_QUIET;
    int maxClient = DEFAULT_MAXCLIENT;

    readArgs(argc,argv,&port,&bufferSize,&isQuiet,&maxClient);

    logger.setQuiet(isQuiet);
    logger.setDebug(true);
    
    if(!isQuiet)
    {
        printInfo(port,bufferSize);
    }
    
    List<Client*> list;
    sf::Mutex listMutex;
    
    Buffer exchangeBuffer;
    setupExchangeBuffer(&exchangeBuffer, bufferSize);

    logger.printLog("Init'ed ...");
    
    sf::Thread answerThread(&answerToClient,AnswerDataSet(&list,&listMutex,port,maxClient));
    answerThread.launch();
    
    while(1) //TODO receive system interrupts
    {
        checkForInputs(&list, &listMutex,&exchangeBuffer);
        sf::sleep(sf::milliseconds(5));
    }
    
    return 0;
}
