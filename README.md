# VirtualDub2 External Pipe Filter

A VirtualDub2 video filter plugin that pipes raw video frames to an external
command's stdin and reads processed frames back from its stdout. This lets you
use any command-line tool (FFmpeg, ImageMagick, custom scripts, etc.) as a
VirtualDub2 filter.

![screenshot.jpg](screenshot.jpg)

## How it works

Each frame is sent as raw BGRA pixels (top-down, no header) to the external
process via stdin. The process is expected to write back one frame of the same
size and format to stdout for every frame it receives.

## Configuration

- **Command** -- the command line to execute. Placeholders are substituted at
  runtime:
  - `%(width)` -- frame width in pixels
  - `%(height)` -- frame height in pixels
  - `%(fpsnum)` -- frame rate numerator
  - `%(fpsden)` -- frame rate denominator
  - `%(fps)` -- frame rate as a decimal number

- **Buffer frames (lag)** -- if the external command buffers frames internally
  before producing output, set this to the number of buffered frames. VirtualDub
  uses this to keep audio in sync. Minimum 1, otherwise it crashes.

## Example: FFmpeg negate filter

```
ffmpeg -f rawvideo -pix_fmt bgra -s %(width)x%(height) -r %(fpsnum)/%(fpsden) -i pipe:0 -vf negate -f rawvideo -pix_fmt bgra pipe:1
```

## Building

Requires CMake and Visual Studio 2022 Build Tools (probably other versions will also work). Edit `build.cmd` to add the right paths for your setup, and then run it.
VirtualDub2 plugins directory.

Download VDPluginSDK-1.2.zip from https://sourceforge.net/p/vdfiltermod/wiki/sdk/ and extract its contents into a directory named VDPluginSDK-1.2 next to the VirtualDubPipeFilter (this repository) directory.

Then run.

```
build.cmd
```
