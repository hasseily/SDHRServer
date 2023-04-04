// SDHDServer.cpp : Defines the entry point for the application.
//

#include "SDHDServer.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

#pragma pack(push, 1)
struct Packet {
	uint16_t address;
	uint8_t data;
	uint8_t pad;
};
#pragma pack(pop)

int main() {
	int server_fd, client_fd;
	struct sockaddr_in server_addr, client_addr;
	socklen_t client_len = sizeof(client_addr);

	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		std::cerr << "Error creating socket" << std::endl;
		return 1;
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(8080);
	server_addr.sin_addr.s_addr = INADDR_ANY;
	memset(&(server_addr.sin_zero), '\0', 8);

	if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
		std::cerr << "Error binding socket" << std::endl;
		return 1;
	}

	if (listen(server_fd, 1) == -1) {
		std::cerr << "Error listening on socket" << std::endl;
		return 1;
	}

	while (true)
	{

		if ((client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len)) == -1) {
			std::cerr << "Error accepting connection" << std::endl;
			return 1;
		}

		std::cout << "Client connected" << std::endl;

		Packet packet;
		ssize_t bytes_received;

		while ((bytes_received = recv(client_fd, &packet, sizeof(packet), 0)) > 0) {
			if (bytes_received == sizeof(packet)) {
				std::cout << "Received packet:" << std::endl;
				std::cout << "  address: " << packet.address << std::endl;
				std::cout << "  data: " << static_cast<unsigned>(packet.data) << std::endl;
				std::cout << "  pad: " << static_cast<unsigned>(packet.pad) << std::endl;
			}
			else {
				std::cerr << "Error receiving data or incomplete data" << std::endl;
			}
		}

		if (bytes_received == -1) {
			std::cerr << "Error receiving data" << std::endl;
		}

		close(client_fd);
	}
	close(server_fd);

	return 0;
}

