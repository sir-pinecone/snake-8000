#!/bin/sh

version=64
debug=1

code_dir=$PWD

# Compiler Flags
# --------------
warning_level="-W4"
compiler_warnings="$warning_level -wd4201 -wd4100 -wd4189 -wd4505 -wd4456 -wd4146"
compiler_env="-DSNAKE_INTERNAL=1 -DSNAKE_SLOW=1 -DSNAKE_WIN32=1"

[[ $debug -eq 1 ]] && conditional_flags="-MTd -Od" || conditional_flags="-MT -O2"

# NOTE: according to MSDN, -Gm is not available when using -Z7. Also -Zi is compatible but it implies /debug mode
common_compiler_flags="$conditional_flags -nologo -fp:fast -EHa- -GS- -Gm- -GR- -Oi -WX $compiler_warnings $compiler_env -FC -Z7"

# Linker Flags
# ------------
no_junk="-incremental:no -opt:ref"
dependencies="user32.lib gdi32.lib winmm.lib"
common_linker_flags="$no_junk"
_32bit_linker="-subsystem:windows,5.02 $common_linker_flags"
_64bit_linker=$common_linker_flags
[[ $version -eq 64 ]] && common_linker="$_64bit_linker" || common_linker="$_32bit_linker"

# Linkers
# -------
platform_linker="$common_linker $dependencies"
snake_linker="$common_linker -PDB:snake_$RANDOM.pdb -DLL -EXPORT:GameGetSoundSamples -EXPORT:GameUpdateAndRender"

# Build
#------
build_path="../build/win$version"

win32_source_file="$code_dir/win32_snake_game.cpp"
snake_source_file="$code_dir/snake_game.cpp"

mkdir $build_path -p
pushd $build_path
rm $build_path/*.pdb &>/dev/null

#-----------------------------------------------------------------------------------------
# Compile!

cl $common_compiler_flags $snake_source_file -Fmsnake_game.map -LD -link $snake_linker
cl $common_compiler_flags $win32_source_file -Fmwin32_snake.map -link $platform_linker

popd
