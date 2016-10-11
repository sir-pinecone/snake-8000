/* Platform independent layer API
 *
 * This is loaded as a DLL so that code changes can be reloaded while the game runs.
 * Make sure to not include anything static in the DLL. Put state in the game memory.
 */

#include "handmade.h"

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

void RenderSnakeParts(game_offscreen_buffer *buffer, snake_state *snake) {
  uint8 *end_of_buffer = (uint8 *)buffer->memory + (buffer->height * buffer->pitch);
  for (int part_idx = 0; part_idx < ArrayCount(snake->parts); ++part_idx) {
    snake_part *part = &snake->parts[part_idx];
    if (part) {
      for (int x = part->x; x < part->x + snake->block_size; ++x) {
        uint8 *pixel = (uint8 *)buffer->memory +
                       (x * buffer->bytes_per_pixel) +
                       (part->y * buffer->pitch);

        for (int y = part->y; y < part->y + snake->block_size; ++y) {
          if ((pixel >= buffer->memory) && ((pixel + 4) <= end_of_buffer)) {
            *(uint32 *)pixel = RGBColor(110,255,180);
          }
          pixel += buffer->pitch;
        }
      }
    }
  }
}

void UpdateSnake(game_offscreen_buffer *buffer, snake_state *snake) {
  int default_speed = 4;

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

  int new_x = snake->x;
  int new_y = snake->y;
  switch(snake->dir) {
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
  snake->x = new_x;
  snake->y = new_y;
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

    snake.x = 300;
    snake.y = 300;
    snake.length = 2;
    snake.block_size = 25;
    snake.dir = EAST;

    for (int part_idx = 0;
        part_idx < ArrayCount(snake.parts);
        ++part_idx) {
      snake_part *part = &snake.parts[part_idx];
      // TODO pick random starting pos
      part->x = part_idx * (snake.block_size + 4) + 200;
      part->y = 200;
      part->dir = EAST;
    }

    state->snake = snake;

    state->tone_hz = 220;
    state->t_sine = 0.0f;
    state->red_offset = 1;

    // TODO this may be more appropriate to do in the platform layer
    memory->is_initialized = true;
  }

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
      /* NOTE: Use digital movement tuning */
      if (controller->move_left.ended_down) {
        state->blue_offset -= 1;
        state->snake.dir = WEST;
      }

      if (controller->move_right.ended_down) {
        state->blue_offset += 1;
        state->snake.dir = EAST;
      }

      if (controller->move_up.ended_down) {
        state->green_offset -= 1;
        state->snake.dir = NORTH;
      }

      if (controller->move_down.ended_down) {
        state->green_offset += 1;
        state->snake.dir = SOUTH;
      }

      if (controller->left_shoulder.ended_down) {
        state->snake.length += 1;
      }
      else if (controller->right_shoulder.ended_down && state->snake.length > 1) {
        state->snake.length -= 1;
      }
    }

    if (controller->action_down.ended_down) {
      state->red_offset += 1;
    }

    //state->snake.x += (int32)(4.0f * controller->stick_avg_x);
    //state->snake.y -= (int32)(4.0f * controller->stick_avg_y);
  }

  RenderWeirdGradient(screen_buffer, state->blue_offset, state->green_offset, state->red_offset);
  UpdateSnake(screen_buffer, &state->snake);
  RenderSnakeParts(screen_buffer, &state->snake);
}

// extern "C" tells the compiler to use the old C naming process which will preserve the
// function name. This is needed in order for us to call the function from a DLL.
extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples) {
  game_state *state = (game_state *)memory->permanent_storage;
  GameOutputSound(state, sound_buffer, state->tone_hz);
}
