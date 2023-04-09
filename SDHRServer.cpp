
#include "SDHRServer.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include "DrawVBlank.h"
#include "SDHRManager.h"

static SDHRManager* sdhrMgr;

#pragma pack(push, 1)
struct SDHRPacket {
	uint16_t addr;
	uint8_t data;
	uint8_t pad;
};
#pragma pack(pop)

int main() {
	sdhrMgr = SDHRManager::GetInstance();

	// commands socket and descriptors
	int server_fd, client_fd;
	struct sockaddr_in server_addr, client_addr;
	socklen_t client_len = sizeof(client_addr);

	// DRM variables and initialization
	int ret_drm;
	fd_set drm_fds;
	drmEventContext evctx = {
		.version = 2,	// supports page_flip_handler
		.page_flip_handler = modeset_page_flip_event,
	};
	struct modeset_dev* iter;

	// Initialize the Apple 2 memory duplicate
	// Whenever memory is written from the Apple2
	// in the main bank between $200 and $BFFF it will
	// be sent through the socket and this buffer will be updated
	a2mem = new uint8_t[0xbfff];	// anything below $200 is unused

	FD_ZERO(&drm_fds);
	memset(&evctx, 0, sizeof(evctx));
	// Draw once
	for (iter = modeset_list; iter; iter = iter->next) {
		modeset_draw_dev(modeset_fd, iter);
	}

	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) 
	{
		std::cerr << "Error creating socket" << std::endl;
		return 1;
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(8080);
	server_addr.sin_addr.s_addr = INADDR_ANY;
	memset(&(server_addr.sin_zero), '\0', 8);

	if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) 
	{
		std::cerr << "Error binding socket" << std::endl;
		return 1;
	}

	if (listen(server_fd, 1) == -1) 
	{
		std::cerr << "Error listening on socket" << std::endl;
		return 1;
	}

	while (true)
	{
		if ((client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len)) == -1) 
		{
			std::cerr << "Error accepting connection" << std::endl;
			return 1;
		}

		std::cout << "Client connected" << std::endl;

		SDHRPacket packet;
		ssize_t bytes_received;

		while ((bytes_received = recv(client_fd, &packet, sizeof(packet), 0)) > 0) 
		{
			if (bytes_received == sizeof(packet)) 
			{
				std::cout << "Received packet:" << std::endl;
				std::cout << "  address: " << std::hex << packet.addr << std::endl;
				std::cout << "  data: " << std::hex << static_cast<unsigned>(packet.data) << std::endl;
				std::cout << "  pad: " << std::hex << static_cast<unsigned>(packet.pad) << std::endl;

				if ((packet.addr >= 0x200) && (packet.addr <= 0xbfff))
				{
					// it's a memory write
					a2mem[packet.addr] = packet.data;
					continue;
				}
				SDHRCtrl_e _ctrl;
				switch (packet.addr & 0x0f) 
				{
				case 0x00:
					// std::cout << "This is a control packet!" << std::endl;
					_ctrl = (SDHRCtrl_e)packet.data;
					switch (_ctrl)
					{
					case SDHR_CTRL_DISABLE:
						sdhrMgr->ToggleSdhr(false);
						break;
					case SDHR_CTRL_ENABLE:
						sdhrMgr->ToggleSdhr(true);
						break;
					case SDHR_CTRL_RESET:
						sdhrMgr->ResetSdhr();
						break;
					case SDHR_CTRL_PROCESS:
					{
						/*
						At this point we have a complete set of commands to process.
						Some more data may be in the kernel socket receive buffer, but we don't care.
						They'll be processed in the next batch.
						Continue processing commands until the framebuffer is flipped. Once the framebuffer
						has flipped, run the framebuffer drawing with the current state and schedule a flip.
						Rince and repeat.
						*/
						bool processingSucceeded = sdhrMgr->ProcessCommands();
						// Whether or not the processing worked, clear the buffer. If the processing failed,
						// the data was corrupt and shouldn't be reprocessed
						sdhrMgr->ClearBuffer();
						if (processingSucceeded && sdhrMgr->IsSdhrEnabled())
						{
							// We have processed some commands.
							// Check if FB flipped since last time. If the FB has flipped, draw!
							// Drawing is done in the modeset_page_flip_event handler
							FD_SET(modeset_fd, &drm_fds);
							ret_drm	= select(modeset_fd + 1, &drm_fds, NULL, NULL, NULL);
							if (ret_drm < 0)
							{
								fprintf(stderr, "select() failed with %d: %m\n", errno);
								break;
							}
							else if (FD_ISSET(modeset_fd, &drm_fds))
							{
								// Page flip has happened, the FD is readable again
								// We can now trigger a framebuffer draw
								drmHandleEvent(modeset_fd, &evctx);
							}
						}
						break;
					}
					default:
						break;
					}
					break;
				case 0x01:
					// std::cout << "This is a data packet" << std::endl;
					sdhrMgr->AddPacketDataToBuffer(packet.data);
					break;
				}
			}
			else 
			{
				std::cerr << "Error receiving data or incomplete data" << std::endl;
			}
		}

		if (bytes_received == -1) 
		{
			std::cerr << "Error receiving data" << std::endl;
		}

		close(client_fd);
	}
	close(server_fd);

	delete[] a2mem;
	return 0;
}

