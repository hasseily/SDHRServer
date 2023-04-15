#include "SDHRServer.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include "DrawVBlank.h"
#include "SDHRManager.h"

/**
 * 
 * SDHRServer
 * Main entry point of the application. After initialization of the necessary variables,
 * it calls the Direct Rendering Manager (DRM) routines once to draw the screen.
 * Then it runs the main loop in which it listens to a socket.
 * This socket emulates the Apple 2 card bus, which is essentially
 * 16 bits of address and 8 bits of data. When a packet is received it calls
 * the relevant SDHRManager methods unless it's a memory update, in which it updates
 * a2mem. This allows SDHRServer to "see" the current state of the Apple 2's memory
 * (only between 0x200 and 0xbfff in main memory)
 * without being connected physically to the bus. Of course anything _before_ the server
 * is turned on won't be mapped to a2mem, but it is expected that the server will only be
 * interested by memory updates after SDHR is activated.
 * 
 * When receiving a PROCESS command, it first calls SDHRManager to process all the queued
 * commands, and then it checks if the double-buffered framebuffer is available. If it is,
 * it calls the drawing routines in DrawVBlank which schedules a frame flip for the next vblank.
 * If the framebuffer isn't yet available, it continues waiting for commands.
 * 
 */

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

	uint8_t* a2mem = sdhrMgr->GetApple2MemPtr();

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
	modeset_initialize();

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
				/*
				std::cout << "Received packet:" << std::endl;
				std::cout << "  address: " << std::hex << packet.addr << std::endl;
				std::cout << "  data: " << std::hex << static_cast<unsigned>(packet.data) << std::endl;
				std::cout << "  pad: " << std::hex << static_cast<unsigned>(packet.pad) << std::endl;
				*/

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
						std::cout << "CONTROL: Disable SDHR" << std::endl;
						sdhrMgr->ToggleSdhr(false);
						break;
					case SDHR_CTRL_ENABLE:
						std::cout << "CONTROL: Enable SDHR" << std::endl;
						sdhrMgr->ToggleSdhr(true);
						break;
					case SDHR_CTRL_RESET:
						std::cout << "CONTROL: Reset SDHR" << std::endl;
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
						//std::cout << "CONTROL: Process SDHR" << std::endl;
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

	modeset_cleanup();
	return 0;
}

