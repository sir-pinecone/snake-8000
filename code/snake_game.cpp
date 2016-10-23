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

void
DrawBlock(game_offscreen_buffer* buffer, uint32 color,
          int x_start, int y_start, int block_size) {
  uint8 *end_of_buffer = (uint8 *)buffer->memory + (buffer->height * buffer->pitch);
  for (int x = x_start; x < x_start + block_size; ++x) {
    uint8 *pixel = (uint8 *)buffer->memory +
                   (x * buffer->bytes_per_pixel) +
                   (y_start * buffer->pitch);
    for (int y = y_start; y < y_start + block_size; ++y) {
      Assert((pixel >= buffer->memory) && ((pixel + block_size) <= end_of_buffer));
      *(uint32 *)pixel = color;
      pixel += buffer->pitch;
    }
  }
}

void RenderGrid(game_offscreen_buffer* buffer, game_state *state) {
  for (int y = 1; y <= state->num_tiles_y; ++y) {
    int y_pixel = GetTilePixel(y, state->num_tiles_y, state->tile_size);
    for (int x = 1; x <= state->num_tiles_x; ++x) {
      int x_pixel = GetTilePixel(x, state->num_tiles_x, state->tile_size);
      DrawBlock(buffer, RGBColor(200, 200, 200), x_pixel, y_pixel, state->tile_size);
    }
  }
}

snake_piece * GetSnakePiece(snake_state *snake, int index) {
  Assert(index < ArrayCount(snake->pieces));
  return &snake->pieces[index];
}

snake_piece * GetSnakeHead(snake_state *snake) {
  return GetSnakePiece(snake, 0);
}

void MoveSnakePiece(snake_piece *piece, direction in_direction) {
  switch(in_direction) {
    case NORTH: {
      piece->y--;
    } break;

    case SOUTH: {
      piece->y++;
    } break;

    case EAST: {
      piece->x++;
    } break;

    case WEST: {
      piece->x--;
    } break;
  }
}

void ExtendSnake(snake_state *snake, direction in_direction) {
  Assert((snake->length - 1) >= 0);
  if (snake->length < ArrayCount(snake->pieces)) {
    snake_piece *last = GetSnakePiece(snake, snake->length - 1);
    snake_piece new_piece;
    new_piece.x = last->x;
    new_piece.y = last->y;
    // We want to place the piece in the correct spot
    MoveSnakePiece(&new_piece, in_direction);
    new_piece.dir = last->dir;
    snake->pieces[snake->length++] = new_piece;
  }
}

void ChangeSnakeDirection(snake_state *snake, direction new_dir) {
  snake_piece *head = GetSnakeHead(snake);
  if (head->dir != new_dir && snake->new_direction != new_dir) {
    snake->new_direction = new_dir;
    // Record the path change
    Assert(snake->num_dir_recordings < ArrayCount(snake->dir_recordings));
    dir_change_record record = {};
    record.dir = new_dir;
    record.x = head->x;
    record.y = head->y;
    snake->dir_recordings[snake->num_dir_recordings++] = record;
  }
}

// TODO LYNDA create an x and y version of GetTilePixel()
void RenderRecordingSpot(game_offscreen_buffer *buffer, game_state *state) {
  uint32 color = RGBColor(0, 255, 255);
  snake_state *snake = &state->snake;
  for (int idx = 0; idx < snake->num_dir_recordings; ++idx) {
    dir_change_record *record = &snake->dir_recordings[idx];
    int x_pixel = GetTilePixel(record->x, state->num_tiles_x, state->tile_size);
    int y_pixel = GetTilePixel(record->y, state->num_tiles_y, state->tile_size);
    DrawBlock(buffer, color, x_pixel, y_pixel, state->tile_size);
  }
}

void RenderFood(game_offscreen_buffer *buffer, game_state *state) {
  snake_food *food = &state->foods[0];
  if (food) {
    uint32 color = RGBColor(0, 255, 0);
    int x_pixel = GetTilePixel(food->x, state->num_tiles_x, state->tile_size);
    int y_pixel = GetTilePixel(food->y, state->num_tiles_y, state->tile_size);
    DrawBlock(buffer, color, x_pixel, y_pixel, state->tile_size);
  }
}

void RenderSnake(game_offscreen_buffer *buffer, game_state *state) {
  snake_state *snake = &state->snake;
  snake_piece *head = GetSnakeHead(snake);
  uint32 color = snake->alive ? RGBColor(20, 90, 255) : RGBColor(255, 0, 0);
  for (int piece_idx = 0; piece_idx < snake->length; ++piece_idx) { //snake->length; ++piece_idx) {
    snake_piece *piece = GetSnakePiece(snake, piece_idx);
    if (piece) {
      int x_pixel = GetTilePixel(piece->x, state->num_tiles_x, state->tile_size);
      int y_pixel = GetTilePixel(piece->y, state->num_tiles_y, state->tile_size);
      DrawBlock(buffer, color, x_pixel, y_pixel, state->tile_size);
    }
  }
}

