#include "List.h"

#include <SFML/Network.hpp>
#include <iostream>
#include <cstdlib>
#include <cstring>

#define DEFAULT_PORT 63000
#define DEFAULT_BUFFER_SIZE 8192
#define DEFAULT_QUIET 0



struct buffer{
    unsigned char * pointer;
    int size;
};

struct client{
    sf::TcpSocket * socket;
};

class ClientList{

private:
    List<client*> clients;
    int maxClientCount;
public:
    
    bool setMaxClient(int newMaxClientCount)
    {
        maxClientCount = maxClientCount;
    }
    
    bool isFull()
    {
        return clients.getSize() == maxClientCount;
    }
    int getConnectedCount()
    {
        clients.getSize();
    }
    
    
    void addClient(client * newClient)
    {
        clients.add(newClient);
    }
    
    void removeClient(int id)
    {
        clients.remove(id);
    }
    
    
    client * getClient(int id)
    {
        return clients.get(id);
    }
    
    int getMaxClientCount()
    {
        return maxClientCount;
    }


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
    printf(" -p, --port    Select the port to be used                   Default: %d\n",DEFAULT_PORT);
    printf(" -b, --buffer  Set the maximum size of the exchange buffer  Default: %d\n",DEFAULT_BUFFER_SIZE);
    printf(" -q, --quiet   Prints only errors                           Default: %d\n",DEFAULT_QUIET);
    printf(" -h, --help    Prints this help\n");
}

void readArgs(int argc, const char * argv[], int * port, int * bufferSize, bool * quiet)
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



void printInfo(int port, int bufferSize)
{
    std::cout << "Calculib server V2.0 from the NESSCASDK project" << std::endl;
    std::cout << "By Nessotrin for the Casio community, 2016" << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "Buffer size: " << bufferSize << "o" << std::endl;
}


bool sendBuffer(sf::TcpSocket * socket, buffer * toSend)
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
bool receiveBuffer(sf::TcpSocket * socket, buffer * toFill, int max)
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

bool receiveBufferWithTimeout(sf::TcpSocket * socket, buffer * toFill, int max, int timeout)
{
    toFill->size = 0;
    while(timeout > 0)
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
        timeout -= 10;
    }
    return false;
}


void sendFullMessage(sf::TcpSocket * socket)
{
    buffer toSend;
    toSend.pointer = (unsigned char *) "CALCULIB2_SERVER_FULL";
    toSend.size = 21;
    sendBuffer(socket, &toSend);
    logger.printLog("Full: Refused a client !");
}
//1 = error
bool connectProtocol(sf::TcpSocket * socket)
{
    unsigned char data[17];
    buffer inputBuffer;
    inputBuffer.pointer = data;
    
    logger.printDebug("Starting protocol connect");
    
    if(receiveBufferWithTimeout(socket,&inputBuffer,17,5000))
    {
        logger.printDebug("Protocol abort, receive error");
        return 1;
    }
    if(inputBuffer.size == 17 && memcmp("CALCULIB2_CONNECT",inputBuffer.pointer,17) == 0)
    {
        logger.printDebug("Received connection marker");
        buffer toSendBuffer;
        toSendBuffer.size = 18;
        toSendBuffer.pointer = (unsigned char *) "CALCULIB2_ACCEPTED";
        if(sendBuffer(socket,&toSendBuffer))
        {
            logger.printDebug("Protocol abort, send error");
            return 1;
        }
    }
    else
    {
        logger.printLog("Unknown connection refused !");
        return 1;
    }
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


void killClient(ClientList * list, int id)
{
    logger.printLog("Client disconnected !");
    printf("ID %d\n",id);
    
    list->getClient(id)->socket->disconnect();
    delete(list->getClient(id)->socket);
    
    delete(list->getClient(id));
    list->removeClient(id);
}

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

void answerToClient(ClientList * list, sf::TcpListener * listener)
{
    if(list->isFull())
    {
        
        while(listenerAccept(listener,acceptSocket))
        {
            logger.printDebug("Refusing");
            sendFullMessage(acceptSocket);
            acceptSocket->disconnect();
        }
        
    }
    else
    {
        while(!list->isFull())
        {
            if(!listenerAccept(listener,acceptSocket)) // no client
            {
                break;
            }

            logger.printDebug("Going to try protocol");
            
            printf("SOCKET %lu\n",(unsigned long)acceptSocket);
            
            if(connectProtocol(acceptSocket))
            {
                acceptSocket->disconnect();
                logger.printDebug("Killed because protocol");
                continue;
            }

            client * newClient = new client;
            newClient->socket = acceptSocket;

            setupAcceptSocket(); //reset for the next client

            list->addClient(newClient);
            
            logger.printLog("Client connected !");
            printf("ID %lu\n",(unsigned long)newClient);
            
            sf::sleep(sf::milliseconds(500));
        }
        
    }    
}


void dispatchMessage(ClientList * list, buffer * message, int senderId)
{
    for(int i = 0 ; i < list->getConnectedCount() ; i++)
    {
        if(i != senderId)
        {
            logger.printDebug("dispatching ...");
            printf("To %d\n",i);
            if(sendBuffer(list->getClient(i)->socket,message))
            {
                killClient(list,i);
            }
        }
    }
}

void checkForInputs(ClientList * list, buffer * exchangeBuffer)
{
    buffer workExchangeBuffer; // use a copy to keep the size
    workExchangeBuffer.pointer = exchangeBuffer->pointer;
    for(int i = 0 ; i < list->getConnectedCount() ; i++)
    {
        if(receiveBuffer(list->getClient(i)->socket,&workExchangeBuffer,exchangeBuffer->size))
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


void setupExchangeBuffer(buffer * exchangeBuffer, int bufferSize)
{
    exchangeBuffer->pointer = (unsigned char *) malloc(bufferSize);
    if(exchangeBuffer->pointer == NULL)
    {
        printf("ERROR, can't alloc the exchange buffer !\n");
        exit(1);
    }
    exchangeBuffer->size = bufferSize;
}



#define CLIENT_COUNT 2

int main (int argc, const char * argv[]) {
    
    int port = 63000;    
    int bufferSize = 8192;
    bool isQuiet = false;

    readArgs(argc,argv,&port,&bufferSize,&isQuiet);

    logger.setQuiet(isQuiet);
    logger.setDebug(true);
    
    if(!isQuiet)
    {
        printInfo(port,bufferSize);
    }
    
    sf::TcpListener listener;
    setupListener(&listener, port);
    
    ClientList list;
    list.setMaxClient(CLIENT_COUNT);
    
    buffer exchangeBuffer;
    setupExchangeBuffer(&exchangeBuffer, bufferSize);
    
    setupAcceptSocket();
    
    logger.printLog("Init'ed ...");
    
    while(1) //TODO receive system interrupts
    {
        answerToClient(&list, &listener);
        checkForInputs(&list, &exchangeBuffer);
    }
    
    return 0;
}
