#;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
# NORMAL Profile   Transcoding with normal quality
# Profile setting for tvpvrd
#;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

############################################################################
# encoder - HW MP2 Encoder setting
############################################################################
# These settings control the output of the HW MP2 encoder on the Capture
# card.
# While it is perfectly possible to save and only keep the stored MP2 stream
# it will usually be quite large. To get "average" quality on a full
# screen viewing requires ~1.3GB - 1.6GB of disk space per recorded hour
# and ~2 times more if you want to have really high quality.
# As comparison "DVD" quality corresponds to roughly ~4GB/hour of MP2
# recording
# As a general rule the h.264 transcoding will reduce the size by a factor
# of ~4 while maintaining the ~same video quality. This means that each
# hour of h.264 encoding correspnds to roughly ~400MB of storage.
# Video quality is highly subjective and these figures are only given as
# an indication. Your preference might be different.
# If you don't intend to keep the MP2 files then it is probably a good idea
# to use a quite high data reate (>= 4Mbps) in order to have good quality
# for the transcoding process to work with. Of course, the larger input
# file the transcoder have to work with the longer time it takes.
############################################################################
[encoder]

#----------------------------------------------------------------------------
# KEEP_MP2FILE boolean
# Keep the original MP2 file from the video card after transcoding
#----------------------------------------------------------------------------
keep_mp2file=no

#----------------------------------------------------------------------------
# DEFAULT_VIDEO_BITRATE integer [500 000, 10 000 000]
# DEFAULT_VIDEO_PEAK_BITRATE integer
# Deafult HW MP2 encoding bitrates in bps (bits per second).
# Values must be in range 500,000 up to 10,000,000
# Default values are 4.2Mbps, 5.0Mbps
#----------------------------------------------------------------------------
video_bitrate=4200000
video_peak_bitrate=5000000

#----------------------------------------------------------------------------
# VIDEO_FRAME_SIZE string
# This is the sie of the frame we request from the HW encoder on
# the TV card. By default the size will be full PAL size (720x576)
# on a PAL system. NTSC will have a different default size.
# It is not really necesary to requre such a "large" frame for 
# good-enough quality when transcoding to MP4 and viewing on computer.
# To reduce the file size it is often enough to use "3q" format
# with a lower bitrate (~3Mbps - 4Mbps) on the MP2 encoding before we
# do the transcoding to MP4. Of course if you want really high quality
# you can keep the default size but you also should increase the
# the video bitrate since the bits must now be enough for a larger
#  
# For good quality everyday viewing 3qmp4 with ~3Mbps MP2 bitrate is
# adequate for most viewers.
#  
# Supported named formats at the present are
#  pal     = 720,576 (For PAL video standard)
#  vga     = 640,480
#  qvga    = 320,240
#  qqvga   = 160,120
#  cif     = 352,288
#  3q      = 480,384 ("3 quarter default size")
#  3qmp4   = 480,352 ("3 quarter default size" optimized for ffmpeg MP4)
#  half    = 360,288
#----------------------------------------------------------------------------
video_frame_size=pal

#----------------------------------------------------------------------------
# AUDIO_SAMPLING integer
# Audio sampling frequency used by the capture card
# Possible values are: 
# 0 = 44100 Hz 
# 1 = 48000 Hz (HW Default)
# 2 = 32000 Hz
#----------------------------------------------------------------------------
audio_sampling=0

#----------------------------------------------------------------------------
# AUDIO_BITRATE integer
# Default audio bitrate for MPEG Layer 2/layer 3 used by the HW encoder
# Possible values are: 
# 9  = 192kpbs 
# 10 = 224kpbs (HW Default)
# 11 = 256kpbs 
# 12 = 320kbps 
# 13 = 384kbps
#
# Note: by setting the transcoding option "acodec" to "copy" the original
# audio from the HW encoding (with this bitrate) will be copied straight
# through to the transcoded file. This gives the best sound quality since
# it avoids re-coding the audio which is almost always bound to create
# sound "artifacts".
# Note: When using the copy option the ffmpeg encoder does not put any
# meta information in the container about the audio stream (since it doesn't)
# seem to know what it is. This means that Windows media player cannot
# play the audio. However using VLC (for example works well). If you know
# that you need Windows MP compatibility use a high data rate here and
# then make sure your profile does transcoding to either acc or mp3.
#----------------------------------------------------------------------------
audio_bitrate=11

