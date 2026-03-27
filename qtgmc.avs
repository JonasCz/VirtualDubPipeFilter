# Reads YUV4MPEG2 from stdin, fed by ffmpeg converting PipeFilter's raw BGRA frames.
RawSourcePlus("-")

# Set field order to match your source.
# AssumeTFF() = Top Field First  (most HD broadcast/Blu-ray content)
# AssumeBFF() = Bottom Field First (SD/NTSC/DV content)
AssumeTFF()

QTGMC(Preset="Fast", FPSDivisor=1)
