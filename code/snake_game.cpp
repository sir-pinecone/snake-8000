/* Platform independent layer API
 *
 * This is loaded as a DLL so that code changes can be reloaded while the game runs.
 * Make sure to not include anything static in the DLL. Put state in the game memory.
 */

#include "snake_game.h"

void GameOutputSound(game_state *state, game_sound_output_buffer *sound_buffer, int32 tone_hz) {
  int16 tone_volume = 1000;
  // Middle c note - 256 cycles (1 complete pattern of the wave, from one trough to the
  // next) / per second
  int32 wave_period = sound_buffer->samples_per_second / tone_hz;
  int16 *samples = sound_buffer->samples;

  for (int32 sample_idx = 0;
      sample_idx < sound_buffer->sample_count;
      ++sample_idx) {
#if 0
    real32 sine_value = sinf(state->t_sine);
    int16 sample_value = (int16)(sine_value * tone_volume);
    *samples++ = sample_value;
    *samples++ = sample_value;

    // Change by 1 samples worth over the wave period. Allows for smooth frequency
    // changes without weird audible sound spike
    state->t_sine += (2.0f * Pi32) / (real32)wave_period;
    if (state->t_sine > 2.0f * Pi32) {
      state->t_sine -= 2.0 * Pi32;
    }
#endif
  }
}

void RenderWeirdGradient(game_offscreen_buffer* buffer,
                         int32 blue_offset, int32 green_offset, int32 red_offset) {
  // Points to a single byte memory address, starting with the head of the buffer
  // We use bytes because the pointer moves down a row by shifting by the pitch, which
  // is the buffer width in bytes
  uint8 *row = (uint8 *)buffer->memory;
  for (int32 y = 0; y < buffer->height; ++y) {
    uint32 *pixel = (uint32*)row; // Get a 4-byte pixel pointer (using 32-bit RGB)
    for (int32 x = 0; x < buffer->width; ++x) {
      // 8 bits can go to 255 (for color); wraps to 0 when it overflows
      // Creates a block effect
      uint32 blue = x / 12 + blue_offset;
      uint32 green = y / 12 + green_offset;
      /* Create blocks by dividing y and y by some value (e.g. 80) and then multiply
       * the result */
      uint32 red = (x / 40) * (y / 40);
      // Set value then increment by 4-bytes (size of pixel)
      *pixel++ = RGBColor(red, green, blue);
    }
    row += buffer->pitch; // Move the pointer to the start of the next row
  }
}

int GetTileIndex(int x, int y, int width, int height) {
  Assert(x > 0 && y > 0);
  int x_part = (x - 1) % width;
  int y_part = (y - 1) % height;
  int idx = x_part + (width * y_part);
  return idx;
}

int GetTilePixel(int tile_n, int num_tiles, int tile_size) {
  Assert(tile_n > 0 && num_tiles > 0);
  return ((tile_n - 1) % num_tiles) * tile_size;
}

void RenderGrid(game_offscreen_buffer* buffer, int grid_item_size) {
  uint8 *row = (uint8 *)buffer->memory;
  for (int32 y = 0; y < buffer->height; ++y) {
    uint32 *pixel = (uint32*)row; // Get a 4-byte pixel pointer (using 32-bit RGB)
    for (int32 x = 0; x < buffer->width; ++x) {
      *pixel++ = RGBColor(255,150,200);
    }
    row += buffer->pitch; // Move the pointer to the start of the next row
  }
}

direction * GetSnakePieceDir(snake_state *snake, int index) {
  Assert(index < ArrayCount(snake->parts));
  return &snake->parts[index];
}

void ExtendSnake(snake_state *snake) {
  // Adds a new part to the tail of the snake. Uses the direction of its sibling.
  if (snake->length < ArrayCount(snake->parts)) {
    snake->parts[snake->length++] = *GetSnakePieceDir(snake, snake->length - 1);
  }
}

void ChangeSnakeDirection(snake_state *snake, direction new_dir) {
  snake->new_direction = new_dir;
}

void RenderSnake(game_offscreen_buffer *buffer, game_state *state) {
  uint8 *end_of_buffer = (uint8 *)buffer->memory + (buffer->height * buffer->pitch);
  snake_state *snake = &state->snake;

  int start_x = snake->head_tile_x;
  int start_y = snake->head_tile_y;
  int prev_direction = NONE;

  for (int piece_idx = 0; piece_idx < 1; ++piece_idx) { //snake->length; ++piece_idx) {
    direction *piece_dir = GetSnakePieceDir(snake, piece_idx);
    if (piece_dir) {
      // we can get the side to draw on by looking at the previous direct and then drawing
      // at the opposing edge.
      int x_start = (prev_direction == NONE) ? start_x : start_x; // TODO get tile in opposite direction of previous_dir
      int y_start = (prev_direction == NONE) ? start_y : start_y; // TODO get tile in opposite direction of previous_dir
      int x_pixel = GetTilePixel(x_start, state->num_tiles_x, state->tile_size);
      int y_pixel = GetTilePixel(y_start, state->num_tiles_y, state->tile_size);

      for (int x = x_pixel; x < x_pixel + state->tile_size; ++x) {
        uint8 *pixel = (uint8 *)buffer->memory +
                       (x * buffer->bytes_per_pixel) +
                       (y_pixel * buffer->pitch);

        for (int y = y_pixel; y < y_pixel + state->tile_size; ++y) {
          if ((pixel >= buffer->memory) && ((pixel + 4) <= end_of_buffer)) {
            *(uint32 *)pixel = RGBColor(110,255,180);
          }
          pixel += buffer->pitch;
        }
      }
    }
  }
}