#----------------------------------------------------------------------------
# VIDEO_ASPECT
# Default video aspect rate
# This will not affect the video frame size but it will affect how
# the pixels w/h should be interpretated by the player. To understand this
# parmatere fully it is necessary to recall that MP2 encoded video is
# anamorphic and that video "pixels" does not correspond to screen pixels.
# This parmater can be thought of as pre-filtering the MP2 stream in
# anticipation on how it will be played.
# Possible values are: 
# 0  = 1:1 
# 1  = 4:3 (HW Default)
# 2  = 16:9 
# 3  = 221:100
#----------------------------------------------------------------------------
video_aspect=1


############################################################################
# ffmppeg - Settings for ffmpeg transcoding
# Notes for good quality:
# - It is more efficient to have the HW encoder do as much work as possible
# - Keep the same size on the HW encoder frames as the ffmpeg output size
# - Avoid transcoding audio unless you have a very high quality
#   setting on the HW audio encoder >= 256kbps to avoid artificacts.
# - Remember that to reduce the overall file size most humans can better
#   tolerate lower quality video than low quality sound
############################################################################
[ffmpeg]

#----------------------------------------------------------------------------
# USE_TRANSCODING boolean
# Should transcoding be used at all for this profile
#----------------------------------------------------------------------------
use_transcoding=yes

#----------------------------------------------------------------------------
# VIDEO_BITRATE integer [100,1500]
# Average video bitrate in kbps
#----------------------------------------------------------------------------
video_bitrate=1000

#----------------------------------------------------------------------------
# VIDEO_PEAK_BITRATE integer [200,1800]
# The tolerance for deviation in bits from the set average bitrate when
# doing one pass encoding. For two pass encoding this parameter has no
# meaning.
#----------------------------------------------------------------------------
video_peak_bitrate=1500

#----------------------------------------------------------------------------
# VCODEC string
# The video codec to be used
#----------------------------------------------------------------------------
vcodec=libx264

#----------------------------------------------------------------------------
# VPRE string
# The preset used with the vcodec. Corresponds to the ffmpeg -vpre option
#----------------------------------------------------------------------------
vpre=medium

#----------------------------------------------------------------------------
# PASS integer [1,2]
# Number of encoding pass. Must be 1 or 2
#----------------------------------------------------------------------------
pass=1

#----------------------------------------------------------------------------
# ACODEC string
# This is depending on the libraries installed and supported for
# ffmpeg. If you want to use MP3 encoding instead it might not work
# by just specifying "mp3" it might be necessary to specify
# libmp3lame in roder to do MP3 encoding.
# Other options might be "ac3" or "vorbis" for OGG encoding.
# NOTE: Not all players can handle all audio stream codec.
# NOTE: By setting the codec to "copy" the original audio
# from the HW encoding will be copied straight through to the transcoded
# file. Note on this note: Windows mediaplyer have problem decoding a copied
# stream by ffmpeg. 
#----------------------------------------------------------------------------
acodec=libfaac

#----------------------------------------------------------------------------
# AUDIO_BITRATE integer [32,320]
# Audio bitrate in kbps for the encoder specified above
# Note: If "acodec" is set to "copy" this option will have no effect
#----------------------------------------------------------------------------
audio_bitrate=128

#----------------------------------------------------------------------------
# VIDEO_SIZE string-menu
# Resulting video size for MP4 stream. If this is not set or set to the empty
# string the same size as the original MP2 stream will be used
# frame size as the original MP2 stream
# The following abbrevations are recognized
#          sqcif      128x96
#          qcif       176x144
#          cif        352x288
#          4cif       704x576
#          qqvga      160x120
#          qvga       320x240
#          vga        640x480
# Note: The native (anamorphic) size for a PAL MP2 recording is 720x576
#----------------------------------------------------------------------------
video_size=

#----------------------------------------------------------------------------
# FFMPEG_EXTRA_OPTIONS string
# Any additional options to give to ffmpeg (see ffmpeg(1))
#----------------------------------------------------------------------------
ffmpeg_extra_options=

#----------------------------------------------------------------------------
# FILE_EXTENSION string
# The file extension (including '.') to be used on the resulting file
# of the transcoding
#----------------------------------------------------------------------------
file_extension=".mp4"

# EOF
