#include "SDHRManager.h"
#include <cstring>
#include <iostream>
#include <fstream>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// below because "The declaration of a static data member in its class definition is not a definition"
SDHRManager* SDHRManager::s_instance;

//////////////////////////////////////////////////////////////////////////
// Commands structs
//////////////////////////////////////////////////////////////////////////

#pragma pack(push)
#pragma pack(1)

struct UploadDataCmd {
	uint8_t dest_addr_med;
	uint8_t dest_addr_high;
	uint8_t source_addr_med;
	uint8_t num_256b_pages;
};

struct UploadDataFilenameCmd {
	uint8_t dest_addr_med;
	uint8_t dest_addr_high;
	uint8_t filename_length;
	uint8_t filename[];
};

struct DefineImageAssetCmd {
	uint8_t asset_index;
	uint8_t upload_addr_med;
	uint8_t upload_addr_high;
	uint16_t upload_page_count;
};

struct DefineImageAssetFilenameCmd {
	uint8_t asset_index;
	uint8_t filename_length;
	uint8_t filename[];  // don't include the trailing null either in the data or counted in the filename_length
};

struct DefineTilesetCmd {
	uint8_t tileset_index;
	uint8_t num_entries;
	uint8_t xdim;
	uint8_t ydim;
	uint8_t asset_index;
	uint8_t data_med;
	uint8_t data_high;
};

struct DefineTilesetImmediateCmd {
	uint8_t tileset_index;
	uint8_t num_entries;
	uint8_t xdim;
	uint8_t ydim;
	uint8_t asset_index;
	uint8_t data[];  // data is 4-uint8_t records, 16-bit x and y offsets (scaled by x/ydim), from the given asset
};

struct DefineWindowCmd {
	int8_t window_index;
	bool black_or_wrap;			// false: viewport is black outside of tile range, true: viewport wraps
	uint64_t screen_xcount;		// width in pixels of visible screen area of window
	uint64_t screen_ycount;
	int64_t screen_xbegin;		// pixel xy coordinate where window begins
	int64_t screen_ybegin;
	int64_t tile_xbegin;		// pixel xy coordinate on backing tile array where aperture begins
	int64_t tile_ybegin;
	uint64_t tile_xdim;			// xy dimension, in pixels, of tiles in the window.
	uint64_t tile_ydim;
	uint64_t tile_xcount;		// xy dimension, in tiles, of the tile array
	uint64_t tile_ycount;
};

struct UpdateWindowSetBothCmd {
	int8_t window_index;
	int64_t tile_xbegin;
	int64_t tile_ybegin;
	uint64_t tile_xcount;
	uint64_t tile_ycount;
	uint8_t data[];  // data is 2-uint8_t records per tile, tileset and index
};

struct UpdateWindowSetUploadCmd {
	int8_t window_index;
	int64_t tile_xbegin;
	int64_t tile_ybegin;
	uint64_t tile_xcount;
	uint64_t tile_ycount;
	uint8_t upload_addr_med;
	uint8_t upload_addr_high;
};

struct UpdateWindowSingleTilesetCmd {
	int8_t window_index;
	int64_t tile_xbegin;
	int64_t tile_ybegin;
	uint64_t tile_xcount;
	uint64_t tile_ycount;
	uint8_t tileset_index;
	uint8_t data[];  // data is 1-uint8_t record per tile, index on the given tileset
};

struct UpdateWindowShiftTilesCmd {
	int8_t window_index;
	int8_t x_dir; // +1 shifts tiles right by 1, negative shifts tiles left by 1, zero no change
	int8_t y_dir; // +1 shifts tiles down by 1, negative shifts tiles up by 1, zero no change
};

struct UpdateWindowSetWindowPositionCmd {
	int8_t window_index;
	int64_t screen_xbegin;
	int64_t screen_ybegin;
};

struct UpdateWindowAdjustWindowViewCommand {
	int8_t window_index;
	int64_t tile_xbegin;
	int64_t tile_ybegin;
};

struct UpdateWindowEnableCmd {
	int8_t window_index;
	bool enabled;
};

