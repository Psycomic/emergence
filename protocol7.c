#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "protocol7.h"
#include "random.h"

#define SECONDS(s) ((clock_t)(s * CLOCKS_PER_SEC))

int p7_server_socket;
uint256 p7_id;

static DynamicArray p7_outbound_nodes;
static HashTable* p7_inbound_nodes;

static clock_t p7_clock;

uint p7_addr_hash(struct sockaddr* addr) {
	uchar* as_bytes = (uchar*)addr;

	uint hash = as_bytes[0];
	for (uint i = 0; i < sizeof(struct sockaddr); i++) {
		hash += (as_bytes[i] ^ as_bytes[(i + 5) % sizeof(struct sockaddr)]) << (as_bytes[i] / 50);
	}

	return hash;
}

GLboolean uint256_eq(uint256 a, uint256 b) {
	return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

void uint256_print(uint256 x) {
	printf("0x%lx%lx%lx%lx", x[0], x[1], x[2], x[3]);
}

int p7_socket() {
	int sock;
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0)
		return -1;

#ifdef __linux__
	if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0)
		return -1;
#endif
#ifdef _WIN32
	u_long iMode = 0;
	if (ioctlsocket(sock, FIONBIO, &iMode) != NO_ERROR)
		return -1;
#endif

	return sock;
}

void p7_ping_packet_init(P7PingPacket* packet) {
	packet->type = P7_PING;
}

void p7_pong_packet_init(P7PongPacket* packet) {
	packet->type = P7_PONG;
}

void p7_node_init(P7Node* node, int sock, struct sockaddr_in* addr) {
	node->socket = sock;
	node->state = P7_NODE_NOT_CONNECTED;
	node->addr = *addr;
	node->last_activity = 0;
	node->last_ping = 0;
}

void p7_node_print(P7Node* node) {
	printf("#<NODE id: ");
	uint256_print(node->id);
	printf(" state: ");

	switch (node->state) {
	case P7_NODE_NOT_CONNECTED:
		printf("P7_NODE_NOT_CONNECTED");
		break;
	case P7_NODE_CONNECTED:
		printf("P7_NODE_CONNECTED");
		break;
	}

	printf(">\n");
}

void p7_node_destroy(P7Node* node) {
	close(node->socket);
}

int p7_init() {
#ifdef _WIN32
	WSADATA wsaData;

	int iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (iResult != NO_ERROR)
		printf("Error at WSAStartup()\n");
#endif

	DYNAMIC_ARRAY_CREATE(&p7_inbound_nodes, P7Node);
	DYNAMIC_ARRAY_CREATE(&p7_outbound_nodes, P7Node);

	p7_inbound_nodes = hash_table_create(64, p7_addr_hash);

	random_csprng_bytes(p7_id, sizeof(p7_id));
	printf("====ID IS 0x%lx%lx%lx%lx====\n", p7_id[0], p7_id[1], p7_id[2], p7_id[3]);

	struct sockaddr_in server_address;
	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = inet_addr("0.0.0.0");

	p7_server_socket = p7_socket();

	unsigned short port = P7_PORT;

bind:
	server_address.sin_port = htons(port);

	if (bind(p7_server_socket, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
		port = random_randint() % 80 + P7_PORT;
		printf("Trying port %d...\n", port);
		goto bind;
	}

	return 0;
}

int p7_node_send(P7PacketHeader* packet, size_t p_size, P7Node* node) {
	memcpy(packet->source, p7_id, sizeof(p7_id));
	sendto(node->socket, packet, p_size, 0, (struct sockaddr*)&node->addr, sizeof(struct sockaddr_in));

	return 0;
}

int p7_node_discover(const char* host, unsigned short port) {
	struct sockaddr_in dest_addr;
	dest_addr.sin_family = AF_INET;
	dest_addr.sin_port = htons(port);
	dest_addr.sin_addr.s_addr = inet_addr(host);

	P7Node* node = dynamic_array_push_back(&p7_outbound_nodes, 1);
	int sock = p7_socket();

	p7_node_init(node, sock, &dest_addr);

	return 0;
}

void p7_handle_packet(void* buffer, size_t size, struct sockaddr* addr, P7Node* node) {
	if (size >= sizeof(P7PacketHeader)) {
		P7PacketHeader* header = buffer;

		node->last_activity = p7_clock;

		if (!uint256_eq(header->source, p7_id)) {
			switch (header->type) {
			case P7_PING:
			{
				if (node->state == P7_NODE_NOT_CONNECTED) {
					node->state = P7_NODE_CONNECTED;
					memcpy(node->id, header->source, sizeof(uint256));
				}

				P7PongPacket packet;
				p7_pong_packet_init(&packet);
				p7_node_send(&packet, sizeof(packet), node);

				printf("PING from ");
				p7_node_print(node);
			}
			break;
			case P7_PONG:
				if (node->state == P7_NODE_NOT_CONNECTED) {
					memcpy(node->id, header->source, sizeof(uint256));
					node->state = P7_NODE_CONNECTED;
				}

				printf("PONG from ");
				p7_node_print(node);
				break;
			default:
				printf("Received malformed packet!\n");
			}
		}
	}
}

void p7_node_update(P7Node* node) {
	if ((p7_clock - node->last_ping) > 100000) {
		node->last_ping = p7_clock;

		P7PingPacket ping;
		p7_ping_packet_init(&ping);

		p7_node_send(&ping, sizeof(ping), node);
	}

	if (node->state == P7_NODE_CONNECTED && p7_clock - node->last_activity > 300000) {
		node->state = P7_NODE_NOT_CONNECTED;

		printf("Node disconnected: ");
		p7_node_print(node);
	}
}

void p7_loop() {
	p7_clock = clock();

	char buffer[1024];

	struct sockaddr_in client_addr;
	socklen_t client_addr_size = sizeof(client_addr);

	ssize_t s;
	if ((s = recvfrom(p7_server_socket, buffer, sizeof(buffer),
					  0, (struct sockaddr*)&client_addr, &client_addr_size)) >= 0)
	{
		P7Node* node = hash_table_get(p7_inbound_nodes, &client_addr);
		if (node == NULL) {
			P7Node new_node;
			p7_node_init(&new_node, p7_server_socket, &client_addr);

			hash_table_set(p7_inbound_nodes, &client_addr, &new_node, sizeof(P7Node));
			node = hash_table_get(p7_inbound_nodes, &client_addr);
		}

		p7_handle_packet(buffer, s, (struct sockaddr*)&client_addr, node);
	}

	for (uint i = 0; i < p7_inbound_nodes->size; i++) { /* Updating inbound nodes */
		for (HashTableEntry* entry = p7_inbound_nodes->entries[i]; entry != NULL; entry = entry->next_entry) {
			P7Node* node = (P7Node*)entry->data;
			p7_node_update(node);
		}
	}

	for (uint i = 0; i < p7_outbound_nodes.size; i++) { /* Updating outbound nodes */
		P7Node* node = dynamic_array_at(&p7_outbound_nodes, i);
		if ((s = recvfrom(node->socket, buffer, sizeof(buffer), 0,
						  (struct sockaddr*)&client_addr, &client_addr_size)) >= 0)
		{
			p7_handle_packet(buffer, s, (struct sockaddr*)&client_addr, node);
		}

		p7_node_update(node);
	}
}
