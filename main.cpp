#include "List.h"

#include <SFML/Network.hpp>
#include <iostream>
#include <cstdlib>
#include <cstring>

#define DEFAULT_PORT 63000
#define DEFAULT_BUFFER_SIZE 8192
#define DEFAULT_QUIET 0
#define DEFAULT_DEBUG 0
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

int getNextArgValue(int argc, const char * argv[], int pos)
{
    if(argc-pos-1 == 0) // no argument next
    {
        printf("Bad argument count: \"%s\" requires 1 arguments !\n",argv[pos]);
        exit(1);
    }
    
    int value = strtol(argv[pos+1],NULL,10);
                
    if(value == 0) // 0 = bad value OR 0 = convertion error
    {
        printf("Bad argument value: \"%s\" requires a non 0 value !\n",argv[pos]);
        exit(1);                
    }
    
    return value;
}
void checkArgCount(int count, const char * arg, int requiered)
{

}

void helpArgs()
{
    printf("USAGE: calculib_server [ARGS]\n");
    printf(" -p, --port    Select the port to be used                                Default: %d\n",DEFAULT_PORT);
    printf(" -b, --buffer Size of the exchange buffer                             Default: %d\n",DEFAULT_BUFFER_SIZE);
    printf(" -q, --quiet   Prints only errors                                              Default: %d\n",DEFAULT_QUIET);
    printf(" -d, --debug  Prints debug info                                             Default: %d\n",DEFAULT_DEBUG);
    printf(" -m, --max   Maximum number of simultaneous client         Default: %d\n",DEFAULT_MAXCLIENT);
    printf(" -h, --help    Prints this help\n");
}

void readArgs(int argc, const char * argv[], int * port, int * bufferSize, bool * isQuiet, bool * isDebug, int * maxClient)
{
    for(int i = 1 ; i < argc ; i++)
    {
        if(strcmp(argv[i],"--port") == 0 || strcmp(argv[i],"-p") == 0 )
        {
            *port = getNextArgValue(argc,argv,i);
            i++;
        }
        else if(strcmp(argv[i],"--buffer") == 0 || strcmp(argv[i],"-b") == 0 )
        {
            *bufferSize = getNextArgValue(argc,argv,i);
            i++;
        }
        else if(strcmp(argv[i],"--quiet") == 0 || strcmp(argv[i],"-q") == 0 )
        {
            *isQuiet = true;
        }
        else if(strcmp(argv[i],"--debug") == 0 || strcmp(argv[i],"-d") == 0 )
        {
            *isDebug = true;
        }
        else if(strcmp(argv[i],"--max")  == 0 || strcmp(argv[i],"-m") == 0 )
        {
            *maxClient = getNextArgValue(argc,argv,i);
            i++;        
        }
        else
        {
            if(! (strcmp(argv[i],"--help") == 0 || strcmp(argv[i],"-h") == 0 ) )
            {
                printf("Invalid argument ! \"%s\"\n",argv[i]);
            }
            helpArgs();
        }
    }
}



void printInfo(int port, int bufferSize, bool isDebug, int maxClient)
{
    std::cout << "Calculib server V2.0 from the NESSCASDK project" << std::endl;
    std::cout << "By Nessotrin for the Casio community, 2016" << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "Buffer size: " << bufferSize << "o" << std::endl;
    std::cout << "Debug mode: "  << (isDebug?"yes":"no") << std::endl;
    std::cout << "Max client: " << maxClient << std::endl;
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

    logger.printLog("Client sucessfully connected !");
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

    if(result == sf::Socket::Done)
    {
        logger.printLog("Client requesting connection ...");
        return 1;
    }
    
    if(result == sf::Socket::Error)
    {
        logger.printError("LISTENER ACCEPT ERROR !");
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

void answerToClient(AnswerDataSet data) //AnswerDataSet = List<Client*> * list, sf::Mutex * listMutex, int port
{
    sf::TcpListener listener;
    setupListener(&listener, data.port);
    
    setupAcceptSocket();

    while(1)
    {
        data.listMutex->lock();
        bool isFull = (data.list->getSize() == data.maxClient);
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
            while(listenerAccept(&listener,acceptSocket))
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
                isFull = (data.list->getSize() == data.maxClient);
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
            if(sendBuffer(list->get(i)->socket,message))
            {
                logger.printLog("Client disconnected !");
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
                logger.printLog("Client disconnected !");
                killClient(list,i);
            }
            if(workExchangeBuffer.size > 0)
            {
                logger.printDebug("Got data, dispatching");
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
    bool isDebug = DEFAULT_DEBUG;
    int maxClient = DEFAULT_MAXCLIENT;

    readArgs(argc,argv,&port,&bufferSize,&isQuiet,&isDebug,&maxClient);

    logger.setQuiet(isQuiet);
    logger.setDebug(isDebug);
    
    if(!isQuiet)
    {
        printInfo(port,bufferSize,isDebug,maxClient);
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
