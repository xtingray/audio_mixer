# audio_mixer
# FFmpeg amix filter example
This C example gets two wav audio files and merges them to generate a new wav file using ffmpeg-4.4 API.

## Compilation command line
    export LD_LIBRARY_PATH=/usr/local/ffmpeg/lib
    gcc -I/usr/local/ffmpeg/include -L/usr/local/ffmpeg/lib audio_mixer.c -o audio_mixer -lavfilter -lavformat -lavcodec -lavutil -lmp3lame -lswresample -lswscale -lavdevice -lpostproc -lpthread -lm -ldl

## Usage
    ./audio_mixer audio_input1.wav audio_input2.wav audio_output.wav
