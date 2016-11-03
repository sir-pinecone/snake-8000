#if !defined(SNAKE_GAME_H)

/*
 * NOTE: Build flags
 *
 * SNAKE_INTERNAL:
 *  0 - Build for public release
 *  1 - Build for developer only
 *
 * SNAKE_SLOW:
 *  0 - No slow code allowed! Frick off, Rick.
 *  1 - Slow code is a-ok. Smokes, let's go.
 */

#include <stdint.h>
#include <math.h> // TODO implement sine ourselves

#define internal static
#define local_persist static
#define global_variable static

#define Pi32 3.141592653589f

// TODO possibly switch to int_fast<num>_t types. What are the implications?
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef int32 bool32;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef float real32;
typedef double real64;

// ---------------------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------------------

// TODO make this assert better
#if SNAKE_SLOW
#define Assert(expression) if (!(expression)) { *(int *)0 = 0; }
#else
#define Assert(expression)
#endif

// TODO: Should these always use 64-bit?
#define Kilobytes(value) ((value) * 1024LL)
#define Megabytes(value) (Kilobytes(value) * 1024LL)
#define Gigabytes(value) (Megabytes(value) * 1024LL)
#define Terabytes(value) (Gigabytes(value) * 1024LL)

// NOTE: we wrap `Array` before getting 0 element so that we can support an expr being passed in
#define ArrayCount(array) (sizeof(array) / sizeof((array)[0]))

#define Min(a, b) ((a) < (b) ? (a) : (b))
#define Max(a, b) ((a) > (b) ? (a) : (b))

inline uint32
SafeTruncateUInt64(uint64 value) {
  // TODO add defines for max values
  Assert(value <= 0xFFFFFFFF);
  uint32 result = (uint32)value;
  return result;
}

/* Returns a uint32 color value made up of red, green and blue channels.
 * The channels should be in the 0 - 255 range. If they are larger then the value will
 * wrap to 0.
 */
inline uint32
RGBColor(int32 r, int32 g, int32 b) {
  uint8 alpha = 255; // TODO: doesn't seem to work when bit shifted in by 24
  return ((uint8)r << 16) | ((uint8)g << 8) | (uint8)b;
}

inline int
StrLen(char *string) {
  int c = 0;
  while(*string++) {
    ++c;
  }
  return c;
}

inline void
ConcatStr(char *source_a, size_t source_a_count,
          char *source_b, size_t source_b_count,
          char *dest, size_t dest_count) {
  for (int i = 0; i <  source_a_count; i++) {
    *dest++ = *source_a++;
  }
  for (int i = 0; i <  source_b_count; i++) {
    *dest++ = *source_b++;
  }
  *dest++ = 0;
}

// Will help provide info on the thread we're running in when in a multi-threaded env.
// Not all platforms do a good job supplying this info so we'll manage it ourselves.
struct thread_context {
  int placeholder;
};

// TODO: swap as a macro??

// ---------------------------------------------------------------------------------------
// Services that the platform layer provides to the game
// ---------------------------------------------------------------------------------------

#if SNAKE_INTERNAL

/* IMPORTANT:
 *
 * These are not for doing anything in the shipping game - they are blocking and the write
 * doesn't protect against lost data!!!
 */
struct debug_read_file_result {
  uint32 content_size;
  void *content;
};

// We define the types of these functions and use macros to define something of that type
// so that they can be used inside the platform layer. We expect that platform layer to
// provide pointers to the implementation of these functions (via game memory struct).

#define DEBUG_PLATFORM_READ_ENTIRE_FILE(name) debug_read_file_result name(thread_context *thread, char *filename)
typedef DEBUG_PLATFORM_READ_ENTIRE_FILE(debug_platform_read_entire_file);

#define DEBUG_PLATFORM_WRITE_ENTIRE_FILE(name) bool32 name(thread_context *thread, char *filename, uint32 memory_size, void *memory)
typedef DEBUG_PLATFORM_WRITE_ENTIRE_FILE(debug_platform_write_entire_file);

#define DEBUG_PLATFORM_FREE_FILE_MEMORY(name) void name(thread_context *thread, void *memory)
typedef DEBUG_PLATFORM_FREE_FILE_MEMORY(debug_platform_free_file_memory);

#endif

// ---------------------------------------------------------------------------------------
// Services that the game provides to the platform layer.
// (this may expand in the future - sound on separate thread, etc.)
// ---------------------------------------------------------------------------------------

