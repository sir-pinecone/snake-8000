#if !defined(WIN32_SNAKE_GAME_H)

struct Win32OffscreenBuffer {
  // NOTE: Pixels are always 32-bit wide, Little Endian 0x XX RR GG BB
  //  memory order BB GG RR XX
  BITMAPINFO info;
  void *memory;
  int32 bytes_per_pixel;
  int32 width;
  int32 height;
  int32 pitch; // the width of buffer in bytes (also known as stride)
};

struct Win32WindowDimension {
  int32 width;
  int32 height;
};

struct win32_sound_output {
  int32 samples_per_second;
  uint32 running_sample_index; // don't wrap into negative values
  int8 num_channels; // Mono or stereo or CRAZY??
  int8 bits_per_channel_sample;  // CD quality
  int32 bytes_per_sample;
  DWORD secondary_buffer_size;
  DWORD safety_bytes; // How much variability we think there is in our output timing
  // TODO math gets simpler if we add a bytes per second field
  // TODO should running smaple index be in bytes as well?
};

struct Win32DebugAudioTimeMarker {
  DWORD expected_flip_play_cursor;
  DWORD flip_play_cursor;
  DWORD flip_write_cursor;
  DWORD output_play_cursor;
  DWORD output_write_cursor;
  DWORD output_location;
  DWORD output_byte_count;
};

struct Win32GameCode {
  HMODULE game_code_dll;
  FILETIME dll_last_compile_time;

  // IMPORTANT: either of the callbacks can be 0. You must check before calling.
  game_update_and_render *UpdateAndRender;
  game_get_sound_samples *GetSoundSamples;

  bool32 is_valid;
};

#define WIN32_STATE_FILE_NAME_COUNT MAX_PATH
struct Win32ReplayBuffer {
  HANDLE file_handle;
  HANDLE memory_map;
  char filename[WIN32_STATE_FILE_NAME_COUNT];
  void *memory_block;
};

struct Win32PlatformState {
  uint64 total_size;
  void *game_store_block;
  Win32ReplayBuffer replay_buffers[4];

  HANDLE recording_handle;
  int input_recording_index;

  HANDLE playback_handle;
  int input_playback_index;

  char exe_filename[WIN32_STATE_FILE_NAME_COUNT];
  char *one_past_last_exe_filename_slash;
};

struct Win32InputSnapshot {
  uint32 vk_code;
  bool32 is_down;
  bool32 was_down;
  bool32 alt_is_down;
};

#define WIN32_SNAKE_GAME_H
#endif
