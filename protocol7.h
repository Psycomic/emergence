#ifndef PROTOCOL_7_HEADER
#define PROTOCOL_7_HEADER

#include <inttypes.h>

#include "misc.h"

#define P7_PORT 36666

typedef uint64_t uint128[2];
typedef uint64_t uint256[4];

typedef struct {
	int socket;
	enum {
		P7_NODE_PINGED,
		P7_NODE_CONNECTED,
	} state;
	uint256 id;
} P7Node;

typedef struct {
	uint id;
	uint256 source;
} P7PacketHeader;

typedef enum {
	P7_PING,
	P7_PONG
} P7PacketType;

typedef P7PacketHeader P7PingPacket;
typedef P7PacketHeader P7PongPacket;

DynamicArray p7_inbound_nodes;
DynamicArray p7_outbound_nodes;

int p7_server_socket;
uint256 p7_id;

int p7_init();
void p7_loop();

int p7_node_discover(const char* host, unsigned short port);

#endif
