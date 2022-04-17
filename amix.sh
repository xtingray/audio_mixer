export LD_LIBRARY_PATH=/usr/local/ffmpeg/lib
gcc -I/usr/local/ffmpeg/include -L/usr/local/ffmpeg/lib amix.c -o amix -lavfilter -lavformat -lavcodec -lavutil -lmp3lame -lswresample -lswscale -lavdevice -lpostproc -lpthread -lm -ldl
