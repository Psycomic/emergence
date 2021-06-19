#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#ifdef __linux__
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#endif
#ifdef _WIN32
#include <winsock2.h>
#endif

#include "protocol7.h"
#include "random.h"

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
	packet->id = P7_PING;
	memcpy(packet->source, p7_id, sizeof(p7_id));
}

void p7_pong_packet_init(P7PongPacket* packet) {
	packet->id = P7_PONG;
	memcpy(packet->source, p7_id, sizeof(p7_id));
}

void p7_node_init(P7Node* node, int sock) {
	node->socket = sock;
	node->state = P7_NODE_PINGED;
}

int p7_node_discover(const char* host, unsigned short port) {
	P7Node* node = dynamic_array_push_back(&p7_outbound_nodes, 1);
	int sock = p7_socket();

	p7_node_init(node, sock);

	struct sockaddr_in dest_addr;
	socklen_t dest_addr_size = sizeof(dest_addr);
	dest_addr.sin_family = AF_INET;
	dest_addr.sin_port = htons(port);
	dest_addr.sin_addr.s_addr = inet_addr(host);

	P7PingPacket packet;
	p7_ping_packet_init(&packet);

	sendto(sock, &packet, sizeof(packet), 0, (struct sockaddr*)&dest_addr, dest_addr_size);

	return 0;
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

	random_csprng_bytes(p7_id, sizeof(p7_id));

	struct sockaddr_in server_address;
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(P7_PORT);
	server_address.sin_addr.s_addr = inet_addr("0.0.0.0");

	p7_server_socket = p7_socket();

	if (bind(p7_server_socket, (struct sockaddr*)&server_address, sizeof(server_address)) < 0)
		return -1;

	return 0;
}

void p7_loop() {
	char buffer[1024];

	struct sockaddr_in client_addr;
	socklen_t client_addr_size = sizeof(client_addr);

	ssize_t s;
	if ((s = recvfrom(p7_server_socket, buffer, sizeof(buffer), 0,
					  (struct sockaddr*)&client_addr, &client_addr_size)) >= 0)
	{
		uint type = *(uint*)buffer;
		switch (type) {
		case P7_PING:
			printf("Received ping!\n");
			break;
		case P7_PONG:
			printf("Received pong!\n");
			break;
		}
	}
}
