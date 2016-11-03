// TODO BUG: if I stop a loop while it's playing back input (say keyboard movement) then it
//   will continue to apply that input until I generate a new input event.
// TODO switch back to just int instead of int32?
/* TODO Future Windows work to make it production ready

   - Save game locations
   - Getting a handle to our own executable file
   - Asset loading path
   - Threading (launch a thread)
   - Raw input (support for multiple keyboards)
   - Sleep/timeBeginPeriod
   - ClipCursor() (for multimonitor support)
   - Fullscreen support
   - WM_SETCURSOR (control cursor visibility)
   - QueryCancelAutoplay
   - WM_ACTIVATEAPP (for when we are not the active application)
   - Blit speed improvements (BitBlt)
   - Hardware acceleration (OpenGL or DirectX or BOTH??)
   - GetKeyboardLayout (for French keyboards, international WASD support)

   and more!
*/

#include "snake_game.h"

#include <windows.h>
#include <stdio.h>
#include <xinput.h>
#include <dsound.h>
#include "pcg_basic.h"

#include "win32_snake_game.h"


// ---------------------------------------------------------------------------------------
// Game Globals
// ---------------------------------------------------------------------------------------
// TODO: global for now
global_variable bool32 global_running;
global_variable bool32 global_pause;
global_variable win32_offscreen_buffer global_backbuffer;
global_variable LPDIRECTSOUNDBUFFER global_secondary_audio_buffer;
global_variable int64 global_perf_count_freq;
global_variable pcg32_random_t rng;

// ---------------------------------------------------------------------------------------
// Utils
// ---------------------------------------------------------------------------------------

internal
void Win32GetEXEFilename(win32_platform_state *state) {
  // NOTE: Never use WIN32_STATE_FILE_NAME_COUNT in code that is user-facing because it's not very accurate
  // and lead to bad results.
  DWORD size_of_current_filename = GetModuleFileNameA(0, state->exe_filename, sizeof(state->exe_filename));
  state->one_past_last_exe_filename_slash = state->exe_filename;
  for(char *scan = state->exe_filename; *scan; ++scan) {
    if (*scan == '\\') {
      state->one_past_last_exe_filename_slash = scan + 1;
    }
  }
}

internal
void Win32RelativeEXEFilePath(win32_platform_state *state, char *filename, char *dest, int dest_count) {
  ConcatStr(state->exe_filename,
            state->one_past_last_exe_filename_slash - state->exe_filename,
            filename, StrLen(filename),
            dest, dest_count);
}

inline LARGE_INTEGER
Win32GetWallClock() {
  LARGE_INTEGER result;
  QueryPerformanceCounter(&result);
  return result;
}

inline real32
Win32GetSecondsElapsed(LARGE_INTEGER start, LARGE_INTEGER end) {
  real32 result = ((real32)(end.QuadPart - start.QuadPart) /
                   (real32)global_perf_count_freq);
  return result;
}

// ---------------------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------------------

DEBUG_PLATFORM_FREE_FILE_MEMORY(DEBUGPlatformFreeFileMemory) {
  if (memory) {
    VirtualFree(memory, 0, MEM_RELEASE);
  }
}

DEBUG_PLATFORM_READ_ENTIRE_FILE(DEBUGPlatformReadEntireFile) {
  debug_read_file_result result = {};

  HANDLE file_handle = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
  if (file_handle != INVALID_HANDLE_VALUE) {
    LARGE_INTEGER file_size;
    if (GetFileSizeEx(file_handle, &file_size)) {
      uint32 file_size32 = SafeTruncateUInt64(file_size.QuadPart);
      result.content = VirtualAlloc(0, file_size32, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);

      if (result.content) {
        DWORD bytes_read;
        if (ReadFile(file_handle, result.content, file_size32, &bytes_read, 0) &&
            // Protect against the file being truncated in between getting the size and reading
            (file_size32 == bytes_read)) {
          /* NOTE: file read successfully */
          result.content_size = file_size32;
        }
        else {
          // TODO log error
          DEBUGPlatformFreeFileMemory(thread, result.content);
          result.content = 0;
        }
      }
      else {
        // TODO log error
      }
    }
    else {
      // TODO log error
    }
    CloseHandle(file_handle);
  }
  else {
    // TODO log error
  }

  return result;
}

