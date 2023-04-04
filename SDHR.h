enum ShdrCmd_e {
	SDHR_CMD_UPLOAD_DATA = 1,
	SDHR_CMD_DEFINE_IMAGE_ASSET = 2,
	SDHR_CMD_DEFINE_IMAGE_ASSET_FILENAME = 3,
	SDHR_CMD_DEFINE_TILESET = 4,
	SDHR_CMD_DEFINE_TILESET_IMMEDIATE = 5,
	SDHR_CMD_DEFINE_WINDOW = 6,
	SDHR_CMD_UPDATE_WINDOW_SET_BOTH = 7,
	SDHR_CMD_UPDATE_WINDOW_SINGLE_TILESET = 8,
	SDHR_CMD_UPDATE_WINDOW_SHIFT_TILES = 9,
	SDHR_CMD_UPDATE_WINDOW_SET_WINDOW_POSITION = 10,
	SDHR_CMD_UPDATE_WINDOW_ADJUST_WINDOW_VIEW = 11,
	SDHR_CMD_UPDATE_WINDOW_SET_BITMASKS = 12,
	SDHR_CMD_UPDATE_WINDOW_ENABLE = 13,
	SDHR_CMD_READY = 14,
	SDHR_CMD_UPLOAD_DATA_FILENAME = 15,
	SDHR_CMD_UPDATE_WINDOW_SET_UPLOAD = 16,
};
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
	uint8_t data[];  // data is 4-byte records, 16-bit x and y offsets (scaled by x/ydim), from the given asset
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
	uint8_t data[];  // data is 2-byte records per tile, tileset and index
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
	uint8_t data[];  // data is 1-byte record per tile, index on the given tileset
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