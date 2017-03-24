// Konstantinos Peratinos , Surafel Meheret
/*********************************************************** -- HEAD -{{{1- */
/* Echo Server for Network API Lab: Part I in Internet Technology 2011.
 *
 * Iterative server capable of accpeting and processing a single connection
 * at any given time. Data  eceived from the connection is simply sent back
 * unmodified ("echoed").
 *
 * Build the server using e.g.
 * 		$ g++ -Wall -Wextra -o server-iter server-iterative.cpp
 *
 * Start using
 * 		$ ./server-iter
 * or
 * 		$ ./server-iter 31337
 * to listen on a port other than the default 5703.
 */
/******************************************************************* -}}}1- */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <sys/time.h>

#include <vector>
#include <algorithm>

//--//////////////////////////////////////////////////////////////////////////
//--    configurables       ///{{{1///////////////////////////////////////////

// Set VERBOSE to 1 to print additional, non-essential information.
#define VERBOSE 1

// If NONBLOCKING is set to 1, all sockets are put into non-blocking mode.
// Use this for Part II, when implementing the select() based server, where
// no blocking operations other than select() should occur. (If an blocking
// operation is attempted, a EAGAIN or EWOULDBLOCK error is raised, probably
// indicating a bug in the code!)s
#define NONBLOCKING 1


// Default port of the server. May be overridden by specifying a different
// port as the first command line argument to the server program.
const int kServerPort = 5703;

// Second parameter to listen().
// Note - this parameter is (according to the POSIX standard) merely a hint.
// The implementation may choose a different value, or ignore it altogether.
const int kServerBacklog = 8;

// Size of the buffer used to transfer data. A single read from the socket
// may return at most this much data, and consequently, a single send may
// send at most this much data.
const size_t kTransferBufferSize = 64;

//--    constants           ///{{{1///////////////////////////////////////////

/* Connection states.
 * A connection may either expect to receive data, or require data to be
 * sent.
 */
enum EConnState
{
	eConnStateReceiving,
	eConnStateSending,
	eConnStateDead
};

//--    structures          ///{{{1///////////////////////////////////////////

/* Per-connection data
 * In the iterative server, there is a single instance of this structure, 
 * holding data for the currently active connection. A concurrent server will
 * need an instance for each active connection.
 */

struct ConnectionData
{
	EConnState state; // state of the connection; see EConnState enum

	int sock; // file descriptor of the connections socket.

	// items related to buffering.
	size_t bufferOffset, bufferSize;
	char buffer[kTransferBufferSize+1];
};

//struct connList

//{
//ConnectionData cSock;
//struct ConnList* next;

//}

//--    prototypes          ///{{{1///////////////////////////////////////////

/* Receive data and place it in the connection's buffer.
 *
 * Requires that ConnectionData::state is eConnStateReceiving; if not, an
 * assertation fault is generated.
 *
 * If _any_ data is received, the connection's state is transitioned to
 * eConnStateSending.
 *
 * Returns `true' if the connection remains open and further processing is
 * required. Returns `false' to indicate that the connection is closing or
 * has closed, and the connection should not be processed further.
 */
static bool process_client_recv( ConnectionData& cd );

/* Send data from the connection's buffer.
 *
 * Requires that ConnectionData::state is eConnStateSending; if not, an
 * asseration fault is generated.
 *
 * When all data is sent, the connection's state is transitioned to
 * eConnStateReceiving. If data remains in the buffer, no state-transition
 * occurs.
 *
 * Returns `true' if the connection remains open and further processing is
 * required. Returns `false' to indicate that the connection is closing or
 * has closed, and the connection should not be processed further.
 */
static bool process_client_send( ConnectionData& cd );

/* Places the socket identified by `fd' in non-blocking mode.
 *
 * Returns `true' if successful, and `false' otherwise.
 */
static bool set_socket_nonblocking( int fd );

/* Returns `true' if the connection `cd' has an invalid socket (-1), and
 * `false' otherwise.
 */
static bool is_invalid_connection( const ConnectionData& cd );


/* Sets up a listening socket on `port'.
 *
 * Returns, if successful, the new socket fd. On error, -1 is returned.
 */
static int setup_server_socket( short port );