#pragma pack(pop)

//////////////////////////////////////////////////////////////////////////
// Image Asset Methods
//////////////////////////////////////////////////////////////////////////

void SDHRManager::ImageAsset::AssignByFilename(const char* filename) {
	int width;
	int height;
	int channels;
	data = stbi_load(filename, &width, &height, &channels, 4);
	if (data == NULL) {
		// image failed to load
		SDHRManager::GetInstance()->error_flag = true;
		return;
	}
	image_xcount = width;
	image_ycount = height;
}

void SDHRManager::ImageAsset::AssignByMemory(const uint8_t* buffer, uint64_t size) {
	int width;
	int height;
	int channels;
	data = stbi_load_from_memory(buffer, size, &width, &height, &channels, 4);
	if (data == NULL) {
		SDHRManager::GetInstance()->error_flag = true;
		return;
	}
	image_xcount = width;
	image_ycount = height;
}

void SDHRManager::ImageAsset::ExtractTile(uint16_t* tile_p, uint16_t tile_xdim, uint16_t tile_ydim, uint64_t xsource, uint64_t ysource) {
	uint16_t* dest_p = tile_p;
	if (xsource + tile_xdim > image_xcount ||
		ysource + tile_ydim > image_ycount) {
		SDHRManager::GetInstance()->CommandError("ExtractTile out of bounds");
		SDHRManager::GetInstance()->error_flag = true;
		return;
	}

	for (uint64_t y = 0; y < tile_ydim; ++y) {
		uint64_t source_yoffset = (ysource + y) * image_xcount * 4;
		for (uint64_t x = 0; x < tile_xdim; ++x) {
			uint64_t pixel_offset = source_yoffset + (xsource + x) * 4;
			uint8_t r = data[pixel_offset];
			uint8_t g = data[pixel_offset + 1];
			uint8_t b = data[pixel_offset + 2];
			uint8_t a = data[pixel_offset + 3];
			uint16_t dest_pixel = 0;
			dest_pixel |= (a & 0x80) ? 0x8000 : 0x0;
			dest_pixel |= (r >> 3) << 10;
			dest_pixel |= (g >> 3) << 5;
			dest_pixel |= (b >> 3);
			*dest_p = dest_pixel;
			++dest_p;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Methods
//////////////////////////////////////////////////////////////////////////

void SDHRManager::Initialize()
{
	m_bEnabled = false;
	error_flag = false;
	memset(error_str, 0, sizeof(error_str));
	memset(uploaded_data_region, 0, sizeof(uploaded_data_region));
	*tileset_records = {};
	*windows = {};

	command_buffer.reserve(64 * 1024);

	// Initialize the Apple 2 memory duplicate
	// Whenever memory is written from the Apple2
	// in the main bank between $200 and $BFFF it will
	// be sent through the socket and this buffer will be updated
	a2mem = new uint8_t[0xbfff];	// anything below $200 is unused
}

SDHRManager::~SDHRManager()
{
	for (uint16_t i = 0; i < 256; ++i) {
		if (image_assets[i].data) {
			stbi_image_free(image_assets[i].data);
		}
		if (tileset_records[i].tile_data) {
			free(tileset_records[i].tile_data);
		}
		if (windows[i].tilesets) {
			free(windows[i].tilesets);
		}
		if (windows[i].tile_indexes) {
			free(windows[i].tile_indexes);
		}
	}
	delete[] a2mem;
}

void SDHRManager::AddPacketDataToBuffer(uint8_t data)
{
	command_buffer.push_back(data);
}

void SDHRManager::ClearBuffer()
{
	command_buffer.clear();
}

void SDHRManager::CommandError(const char* err) {
	strcpy(error_str, err);
	error_flag = true;
	std::cerr << "Command Error: " << error_str << std::endl;
}

bool SDHRManager::CheckCommandLength(uint8_t* p, uint8_t* e, size_t sz) {
	size_t command_length = e - p;
	if (command_length < sz) {
		CommandError("Insufficient buffer space");
		return false;
	}
	return true;
}

uint32_t SDHRManager::ARGB555_to_ARGB888(uint16_t argb555) {
	uint8_t r = (argb555 >> 10) & 0x1F;
	uint8_t g = (argb555 >> 5) & 0x1F;
	uint8_t b = argb555 & 0x1F;
	uint8_t a = (argb555 & 0x8000);	// alpha in RGB555 is all or nothing

	uint32_t r888 = (r << 3) | (r >> 2);
	uint32_t g888 = (g << 3) | (g >> 2);
	uint32_t b888 = (b << 3) | (b >> 2);
	uint32_t a888 = a * 0xFF;

	uint32_t argb888 = (a888 << 24) | (r888 << 16) | (g888 << 8) | b888;
	return argb888;
}

uint8_t* SDHRManager::GetApple2MemPtr()
{
	return a2mem;
}

void SDHRManager::DefineTileset(uint8_t tileset_index, uint16_t num_entries, uint8_t xdim, uint8_t ydim,
	ImageAsset* asset, uint8_t* offsets) {
	uint64_t store_data_size = (uint64_t)xdim * ydim * 2 * num_entries;
	TilesetRecord* r = tileset_records + tileset_index;
	if (r->tile_data) {
		free(r->tile_data);
	}
	*r = {};
	r->xdim = xdim;
	r->ydim = ydim;
	r->num_entries = num_entries;
	r->tile_data = (uint16_t*)malloc(store_data_size);

	uint8_t* offset_p = offsets;
	uint16_t* dest_p = r->tile_data;
	for (uint64_t i = 0; i < num_entries; ++i) {
		uint64_t xoffset = *((uint16_t*)offset_p);
		offset_p += 2;
		uint64_t yoffset = *((uint16_t*)offset_p);
		offset_p += 2;
		uint64_t asset_xoffset = xoffset * xdim;
		uint64_t asset_yoffset = yoffset * xdim;
		asset->ExtractTile(dest_p, xdim, ydim, asset_xoffset, asset_yoffset);
		dest_p += (uint64_t)xdim * ydim;
	}
}

/**
 * Commands in the buffer look like:
 * First 2 bytes are the command length (excluding these bytes)
 * 3rd byte is the command id
 * Anything after that is the command's packed struct,
 * for example UpdateWindowEnableCmd.
 * So the buffer of UpdateWindowEnable will look like:
 * {03, 00, 13, 0, 1} to enable window 0
*/

bool SDHRManager::ProcessCommands(void)
{
	error_flag = false;
	if (command_buffer.empty()) {
		//nothing to do
		return true;
	}
	uint8_t* begin = &command_buffer[0];
	uint8_t* end = begin + command_buffer.size();
	uint8_t* p = begin;

	while (p < end) {
		// Header (2 bytes) giving the size in bytes of the command
		if (!CheckCommandLength(p, end, 2)) {
			std::cerr << "CheckCommandLength failed!" << std::endl;
			return false;
		}
		uint16_t message_length = *((uint16_t*)p);
		if (!CheckCommandLength(p, end, message_length)) return false;
		p += 2;
		// Command ID (1 byte)
		uint8_t cmd = *p++;
		// Command data (variable)
		switch (cmd) {
		case SDHR_CMD_UPLOAD_DATA: {
			if (!CheckCommandLength(p, end, sizeof(UploadDataCmd))) return false;
			UploadDataCmd* cmd = (UploadDataCmd*)p;
			if (cmd->num_256b_pages > (256 - cmd->source_addr_med)) {
				CommandError("UploadData attempting to load past top of memory");
				return false;
			}
			uint64_t dest_offset = DataOffset(0, cmd->dest_addr_med, cmd->dest_addr_high);
			uint64_t data_size = (uint64_t)256 * cmd->num_256b_pages;
			if (!DataSizeCheck(dest_offset, data_size)) {
				std::cerr << "DataSizeCheck failed!" << std::endl;
				return false;
			}
			memcpy(uploaded_data_region + dest_offset, a2mem + ((uint16_t)cmd->source_addr_med * 256), data_size);
		} break;
		case SDHR_CMD_DEFINE_IMAGE_ASSET: {
			if (!CheckCommandLength(p, end, sizeof(DefineImageAssetCmd))) return false;
			DefineImageAssetCmd* cmd = (DefineImageAssetCmd*)p;
			uint64_t upload_start_addr = (uint64_t)cmd->upload_addr_high * 65536 + (uint64_t)cmd->upload_addr_med * 256;
			uint64_t upload_data_size = (uint64_t)cmd->upload_page_count * 256;
			if (upload_start_addr + upload_data_size > (uint64_t)256 * 256 * 256) {
				CommandError("DefineImageAsset attempting to reference past top of upload memory");
				return false;
			}
			ImageAsset* r = image_assets + cmd->asset_index;

			if (r->data != NULL) {
				stbi_image_free(r->data);
			}
			r->AssignByMemory(uploaded_data_region + upload_start_addr, upload_data_size);
			if (error_flag) {
				std::cerr << "AssignByMemory failed!" << std::endl;
				return false;
			}
			std::cout << "SDHR_CMD_UPLOAD_DATA: Success!" << std::endl;
		} break;
		case SDHR_CMD_DEFINE_IMAGE_ASSET_FILENAME: {
			std::cout << "SDHR_CMD_DEFINE_IMAGE_ASSET_FILENAME: Not Implemented." << std::endl;
			// NOT IMPLEMENTED
		} break;
		case SDHR_CMD_UPLOAD_DATA_FILENAME: {
			std::cout << "SDHR_CMD_UPLOAD_DATA_FILENAME: Not Implemented." << std::endl;
			// NOT IMPLEMENTED
		} break;
		case SDHR_CMD_DEFINE_TILESET: {
			if (!CheckCommandLength(p, end, sizeof(DefineTilesetCmd))) return false;
			DefineTilesetCmd* cmd = (DefineTilesetCmd*)p;
			uint16_t num_entries = cmd->num_entries;
			if (num_entries == 0) {
				num_entries = 256;
			}
			uint64_t load_data_size;
			load_data_size = (uint64_t)cmd->xdim * cmd->ydim * num_entries * 2;
			uint64_t data_region_offset = DataOffset(0, cmd->data_med, cmd->data_high);
			if (!DataSizeCheck(data_region_offset, load_data_size)) {
				return false;
			}
			uint8_t* source_p = uploaded_data_region + data_region_offset;
			ImageAsset* asset = image_assets + cmd->asset_index;
			DefineTileset(cmd->tileset_index, num_entries, cmd->xdim, cmd->ydim, asset, source_p);
			std::cout << "SDHR_CMD_DEFINE_TILESET: Success! " << (uint32_t)cmd->tileset_index << ';'<< (uint32_t)num_entries << std::endl;
		} break;
		case SDHR_CMD_DEFINE_TILESET_IMMEDIATE: {
			if (!CheckCommandLength(p, end, sizeof(DefineTilesetImmediateCmd))) return false;
			DefineTilesetImmediateCmd* cmd = (DefineTilesetImmediateCmd*)p;
			uint16_t num_entries = cmd->num_entries;
			if (num_entries == 0) {
				num_entries = 256;
			}
			uint64_t load_data_size;
			load_data_size = (uint64_t)num_entries * 4;
			if (message_length != sizeof(DefineTilesetImmediateCmd) + load_data_size) {
				CommandError("DefineTilesetImmediate data size mismatch");
				return false;
			}
			ImageAsset* asset = image_assets + cmd->asset_index;
			DefineTileset(cmd->tileset_index, num_entries, cmd->xdim, cmd->ydim, asset, cmd->data);
			std::cout << "SDHR_CMD_DEFINE_TILESET_IMMEDIATE: Success! " << (uint32_t)cmd->tileset_index << ';' << (uint32_t)num_entries << std::endl;
		} break;
		case SDHR_CMD_DEFINE_WINDOW: {
			if (!CheckCommandLength(p, end, sizeof(DefineWindowCmd))) return false;
			DefineWindowCmd* cmd = (DefineWindowCmd*)p;
			Window* r = windows + cmd->window_index;
			if (r->screen_xcount > screen_xcount) {
				CommandError("Window exceeds max x resolution");
				return false;
			}
			if (r->screen_ycount > screen_ycount) {
				CommandError("Window exceeds max y resolution");
				return false;
			}
			r->enabled = false;
			r->black_or_wrap = cmd->black_or_wrap;
			r->screen_xcount = cmd->screen_xcount;
			r->screen_ycount = cmd->screen_ycount;
			r->screen_xbegin = cmd->screen_xbegin;
			r->screen_ybegin = cmd->screen_ybegin;
			r->tile_xbegin = cmd->tile_xbegin;
			r->tile_ybegin = cmd->tile_ybegin;
			r->tile_xdim = cmd->tile_xdim;
			r->tile_ydim = cmd->tile_ydim;
			r->tile_xcount = cmd->tile_xcount;
			r->tile_ycount = cmd->tile_ycount;
			if (r->tilesets) {
				free(r->tilesets);
			}
			r->tilesets = (uint8_t*)malloc(r->tile_xcount * r->tile_ycount);
			if (r->tile_indexes) {
				free(r->tile_indexes);
			}
			r->tile_indexes = (uint8_t*)malloc(r->tile_xcount * r->tile_ycount);
			std::cout << "SDHR_CMD_DEFINE_WINDOW: Success! " 
				<< cmd->window_index << ';' << (uint32_t)r->tile_xcount << ';' << (uint32_t)r->tile_ycount << std::endl;
		} break;
		case SDHR_CMD_UPDATE_WINDOW_SET_BOTH: {
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowSetBothCmd))) return false;
			UpdateWindowSetBothCmd* cmd = (UpdateWindowSetBothCmd*)p;
			Window* r = windows + cmd->window_index;
			if ((uint64_t)cmd->tile_xbegin + cmd->tile_xcount > r->tile_xcount ||
				(uint64_t)cmd->tile_ybegin + cmd->tile_ycount > r->tile_ycount) {
				CommandError("tile update region exceeds tile dimensions");
				return false;
			}
			// full tile specification: tileset and index
			uint64_t data_size = (uint64_t)cmd->tile_xcount * cmd->tile_ycount * 2;
			if (data_size + sizeof(UpdateWindowSetBothCmd) != message_length) {
				CommandError("UpdateWindowSetBoth data size mismatch");
				return false;
			}
			uint8_t* sp = cmd->data;
			for (uint64_t tile_y = 0; tile_y < cmd->tile_ycount; ++tile_y) {
				uint64_t line_offset = (uint64_t)(cmd->tile_ybegin + tile_y) * r->tile_xcount + cmd->tile_xbegin;
				for (uint64_t tile_x = 0; tile_x < cmd->tile_xcount; ++tile_x) {
					uint8_t tileset_index = *sp++;
					uint8_t tile_index = *sp++;
					if (tileset_records[tileset_index].xdim != r->tile_xdim ||
						tileset_records[tileset_index].ydim != r->tile_ydim ||
						tileset_records[tileset_index].num_entries <= tile_index) {
						CommandError("invalid tile specification");
						return false;
					}
					r->tilesets[line_offset + tile_x] = tileset_index;
					r->tile_indexes[line_offset + tile_x] = tile_index;
				}
			}
			std::cout << "SDHR_CMD_UPDATE_WINDOW_SET_BOTH: Success!" << std::endl;
		} break;
		case SDHR_CMD_UPDATE_WINDOW_SET_UPLOAD: {
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowSetUploadCmd))) return false;
			UpdateWindowSetUploadCmd* cmd = (UpdateWindowSetUploadCmd*)p;
			Window* r = windows + cmd->window_index;
			if ((uint64_t)cmd->tile_xbegin + cmd->tile_xcount > r->tile_xcount ||
				(uint64_t)cmd->tile_ybegin + cmd->tile_ycount > r->tile_ycount) {
				CommandError("tile update region exceeds tile dimensions");
				return false;
			}
			// full tile specification: tileset and index
			uint64_t data_size = (uint64_t)cmd->tile_xcount * cmd->tile_ycount * 2;
			uint64_t data_offset = (uint64_t)cmd->upload_addr_high * 65536 + (uint64_t)cmd->upload_addr_med * 256;
			if (data_size + data_offset > sizeof(uploaded_data_region)) {
				CommandError("UploadWindowSetUpload attempting to read past top of upload memory");
				return false;
			}
			uint8_t* sp = uploaded_data_region + data_offset;
			for (uint64_t tile_y = 0; tile_y < cmd->tile_ycount; ++tile_y) {
				uint64_t line_offset = (uint64_t)(cmd->tile_ybegin + tile_y) * r->tile_xcount + cmd->tile_xbegin;
				for (uint64_t tile_x = 0; tile_x < cmd->tile_xcount; ++tile_x) {
					uint8_t tileset_index = *sp++;
					uint8_t tile_index = *sp++;
					if (tileset_records[tileset_index].xdim != r->tile_xdim ||
						tileset_records[tileset_index].ydim != r->tile_ydim ||
						tileset_records[tileset_index].num_entries <= tile_index) {
						CommandError("invalid tile specification");
						return false;
					}
					r->tilesets[line_offset + tile_x] = tileset_index;
					r->tile_indexes[line_offset + tile_x] = tile_index;
				}
			}
			std::cout << "SDHR_CMD_UPDATE_WINDOW_SET_UPLOAD: Success!" << std::endl;
		} break;
		case SDHR_CMD_UPDATE_WINDOW_SINGLE_TILESET: {
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowSingleTilesetCmd))) return false;
			UpdateWindowSingleTilesetCmd* cmd = (UpdateWindowSingleTilesetCmd*)p;
			Window* r = windows + cmd->window_index;
			if ((uint64_t)cmd->tile_xbegin + cmd->tile_xcount > r->tile_xcount ||
				(uint64_t)cmd->tile_ybegin + cmd->tile_ycount > r->tile_ycount) {
				CommandError("tile update region exceeds tile dimensions");
				return false;
			}
			// partial tile specification: index and palette, single tileset
			uint64_t data_size = (uint64_t)cmd->tile_xcount * cmd->tile_ycount;
			if (data_size + sizeof(UpdateWindowSingleTilesetCmd) != message_length) {
				CommandError("UpdateWindowSingleTileset data size mismatch");
				return false;
			}
			uint8_t* dp = cmd->data;
			for (uint64_t tile_y = 0; tile_y < cmd->tile_ycount; ++tile_y) {
				uint64_t line_offset = (cmd->tile_ybegin + tile_y) * r->tile_xcount + cmd->tile_xbegin;
				for (uint64_t tile_x = 0; tile_x < cmd->tile_xcount; ++tile_x) {
					uint8_t tile_index = *dp++;
					if (tileset_records[cmd->tileset_index].xdim != r->tile_xdim ||
						tileset_records[cmd->tileset_index].ydim != r->tile_ydim ||
						tileset_records[cmd->tileset_index].num_entries <= tile_index) {
						CommandError("invalid tile specification");
						return false;
					}
					r->tilesets[line_offset + tile_x] = cmd->tileset_index;
					r->tile_indexes[line_offset + tile_x] = tile_index;
				}
			}
			std::cout << "SDHR_CMD_UPDATE_WINDOW_SINGLE_TILESET: Success!" << std::endl;
		} break;
		case SDHR_CMD_UPDATE_WINDOW_SHIFT_TILES: {
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowShiftTilesCmd))) return false;
			UpdateWindowShiftTilesCmd* cmd = (UpdateWindowShiftTilesCmd*)p;
			Window* r = windows + cmd->window_index;
			if (cmd->x_dir < -1 || cmd->x_dir > 1 || cmd->y_dir < -1 || cmd->y_dir > 1) {
				CommandError("invalid tile shift");
				return false;
			}
			if (r->tile_xcount == 0 || r->tile_ycount == 0) {
				CommandError("invalid window for tile shift");
				return false;
			}
			if (cmd->x_dir == -1) {
				for (uint64_t y_index = 0; y_index < r->tile_ycount; ++y_index) {
					uint64_t line_offset = y_index * r->tile_xcount;
					for (uint64_t x_index = 1; x_index < r->tile_xcount; ++x_index) {
						r->tilesets[line_offset + x_index - 1] = r->tilesets[line_offset + x_index];
						r->tile_indexes[line_offset + x_index - 1] = r->tile_indexes[line_offset + x_index];
					}
				}
			}
			else if (cmd->x_dir == 1) {
				for (uint64_t y_index = 0; y_index < r->tile_ycount; ++y_index) {
					uint64_t line_offset = y_index * r->tile_xcount;
					for (uint64_t x_index = r->tile_xcount - 1; x_index > 0; --x_index) {
						r->tilesets[line_offset + x_index] = r->tilesets[line_offset + x_index - 1];
						r->tile_indexes[line_offset + x_index] = r->tile_indexes[line_offset + x_index - 1];
					}
				}
			}
			if (cmd->y_dir == -1) {
				for (uint64_t y_index = 1; y_index < r->tile_ycount; ++y_index) {
					uint64_t line_offset = y_index * r->tile_xcount;
					uint64_t prev_line_offset = line_offset - r->tile_xcount;
					for (uint64_t x_index = 0; x_index < r->tile_xcount; ++x_index) {
						r->tilesets[prev_line_offset + x_index] = r->tilesets[line_offset + x_index];
						r->tile_indexes[prev_line_offset + x_index] = r->tilesets[line_offset + x_index];
					}
				}
			}
			else if (cmd->y_dir == 1) {
				for (uint64_t y_index = r->tile_ycount - 1; y_index > 0; --y_index) {
					uint64_t line_offset = y_index * r->tile_xcount;
					uint64_t prev_line_offset = line_offset - r->tile_xcount;
					for (uint64_t x_index = 0; x_index < r->tile_xcount; ++x_index) {
						r->tilesets[line_offset + x_index] = r->tilesets[prev_line_offset + x_index];
						r->tile_indexes[line_offset + x_index] = r->tilesets[prev_line_offset + x_index];
					}
				}
			}
			std::cout << "SDHR_CMD_UPDATE_WINDOW_SHIFT_TILES: Success! " 
				<< (uint32_t)cmd->window_index << ';' << (uint32_t)cmd->x_dir << ';' << (uint32_t)cmd->y_dir << std::endl;
		} break;
		case SDHR_CMD_UPDATE_WINDOW_SET_WINDOW_POSITION: {
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowSetWindowPositionCmd))) return false;
			UpdateWindowSetWindowPositionCmd* cmd = (UpdateWindowSetWindowPositionCmd*)p;
			Window* r = windows + cmd->window_index;
			r->screen_xbegin = cmd->screen_xbegin;
			r->screen_ybegin = cmd->screen_ybegin;
			std::cout << "SDHR_CMD_UPDATE_WINDOW_SET_WINDOW_POSITION: Success! "
				<< (uint32_t)cmd->window_index << ';' << (uint32_t)cmd->screen_xbegin << ';' << (uint32_t)cmd->screen_ybegin << std::endl;
		} break;
		case SDHR_CMD_UPDATE_WINDOW_ADJUST_WINDOW_VIEW: {
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowAdjustWindowViewCommand))) return false;
			UpdateWindowAdjustWindowViewCommand* cmd = (UpdateWindowAdjustWindowViewCommand*)p;
			Window* r = windows + cmd->window_index;
			r->tile_xbegin = cmd->tile_xbegin;
			r->tile_ybegin = cmd->tile_ybegin;
			std::cout << "SDHR_CMD_UPDATE_WINDOW_ADJUST_WINDOW_VIEW: Success! "
				<< (uint32_t)cmd->window_index << ';' << (uint32_t)cmd->tile_xbegin << ';' << (uint32_t)cmd->tile_ybegin << std::endl;
		} break;
		case SDHR_CMD_UPDATE_WINDOW_ENABLE: {
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowEnableCmd))) return false;
			UpdateWindowEnableCmd* cmd = (UpdateWindowEnableCmd*)p;
			Window* r = windows + cmd->window_index;
			if (!r->tile_xcount || !r->tile_ycount) {
				CommandError("cannote enable empty window");
				return false;
			}
			r->enabled = cmd->enabled;
			std::cout << "SDHR_CMD_UPDATE_WINDOW_ENABLE: Success! "
				<< (uint32_t)cmd->window_index << std::endl;
		} break;
		default:
			CommandError("unrecognized command");
			return false;
		}
		p += message_length;
	}
	// we're ready to draw
	command_buffer.clear();
	return true;
}