void UpdateSnake(game_offscreen_buffer *buffer, game_state *state) {
  // TODO fix snek bounce at bottom of screen ... or not? could just kill him
  // TODO decrease step speed as snake length increases
  if (state->snake_update_timer > 0.2f) {
    state->snake_update_timer = 0;

    snake_state *snake = &state->snake;
    snake_piece *head = GetSnakeHead(snake);

    if (snake->new_direction != NONE) {
      head->dir = snake->new_direction;
      snake->new_direction = NONE;
    }

    // Move the pieces
    for (int piece_idx = 0; piece_idx < snake->length; ++piece_idx) {
      snake_piece *piece = GetSnakePiece(snake, piece_idx);
      MoveSnakePiece(piece, piece->dir);
      // TODO use the index to decide how many direction recordings to check.
      // Look at oldest recording first.
      // For now we do a simple check all loop
      bool32 last_piece = piece_idx == snake->length - 1;
      for (int idx = 0; idx < snake->num_dir_recordings; ++idx) {
        dir_change_record *record = &snake->dir_recordings[idx];
        if (piece->x == record->x && piece->y == record->y) {
          piece->dir = record->dir;

          if (last_piece) {
            // Shift recordings
            for (int i = 0; i < snake->num_dir_recordings - 1; ++i) {
              snake->dir_recordings[i] = snake->dir_recordings[i + 1];
            }
            snake->num_dir_recordings--;
          }
        }
      }
    }

    // Check for death
    // TODO check for collision against self
    if (head->x == 0 || head->x == state->num_tiles_x + 1
        || head->y == 0 || head->y == state->num_tiles_y + 1) {
      // TODO dead
      snake->alive = false;
      head->x = Max(1, Min(head->x, state->num_tiles_x));
      head->y = Max(1, Min(head->y, state->num_tiles_y));
    }

    // TODO check for food collision - if hit then delete food and extend the snake
    snake_piece *tail = &snake->pieces[snake->length - 1];
    snake_food *food = &state->foods[0];
    if (food && tail->x == food->x && tail->y == food->y) {
      state->foods[0] = {};
    }
  }
  else {
    state->snake_update_timer += 0.1f;
  }
}

void ProcessInput(game_input *input, game_state *state) {
  for (int controller_idx = 0;
      controller_idx < ArrayCount(input->controllers);
      ++controller_idx) {
    game_controller_input *controller = GetController(input, controller_idx);
    snake_state *snake = &state->snake;

    if (controller->is_analog) {
      /* NOTE:  Use analog movement tuning */
      state->blue_offset += (int32)(4.0f * controller->stick_avg_x);
      state->tone_hz = 220 + (int32)(128.0f * controller->stick_avg_y);
    }
    else {
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

      if (controller->right_shoulder.ended_down && snake->length < ArrayCount(snake->pieces)) {
        ExtendSnake(snake, WEST);
      }
      else if (controller->left_shoulder.ended_down && snake->length > 1) {
        snake->length--;
      }
    }

    if (controller->action_down.ended_down) {
      state->red_offset += 1;
      snake->alive = true;
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

  if (!memory->is_initialized) {
    char *filename = __FILE__;

    debug_read_file_result file = memory->DEBUGPlatformReadEntireFile(thread, filename);
    if (file.content) {
      memory->DEBUGPlatformWriteEntireFile(thread, "test.out", file.content_size, file.content);
      memory->DEBUGPlatformFreeFileMemory(thread, file.content);
    }

    state->tone_hz = 220;
    state->t_sine = 0.0f;
    state->red_offset = 1;

    state->game_width = screen_buffer->width;
    state->game_height = screen_buffer->height;
    state->tile_size = 25;
    state->num_tiles_x = (int)(state->game_width / state->tile_size);
    state->num_tiles_y = (int)(state->game_height / state->tile_size);

    // TODO implement no walls mode

    // TODO pick random starting pos
    snake_state snake = {};
    snake.new_direction = NONE;
    snake.num_dir_recordings = 0;
    snake_piece head = {};
    head.dir = EAST;
    head.x = 1;
    head.y = 1;
    snake.pieces[0] = head;
    snake.length = 1;
    snake.alive = true;

    state->snake = snake;
    state->snake_update_timer = 0.0f;

    snake_food food = {};
    food.x = 25;
    food.y = 25;
    state->foods[0] = food;

    // TODO do we really need 1-indexed tiles?

    // TODO this may be more appropriate to do in the platform layer
    memory->is_initialized = true;
  }

  ProcessInput(input, state);
  // RenderWeirdGradient(screen_buffer, state->blue_offset, state->green_offset, state->red_offset);
  RenderGrid(screen_buffer, state);
  UpdateSnake(screen_buffer, state);
  RenderSnake(screen_buffer, state);
  RenderFood(screen_buffer, state);
  RenderRecordingSpot(screen_buffer, state);
}

// extern "C" tells the compiler to use the old C naming process which will preserve the
// function name. This is needed in order for us to call the function from a DLL.
extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples) {
  game_state *state = (game_state *)memory->permanent_storage;
  GameOutputSound(state, sound_buffer, state->tone_hz);
}