//--    main()              ///{{{1///////////////////////////////////////////
int main( int argc, char* argv[] )
{
	int serverPort = kServerPort;
	//struct connList *list = NULL;
	// did the user specify a port?
	if( 2 == argc )
	{
		serverPort = atoi(argv[1]);
	}

	int maxfd, listenfd, connfd;
	int nread;
	std::vector<ConnectionData> clientsVec;
	fd_set rfd, wfd;
	struct timeval tv;
	tv.tv_sec = 5;  /* seconds */
	tv.tv_usec = 0;  /* microseconds */

#	if VERBOSE
	printf( "Attempting to bind to port %d\n", serverPort );
#	endif

	// set up listening socket - see setup_server_socket() for details.
	listenfd = setup_server_socket( serverPort );

	if( -1 == listenfd )
		return 1;

	maxfd = listenfd;
	clientsVec.clear();

	while (1)
	{
		sockaddr_in clientAddr;
		socklen_t addrSize = sizeof(clientAddr);

		// clear read and write set every loop iteration
		FD_ZERO(&wfd);
		FD_ZERO(&rfd);
		FD_SET(listenfd, &rfd);


		// Now we are going to order active clients based on respective queue 
		for(std::vector<ConnectionData>::iterator it = clientsVec.begin(); it != clientsVec.end(); ++it)
		{
			switch( it->state )
			{
				case eConnStateSending:
					FD_SET( it->sock, &wfd );
					break;

				case eConnStateReceiving:
					FD_SET( it->sock, &rfd );
					break;
			}

			// change the value of maxfd if there is bigger file descriptor in clientsVec
			maxfd = std::max( it->sock, maxfd );
		}

		// check if there are any file descriptors ready. will wait for 5 seconds
		if ( (nread = select(maxfd+1, &rfd, &wfd, NULL, &tv)) < 0 )
		{
			perror("select() failed");
			exit(0);
		}

		if (FD_ISSET(listenfd, &rfd))
		{
			# if VERBOSE
			printf("Trying to Establish a connection\n");
			# endif
			if ( (connfd = accept(listenfd, (struct sockaddr*)&clientAddr, &addrSize)) < 0)
			{
				if(EWOULDBLOCK != errno) // If there exist an error from the server side 
				{
					perror("accept()"); 
					exit(1);
				}
			}
			else
			{
				// add a new connection to the vector
				ConnectionData lion;
				memset(&lion, 0, sizeof(lion)); //Fill the memory that is created with the vector
				lion.sock = connfd;
				lion.state = eConnStateReceiving;
				clientsVec.push_back(lion);

				# if NONBLOCKING
				// enable non-blocking sends and receives on this socket
				if( !set_socket_nonblocking(connfd) )
					continue;
				# endif

				# if VERBOSE
				// print some information about the new client
				char buff[128];
				printf( "Connection from %s:%d -> socket %d\n",
				inet_ntop( AF_INET, &clientAddr.sin_addr, buff, sizeof(buff) ),
				ntohs(clientAddr.sin_port), connfd);
				fflush( stdout );
				#	endif
			}
		}


		for(std::vector<ConnectionData>::iterator it = clientsVec.begin(); it != clientsVec.end();)
		{
			bool processFurther = true;

			if( it->state == eConnStateDead )
			{
				processFurther = false;
			}

			if( processFurther && FD_ISSET(it->sock, &wfd) )
			{
				processFurther = process_client_send(*it);
			}

			else if( processFurther && FD_ISSET(it->sock, &rfd) )
			{
				processFurther = process_client_recv(*it);
			}

			if( !processFurther)
			{
				// If the connection has been shutdown update the connection as dead
				close(it->sock);
				it->sock = -1;
				it->state = eConnStateDead;
				it = clientsVec.erase(it);
			}

			else
			{
				it++;
			}
		}
	}

	// The final step to exit the connection 
	close( listenfd );

	return 0;
}

//--    process_client_recv()   ///{{{1///////////////////////////////////////
static bool process_client_recv( ConnectionData& cd )
{
	assert( cd.state == eConnStateReceiving );

	// receive from socket
	ssize_t ret = recv( cd.sock, cd.buffer, kTransferBufferSize, 0 );

#	if VERBOSE
	printf("Received %d bytes\n", ret);
	if ( ret < 0)
	{
		printf("recv() -1\n");
	}
# 	endif


	if( 0 == ret )
	{
#		if VERBOSE
		printf( "  socket %d - orderly shutdown\n", cd.sock );
		fflush( stdout );
#		endif

		return false;
	}

	if( -1 == ret )
	{
#		if VERBOSE
		printf("socket %d - error on receive: '%s'\n", cd.sock,
			strerror(errno) );
		fflush( stdout );
#		endif

		return false;
	}

	// update connection buffer
	cd.bufferSize += ret;

	// zero-terminate received data
	cd.buffer[cd.bufferSize] = '\0';

	// transition to sending state
	cd.bufferOffset = 0;
	cd.state = eConnStateSending;

#if	VERBOSE
	printf("Read buffer content:  %s\n", cd.buffer);
#	endif

	return true;
}

