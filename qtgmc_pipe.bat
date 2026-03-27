@echo off
REM QTGMC deinterlacing pipeline for VirtualDub PipeFilter
REM
REM Usage (called by PipeFilter automatically):
REM   qtgmc_pipe.bat <width> <height> <fpsnum> <fpsden>
REM
REM In PipeFilter's "Command" field enter:
REM   "C:\full\path\to\qtgmc_pipe.bat" %(width) %(height) %(fpsnum) %(fpsden)
REM
REM Pipeline:
REM   PipeFilter stdin (raw BGRA, top-down)
REM     -> ffmpeg (rawvideo bgra -> yuv4mpegpipe yuv420p)
REM     -> avs2yuv (RawSourcePlus reads y4m, runs QTGMC, outputs y4m)
REM     -> ffmpeg (yuv4mpegpipe -> rawvideo bgra)
REM   -> PipeFilter stdout
REM
REM NOTE: FPSDivisor=1 means QTGMC outputs one frame per field = DOUBLE the input
REM frame rate. VirtualDub receives output at 2x fps. Set "Buffer frames (lag)"
REM high enough for QTGMC's internal lookahead (Preset="Slower" needs ~30-60 frames).
REM
REM Requirements:
REM   ffmpeg.exe   in PATH  https://ffmpeg.org/download.html
REM   avs2yuv.exe  in PATH  https://github.com/MasterNobody/avs2yuv/releases
REM   AviSynth+    installed https://github.com/AviSynth/AviSynthPlus/releases
REM   RawSourcePlus plugin   https://github.com/Asd-g/RawSource_2.6x/releases
REM   QTGMC plugin (part of mvtools2 + havsfunc)

ffmpeg -hide_banner -loglevel error -f rawvideo -pix_fmt bgra -s %1x%2 -r %3/%4 -i - -f yuv4mpegpipe -pix_fmt yuv420p - | avs2yuv64 "%~dp0qtgmc.avs" -o - | ffmpeg -hide_banner -loglevel error -f yuv4mpegpipe -i - -f rawvideo -pix_fmt bgra -