DEBUG_PLATFORM_WRITE_ENTIRE_FILE(DEBUGPlatformWriteEntireFile) {
  bool32 result = false;
  HANDLE file_handle = CreateFileA(filename, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
  if (file_handle != INVALID_HANDLE_VALUE) {
    DWORD bytes_written;
    if (WriteFile(file_handle, memory, memory_size, &bytes_written, 0)) {
      /* NOTE: file written successfully */
      result = (bytes_written == memory_size);
    }
    else {
      // TODO log error
    }
    CloseHandle(file_handle);
  }
  else {
    // TODO log error
  }

  return result;
}

inline FILETIME
Win32GetLastFileWriteTime(char *filename) {
  FILETIME last_write_time = {};
  WIN32_FILE_ATTRIBUTE_DATA data;
  if (GetFileAttributesEx(filename, GetFileExInfoStandard, &data)) {
    last_write_time = data.ftLastWriteTime;
  }
  return last_write_time;
}

internal win32_game_code
Win32LoadGameCode(char *source_dll_name, char *temp_dll_name) {
  win32_game_code result = {};

  result.dll_last_compile_time = Win32GetLastFileWriteTime(source_dll_name);
  CopyFile(source_dll_name, temp_dll_name, FALSE);
  result.game_code_dll = LoadLibraryA(temp_dll_name);

  if (result.game_code_dll) {
    result.UpdateAndRender = (game_update_and_render *)GetProcAddress(result.game_code_dll, "GameUpdateAndRender");
    result.GetSoundSamples = (game_get_sound_samples *)GetProcAddress(result.game_code_dll, "GameGetSoundSamples");

    result.is_valid = (result.UpdateAndRender && result.GetSoundSamples);
  }

  if (!result.is_valid) {
    result.UpdateAndRender = 0;
    result.GetSoundSamples = 0;
  }

  return result;
}

internal void
Win32UnloadGameCode(win32_game_code *game_code) {
  if (game_code->game_code_dll) {
    FreeLibrary(game_code->game_code_dll);
  }
  game_code->is_valid = false;
  game_code->UpdateAndRender = 0;
  game_code->GetSoundSamples = 0;
}

// ---------------------------------------------------------------------------------------
// Controller Setup
// ---------------------------------------------------------------------------------------

/* Avoid having to link to xinput since it's really sketchy. The docs mention
 * which lib to use based on the OS. The problem is that if the user doesn't
 * run that OS then they won't be able to run the game. Quite silly since gamepads
 * are optional.
 *
 * A simple typedef of the Windows function signature isn't enough to avoid linking.
 * We have to create a pointer that points to the actual function of that type
 * We also don't want the game to crash if the pointers have nothing to point to.
 * They will start off initialized to 0 because they are global. We deal with this
 * via a macro that outputs a function with the given name and the expected
 * signature. We then make a type of that function for the pointer. And finally
 * we make a stub function that does nothing and use it for the initial pointer value.
 */

// REVIEW: need to relearn this macro trick. It's slightly confusing. MC 2016-04-10

// NOTE: XInputGetState
#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dw_user_index, XINPUT_STATE* p_state)
typedef X_INPUT_GET_STATE(x_input_get_state);
X_INPUT_GET_STATE(XInputGetStateStub) {
  return ERROR_DEVICE_NOT_CONNECTED;
}
global_variable x_input_get_state *_XInputGetState = XInputGetStateStub;
#define XInputGetState _XInputGetState

// ---------------------------------------------------------------------------------------

// NOTE: XInputSetState
#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dw_user_index, XINPUT_VIBRATION* p_vibration)
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(XInputSetStateStub) {
  return ERROR_DEVICE_NOT_CONNECTED;
}
global_variable x_input_set_state *_XInputSetState = XInputSetStateStub;
#define XInputSetState _XInputSetState


// ---------------------------------------------------------------------------------------
// Sound Setup
// ---------------------------------------------------------------------------------------

#define DIRECT_SOUND_CREATE(name) HRESULT WINAPI name(LPCGUID pc_guid_device, LPDIRECTSOUND *pp_ds, LPUNKNOWN p_unk_outer)
typedef DIRECT_SOUND_CREATE(direct_sound_create);

internal void
Win32LoadXInput(void) {
  // TODO: test this on Windows 8
  HMODULE x_input_dll = LoadLibraryA("xinput1_4.dll"); // Win8
  if (!x_input_dll) {
    // TODO: Diagnostic error report to know the version that failed
    x_input_dll = LoadLibraryA("xinput9_1_0.dll"); // Win7 and older
  }
  if (!x_input_dll) {
    // TODO: Diagnostic error report to know the version that failed
    x_input_dll = LoadLibraryA("xinput9_1_0.dll"); // Win7 and older
  }
  if (x_input_dll) {
    // Cast since it returns a void*
    XInputGetState = (x_input_get_state *)GetProcAddress(x_input_dll, "XInputGetState");
    if (!XInputGetState) { XInputGetState = XInputGetStateStub; }

    XInputSetState = (x_input_set_state *)GetProcAddress(x_input_dll, "XInputSetState");
    if (!XInputSetState) { XInputSetState = XInputSetStateStub; }
    // TODO: Diagnostic message
  }
  else {
    // TODO: Diagnostic message
  }
}

internal void
Win32InitDSound(HWND window, int32 samples_per_second, int32 buffer_size,
                int8 num_channels, int8 bits_per_channel_sample) {
  // TODO: load the library and allow the game to run without DSound
  HMODULE direct_sound_dll = LoadLibraryA("dsound.dll");
  if (direct_sound_dll) {
    // TODO: Get a DirectSound object! - cooperative mode
    direct_sound_create *DirectSoundCreate = (direct_sound_create *)GetProcAddress(direct_sound_dll, "DirectSoundCreate");

    // TODO: Double-check that this works on XP - directsound8 or 7?
    LPDIRECTSOUND direct_sound;
    // Returned object has virtual funcs, so we are using its vtable to call functions, hence why we don't need to use GetProcAddress()
    // The vtable is stored in the dsound DLL. The table gets loaded into memory when the DLL is loaded.
    if (DirectSoundCreate && SUCCEEDED(DirectSoundCreate(0, &direct_sound, 0))) {
      WAVEFORMATEX wave_format = {};
      wave_format.wFormatTag = WAVE_FORMAT_PCM;
      wave_format.nChannels = num_channels;
      wave_format.wBitsPerSample = bits_per_channel_sample;
      wave_format.nSamplesPerSec = samples_per_second;
      wave_format.nBlockAlign = (wave_format.nChannels * wave_format.wBitsPerSample) / 8;
      wave_format.nAvgBytesPerSec = wave_format.nSamplesPerSec * wave_format.nBlockAlign;
      wave_format.cbSize = 0;

      if (SUCCEEDED(direct_sound->SetCooperativeLevel(window, DSSCL_PRIORITY))) {
        // Create a primary buffer - but not a real buffer - in order to
        // get a handle on the sound card so that we can set the format.
        // THe real buffer is the secondary one.
        DSBUFFERDESC buffer_description = {};
        buffer_description.dwSize = sizeof(buffer_description);
        buffer_description.dwFlags = DSBCAPS_PRIMARYBUFFER;

        // TODO: use primary_buffer.dwFlags -> DSBCAPS_GLOBALFOCUS?
        LPDIRECTSOUNDBUFFER primary_buffer;
        if (SUCCEEDED(direct_sound->CreateSoundBuffer(&buffer_description, &primary_buffer, 0))) {
          HRESULT error = primary_buffer->SetFormat(&wave_format);
          if (SUCCEEDED(error)) {
            /* NOTE: we have finally set the format! */
            OutputDebugStringA("Primary sound buffer created\n");
          }
          else {
            // TODO: diagnostic
          }
        }
        else {
          // TODO: diagnostic
        }
      }
      else {
        // TODO: diagnostic
      }

      DSBUFFERDESC buffer_description = {};
      buffer_description.dwSize = sizeof(buffer_description);
      buffer_description.dwFlags = DSBCAPS_GETCURRENTPOSITION2;
      buffer_description.dwBufferBytes = buffer_size;
      buffer_description.lpwfxFormat = &wave_format;
      HRESULT error = direct_sound->CreateSoundBuffer(&buffer_description, &global_secondary_audio_buffer, 0);
      if (SUCCEEDED(error)) {
        // TODO: Start it playing
        OutputDebugStringA("Secondary sound buffer created\n");
      }
    }
    else {
      // TODO: diagnostic error
    }
  }
  else {
    // TODO: diagnostic error
  }
}

internal
win32_window_dimension Win32GetWindowDimension(HWND window) {
  win32_window_dimension ret;
  RECT client_rect;
  GetClientRect(window, &client_rect);
  ret.width = client_rect.right - client_rect.left;
  ret.height = client_rect.bottom - client_rect.top;
  return ret;
}

internal void
Win32ResizeDIBSection(win32_offscreen_buffer *buffer, int32 width, int32 height) {
  // TODO: bulletproof this. Maybe don't free first, free after, then free first
  //  if that fails.
  if (buffer->memory) {
    // Need to free before we do anything
    // TODO: look into using VirtualProtect to restrict access. If a pointer
    //  tries to change protected data then we will know. Those type of bugs
    //  are very hard to find. See ep 4, 16mins for initial idea.
    VirtualFree(buffer->memory, 0, MEM_RELEASE);
  }

  int32 bytes_per_pixel = 4;

  buffer->bytes_per_pixel = bytes_per_pixel;
  buffer->width = width;
  buffer->height = height;
  buffer->pitch = width * buffer->bytes_per_pixel; // the width of buffer in bytes (also known as stride)

  /* NOTE: When the biHeight field is negative, this is the clue to Windows to
   * treat this bitmap as top-down, not bottom-up, meaning that the first three
   * bytes of the image are the color for the top left pixel in the bitmap, not
   * the bottom left!
   */
  buffer->info.bmiHeader.biSize = sizeof(buffer->info.bmiHeader);
  buffer->info.bmiHeader.biWidth = buffer->width;
  buffer->info.bmiHeader.biHeight = -buffer->height; // make this a top-down DIB
  buffer->info.bmiHeader.biPlanes = 1;
  // 4 bytes per px, 2^32 colors, 1 byte per color + 1 byte for alpha/padding
  buffer->info.bmiHeader.biBitCount = 32;
  buffer->info.bmiHeader.biCompression = BI_RGB;

  // Allow the bitmap size to be modified by code. If we didn't want this then
  // we can just allocate this buffer once outside of the function.
  // We will eventually subdivide this chunk of memory for various uses in the
  // game.
  int32 bitmap_memory_size = width * height * buffer->bytes_per_pixel;
  buffer->memory = VirtualAlloc(0, bitmap_memory_size, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
  // TODO: probably want to clear this to black
}

// Takes a buffer of pixel data and blits it to a Window that is associated with
// `device_context`.
internal void
Win32RenderBuffer(win32_offscreen_buffer* buffer,
                  HDC device_context, int32 window_width, int32 window_height) {
  // TODO: fix aspect ratio
  // NOTE: for prototyping purposes, we're going to always blit 1-to-1 pixels to make
  // sure we don't introduce artifacts with stretching while we are learning to code the
  // renderer.
  StretchDIBits(
    device_context,
    0, 0, buffer->width, buffer->height,
    0, 0, buffer->width, buffer->height,
    buffer->memory,
    &buffer->info,
    DIB_RGB_COLORS, SRCCOPY);
}

internal LRESULT CALLBACK
Win32MainWindowCallback(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
  LRESULT result = 0;
  switch(message) {
    case WM_SIZE:
    {}
    break;
    case WM_ACTIVATEAPP: {
#if 0
      if (w_param == TRUE) {
        SetLayeredWindowAttributes(window, RGB(0, 0, 0), 255, LWA_ALPHA);
      }
      else {
        SetLayeredWindowAttributes(window, RGB(0, 0, 0), 100, LWA_ALPHA);
      }
#endif
    }
    break;
    case WM_CLOSE: {
      // TODO: handle this with a message to the user?
      global_running = false;
    }
    break;
    case WM_DESTROY: {
      // TODO handle this as an error then recreate window?
      global_running = false;
    }
    break;
    // syskey will include things like alt-f4
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_KEYDOWN:
    case WM_KEYUP: {
      Assert(!"Keyboard input came in through a non-dispatch message!");
    } break;
    case WM_PAINT: {
      // Only repaint the area that needs it
      PAINTSTRUCT paint;
      HDC device_context = BeginPaint(window, &paint);
      win32_window_dimension dimension = Win32GetWindowDimension(window);
      Win32RenderBuffer(&global_backbuffer, device_context, dimension.width, dimension.height);
      EndPaint(window, &paint);
    }
    break;
    default: {
      result = DefWindowProcA(window, message, w_param, l_param);
    }
    break;
  }
  return result;
}

/* Clears the entire buffer to 0 */
internal void
Win32ClearSoundBuffer(win32_sound_output *sound_output) {
  VOID *region_1;
  DWORD region_1_size;
  VOID *region_2;
  DWORD region_2_size;
  if (SUCCEEDED(global_secondary_audio_buffer->Lock(0, sound_output->secondary_buffer_size,
                                                    &region_1, &region_1_size,
                                                    &region_2, &region_2_size,
                                                    0))) {
    /* NOTE: could use memset from std lib but we're doing it ourselves :) */
    uint8 *dest_sample = (uint8 *)region_1;
    for (DWORD byte_index = 0;
         byte_index < region_1_size;
         ++byte_index) {
      *dest_sample++ = 0;
    }

    dest_sample = (uint8 *)region_2;
    for (DWORD byte_index = 0;
         byte_index < region_2_size;
         ++byte_index) {
      *dest_sample++ = 0;
    }
    global_secondary_audio_buffer->Unlock(region_1, region_1_size, region_2, region_2_size);
  }
}

internal void
Win32FillSoundBuffer(win32_sound_output *sound_output, DWORD byte_to_lock,
                     DWORD bytes_to_write, game_sound_output_buffer *source_buffer) {
  // The sound buffer will have 16bit samples with the left and right channels interleaved.
  // We want to group the left and right samples into pairs and then consider those as
  // single samples. That's needed for proper stereo sound!
  //
  // int16  int16  int16 int16 ...
  // [LEFT  RIGHT] [LEFT RIGHT] ...
  VOID *region_1;
  DWORD region_1_size;
  VOID *region_2;
  DWORD region_2_size;
  if (SUCCEEDED(global_secondary_audio_buffer->Lock(byte_to_lock, bytes_to_write,
                                                    &region_1, &region_1_size,
                                                    &region_2, &region_2_size,
                                                    0))) {
    // TODO: assert that region_1_size/region_2_size is valid  (int16 per channel, even
    // number of blocks locked)
    // TODO Collapse these two loops
    DWORD region_1_sample_count = region_1_size / sound_output->bytes_per_sample;
    int16 *dest_sample = (int16 *)region_1;
    int16 *source_sample = source_buffer->samples;
    for (DWORD sample_idx = 0; sample_idx < region_1_sample_count; ++sample_idx) {
      *dest_sample++ = *source_sample++;
      *dest_sample++ = *source_sample++;
      ++sound_output->running_sample_index;
    }
    // Write second region
    DWORD region_2_sample_count = region_2_size / sound_output->bytes_per_sample;
    dest_sample = (int16 *)region_2;
    for (DWORD sample_idx = 0; sample_idx < region_2_sample_count; ++sample_idx) {
      *dest_sample++ = *source_sample++;
      *dest_sample++ = *source_sample++;
      ++sound_output->running_sample_index;
    }

    global_secondary_audio_buffer->Unlock(region_1, region_1_size, region_2, region_2_size); }
}

internal void
Win32ProcessXInputDigitalButton(DWORD xinput_button_state, DWORD button_bit,
                                game_button_state *old_state, game_button_state *new_state) {
  new_state->ended_down = ((xinput_button_state & button_bit) == button_bit);
  new_state->half_transition_count = (old_state->ended_down != new_state->ended_down) ? 1 : 0;
}

internal void
Win32ProcessInputMessage(game_button_state *new_state, bool32 is_down) {
  // NOTE: can happen when switching apps. Not so resilient at the moment
  if (new_state->ended_down != is_down) {
    new_state->ended_down = is_down;
    ++new_state->half_transition_count;
  }
}

internal real32
Win32ProcessXInputStickValue(SHORT value, SHORT deadzone_threshold) {
  real32 result = 0;

  // NOTE: We subtact/add the deadzone in order to rescale the value to [0, 1] instead of
  // the first value outside of the deadzone (around 25% of the total range). Without this
  // the first value may be 0.2 which is a loss of input precision. That would suck for
  // say, aiming a scoped sniper rifle.
  // TODO switch to a scaled radial deadzone
  if (value < -deadzone_threshold) {
    result = (real32)((value + deadzone_threshold) / (32768.0f - deadzone_threshold)); // negative short
  }
  else if (value > deadzone_threshold) {
    result = (real32)((value - deadzone_threshold) / (32767.0f - deadzone_threshold)); // pos short
  }
  return result;
}

internal void
Win32GetInputFileLocation(win32_platform_state *state, int slot_index, char *dest, int dest_count) {
  char temp[64];
  wsprintf(temp, "loop_recording_%d.hmi", slot_index);
  Win32RelativeEXEFilePath(state, temp, dest, sizeof(dest));
}

internal win32_replay_buffer *
Win32GetReplyBuffer(win32_platform_state *state, int unsigned index) {
  Assert(index < ArrayCount(state->replay_buffers));
  win32_replay_buffer *replay = &state->replay_buffers[index];
  return replay;
}

internal void
Win32StartRecordingInput(win32_platform_state *state, int input_recording_index) {
  win32_replay_buffer *replay_buffer = Win32GetReplyBuffer(state, input_recording_index);
  if (replay_buffer->memory_block) {
    state->input_recording_index = input_recording_index;
    state->recording_handle = replay_buffer->file_handle;

    // Move file pointer
    LARGE_INTEGER file_position;
    file_position.QuadPart = state->total_size;
    SetFilePointerEx(state->recording_handle, file_position, 0, FILE_BEGIN);

    // Mem copy
    // TODO the initial copy is still a little slow. Should revisit this and try to understand
    // why this is the case
    CopyMemory(replay_buffer->memory_block, state->game_store_block, state->total_size);
  }
}

internal void
Win32StopRecordingInput(win32_platform_state *state) {
  state->input_recording_index = 0;
}

internal void
Win32StartInputPlayback(win32_platform_state *state, int input_playback_index) {
  win32_replay_buffer *replay_buffer = Win32GetReplyBuffer(state, input_playback_index);
  if (replay_buffer->memory_block) {
    state->input_playback_index = input_playback_index;
    state->playback_handle = replay_buffer->file_handle;

    // Move file pointer
    LARGE_INTEGER file_position;
    file_position.QuadPart = state->total_size;
    SetFilePointerEx(state->playback_handle, file_position, 0, FILE_BEGIN);

    // Mem copy
    CopyMemory(state->game_store_block, replay_buffer->memory_block, state->total_size);
  }
}

internal void
Win32StopInputPlayback(win32_platform_state *state) {
  state->input_playback_index = 0;
}

internal void
Win32RecordInput(win32_platform_state *state, game_input *input) {
  DWORD bytes_written;
  WriteFile(state->recording_handle, input, sizeof(*input), &bytes_written, 0);
}

internal void
Win32PlaybackInput(win32_platform_state *state, game_input *input) {
  DWORD bytes_read = 0;
  if (ReadFile(state->playback_handle, input, sizeof(*input), &bytes_read, 0)) {
    if (bytes_read == 0) {
      // NOTE: hit the end of the stream so go back to beginning.
      Win32StartInputPlayback(state, state->input_playback_index);
      ReadFile(state->playback_handle, input, sizeof(*input), &bytes_read, 0);
    }
  }
}

internal void
Win32ProcessKeyboard(win32_platform_state *state,
                     game_controller_input *keyboard_controller,
                     win32_input_snapshot *input_snapshot) {
  bool32 is_down = input_snapshot->is_down;
  // stop key repeats
  if (input_snapshot->was_down != is_down) {
    switch(input_snapshot->vk_code) {
      case 'W': {
        Win32ProcessInputMessage(&keyboard_controller->move_up, is_down);
      } break;
      case'A': {
        Win32ProcessInputMessage(&keyboard_controller->move_left, is_down);
      } break;
      case 'S': {
        Win32ProcessInputMessage(&keyboard_controller->move_down, is_down);
      } break;
      case 'D': {
        Win32ProcessInputMessage(&keyboard_controller->move_right, is_down);
      } break;
      case 'Q': {
        Win32ProcessInputMessage(&keyboard_controller->left_shoulder, is_down);
      } break;
      case 'E': {
        Win32ProcessInputMessage(&keyboard_controller->right_shoulder, is_down);
      } break;
      case VK_UP: {
        Win32ProcessInputMessage(&keyboard_controller->action_up, is_down);
      } break;
      case VK_LEFT: {
        Win32ProcessInputMessage(&keyboard_controller->action_left, is_down);
      } break;
      case VK_DOWN: {
        Win32ProcessInputMessage(&keyboard_controller->action_down, is_down);
      } break;
      case VK_RIGHT: {
        Win32ProcessInputMessage(&keyboard_controller->action_right, is_down);
      } break;
      // TODO Using VK_RESULT code will cause a seg fault when the game runs in a
      // console.  For some unknown reason Win32ProcessInputMessage is given
      // invalid memory when the game starts. I'm not sure why this executes without
      // VK_RETURN actually being pressed. I tried to debug it by setting a
      // breakpoint, but that didn't trigger. So strange. Out of the keys in use here,
      // VK_RETURN is the only one that is causing a seg fault.
      case VK_SPACE: {
        Win32ProcessInputMessage(&keyboard_controller->start, is_down);
      } break;
      case VK_BACK: {
        Win32ProcessInputMessage(&keyboard_controller->back, is_down);
      } break;
      case VK_ESCAPE: {
        global_running = false;
      } break;
      case 'P': {
        if (is_down) {
          global_pause = !global_pause;
        }
      } break;
      case 'L': {
        if (is_down) {
          if (state->input_playback_index == 0) {
            if (state->input_recording_index == 0) {
              Assert(state->input_playback_index == 0);
              Win32StartRecordingInput(state, 1);
            }
            else {
              Win32StopRecordingInput(state);
              Win32StartInputPlayback(state, 1);
            }
          }
          else {
            Win32StopInputPlayback(state);
          }
        }
      } break;

      case VK_F4: {
        if (input_snapshot->alt_is_down) {
          global_running = false;
        }
      } break;
    }
  }
}

internal void
Win32ProcessPendingMessages(win32_platform_state *state,
                            game_controller_input *keyboard_controller) {
  MSG message;
  // Don't use the blocking GetMessageA since we want to use the idle time
  while (PeekMessage(&message, 0, 0, 0, PM_REMOVE)) {
    switch (message.message) {
      case WM_QUIT: {
        global_running = false;
      } break;

      case WM_SYSKEYDOWN:
      case WM_SYSKEYUP:
      case WM_KEYDOWN:
      case WM_KEYUP: {
        win32_input_snapshot input_snapshot = {};
        input_snapshot.vk_code = (uint32)message.wParam;
        input_snapshot.was_down = ((message.lParam & (1 << 30)) != 0);
        input_snapshot.is_down = ((message.lParam & (1 << 31)) == 0);
        input_snapshot.alt_is_down = (message.lParam & (1 << 29));

        Win32ProcessKeyboard(state, keyboard_controller, &input_snapshot);
      } break;

      default: {
        TranslateMessage(&message);
        DispatchMessage(&message);
      } break;
    }
  }
}

internal void
Win32DebugDrawVertical(win32_offscreen_buffer *backbuffer,
                       int32 x, int32 top, int32 bottom, uint32 color) {
  if (top <= 0) {
    top = 0;
  }

  if (bottom > backbuffer->height) {
    bottom = backbuffer->height;
  }

  if ((x >= 0) && (x < backbuffer->width)) {
    uint8 *pixel = ((uint8 *)backbuffer->memory +
                    x * backbuffer->bytes_per_pixel +
                    top * backbuffer->pitch);
    for (int32 y = top; y < bottom; ++y) {
      *(uint32 *)pixel = color;
      pixel += backbuffer->pitch; // shift pixel by pitch so that we get to the next row
    }
  }
}

inline void
Win32DrawSoundBufferMarker(DWORD value, win32_offscreen_buffer *backbuffer,
                           win32_sound_output *sound_output, real32 coefficient,
                           int32 pad_x, int top, int bottom, uint32 color) {
  int32 x = pad_x + (int32)(coefficient * (real32)value);
  Win32DebugDrawVertical(backbuffer, x, top, bottom, color);
}

internal void
Win32DebugDrawAudio(win32_offscreen_buffer *backbuffer, int32 marker_count,
                    win32_debug_audio_time_marker *markers,
                    int current_marker_index, win32_sound_output *sound_output,
                    real32 target_seconds_per_frame) {
  // Pad the sides in pixels
  int pad_x = 20;
  int pad_y = 20;
  int line_height = 64;
  int current_item_pad = line_height + pad_y;

  uint32 play_color = RGBColor(255,255,255);
  uint32 write_color = RGBColor(255,0,0);
  uint32 expected_flip_color = RGBColor(255,200,50);
  uint32 play_window_color = RGBColor(130,10,255);

  real32 c = (real32)(backbuffer->width - (2 * pad_x)) / (real32)sound_output->secondary_buffer_size;

  for (int32 marker_idx = 0; marker_idx < marker_count; ++marker_idx) {
    int top = pad_y;
    int bottom = pad_y + line_height;

    win32_debug_audio_time_marker *marker = &markers[marker_idx];
    Assert(marker->output_play_cursor < sound_output->secondary_buffer_size);
    Assert(marker->output_write_cursor < sound_output->secondary_buffer_size);
    Assert(marker->output_location < sound_output->secondary_buffer_size);
    Assert(marker->output_byte_count < sound_output->secondary_buffer_size);
    Assert(marker->flip_play_cursor < sound_output->secondary_buffer_size);
    Assert(marker->flip_write_cursor < sound_output->secondary_buffer_size);

    if (marker_idx == current_marker_index) {
      top += current_item_pad;
      bottom += current_item_pad;
      int first_top = top;

      Win32DrawSoundBufferMarker(marker->output_play_cursor, backbuffer, sound_output, c, pad_x, top, bottom, play_color);
      Win32DrawSoundBufferMarker(marker->output_write_cursor, backbuffer, sound_output, c, pad_x, top, bottom, write_color);

      top += current_item_pad;
      bottom += current_item_pad;
      Win32DrawSoundBufferMarker(marker->output_location, backbuffer, sound_output, c, pad_x, top, bottom, play_color);
      Win32DrawSoundBufferMarker(marker->output_byte_count + marker->output_location, backbuffer, sound_output, c, pad_x, top, bottom, write_color);

      top += current_item_pad;
      bottom += current_item_pad;

      Win32DrawSoundBufferMarker(marker->expected_flip_play_cursor, backbuffer, sound_output, c, pad_x, first_top, bottom, expected_flip_color);
    }

    Win32DrawSoundBufferMarker(marker->flip_play_cursor, backbuffer, sound_output, c, pad_x, top, bottom, play_color);
    Win32DrawSoundBufferMarker(marker->flip_write_cursor, backbuffer, sound_output, c, pad_x, top, bottom, write_color);
    // TODO fix bug that causes the expected play cursor to be outside of the safe region
    Win32DrawSoundBufferMarker(marker->flip_play_cursor + (480 * sound_output->bytes_per_sample), backbuffer, sound_output, c, pad_x, top, bottom, play_window_color);
  }
}

// In VS, change the exe build directory to w:/snake/data
// Use F11 in Visual Studio to debug
int32 CALLBACK
WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR command_line, int32 show_code) {
  // Seed the random number generator
  int rounds = 1;
  LARGE_INTEGER rand_t = Win32GetWallClock();
  uint64 rand_seed = rand_t.QuadPart ^ (intptr_t)&printf;
  uint64 rand_rounds = (intptr_t)&rounds;
  pcg32_srandom_r(&rng, rand_seed, rand_rounds);

  char text_buffer[256];
  _snprintf_s(text_buffer, sizeof(text_buffer), "Rand seed: %I64u, Rand rounds: %I64u\n",
             (int64)rand_seed, (int64)rand_rounds);
  OutputDebugStringA(text_buffer);

  LARGE_INTEGER perf_count_freq_result;
  QueryPerformanceFrequency(&perf_count_freq_result);
  global_perf_count_freq = perf_count_freq_result.QuadPart;

  // Set the OS scheduler granularity to 1ms so that our frame sleep can be more granular.
  UINT desired_scheduler_ms = 1;
  bool32 sleep_is_granular = (timeBeginPeriod(desired_scheduler_ms) == TIMERR_NOERROR);

  Win32LoadXInput();

  WNDCLASSA window_class = {}; // reset block to 0

  Win32ResizeDIBSection(&global_backbuffer, 1280, 720);

  window_class.style = CS_HREDRAW|CS_VREDRAW;
  window_class.lpfnWndProc = Win32MainWindowCallback;
  window_class.hInstance = instance; // or get the instance of global_running window with GetModulehandle(0)
  // window_class.hIcon = ;
  window_class.lpszClassName = "SnakeWindowClass";

  if (RegisterClassA(&window_class)) {
    HWND window = CreateWindowExA(
        0, //WS_EX_TOPMOST|WS_EX_LAYERED,
        window_class.lpszClassName,
        "Snake",
        WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        CW_USEDEFAULT, // x
        CW_USEDEFAULT, // y
        CW_USEDEFAULT, // w
        CW_USEDEFAULT, // h
        0,
        0,
        instance,
        0 // provides a callback message called WM_CREATE
    );

    if (window) {
      int monitor_refresh_hz = 60;
      HDC refresh_dc = GetDC(window);
      int win32_refresh_rate = GetDeviceCaps(refresh_dc, VREFRESH);
      ReleaseDC(window, refresh_dc);
      if (win32_refresh_rate > 1) {
        monitor_refresh_hz = win32_refresh_rate;
      }
      real32 game_update_hz = monitor_refresh_hz / 2.0f;
      real32 target_seconds_per_frame = 1.0f / game_update_hz;

      win32_sound_output sound_output = {};
      sound_output.samples_per_second = 48000;
      sound_output.num_channels = 2; // Stereo
      sound_output.bits_per_channel_sample = 16;  // CD quality
      sound_output.bytes_per_sample = sizeof(int16) * sound_output.num_channels;
      sound_output.running_sample_index = 0; // don't wrap into negative values
      sound_output.secondary_buffer_size = sound_output.samples_per_second * sound_output.bytes_per_sample;
      // TODO actually compute this variance and see what the lowset reasonable value is
      sound_output.safety_bytes = (int)((real32)(sound_output.samples_per_second * sound_output.bytes_per_sample) / game_update_hz / 3.0f);

      Win32InitDSound(window, sound_output.samples_per_second, sound_output.secondary_buffer_size,
                      sound_output.num_channels, sound_output.bits_per_channel_sample);
      Win32ClearSoundBuffer(&sound_output);

      global_secondary_audio_buffer->Play(0, 0, DSBPLAY_LOOPING);

#if 0
      while(true) {
        DWORD play_cursor = 0;
        DWORD write_cursor = 0;
        global_secondary_audio_buffer->GetCurrentPosition(&play_cursor, &write_cursor);
        DWORD delta = (write_cursor - play_cursor);
        char text_buffer[256];
        _snprintf_s(text_buffer, sizeof(text_buffer), "PC:%u WC:%u D:%u\n", play_cursor, write_cursor, delta);
        OutputDebugStringA(text_buffer);
      }
#endif

#if SNAKE_INTERNAL
      LPVOID base_address = (LPVOID)Terabytes(2);
#else
      LPVOID base_address = 0;
#endif

      // Initialize state
      win32_platform_state win32_state = {};
      Win32GetEXEFilename(&win32_state);

      char source_game_code_dll_full_path[WIN32_STATE_FILE_NAME_COUNT];
      char temp_game_code_dll_full_path[WIN32_STATE_FILE_NAME_COUNT];
      Win32RelativeEXEFilePath(&win32_state, "snake_game.dll", source_game_code_dll_full_path, sizeof(source_game_code_dll_full_path));
      Win32RelativeEXEFilePath(&win32_state, "snake_game_temp.dll", temp_game_code_dll_full_path, sizeof(temp_game_code_dll_full_path));

      // Initialize game memory
      // TODO create different memory profiles based on the type of computer running this
      game_memory game_store = {};

      game_store.rand_seed = rand_seed;
      game_store.rand_rounds = rand_rounds;

      game_store.DEBUGPlatformReadEntireFile = DEBUGPlatformReadEntireFile;
      game_store.DEBUGPlatformWriteEntireFile = DEBUGPlatformWriteEntireFile;
      game_store.DEBUGPlatformFreeFileMemory = DEBUGPlatformFreeFileMemory;

      game_store.permanent_storage_size = Megabytes(64);
      game_store.temp_storage_size = Megabytes(500); // NOTE: Reduced from 1 GB strictly for live loop editing performance


      // TODO: add support for MEM_LARGE_PAGES
      win32_state.total_size = game_store.permanent_storage_size + game_store.temp_storage_size;
      win32_state.game_store_block = VirtualAlloc(base_address, (size_t)win32_state.total_size,
                                                  MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);

      game_store.permanent_storage = win32_state.game_store_block;
      game_store.temp_storage = ((uint8 *)game_store.permanent_storage +
          game_store.permanent_storage_size);

      for (int replay_index = 0;
           replay_index < ArrayCount(win32_state.replay_buffers);
           ++replay_index) {
        win32_replay_buffer *replay_buffer = Win32GetReplyBuffer(&win32_state, replay_index);

        Win32GetInputFileLocation(&win32_state, replay_index,
                                  replay_buffer->filename, sizeof(replay_buffer->filename));

        replay_buffer->file_handle =
          CreateFileA(replay_buffer->filename,
                      GENERIC_READ|GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);

        DWORD max_size_high = win32_state.total_size >> 32;
        DWORD max_size_low = win32_state.total_size & 0xFFFFFFFF;

        replay_buffer->memory_map = CreateFileMapping(
            replay_buffer->file_handle, 0, PAGE_READWRITE,
            max_size_high, max_size_low, 0);

        replay_buffer->memory_block = MapViewOfFile(
            replay_buffer->memory_map, FILE_MAP_ALL_ACCESS, 0, 0, win32_state.total_size);

        if (replay_buffer->memory_block) {
        }
        else {
          // TODO diagnostic
        }
      }

      // Allocate samples to the entire sound buffer size because we know we'll never need
      // more than this.
      // TODO: pull all VirtualAlloc's into a single alloc pool
      int16 *sound_samples = (int16 *)VirtualAlloc(0, sound_output.secondary_buffer_size,
                                                   MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);

      if (game_store.permanent_storage && game_store.temp_storage && sound_samples) {
        global_running = true;
        global_pause = false;

        game_input input[2] = {};
        game_input *new_input = &input[0];
        game_input *old_input = &input[1];

        // Start tracking time
        LARGE_INTEGER last_counter = Win32GetWallClock();
        LARGE_INTEGER flip_wall_clock = Win32GetWallClock();

        int32 debug_audio_time_marker_idx = 0;
        win32_debug_audio_time_marker debug_audio_time_markers[30] = {0};

        DWORD audio_latency_bytes = 0;
        real32 audio_latency_seconds = 0;
        bool32 sound_is_valid = false;

        win32_game_code game = Win32LoadGameCode(source_game_code_dll_full_path, temp_game_code_dll_full_path);

        // @start
        uint64 last_cycle_count = __rdtsc();
        while (global_running) {
          new_input->dt_for_frame = target_seconds_per_frame;

          FILETIME new_dll_compile_time = Win32GetLastFileWriteTime(source_game_code_dll_full_path);
          if (CompareFileTime(&new_dll_compile_time, &game.dll_last_compile_time) != 0) {
            Win32UnloadGameCode(&game);
            game = Win32LoadGameCode(source_game_code_dll_full_path, temp_game_code_dll_full_path);
          }

          // TODO Make a zeroing macro
          game_controller_input *old_keyboard_controller = GetController(old_input, 0);
          game_controller_input *new_keyboard_controller = GetController(new_input, 0);
          game_controller_input zero_controller = {};
          *new_keyboard_controller = zero_controller;
          new_keyboard_controller->is_connected = true; // TODO actually verify instead of assuming
          for (int32 button_idx = 0;
              button_idx < ArrayCount(new_keyboard_controller->buttons);
              ++button_idx) {
            new_keyboard_controller->buttons[button_idx].ended_down =
              old_keyboard_controller->buttons[button_idx].ended_down;
          }

          Win32ProcessPendingMessages(&win32_state, new_keyboard_controller);

          if (!global_pause) {
            POINT mouse_loc;
            GetCursorPos(&mouse_loc);
            ScreenToClient(window, &mouse_loc);

            new_input->mouse_x = mouse_loc.x ;
            new_input->mouse_y = mouse_loc.y;
            new_input->mouse_z = 0;
            Win32ProcessInputMessage(&new_input->mouse_buttons[0], GetKeyState(VK_LBUTTON) & (1 << 15));
            Win32ProcessInputMessage(&new_input->mouse_buttons[1], GetKeyState(VK_MBUTTON) & (1 << 15));
            Win32ProcessInputMessage(&new_input->mouse_buttons[2], GetKeyState(VK_RBUTTON) & (1 << 15));
            Win32ProcessInputMessage(&new_input->mouse_buttons[3], GetKeyState(VK_XBUTTON1) & (1 << 15));
            Win32ProcessInputMessage(&new_input->mouse_buttons[4], GetKeyState(VK_XBUTTON2) & (1 << 15));

            // TODO: Should we poll input more frequently?
            // TODO: Need to not poll disconnected controllers to avoid xinput frame rate hit
            // on older libraries
            DWORD max_controller_count = XUSER_MAX_COUNT;
            // Subtract one since we added an extra spot in the array for the keyboard.
            if (max_controller_count > (ArrayCount(new_input->controllers) - 1)) {
              max_controller_count = (ArrayCount(new_input->controllers) - 1);
            }

            // TODO: move this game-specific code (dpad emulating sticks and sticks emulating keyboard)
            // to the game layer. Can use a filter function at the start of the frame to process the
            // input.
            for (DWORD controller_idx = 0;
                controller_idx < max_controller_count;
                ++controller_idx) {
              DWORD our_controller_idx = controller_idx + 1;
              game_controller_input *old_controller = GetController(old_input, our_controller_idx);
              game_controller_input *new_controller = GetController(new_input, our_controller_idx);

              XINPUT_STATE controller_state;
              if (XInputGetState(controller_idx, &controller_state) == ERROR_SUCCESS) {
                new_controller->is_connected = true;
                // Fallback to previous frame's value when controller isn't being used
                // otherwise we would end up using the value from before the previous frame.
                // That happens because of the two controllers we ping pong in between
                new_controller->is_analog = old_controller->is_analog;

                /* NOTE: the controller is plugged in */
                // TODO: see if dwPacketNumber increments too rapidly
                XINPUT_GAMEPAD *pad = &controller_state.Gamepad;

                // Triggers
                // --------------------------
                uint8 trigger_left = pad->bLeftTrigger;
                uint8 trigger_right = pad->bRightTrigger;

                // Sticks and DPad
                // --------------------------
                // TODO: add support for right thumb stick?
                // stick
                new_controller->stick_avg_x = Win32ProcessXInputStickValue(
                    pad->sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);

                new_controller->stick_avg_y = Win32ProcessXInputStickValue(
                    pad->sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);

                if ((new_controller->stick_avg_x != 0.0f) ||
                    (new_controller->stick_avg_y != 0.0f)) {
                  new_controller->is_analog = true;
                }

                // dpad
                if (pad->wButtons & XINPUT_GAMEPAD_DPAD_UP) {
                  new_controller->stick_avg_y = 1.0f;
                  new_controller->is_analog = false;
                }
                if (pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN) {
                  new_controller->stick_avg_y = -1.0f;
                  new_controller->is_analog = false;
                }
                if (pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT) {
                  new_controller->stick_avg_x = -1.0f;
                  new_controller->is_analog = false;
                }
                if (pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) {
                  new_controller->stick_avg_x = 1.0f;
                  new_controller->is_analog = false;
                }

                // Movement
                // --------------------------
                real32 move_threshold = 0.5f;
                // If the average stick movement is beyond the threshold then we trigger a
                // movement action
                Win32ProcessXInputDigitalButton(
                    (new_controller->stick_avg_x < -move_threshold) ? 1 : 0,
                    1,
                    &old_controller->move_left,
                    &new_controller->move_left);

                Win32ProcessXInputDigitalButton(
                    (new_controller->stick_avg_x > move_threshold) ? 1 : 0,
                    1,
                    &old_controller->move_right,
                    &new_controller->move_right);

                Win32ProcessXInputDigitalButton(
                    (new_controller->stick_avg_y < -move_threshold) ? 1 : 0,
                    1,
                    &old_controller->move_down,
                    &new_controller->move_down);

                Win32ProcessXInputDigitalButton(
                    (new_controller->stick_avg_y > move_threshold) ? 1 : 0,
                    1,
                    &old_controller->move_up,
                    &new_controller->move_up);

                Win32ProcessXInputDigitalButton(pad->wButtons, XINPUT_GAMEPAD_A,
                                                &old_controller->action_down,
                                                &new_controller->action_down);

                Win32ProcessXInputDigitalButton(pad->wButtons, XINPUT_GAMEPAD_B,
                                                &old_controller->action_right,
                                                &new_controller->action_right);

                Win32ProcessXInputDigitalButton(pad->wButtons, XINPUT_GAMEPAD_X,
                                                &old_controller->action_left,
                                                &new_controller->action_left);

                Win32ProcessXInputDigitalButton(pad->wButtons, XINPUT_GAMEPAD_Y,
                                                &old_controller->action_up,
                                                &new_controller->action_up);

                Win32ProcessXInputDigitalButton(pad->wButtons, XINPUT_GAMEPAD_RIGHT_SHOULDER,
                                                &old_controller->right_shoulder,
                                                &new_controller->right_shoulder);

                Win32ProcessXInputDigitalButton(pad->wButtons, XINPUT_GAMEPAD_LEFT_SHOULDER,
                                                &old_controller->left_shoulder,
                                                &new_controller->left_shoulder);

                Win32ProcessXInputDigitalButton(pad->wButtons, XINPUT_GAMEPAD_START,
                                                &old_controller->start,
                                                &new_controller->start);

                Win32ProcessXInputDigitalButton(pad->wButtons, XINPUT_GAMEPAD_BACK,
                                                &old_controller->back,
                                                &new_controller->back);
              }
              else {
                /* NOTE: the controller is not available */

                new_controller->is_connected = false;
              }
            }

            // -----------------------------------------------------------------------------
            // Update and render the game

            thread_context thread = {};

            game_offscreen_buffer screen_buffer = {};
            screen_buffer.memory = global_backbuffer.memory;
            screen_buffer.width = global_backbuffer.width;
            screen_buffer.height = global_backbuffer.height;
            screen_buffer.pitch = global_backbuffer.pitch;
            screen_buffer.bytes_per_pixel = global_backbuffer.bytes_per_pixel;

            if (win32_state.input_recording_index) {
              Win32RecordInput(&win32_state, new_input);
            }

            if (win32_state.input_playback_index) {
              Win32PlaybackInput(&win32_state, new_input);
            }

            if (game.UpdateAndRender) {
              game.UpdateAndRender(&thread, &game_store, new_input, &screen_buffer);
            }

            // -----------------------------------------------------------------------------
            // Write the sound to the sound card

            LARGE_INTEGER audio_wall_clock = Win32GetWallClock();
            real32 from_begin_to_audio_secs = Win32GetSecondsElapsed(flip_wall_clock, audio_wall_clock);

            DWORD play_cursor = 0;
            DWORD write_cursor = 0;
            if (global_secondary_audio_buffer->GetCurrentPosition(&play_cursor, &write_cursor) == DS_OK) {
              /* How this works:
               *
               * We define a safety value that is the number of samples we think our game
               * update loop may vary by (e.g. 2ms, or something determined to be safe,
               * based on whatever we think the variability of our frame computation is).
               *
               * When we wake up to write audio, we will look and see what the play cursor
               * position is and we will forecast ahead where we think the play cursor will
               * be on the next frame boundary.
               *
               * We will then look to see if the write cursor is _before_ the boundary by at
               * least our safety value. If it is then the target fill area is that frame
               * boundary plus one full frame. This gives us perfect audio sync in the case
               * of a sound card with low enough latency.
               *
               * If the write cursor is _after_ that safety margin then we assume we can
               * never sync the audio perfectly. We will write one frame's worth of audio +
               * the safety margin's worth of guard samples.
               */

              if (!sound_is_valid) {
                sound_output.running_sample_index = write_cursor * sound_output.bytes_per_sample;
                sound_is_valid = true;
              }
              DWORD expected_sound_bytes_per_frame =
                (int)((real32)(sound_output.samples_per_second * sound_output.bytes_per_sample) / game_update_hz);

              real32 seconds_left_until_flip = (target_seconds_per_frame - from_begin_to_audio_secs);
              DWORD expected_bytes_until_flip = (DWORD)((seconds_left_until_flip / target_seconds_per_frame) * (real32)expected_sound_bytes_per_frame);
              DWORD expected_frame_boundary_byte = play_cursor + expected_bytes_until_flip;
              DWORD safe_write_cursor = write_cursor;

              if (safe_write_cursor < play_cursor) {
                safe_write_cursor += sound_output.secondary_buffer_size;
              }
              Assert(safe_write_cursor >= play_cursor);
              safe_write_cursor += sound_output.safety_bytes;

              bool32 audio_card_is_fast = (safe_write_cursor < expected_frame_boundary_byte);
              DWORD target_cursor = 0;

              if (audio_card_is_fast) {
                target_cursor = (expected_frame_boundary_byte + expected_sound_bytes_per_frame);
              }
              else {
                target_cursor = (write_cursor + expected_sound_bytes_per_frame +
                    sound_output.safety_bytes);
              }
              target_cursor = target_cursor % sound_output.secondary_buffer_size;

              DWORD bytes_to_write = 0;
              DWORD byte_to_lock =
                ((sound_output.running_sample_index * sound_output.bytes_per_sample) %
                 sound_output.secondary_buffer_size);
              if (byte_to_lock > target_cursor) {
                //  we can write in the ~ area but not - because that already has
                //  data that the play cursor to moving over
                // |~~~~~~~~~---------------------~~~~~~~~~~~~~~~~~~~~~~~|
                // |--------*-target cursor-------*-byte-to-lock---------|
                bytes_to_write = (sound_output.secondary_buffer_size - byte_to_lock) + target_cursor;
              }
              else {
                //  we can write in the ~ area
                // |--------~~~~~~~~~~~~~~~~~~~~~~-----------------------|
                // |--------*-byte-to-lock-------*-target cursor---------|
                bytes_to_write = target_cursor - byte_to_lock;
              }

              // Write the audio buffer after the updating/rendering happens because we need
              // to know how many bytes to write.
              game_sound_output_buffer sound_buffer = {};
              sound_buffer.samples_per_second = sound_output.samples_per_second;
              sound_buffer.sample_count = bytes_to_write / sound_output.bytes_per_sample;
              sound_buffer.samples = sound_samples;

              if (game.GetSoundSamples) {
                game.GetSoundSamples(&thread, &game_store, &sound_buffer);
              }

              Win32FillSoundBuffer(&sound_output, byte_to_lock, bytes_to_write, &sound_buffer);

#if SNAKE_INTERNAL
              win32_debug_audio_time_marker *marker = &debug_audio_time_markers[debug_audio_time_marker_idx];
              marker->output_play_cursor = play_cursor;
              marker->output_write_cursor = write_cursor;
              marker->output_location = byte_to_lock;
              marker->output_byte_count = bytes_to_write;
              marker->expected_flip_play_cursor = expected_frame_boundary_byte;

              if (write_cursor >= play_cursor) {
                audio_latency_bytes = write_cursor - play_cursor;
              }
              else {
                // Remember that it's a circular buffer...
                audio_latency_bytes = (sound_output.secondary_buffer_size - play_cursor) + write_cursor;
              }

              audio_latency_seconds =
                ((real32)audio_latency_bytes / (real32)sound_output.bytes_per_sample)
                / (real32)sound_output.samples_per_second;

              char text_buffer[256];
              _snprintf_s(text_buffer, sizeof(text_buffer),
                  "BTL:%u TC:%u BTW:%u PC:%u WC:%u DELTA:%u ALS:%fs\n",
                  byte_to_lock, target_cursor, bytes_to_write,
                  play_cursor, write_cursor, audio_latency_bytes, audio_latency_seconds);
              OutputDebugStringA(text_buffer);
#endif
            }
            else {
              sound_is_valid = false;
            }

            // -----------------------------------------------------------------------------
            // Deal with frame time

            /* NOTE:
             * We calculate the time here because this is a stable place to check.
             * We will never have anything escape this time window, which can happen
             * if we run this at the top of the loop. We wouldn't know if the OS
             * switched away before processing the next loop.
             */
            real32 work_seconds_elapsed = Win32GetSecondsElapsed(last_counter, Win32GetWallClock());

            real32 seconds_elapsed_for_frame = work_seconds_elapsed;
            if (seconds_elapsed_for_frame < target_seconds_per_frame) {
              while (seconds_elapsed_for_frame < target_seconds_per_frame) {
                if (sleep_is_granular) {
                  DWORD sleep_ms = (DWORD)(1000.0f * (target_seconds_per_frame - seconds_elapsed_for_frame));
                  if (sleep_ms > 0) {
                    Sleep(sleep_ms);
                  }
                }
                seconds_elapsed_for_frame = Win32GetSecondsElapsed(last_counter, Win32GetWallClock());
              }
            }
            else {
              // TODO: MISSED FRAME RATE!
              // TODO: log this
            }

            // We waited above until we hit the target_seconds_per_frame and now we take a
            // time snapshot immediately following the wait. Everything below, rendering,
            // etc. will count towards the next frame's time.
            LARGE_INTEGER end_counter = Win32GetWallClock();
            real32 ms_per_frame = 1000.0f * Win32GetSecondsElapsed(last_counter, end_counter);
            last_counter = end_counter;

            win32_window_dimension dimension = Win32GetWindowDimension(window);
#if SNAKE_INTERNAL
            /*
             * NOTE: temporarily disabling this
            int current_debug_audio_time_marker_idx = debug_audio_time_marker_idx - 1;
            int marker_count = ArrayCount(debug_audio_time_markers);
            if (current_debug_audio_time_marker_idx < 0) {
              current_debug_audio_time_marker_idx = marker_count - 1;
            }
            Win32DebugDrawAudio(&global_backbuffer, marker_count, debug_audio_time_markers,
                                current_debug_audio_time_marker_idx, &sound_output, target_seconds_per_frame);
            */
#endif

            HDC device_context = GetDC(window);
            Win32RenderBuffer(&global_backbuffer, device_context, dimension.width, dimension.height);
            ReleaseDC(window, device_context);

            flip_wall_clock = Win32GetWallClock();

#if SNAKE_INTERNAL
            // NOTE: this is debug code
            {
              DWORD play_cursor = 0;
              DWORD write_cursor = 0;
              if (global_secondary_audio_buffer->GetCurrentPosition(&play_cursor, &write_cursor) == DS_OK) {
                win32_debug_audio_time_marker *marker = &debug_audio_time_markers[debug_audio_time_marker_idx];
                marker->flip_play_cursor = play_cursor;
                marker->flip_write_cursor = write_cursor;
              }
            }
#endif
            game_input *temp = new_input;
            new_input = old_input; // TODO should I clear these here?
            old_input = temp;

            uint64 end_cycle_count = __rdtsc();
            uint64 cycles_elapsed = end_cycle_count - last_cycle_count;
            last_cycle_count = end_cycle_count;

            real64 fps = 0.0f; //(real64)global_perf_count_freq / (real64)counter_elapsed;
            real64 mega_cycles_per_frame = (real64)cycles_elapsed / (1000.0f * 1000.0f);

            char fps_buffer[256];
            _snprintf_s(fps_buffer, sizeof(fps_buffer),
                "%.02fms/f, %.02ff/s, %.02fMc/f\n", ms_per_frame, fps, mega_cycles_per_frame);
            OutputDebugStringA(fps_buffer);

#if SNAKE_INTERNAL
            ++debug_audio_time_marker_idx;
            if (debug_audio_time_marker_idx == ArrayCount(debug_audio_time_markers)) {
              debug_audio_time_marker_idx = 0;
            }
#endif
          }
        }
      }
      else {
        // TODO: Error logging
      }

      // Perform cleanup
      for (int replay_index = 0;
          replay_index < ArrayCount(win32_state.replay_buffers);
          ++replay_index) {
        win32_replay_buffer *replay_buffer = Win32GetReplyBuffer(&win32_state, replay_index);
        CloseHandle(replay_buffer->file_handle);
      }
    }
    else {
      // TODO: Error logging
    }
  }
  else {
    // TODO: Error logging
  }
  return 0;
}
