# handmade-hero
Working through the Handmade Hero project led by Casey Muratori

# Setup

## Windows

1. Create a mapped drive that loads the game directory in the W:/ partition.
  1. Easiest way to do this is by creating a `.bat` file in the Windows Startup folder with the following content (change the path if needed):
  ```
  @echo off
  subst w: d:\Dev\games\projects
  ```
2. Install Visual Studio 14
3. Install Git for Windows
4. Create a shortcut that will boot the game environment. Set its `target` to: `C:\Windows\System32\cmd.exe /k w:\handmade-hero\misc\shell.bat` (replacing the paths if needed). Now you can launch the editor and shell with this shortcut

# Workflow

1. Make code changes
2. Compilation must take place inside the code directory. Compile with `./build` - this will place the build files in `../../build/handmade-hero`.
3. Run debugger from shell using `./misc/debug` -- this will open Visual Studio with the exe loaded as the solution. Press F5 to start the debugger. You can set breakpoints in the included source file. You can also view the assembly code by right-clicking the source line during execution and then clicking "Go to disassembly".

# Note:

The exe will segfault when it's run from a console. It works if you open the game using
Windows Explorer. See issue #1 for details. Also GDB will only work if the exe is compiled
from git-shell. The debugger fails to recognize it as an exe when compiled from MINGW.

# Resources

* [Helpful program](http://www.dependencywalker.com/) to find your exe's dependencies
* [Sean Barrett's stb project](https://github.com/nothings/stb)