void
MoveSnakePart(snake_part *part) {
  int default_speed = 4;
  int new_x = part->x;
  int new_y = part->y;
  switch(part->dir) {
    case NORTH: {
      new_y -= default_speed;
    } break;

    case EAST: {
      new_x += default_speed;
    } break;

    case SOUTH: {
      new_y += default_speed;
    } break;

    case WEST: {
      new_x -= default_speed;
    } break;
  }
  part->x = new_x;
  part->y = new_y;
}

void UpdateSnake(game_offscreen_buffer *buffer, snake_state *snake) {
  /*
  if (snake->y + snake->block_size >= buffer->height) {
    snake->dir = NORTH;
  }
  else if (snake->y <= 0) {
    snake->dir = SOUTH;
  }
  else if (snake->x <= 0) {
    snake->dir = EAST;
  }
  else if (snake->x + (snake->block_size * snake->length) >= buffer->width) {
    snake->dir = WEST;
  }
  */

  // Shift the directions
  for (int piece_idx = snake->length - 1; piece_idx > 0; --piece_idx) {
    direction *current = GetSnakePieceDir(snake, piece_idx);
    direction *next = GetSnakePieceDir(snake, piece_idx - 1);
    *current = *next;
    // TODO MoveSnakePart(part);
  }

  direction *head = GetSnakePieceDir(snake, 0);
  *head = snake->new_direction;
  snake->new_direction = NONE;
  // TODO MoveSnakePart(head);
}

void ProcessInput(game_input *input, game_state *state) {
  for (int controller_idx = 0;
      controller_idx < ArrayCount(input->controllers);
      ++controller_idx) {
    game_controller_input *controller = GetController(input, controller_idx);

    if (controller->is_analog) {
      /* NOTE:  Use analog movement tuning */
      state->blue_offset += (int32)(4.0f * controller->stick_avg_x);
      state->tone_hz = 220 + (int32)(128.0f * controller->stick_avg_y);
    }
    else {
      snake_state *snake = &state->snake;

      /* NOTE: Use digital movement tuning */
      if (controller->move_left.ended_down) {
        state->blue_offset -= 1;
        ChangeSnakeDirection(snake, WEST);
      }

      if (controller->move_right.ended_down) {
        state->blue_offset += 1;
        ChangeSnakeDirection(snake, EAST);
      }

      if (controller->move_up.ended_down) {
        state->green_offset -= 1;
        ChangeSnakeDirection(snake, NORTH);
      }

      if (controller->move_down.ended_down) {
        state->green_offset += 1;
        ChangeSnakeDirection(snake, SOUTH);
      }

      if (controller->right_shoulder.ended_down && snake->length < ArrayCount(snake->parts)) {
        ExtendSnake(snake);
      }
      else if (controller->left_shoulder.ended_down && snake->length > 1) {
        snake->length--;
      }
    }

    if (controller->action_down.ended_down) {
      state->red_offset += 1;
    }

    //state->snake.x += (int32)(4.0f * controller->stick_avg_x);
    //state->snake.y -= (int32)(4.0f * controller->stick_avg_y);
  }
}

// ---------------------------------------------------------------------------------------
// Game services for the platform layer
// ---------------------------------------------------------------------------------------

extern "C" GAME_UPDATE_AND_RENDER(GameUpdateAndRender) {
  Assert((&input->controllers[0].terminator - &input->controllers[0].buttons[0]) ==
         ArrayCount(input->controllers[0].buttons));
  Assert(sizeof(game_state) <= memory->permanent_storage_size);

  game_state *state = (game_state *)memory->permanent_storage;
  snake_state snake = {};

  if (!memory->is_initialized) {
    char *filename = __FILE__;

    debug_read_file_result file = memory->DEBUGPlatformReadEntireFile(thread, filename);
    if (file.content) {
      memory->DEBUGPlatformWriteEntireFile(thread, "test.out", file.content_size, file.content);
      memory->DEBUGPlatformFreeFileMemory(thread, file.content);
    }

    state->game_width = screen_buffer->width;
    state->game_height = screen_buffer->height;
    state->tile_size = 25;
    state->num_tiles_x = (int)(state->game_width / state->tile_size);
    state->num_tiles_y = (int)(state->game_height / state->tile_size);

    snake.new_direction = NONE;

    // Start with a head
    snake.length = 1;
    // TODO pick random starting pos
    snake.head_tile_x = 8;
    snake.head_tile_y = 16;

    snake.parts[0] = EAST;

    state->snake = snake;

    state->tone_hz = 220;
    state->t_sine = 0.0f;
    state->red_offset = 1;

    // TODO this may be more appropriate to do in the platform layer
    memory->is_initialized = true;
  }

  ProcessInput(input, state);
  RenderWeirdGradient(screen_buffer, state->blue_offset, state->green_offset, state->red_offset);
  UpdateSnake(screen_buffer, &state->snake);
  RenderSnake(screen_buffer, state);
}

// extern "C" tells the compiler to use the old C naming process which will preserve the
// function name. This is needed in order for us to call the function from a DLL.
extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples) {
  game_state *state = (game_state *)memory->permanent_storage;
  GameOutputSound(state, sound_buffer, state->tone_hz);
}