void SDHRManager::DrawWindowsIntoBuffer(modeset_buf* framebuffer)
{
	// std::cout << "Entered DrawWindowsIntoBuffer" << std::endl;
	// Draw the windows into the passed-in framebuffer;
	uint32_t pixel_color_argb888 = 0;
	for (uint16_t window_index = 0; window_index < 256; ++window_index) {
		Window* w = windows + window_index;
		if (!w->enabled) {
			continue;
		}
		for (int64_t tile_y = w->tile_ybegin; tile_y < w->tile_ybegin + (int64_t)w->screen_ycount; ++tile_y) {
			uint64_t tile_yindex = tile_y / w->tile_ydim;
			uint64_t tile_yoffset = tile_y % w->tile_ydim;
			for (int64_t tile_x = w->tile_xbegin; tile_x < w->tile_xbegin + (int64_t)w->screen_xcount; ++tile_x) {
				uint64_t tile_xindex = tile_x / w->tile_xdim;
				uint64_t tile_xoffset = tile_x % w->tile_xdim;
				uint64_t entry_index = tile_yindex * w->tile_xcount + tile_xindex;
				TilesetRecord* t = tileset_records + w->tilesets[entry_index];
				uint64_t tile_index = w->tile_indexes[entry_index];
				uint16_t pixel_color;
				if (w->black_or_wrap == 0 &&
					(tile_yindex < 0 || tile_yindex >= w->tile_ycount ||
						tile_xindex < 0 || tile_xindex >= w->tile_xcount)) {
					pixel_color = 0x8000; // outside of tile bounds, pixel is black
				}
				else {
					while (tile_yindex < 0) tile_yindex += w->tile_ycount;
					while (tile_yindex >= w->tile_ycount) tile_yindex -= w->tile_ycount;
					while (tile_xindex < 0) tile_xindex -= w->tile_xcount;
					while (tile_xindex >= w->tile_xcount) tile_xindex -= w->tile_xcount;
					pixel_color = t->tile_data[tile_index * t->xdim * t->ydim + tile_yoffset * t->xdim + tile_xoffset];
				}
				if ((pixel_color & 0x8000) == 0) {
					continue; // zero alpha, don'd draw
				}
				// now, where on the screen to put it?
				int64_t screen_y = tile_y + w->screen_ybegin - w->tile_ybegin;
				int64_t screen_x = tile_x + w->screen_xbegin - w->tile_xbegin;
				if (screen_x < 0 || screen_y < 0 || screen_x > screen_xcount || screen_y > screen_ycount) {
					// destination pixel offscreen, do not draw
					continue;
				}
				pixel_color_argb888 = ARGB555_to_ARGB888(pixel_color);
				// *(uint32_t*)&framebuffer->map[screen_offset] = pixel_color_argb888;
				int64_t screen_offset = ((framebuffer->stride * screen_y) + (screen_x * sizeof(uint32_t)));
				// Scale 3x
				for (size_t i = 0; i < 3; i++)
				{
					for (size_t j = 0; j < 3; j++)
					{
						*(uint32_t*)&framebuffer->map[(screen_offset * 3) + (i * sizeof(uint32_t)) + (j * framebuffer->stride)] = pixel_color_argb888;
					}
				}
			}
		}
		// std::cout << "Drew into buffer window " << window_index << std::endl;
	}
}