struct game_offscreen_buffer {
  /* NOTE: Pixels are always 32-bit wide, Little Endian 0x XX RR GG BB
   * memory order BB GG RR XX
   */
  void *memory;
  int32 width;
  int32 height;
  int32 pitch;
  int bytes_per_pixel;
};

struct game_sound_output_buffer {
  int32 samples_per_second;
  int32 sample_count;
  int16 *samples;
};

struct game_button_state {
  int32 half_transition_count;
  bool32 ended_down;
};

struct game_controller_input {
  bool32 is_connected;
  bool32 is_analog;
  bool32 is_y_inverted;

  // We use the average value over a frame to determine if move action state should be set.
  real32 stick_avg_x;
  real32 stick_avg_y;

  // Use a union so that buttons[] uses the same memory as the struct. We can then do
  // either buttons[0] or move_up, buttons[1] or move_down, etc.
  union {
    game_button_state buttons[12];
    struct {
      game_button_state move_up;
      game_button_state move_down;
      game_button_state move_left;
      game_button_state move_right;

      game_button_state action_up;
      game_button_state action_down;
      game_button_state action_left;
      game_button_state action_right;

      game_button_state left_shoulder;
      game_button_state right_shoulder;

      game_button_state start;
      game_button_state back;

      /* NOTE: All buttons must be added above this line. */

      // TODO: Might want to just name this struct in order to compare its size against the
      // size of the buttons array. As it stands, this terminator approach is janky.
      game_button_state terminator;
    };
  };
};

struct game_input {
  game_button_state mouse_buttons[5];
  int32 mouse_x, mouse_y, mouse_z;
  real32 dt_for_frame;

  // TODO insert clock values here
  game_controller_input controllers[5];
};

inline game_controller_input *GetController(game_input *input, int controller_idx) {
  Assert(controller_idx < ArrayCount(input->controllers));
  game_controller_input *result = &input->controllers[controller_idx];
  return result;
}

struct game_memory {
  bool32 is_initialized;

  uint64 rand_seed;
  uint64 rand_rounds;

  uint64 permanent_storage_size;
  void *permanent_storage; /* NOTE: REQUIRED to be cleared to zero at startup */

  uint64 temp_storage_size;
  void *temp_storage; /* NOTE: REQUIRED to be cleared to zero at startup */

  // Almost like our own little vtable.
  debug_platform_read_entire_file *DEBUGPlatformReadEntireFile;
  debug_platform_write_entire_file *DEBUGPlatformWriteEntireFile;
  debug_platform_free_file_memory *DEBUGPlatformFreeFileMemory;
};

// TODO: needs four things: controller/keyboard input, bitmap buffer to use, sound buffer and timing
#define GAME_UPDATE_AND_RENDER(name) void name(thread_context *thread, game_memory *memory, game_input *input, game_offscreen_buffer *screen_buffer)
typedef GAME_UPDATE_AND_RENDER(game_update_and_render);

// NOTE: at the moment, this has to be a very fast function. Should return in < ~1ms.
// TODO: reduce the pressure on this function's performance by measuring it or asking about it
#define GAME_GET_SOUND_SAMPLES(name) void name(thread_context *thread, game_memory *memory, game_sound_output_buffer *sound_buffer)
typedef GAME_GET_SOUND_SAMPLES(game_get_sound_samples);

//
//
//
//

enum direction {NONE, NORTH, EAST, SOUTH, WEST};

struct snake_piece {
  direction dir;
  int x;
  int y;
};

struct dir_change_record {
  direction dir;
  int x;
  int y;
};

struct snake_state {
  int length;
  bool32 alive;
  direction new_direction;
  dir_change_record dir_recordings[2000];
  int num_dir_recordings;
  snake_piece pieces[200];
};

struct snake_food {
  int x;
  int y;
};

/* NOTE: might relocate this later since the platform layer doesn't need to know about it at all */
 struct game_state {
  snake_state snake;
  snake_food foods[10];
  int num_foods;
  bool32 do_game_reset;

  int32 tone_hz;
  int32 blue_offset;
  int32 green_offset;
  int32 red_offset;

  int game_width;
  int game_height;
  int tile_size; // treated as a square
  int num_tiles_x;
  int num_tiles_y;
  real32 snake_update_timer;
  real32 t_sine;
};

#define SNAKE_GAME_H
#endif