//--    process_client_send()   ///{{{1///////////////////////////////////////
static bool process_client_send( ConnectionData& cd )
{

	assert( cd.state == eConnStateSending );

	// send as much data as possible from buffer
	ssize_t ret = send( cd.sock,
		cd.buffer+cd.bufferOffset,
		cd.bufferSize-cd.bufferOffset,
		MSG_NOSIGNAL // suppress SIGPIPE signals, generate EPIPE instead
	);

#if	VERBOSE
	printf("%d bytes sent\n", ret);
	if ( ret < 0)
	{
		printf("send() -1\n");
	}
#	endif


	if( -1 == ret )
	{
#		if VERBOSE
		printf( "  socket %d - error on send: '%s'\n", cd.sock,
			strerror(errno) );
		fflush( stdout );
#		endif

		return false;
	}

	// update buffer data
	cd.bufferOffset += ret;

	// did we finish sending all data
	if( cd.bufferOffset == cd.bufferSize )
	{
		// if so, transition to receiving state again
		cd.bufferSize = 0;
		cd.bufferOffset = 0;
		cd.state = eConnStateReceiving;
	}

#if	VERBOSE
	printf("Send buffer content: %s\n", cd.buffer);
#	endif

	return true;
}

//--    setup_server_socket()   ///{{{1///////////////////////////////////////
static int setup_server_socket( short port )
{
	// create new socket file descriptor
	int fd = socket( AF_INET, SOCK_STREAM, 0 );
	if( -1 == fd )
	{
		perror( "socket() failed" );
		return -1;
	}

	// bind socket to local address
	sockaddr_in servAddr;
	memset( &servAddr, 0, sizeof(servAddr) );

	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAddr.sin_port = htons(port);

	if( -1 == bind( fd, (const sockaddr*)&servAddr, sizeof(servAddr) ) )
	{
		perror( "bind() failed" );
		close( fd );
		return -1;
	}

	// get local address (i.e. the address we ended up being bound to)
	sockaddr_in actualAddr;
	socklen_t actualAddrLen = sizeof(actualAddr);
	memset( &actualAddr, 0, sizeof(actualAddr) );

	if( -1 == getsockname( fd, (sockaddr*)&actualAddr, &actualAddrLen ) )
	{
		perror( "getsockname() failed" );
		close( fd );
		return -1;
	}

	char actualBuff[128];
	printf( "Socket is bound to %s %d\n",
		inet_ntop( AF_INET, &actualAddr.sin_addr, actualBuff, sizeof(actualBuff) ),
		ntohs(actualAddr.sin_port)
	);

	// and start listening for incoming connections
	if( -1 == listen( fd, kServerBacklog ) )
	{
		perror( "listen() failed" );
		close( fd );
		return -1;
	}

	// allow immediate reuse of the address (ip+port)
	int one = 1;
	if( -1 == setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int) ) )
	{
		perror( "setsockopt() failed" );
		close( fd );
		return -1;
	}

#	if NONBLOCKING
	// enable non-blocking mode
	if( !set_socket_nonblocking( fd ) )
	{
		close( fd );
		return -1;
	}
#	endif

	return fd;
}

//--    set_socket_nonblocking()   ///{{{1////////////////////////////////////
static bool set_socket_nonblocking( int fd )
{
	int oldFlags = fcntl( fd, F_GETFL, 0 );
	if( -1 == oldFlags )
	{
		perror( "fcntl(F_GETFL) failed" );
		return false;
	}

	if( -1 == fcntl( fd, F_SETFL, oldFlags | O_NONBLOCK ) )
	{
		perror( "fcntl(F_SETFL) failed" );
		return false;
	}

	return true;
}

//--    is_invalid_connection()    ///{{{1////////////////////////////////////
static bool is_invalid_connection( const ConnectionData& cd )
{
	return cd.sock == -1;
}
//void addLast ( connectionData ptr*)

//{
//struct connList *l = malloc(sizeof( struct connectionData))
//l->csock = &ptr;
//l->next =
//}
