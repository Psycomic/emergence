#ifndef PROTOCOL_7_HEADER
#define PROTOCOL_7_HEADER

#ifdef __linux__
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#endif
#ifdef _WIN32
#include <winsock2.h>
#endif

#include <time.h>
#include <inttypes.h>

#include "misc.h"

#define P7_PORT 36666

typedef uint64_t uint128[2];
typedef uint64_t uint256[4];

typedef struct {
	int socket;
	enum {
		P7_NODE_NOT_CONNECTED,
		P7_NODE_CONNECTED
	} state;

	clock_t last_activity;
	clock_t last_ping;
	struct sockaddr_in addr;
	uint256 id;
} P7Node;

typedef enum {
	P7_PING,
	P7_PONG
} P7PacketType;

typedef struct {
	P7PacketType type;
	uint256 source;
} P7PacketHeader;

typedef P7PacketHeader P7PingPacket;
typedef P7PacketHeader P7PongPacket;

int p7_server_socket;
uint256 p7_id;

int p7_init();
void p7_loop();

int p7_node_discover(const char* host, unsigned short port);

#endif
