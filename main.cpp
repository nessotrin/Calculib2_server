#include <SFML/Network.hpp>
#include <iostream>
#include <cstdlib>


unsigned char * sharedBuffer;
int sharedBufferSize = 8192;
int port = 63000;

struct clientContext{
    sf::TcpSocket clients[2];
    bool clientsConnected[2] = {false,false};
};


void drawStats(clientContext * context)
{
    std::cout << "\rClients : [";
    for(int id = 0 ; id < 2 ; id++)
    {
        if(context->clientsConnected[id])
        {
            std::cout << "X";
        }
        else
        {
            std::cout << " ";
        }
        if(id == 0)
        {
            std::cout << "|";
        }
    }
    std::cout << "]";
    
    fflush(stdout);
}

void disconnect(int id, clientContext * context)
{
    context->clientsConnected[id] = false;
    context->clients[id].disconnect();
    drawStats(context);
}

sf::Socket::Status sendData(unsigned char * buffer, int size, int id, clientContext * context)
{
    std::size_t sent;
    std::size_t totalSent = 0;
    sf::Socket::Status result;
    while(totalSent < size)
    {
        result = context->clients[id].send(buffer, size, sent);
        if (result != sf::Socket::Done && result != sf::Socket::NotReady)
        {
            if(result == sf::Socket::Error)
            {
                printf("\nSocket ERROR !\n");
            }
            disconnect(id, context);
            return result;
        }
        if(result == sf::Socket::Done) // prevents any garbage value
        {
            totalSent += sent;
        }
    }
    return result;
}
sf::Socket::Status receiveData(unsigned char * buffer, int* size, int max, int id, clientContext * context)
{
    std::size_t received;
    *size = 0;
    // TCP socket:
    sf::Socket::Status result = context->clients[id].receive(buffer, max, received);
    if(result != sf::Socket::Done && result != sf::Socket::NotReady)
    {
        if(result == sf::Socket::Error)
        {
            printf("\nSocket ERROR !\n");
        }
        disconnect(id,context);
    }
    if(result == sf::Socket::Done && received > 0)
    {
        *size = received;
    }
    return result;
}


void checkClient(int id, clientContext * context)
{
    std::size_t received;
    // TCP socket:
    int result = context->clients[id].receive(sharedBuffer, sharedBufferSize, received);
    if(result != sf::Socket::Done && result != sf::Socket::NotReady)
    {
        if(result == sf::Socket::Error)
        {
            printf("\nSocket ERROR !\n");
        }
        disconnect(id,context);
    }
    if(received > 0 && context->clientsConnected[!id])
    {
        if(sendData(sharedBuffer, received, !id, context))
        {
            return;
        }
    }
}

#include <cstring> //memcmp

// return true if client accepted
bool acceptClient(bool reject, int id, clientContext * context, sf::TcpListener * listener)
{
    // accept a new connection
    context->clients[id].setBlocking(true);

    int result = listener->accept(context->clients[id]);
    if(result == sf::Socket::Done)
    {
        context->clientsConnected[id] = true;
        unsigned char buffer[17];
        int received;
        
        int receiveResult;
        for(int i = 0 ; i < 200 ; i++) // 2s
        {
            receiveResult = receiveData(buffer,&received,17,id,context);
            if(receiveResult != sf::Socket::NotReady)
            {
                break;
            }
            sf::sleep(sf::milliseconds(10));
        }
        if(receiveResult != sf::Socket::Done || received != 17 || memcmp("CALCULIB2_CONNECT",buffer,17) != 0)
        {
            printf("Unknown tried to connect ...\n");
            //ignore the client
        }
        else
        {
            if(reject)
            {
                if(sendData((unsigned char *)"CALCULIB2_SERVER_FULL", 21, id, context))
                {
                    disconnect(id, context);
                    return false;
                }
                drawStats(context);
                disconnect(id, context);
            }
            else
            {
                if(sendData((unsigned char *)"CALCULIB2_ACCEPTED", 18, id, context))
                {
                    disconnect(id, context);
                    return false;
                }
                context->clients[id].setBlocking(false);
                drawStats(context);
                return true;
            }
        }
    }
    else if(result != sf::Socket::NotReady)
    {
        printf("\nCan't accept client !\n");
    }
    
    return false;
}

int main (int argc, const char * argv[]) {
    
    

    
    if(argc == 3)
    {
        sharedBufferSize = strtol(argv[2],NULL,10);
        port = strtol(argv[1],NULL,10);
        
    }
    if(argc == 2)
    {
        port = strtol(argv[1],NULL,10);
    }
        
    std::cout << "Calculib server V1.1" << std::endl;
    std::cout << "From the NESSCASDK project" << std::endl;
    std::cout << "By Nessotrin for the Casio community, 2016" << std::endl;
    std::cout << "Using: Calculib Protocol V1" << std::endl;
    std::cout << "  This server is a simple repeater betwen 2 clients with a plain message SERVER_GOOD_DONE or SERVER_EXIT_FULL" << std::endl;
    std::cout << "Use with one argument to set the port" << std::endl;
    std::cout << "Use with two argument to set the port and the transfere buffer size" << std::endl;
    std::cout << "Buffer size: " << sharedBufferSize << "o" << std::endl;
    std::cout << "Port: " << port << std::endl;
    
    sf::TcpListener listener;

    // bind the listener to a port
    if (listener.listen(port) != sf::Socket::Done)
    {
        printf("Can't listen on the port !\n");
        return 1;
    }
    listener.setBlocking(false);
    
    clientContext context;

    for(int i = 0 ; i < 2 ; i++)
    {
        context.clients[i].setBlocking(false);
    }
    
    sharedBuffer = (unsigned char *)malloc(sharedBufferSize);
    if(sharedBuffer == NULL)
    {
        std::cout << "Out of memory on buffer creation !\n" << std::endl;
        return 1;
    }
    
    drawStats(&context);
    
    while(1) //can't stop server
    {
        for(int id = 0 ; id < 2 ; id++)
        {
            if(!context.clientsConnected[id])
            {
                if(acceptClient(false,id,&context,&listener))
                {
                    printf("Client accepted ...\n");
                    context.clientsConnected[id] = true;
                }
            }
            else
            {
                checkClient(id, &context);
            }

        }
        if(context.clientsConnected[0] && context.clientsConnected[1])
        {
            clientContext rejectionContext;
            if(acceptClient(true,0,&rejectionContext,&listener))
            {
                std::cout << std::endl << "SERVER FULL: client rejected !" << std::endl;
            }
        }
        
        sf::sleep(sf::milliseconds(20));
    }


    
    
    
    return 0;
}
