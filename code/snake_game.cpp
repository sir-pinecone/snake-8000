/* Platform independent layer API
 *
 * This is loaded as a DLL so that code changes can be reloaded while the game runs.
 * Make sure to not include anything static in the DLL. Put state in the game memory.
 */

#include "pcg_basic.h"
#include "snake_game.h"

global_variable pcg32_random_t rng;

// IDEA: create a process that plays the game flawlessly. Or introduce randomness in order
// to test the game.

void GameOutputSound(GameState *state, GameSoundOutputBuffer *sound_buffer, int32 tone_hz) {
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

void RenderWeirdGradient(GameOffscreenBuffer* buffer,
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
DrawBlock(GameOffscreenBuffer* buffer, uint32 color,
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

void RenderGrid(GameOffscreenBuffer* buffer, GameState *state) {
  for (int y = 1; y <= state->num_tiles_y; ++y) {
    int y_pixel = GetTilePixel(y, state->num_tiles_y, state->tile_size);
    for (int x = 1; x <= state->num_tiles_x; ++x) {
      int x_pixel = GetTilePixel(x, state->num_tiles_x, state->tile_size);
      DrawBlock(buffer, RGBColor(255, 255, 255), x_pixel, y_pixel, state->tile_size);
    }
  }
}

SnakePiece * GetSnakePiece(SnakeState *snake, int index) {
  Assert(index < ArrayCount(snake->pieces));
  return &snake->pieces[index];
}

SnakePiece * GetSnakeHead(SnakeState *snake) {
  return GetSnakePiece(snake, 0);
}

int SnakePieceNextX(SnakePiece *piece) {
  Direction dir = piece->dir;
  if (dir == EAST) {
    return piece->x + 1;
  }
  else if (dir == WEST) {
    return piece->x - 1;
  }
  return piece->x;
}

int SnakePieceNextY(SnakePiece *piece) {
  Direction dir = piece->dir;
  if (dir == SOUTH) {
    return piece->y + 1;
  }
  else if (dir == NORTH) {
    return piece->y - 1;
  }
  return piece->y;
}

void MoveSnakePiece(SnakePiece *piece, Direction in_direction) {
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

Direction OppositeDirection(Direction dir) {
  switch(dir) {
    case NORTH: {
      return SOUTH;
    } break;

    case SOUTH: {
      return NORTH;
    } break;

    case EAST: {
      return WEST;
    } break;

    case WEST: {
      return EAST;
    } break;
  }
  return NONE;
}

void ExtendSnake(SnakeState *snake) {
  Assert((snake->length - 1) >= 0);
  if (snake->length < ArrayCount(snake->pieces)) {
    SnakePiece *tail = GetSnakePiece(snake, snake->length - 1);
    SnakePiece new_piece;
    new_piece.x = tail->x;
    new_piece.y = tail->y;
    // Can make use of this to shift the piece into place
    MoveSnakePiece(&new_piece, OppositeDirection(tail->dir));
    new_piece.dir = tail->dir;
    snake->pieces[snake->length++] = new_piece;
  }
  // TODO ELSE YOU WIN!
}

void ChangeSnakeDirection(SnakeState *snake, Direction new_dir) {
  if (snake->alive) {
    SnakePiece *head = GetSnakeHead(snake);
    Direction inverse_head_dir = OppositeDirection(head->dir);
    if (head->dir != new_dir &&
        (new_dir != inverse_head_dir || snake->length == 1) &&
        snake->new_direction != new_dir) {
      snake->new_direction = new_dir;
      if (snake->length > 1) {
        // Record the path change
        Assert(snake->num_dir_recordings < ArrayCount(snake->dir_recordings));
        DirChangeRecord record = {};
        record.dir = new_dir;
        record.x = head->x;
        record.y = head->y;
        snake->dir_recordings[snake->num_dir_recordings++] = record;
      }
    }
  }
}

// TODO LYNDA create an x and y version of GetTilePixel()
void RenderRecordingSpot(GameOffscreenBuffer *buffer, GameState *state) {
  uint32 color = RGBColor(0, 255, 255);
  SnakeState *snake = &state->snake;
  for (int idx = 0; idx < snake->num_dir_recordings; ++idx) {
    DirChangeRecord *record = &snake->dir_recordings[idx];
    int x_pixel = GetTilePixel(record->x, state->num_tiles_x, state->tile_size);
    int y_pixel = GetTilePixel(record->y, state->num_tiles_y, state->tile_size);
    DrawBlock(buffer, color, x_pixel, y_pixel, state->tile_size);
  }
}

void RenderFood(GameOffscreenBuffer *buffer, GameState *state) {
  uint32 color = RGBColor(100, 230, 140);
  SnakeFood *food = &state->foods[0];
  for (int idx = 0; idx < state->num_foods; ++idx) {
    SnakeFood *food = &state->foods[idx];
    if (food) {
      int x_pixel = GetTilePixel(food->x, state->num_tiles_x, state->tile_size);
      int y_pixel = GetTilePixel(food->y, state->num_tiles_y, state->tile_size);
      DrawBlock(buffer, color, x_pixel, y_pixel, state->tile_size);
    }
  }
}

void RenderSnake(GameOffscreenBuffer *buffer, GameState *state) {
  SnakeState *snake = &state->snake;
  SnakePiece *head = GetSnakeHead(snake);
  uint32 color = snake->alive ? RGBColor(20, 90, 255) : RGBColor(255, 0, 0);
  uint32 head_color = RGBColor(10, 90, 203);
  for (int piece_idx = 0; piece_idx < snake->length; ++piece_idx) {
    SnakePiece *piece = GetSnakePiece(snake, piece_idx);
    if (piece) {
      int x_pixel = GetTilePixel(piece->x, state->num_tiles_x, state->tile_size);
      int y_pixel = GetTilePixel(piece->y, state->num_tiles_y, state->tile_size);
      uint32 c = (piece_idx == 0) ? head_color : color;
      DrawBlock(buffer, c, x_pixel, y_pixel, state->tile_size);
    }
  }
}

void CreateFood(GameState *state) {
  SnakeFood food = {};
  food.eaten = false;
  // TODO check for collision with player
  if (state->num_foods < ArrayCount(state->foods)) {
    // TODO check if food tile is already occupied
    food.x = (int)(pcg32_boundedrand_r(&rng, state->num_tiles_x - 1) + 1);
    food.y = (int)(pcg32_boundedrand_r(&rng, state->num_tiles_y - 1) + 1);
    state->foods[state->num_foods++] = food;
  }
}

real32 StepSpeed(SnakeState *snake) {
   return snake->length * 0.005f;
}

void UpdateSnake(GameOffscreenBuffer *buffer, GameState *state, real32 dt) {
  // Update is not frame rate independent at all
  SnakeState *snake = &state->snake;
  state->snake_update_timer -= dt;
  if (state->snake_update_timer < 0.0f) {
    // TODO speed slowly grows and then suddenly it's really really fast. Fix
    state->snake_update_timer = 0.25f - StepSpeed(snake);

    SnakePiece *head = GetSnakeHead(snake);
    SnakePiece *tail = &snake->pieces[snake->length - 1];

    if (snake->new_direction != NONE) {
      head->dir = snake->new_direction;
      snake->new_direction = NONE;
    }

    {
      // Check if the next movement position results in death
      int next_x = SnakePieceNextX(head);
      int next_y = SnakePieceNextY(head);

      // Check for collision with walls
      if (next_x == 0 || next_x == state->num_tiles_x + 1
          || next_y == 0 || next_y == state->num_tiles_y + 1) {
        snake->alive = false;
        //head->x = Max(1, Min(head->x, state->num_tiles_x));
        //head->y = Max(1, Min(head->y, state->num_tiles_y));
      }
      // Check body collision
      else if (snake->length > 1) {
        for (int idx = 1; idx < snake->length; ++idx) {
          SnakePiece *piece = GetSnakePiece(snake, idx);
          if (piece && piece->x == next_x && piece->y == next_y) {
            snake->alive = false;
          }
        }
      }
    }

    if (snake->alive) {
      // Move the head
      MoveSnakePiece(head, head->dir);

      // NOTE: two alternative approaches to solving the movement stuff. Have each piece
      // prepare their next movement to be the direction of the previous piece's last move.
      // NOTE: another way is to do the same as above but instead of mantaining a num_dir_recordings,
      // just use the snake length for indexing. No need to delete recordings either.

      // Move the body pieces
      for (int piece_idx = 1; piece_idx < snake->length; ++piece_idx) {
        SnakePiece *piece = GetSnakePiece(snake, piece_idx);
        MoveSnakePiece(piece, piece->dir);
        // TODO use the index to determine how many direction recordings need to be checked
        //   instead of looping over all of them every time.
        // Look at oldest recording first.
        bool32 last_piece = (piece_idx == snake->length - 1);
        for (int idx = 0; idx < snake->num_dir_recordings; ++idx) {
          DirChangeRecord *record = &snake->dir_recordings[idx];
          if (piece->x == record->x && piece->y == record->y) {
            piece->dir = record->dir;

            if (last_piece) {
              // Delete the recording
              for (int i = 0; i < snake->num_dir_recordings; ++i) {
                if (i == snake->num_dir_recordings - 1) {
                  snake->num_dir_recordings--;
                  snake->dir_recordings[i] = {};
                }
                else {
                  // Shift
                  snake->dir_recordings[i] = snake->dir_recordings[i + 1];
                }
              }
            }
          }
        }
      }

      bool32 head_is_tail = (snake->length == 1);

      // Eat
      for (int idx = 0; idx < state->num_foods; ++idx) {
        SnakeFood *food = &state->foods[idx];
        // TODO BUG: looks weird when you move the moment you eat a food
        if (food) {
          if (!food->eaten) {
            if ((head->x == food->x) && (head->y == food->y)) {
              CreateFood(state);
              CreateFood(state);
              CreateFood(state);
              if (head_is_tail) {
                food->eaten = true;
              }
            }
            else if (!head_is_tail && (tail->x == food->x) && (tail->y == food->y)) {
              // We'll remove the piece on the next pass
              food->eaten = true;
            }
          }
          else {
            for (int i = idx; i < state->num_foods; ++i) {
              if (i == state->num_foods - 1) {
                // Delete food
                state->num_foods--;
                state->foods[i] = {};
              }
              else {
                // Shift food
                state->foods[i] = state->foods[i + 1];
              }
            }
            ExtendSnake(snake);
          }
        }
      }
    }
  }
}

void ResetGame(ThreadContext *thread, GameMemory *memory, GameState *state) {
  // TODO implement no walls mode
  state->do_game_reset = false;
  SnakeState snake = {};
  snake.new_direction = NONE;
  snake.num_dir_recordings = 0;
  SnakePiece head = {};

  head.dir = (Direction)(pcg32_boundedrand_r(&rng, 4) + 1);
  head.x = (int)(state->num_tiles_x / 2);
  head.y = (int)(state->num_tiles_y / 2);

  snake.pieces[0] = head;
  snake.length = 1;
  snake.alive = true;

  state->snake = snake;
  state->snake_update_timer = 0.0f;

  CreateFood(state);
  CreateFood(state);
  CreateFood(state);
  CreateFood(state);
  CreateFood(state);
}

void ProcessInput(GameInput *input, GameState *state) {
  for (int controller_idx = 0;
      controller_idx < ArrayCount(input->controllers);
      ++controller_idx) {
    GameControllerInput *controller = GetController(input, controller_idx);
    SnakeState *snake = &state->snake;

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

      if (controller->right_shoulder.ended_down && snake->alive &&
          snake->length < ArrayCount(snake->pieces)) {
        ExtendSnake(snake);
      }
      else if (controller->left_shoulder.ended_down && snake->alive && snake->length > 1) {
        snake->length--;
      }
    }

    if (controller->action_down.ended_down) {
      state->red_offset += 1;
      if (snake->alive == false) {
        state->do_game_reset = true;
      }
      else {
        // TODO kill the window
      }
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
  Assert(sizeof(GameState) <= memory->permanent_storage_size);

  GameState *state = (GameState *)memory->permanent_storage;

  if (!memory->is_initialized) {
    char *filename = __FILE__;

    // Setup the rng
    pcg32_srandom_r(&rng, memory->rand_seed, memory->rand_rounds);

    state->tone_hz = 220;
    state->t_sine = 0.0f;
    state->red_offset = 1;

    state->game_width = screen_buffer->width;
    state->game_height = screen_buffer->height;
    state->tile_size = 25; // TODO investigate bug when this is < 10 ish
    state->num_tiles_x = (int)(state->game_width / state->tile_size);
    state->num_tiles_y = (int)(state->game_height / state->tile_size);

    ResetGame(thread, memory, state);

    // TODO do we really need 1-indexed tiles?
    // TODO this may be more appropriate to do in the platform layer
    memory->is_initialized = true;
  }

  ProcessInput(input, state);

  if (state->do_game_reset) {
    ResetGame(thread, memory, state);
  }
  else {
    // RenderWeirdGradient(screen_buffer, state->blue_offset, state->green_offset, state->red_offset);
    RenderGrid(screen_buffer, state);
    SnakeState *snake = &state->snake;
    if (snake->alive) {
      UpdateSnake(screen_buffer, state, input->dt_for_frame);
    }
    RenderFood(screen_buffer, state);
    RenderSnake(screen_buffer, state);
#if SNAKE_INTERNAL
    RenderRecordingSpot(screen_buffer, state);
#endif
  }
}

// extern "C" tells the compiler to use the old C naming process which will preserve the
// function name. This is needed in order for us to call the function from a DLL.
extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples) {
  GameState *state = (GameState *)memory->permanent_storage;
  GameOutputSound(state, sound_buffer, state->tone_hz);
}
