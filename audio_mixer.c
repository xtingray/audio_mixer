#include <libavformat/avformat.h>

#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libavutil/opt.h"

#define INPUT_SAMPLERATE     44100
#define INPUT_FORMAT         AV_SAMPLE_FMT_S16
#define INPUT_CHANNEL_LAYOUT AV_CH_LAYOUT_STEREO

// The output bit rate in kbit/s 
#define OUTPUT_BIT_RATE 44100
// The number of output channels
#define OUTPUT_CHANNELS 2
// The audio sample output format
#define OUTPUT_SAMPLE_FORMAT AV_SAMPLE_FMT_S16

#define VOLUME_VAL 0.90

AVFormatContext *output_format_context = NULL;
AVCodecContext *output_codec_context = NULL;

AVFormatContext *input_format_context_0 = NULL;
AVCodecContext *input_codec_context_0 = NULL;
AVFormatContext *input_format_context_1 = NULL;
AVCodecContext *input_codec_context_1 = NULL;

AVFilterGraph *graph;
AVFilterContext *src0, *src1, *sink;

static char *const get_error_text(const int error)
{
    static char error_buffer[255];
    av_strerror(error, error_buffer, sizeof(error_buffer));
    return error_buffer;
}

static int init_filter_graph(AVFilterGraph **graph, AVFilterContext **src0, AVFilterContext **src1, AVFilterContext **sink)
{
    AVFilterGraph *filter_graph;
    AVFilterContext *abuffer1_ctx;
    const AVFilter  *abuffer1;
    AVFilterContext *abuffer0_ctx;
    const AVFilter  *abuffer0;
    AVFilterContext *mix_ctx;
    const AVFilter  *mix_filter;
    AVFilterContext *abuffersink_ctx;
    const AVFilter  *abuffersink;
    
    char args[512];
    int err;
    
    // Create a new filtergraph, which will contain all the filters.
    filter_graph = avfilter_graph_alloc();
    if (!filter_graph) {
        av_log(NULL, AV_LOG_ERROR, "Unable to create filter graph.\n");
        return AVERROR(ENOMEM);
    }
    
    /****** abuffer 0 ********/
    
    // Create the abuffer filter;
    // it will be used for feeding the data into the graph.
    abuffer0 = avfilter_get_by_name("abuffer");
    if (!abuffer0) {
        av_log(NULL, AV_LOG_ERROR, "Could not find the abuffer filter.\n");
        return AVERROR_FILTER_NOT_FOUND;
    }
    
    // buffer audio source: the decoded frames from the decoder will be inserted here.
    if (!input_codec_context_0->channel_layout)
        input_codec_context_0->channel_layout = av_get_default_channel_layout(input_codec_context_0->channels);
    snprintf(args, sizeof(args),
             "sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
             input_codec_context_0->sample_rate,
             av_get_sample_fmt_name(input_codec_context_0->sample_fmt), input_codec_context_0->channel_layout);
    
    
    err = avfilter_graph_create_filter(&abuffer0_ctx, abuffer0, "src0",
                                       args, NULL, filter_graph);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
        return err;
    }
    
    // abuffer 1
    
    // Create the abuffer filter;
    // it will be used for feeding the data into the graph.
    abuffer1 = avfilter_get_by_name("abuffer");
    if (!abuffer1) {
        av_log(NULL, AV_LOG_ERROR, "Could not find the abuffer filter.\n");
        return AVERROR_FILTER_NOT_FOUND;
    }
    
    // buffer audio source: the decoded frames from the decoder will be inserted here.
    if (!input_codec_context_1->channel_layout)
        input_codec_context_1->channel_layout = av_get_default_channel_layout(input_codec_context_1->channels);

    snprintf(args, sizeof(args), "sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
             input_codec_context_1->sample_rate,
             av_get_sample_fmt_name(input_codec_context_1->sample_fmt), input_codec_context_1->channel_layout);
    
    err = avfilter_graph_create_filter(&abuffer1_ctx, abuffer1, "src1", args, NULL, filter_graph);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
        return err;
    }
    
    // amix
    // Create mix filter.
    mix_filter = avfilter_get_by_name("amix");
    if (!mix_filter) {
        av_log(NULL, AV_LOG_ERROR, "Could not find the mix filter.\n");
        return AVERROR_FILTER_NOT_FOUND;
    }
    
    snprintf(args, sizeof(args), "inputs=2");
    
    err = avfilter_graph_create_filter(&mix_ctx, mix_filter, "amix", args, NULL, filter_graph);

    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create audio amix filter\n");
        return err;
    }
    
    // Finally create the abuffersink filter;
    // it will be used to get the filtered data out of the graph.

    abuffersink = avfilter_get_by_name("abuffersink");
    if (!abuffersink) {
        av_log(NULL, AV_LOG_ERROR, "Could not find the abuffersink filter.\n");
        return AVERROR_FILTER_NOT_FOUND;
    }
    
    abuffersink_ctx = avfilter_graph_alloc_filter(filter_graph, abuffersink, "sink");
    if (!abuffersink_ctx) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate the abuffersink instance.\n");
        return AVERROR(ENOMEM);
    }
    
    // Same sample fmts as the output file.
    err = av_opt_set_int_list(abuffersink_ctx, "sample_fmts",
                              ((int[]){ AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE }),
                              AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    
    uint8_t ch_layout[64];
    av_get_channel_layout_string(ch_layout, sizeof(ch_layout), 0, OUTPUT_CHANNELS);
    av_opt_set(abuffersink_ctx, "channel_layout", ch_layout, AV_OPT_SEARCH_CHILDREN);
    
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could set options to the abuffersink instance.\n");
        return err;
    }
    
    err = avfilter_init_str(abuffersink_ctx, NULL);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not initialize the abuffersink instance.\n");
        return err;
    }
    
    // Connect the filters
    
    err = avfilter_link(abuffer0_ctx, 0, mix_ctx, 0);
    if (err >= 0)
        err = avfilter_link(abuffer1_ctx, 0, mix_ctx, 1);
    if (err >= 0)
        err = avfilter_link(mix_ctx, 0, abuffersink_ctx, 0);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error connecting filters\n");
        return err;
    }
    
    // Configure the graph.
    err = avfilter_graph_config(filter_graph, NULL);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while configuring graph : %s\n", get_error_text(err));
        return err;
    }
    
    char* dump =avfilter_graph_dump(filter_graph, NULL);
    av_log(NULL, AV_LOG_ERROR, "Graph :\n%s\n", dump);
    
    *graph = filter_graph;
    *src0  = abuffer0_ctx;
    *src1  = abuffer1_ctx;
    *sink  = abuffersink_ctx;
    
    return 0;
}

// Open an input file and the required decoder.
static int open_input_file(const char *filename,
                           AVFormatContext **input_format_context,
                           AVCodecContext **input_codec_context)
{
    AVCodec *input_codec;
    AVStream *in_stream;
    AVCodecParameters *in_codecpar;
    enum AVCodecID audio_codec_id;
    int error;
    
    // Open the input file to read from it.
    if ((error = avformat_open_input(input_format_context, filename, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not open input file '%s' (error '%s')\n",
               filename, get_error_text(error));
        *input_format_context = NULL;
        return error;
    }
    
    // Get information on the input file (number of streams etc.).
    if ((error = avformat_find_stream_info(*input_format_context, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not open find stream info (error '%s')\n",
               get_error_text(error));
        avformat_close_input(input_format_context);
        return error;
    }
    
    // Make sure that there is only one stream in the input file.
    if ((*input_format_context)->nb_streams != 1) {
        av_log(NULL, AV_LOG_ERROR, "Expected one audio input stream, but found %d\n",
               (*input_format_context)->nb_streams);
        avformat_close_input(input_format_context);
        return AVERROR_EXIT;
    }

    av_dump_format((*input_format_context), 0, filename, 0);

    in_stream = (*input_format_context)->streams[0];
    in_codecpar = in_stream->codecpar;
    if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
        av_log(NULL, AV_LOG_ERROR, "File input has no stream audio -> %s\n", filename);
        return -1;
    }

    audio_codec_id = in_codecpar->codec_id;
    
    // Find a decoder for the audio stream.
    if (!(input_codec = avcodec_find_decoder(audio_codec_id))) {
        av_log(NULL, AV_LOG_ERROR, "Could not find input codec\n");
        avformat_close_input(input_format_context);
        return AVERROR_EXIT;
    }

    // Creating codec context for the input file
    *input_codec_context = avcodec_alloc_context3(input_codec);
    if (!(*input_codec_context)) {
        av_log(NULL, AV_LOG_ERROR, "Could not alloc memory for input codec context\n");
        return -1;
    }

    error = avcodec_parameters_to_context((*input_codec_context), (*input_format_context)->streams[0]->codecpar);
    if (error < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't copy codecpar values to codec context (error '%s')\n",
               get_error_text(error));
        return error;
    }
    
    // Open the decoder for the audio stream to use it later.
    if ((error = avcodec_open2((*input_codec_context), input_codec, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not open input codec (error '%s')\n", get_error_text(error));
        avformat_close_input(input_format_context);
        return error;
    }
    
    return 0;
}

/**
 * Open an output file and the required encoder.
 * Also set some basic encoder parameters.
 * Some of these parameters are based on the input file's parameters.
 */
static int open_output_file(const char *filename,
                            AVCodecContext *input_codec_context,
                            AVFormatContext **output_format_context,
                            AVCodecContext **output_codec_context)
{
    AVIOContext *output_io_context = NULL;
    AVStream *stream               = NULL;
    AVCodec *output_codec          = NULL;
    int error;
    
    // Open the output file to write to it.
    if ((error = avio_open(&output_io_context, filename,
                           AVIO_FLAG_WRITE)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not open output file '%s' (error '%s')\n",
               filename, get_error_text(error));
        return error;
    }
    
    // Create a new format context for the output container format.
    if (!(*output_format_context = avformat_alloc_context())) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate output format context\n");
        return AVERROR(ENOMEM);
    }
    
    // Associate the output file (pointer) with the container format context.
    (*output_format_context)->pb = output_io_context;
    
    // Guess the desired container format based on the file extension.
    if (!((*output_format_context)->oformat = av_guess_format(NULL, filename, NULL))) {
        av_log(NULL, AV_LOG_ERROR, "Could not find output file format\n");
        goto cleanup;
    }
   
    // Find the encoder to be used by its name.
    if (!(output_codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE))) {
        av_log(NULL, AV_LOG_ERROR, "Could not find an PCM encoder.\n");
        goto cleanup;
    }
    
    // Create a new audio stream in the output file container.
    if (!(stream = avformat_new_stream(*output_format_context, output_codec))) {
        av_log(NULL, AV_LOG_ERROR, "Could not create new stream\n");
        error = AVERROR(ENOMEM);
        goto cleanup;
    }

    // Save the encoder context for easiert access later.
    *output_codec_context = stream->codec;

    /*
    *output_codec_context = avcodec_alloc_context3(output_codec);
    if (*output_codec_context) {
        return -1;
    }
    error = avcodec_parameters_to_context((*output_codec_context), stream->codecpar);
    if (error < 0) {
        return -1;
    }
    */
    
    // Set the basic encoder parameters.
    (*output_codec_context)->channels       = OUTPUT_CHANNELS;
    (*output_codec_context)->channel_layout = av_get_default_channel_layout(OUTPUT_CHANNELS);
    // (*output_codec_context)->sample_rate = input_codec_context->sample_rate;
    (*output_codec_context)->sample_rate    = 44100;
    (*output_codec_context)->sample_fmt     = AV_SAMPLE_FMT_S16;
    //(*output_codec_context)->bit_rate     = input_codec_context->bit_rate;
    
    av_log(NULL, AV_LOG_INFO, "output bitrate %" PRIu64 "\n", (*output_codec_context)->bit_rate);
    
    // Some container formats (like MP4) require global headers to be present
    // Mark the encoder so that it behaves accordingly.

    if ((*output_format_context)->oformat->flags & AVFMT_GLOBALHEADER)
        (*output_codec_context)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    
    // Open the encoder for the audio stream to use it later.
    if ((error = avcodec_open2(*output_codec_context, output_codec, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not open output codec (error '%s')\n",
               get_error_text(error));
        goto cleanup;
    }
    
    return 0;
    
    cleanup:
        avio_close((*output_format_context)->pb);
        avformat_free_context(*output_format_context);
        *output_format_context = NULL;

    return error < 0 ? error : AVERROR_EXIT;
}

// Initialize one audio frame for reading from the input file
static int init_input_frame(AVFrame **frame)
{
    if (!(*frame = av_frame_alloc())) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate input frame\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

// Initialize one data packet for reading or writing.
static void init_packet(AVPacket *packet)
{
    packet = av_packet_alloc();
    // Set the packet data and size so that it is recognized as being empty.
    packet->data = NULL;
    packet->size = 0;
}

// Decode one audio frame from the input file.
static int decode_audio_frame(AVFrame *frame,
                              AVFormatContext *input_format_context,
                              AVCodecContext *input_codec_context,
                              int *data_present, int *finished)
{
    // Packet used for temporary storage.
    AVPacket input_packet;
    int error;
    init_packet(&input_packet);
    
    // Read one audio frame from the input file into a temporary packet.
    if ((error = av_read_frame(input_format_context, &input_packet)) < 0) {
        // If we are the the end of the file, flush the decoder below.
        if (error == AVERROR_EOF)
            *finished = 1;
        else {
            av_log(NULL, AV_LOG_ERROR, "Could not read frame (error '%s')\n",
                   get_error_text(error));
            return error;
        }
    }
    
    // Decode the audio frame stored in the temporary packet.
    // The input audio stream decoder is used to do this.
    // If we are at the end of the file, pass an empty packet to the decoder
    // to flush it.
    if ((error = avcodec_decode_audio4(input_codec_context, frame,
                                       data_present, &input_packet)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not decode frame (error '%s')\n",
               get_error_text(error));
        av_packet_unref(&input_packet);
        return error;
    }
    
    // If the decoder has not been flushed completely, we are not finished,
    // so that this function has to be called again.
    if (*finished && *data_present)
        *finished = 0;
    av_packet_unref(&input_packet);

    return 0;
}

// Encode one frame worth of audio to the output file.
static int encode_audio_frame(AVFrame *frame,
                              AVFormatContext *output_format_context,
                              AVCodecContext *output_codec_context,
                              int *data_present)
{
    // Packet used for temporary storage.
    AVPacket output_packet;
    int error;
    init_packet(&output_packet);
    
    // Encode the audio frame and store it in the temporary packet.
    // The output audio stream encoder is used to do this.
    if ((error = avcodec_encode_audio2(output_codec_context, &output_packet,
                                       frame, data_present)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not encode frame (error '%s')\n",
               get_error_text(error));
        av_packet_unref(&output_packet);
        return error;
    }
    
    // Write one audio frame from the temporary packet to the output file.
    if (*data_present) {
        if ((error = av_write_frame(output_format_context, &output_packet)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Could not write frame (error '%s')\n",
                   get_error_text(error));
            av_packet_unref(&output_packet);
            return error;
        }
        
        av_packet_unref(&output_packet);
    }
    
    return 0;
}

static int process_all(){
    int ret = 0;
    
    int data_present = 0;
    int finished = 0;
    
    int nb_inputs = 2;
    
    AVFormatContext* input_format_contexts[2];
    AVCodecContext* input_codec_contexts[2];
    input_format_contexts[0] = input_format_context_0;
    input_format_contexts[1] = input_format_context_1;
    input_codec_contexts[0] = input_codec_context_0;
    input_codec_contexts[1] = input_codec_context_1;
    
    AVFilterContext* buffer_contexts[2];
    buffer_contexts[0] = src0;
    buffer_contexts[1] = src1;
    
    int input_finished[2];
    input_finished[0] = 0;
    input_finished[1] = 0;
    
    int input_to_read[2];
    input_to_read[0] = 1;
    input_to_read[1] = 1;
    
    int total_samples[2];
    total_samples[0] = 0;
    total_samples[1] = 0;
    
    int total_out_samples = 0;
    int nb_finished = 0;
    
    while (nb_finished < nb_inputs) {
        int data_present_in_graph = 0;
        
        for (int i = 0 ; i < nb_inputs ; i++) {
            if (input_finished[i] || input_to_read[i] == 0) {
                continue;
            }
            
            input_to_read[i] = 0;
            
            AVFrame *frame = NULL;
            
            if (init_input_frame(&frame) > 0) {
                goto end;
            }
            
            // Decode one frame worth of audio samples.
            if ((ret = decode_audio_frame(frame, input_format_contexts[i], input_codec_contexts[i], &data_present, &finished))) {
                goto end;
            }

            // If we are at the end of the file and there are no more samples
            // in the decoder which are delayed, we are actually finished.
            // This must not be treated as an error.
            if (finished && !data_present) {
                input_finished[i] = 1;
                nb_finished++;
                ret = 0;
                av_log(NULL, AV_LOG_INFO, "Input n°%d finished. Write NULL frame \n", i);
                
                ret = av_buffersrc_write_frame(buffer_contexts[i], NULL);
                if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error writing EOF null frame for input %d\n", i);
                    goto end;
                }
            } else if (data_present) { // If there is decoded data, convert and store it
                // push the audio data from decoded frame into the filtergraph
                ret = av_buffersrc_write_frame(buffer_contexts[i], frame);
                if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error while feeding the audio filtergraph\n");
                    goto end;
                }
                
                av_log(NULL, AV_LOG_INFO, "add %d samples on input %d (%d Hz, time=%f, ttime=%f)\n",
                       frame->nb_samples, i, input_codec_contexts[i]->sample_rate,
                       (double)frame->nb_samples / input_codec_contexts[i]->sample_rate,
                       (double)(total_samples[i] += frame->nb_samples) / input_codec_contexts[i]->sample_rate);
                
            }
            
            av_frame_free(&frame);
            
            data_present_in_graph = data_present | data_present_in_graph;
        }
        
        if (data_present_in_graph) {
            AVFrame *filt_frame = av_frame_alloc();
            
            // pull filtered audio from the filtergraph
            while (1) {
                ret = av_buffersink_get_frame(sink, filt_frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    for (int i = 0 ; i < nb_inputs ; i++) {
                        if (av_buffersrc_get_nb_failed_requests(buffer_contexts[i]) > 0) {
                            input_to_read[i] = 1;
                            av_log(NULL, AV_LOG_INFO, "Need to read input %d\n", i);
                        }
                    }
                    
                    break;
                }
                if (ret < 0)
                    goto end;
                
                av_log(NULL, AV_LOG_INFO, "remove %d samples from sink (%d Hz, time=%f, ttime=%f)\n",
                       filt_frame->nb_samples, output_codec_context->sample_rate,
                       (double)filt_frame->nb_samples / output_codec_context->sample_rate,
                       (double)(total_out_samples += filt_frame->nb_samples) / output_codec_context->sample_rate);
                
                // av_log(NULL, AV_LOG_INFO, "Data read from graph\n");
                ret = encode_audio_frame(filt_frame, output_format_context, output_codec_context, &data_present);
                if (ret < 0)
                    goto end;
                
                av_frame_unref(filt_frame);
            }
            
            av_frame_free(&filt_frame);
        } else {
            av_log(NULL, AV_LOG_INFO, "No data in graph\n");
            for (int i=0; i<nb_inputs; i++) {
                input_to_read[i] = 1;
            }
        }

    }
    
    return 0;
    
    end:
    // avcodec_close(input_codec_context);
    // avformat_close_input(&input_format_context);
    // av_frame_free(&frame);
    // av_frame_free(&filt_frame);
    
    if (ret < 0 && ret != AVERROR_EOF) {
        av_log(NULL, AV_LOG_ERROR, "Error occurred: %s\n", av_err2str(ret));
        exit(1);
    }
    
    exit(0);
}

// Write the header of the output file container
static int write_output_file_header(AVFormatContext *output_format_context)
{
    int error;
    if ((error = avformat_write_header(output_format_context, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not write output file header (error '%s')\n",
               get_error_text(error));
        return error;
    }

    return 0;
}

// Write the trailer of the output file container.
static int write_output_file_trailer(AVFormatContext *output_format_context)
{
    int error;
    if ((error = av_write_trailer(output_format_context)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not write output file trailer (error '%s')\n",
               get_error_text(error));
        return error;
    }
    return 0;
}


int main(int argc, const char * argv[])
{
    const char* audio_input1 = "/tmp/1.mp3";
    const char* audio_input2 = "/tmp/2.mp3";
    const char* audio_output = "/tmp/test.wav";

    /*
    if (argc < 4) {
        printf("usage: ./amix audio_input1 audio_input2 audio_output\n");
        return 1;
    }

    const char* audio_input1 = argv[1];
    const char* audio_input2 = argv[2];
    const char* audio_output = argv[3];
    */

    av_log_set_level(AV_LOG_VERBOSE);
    int err;
    
    if (open_input_file(audio_input1, &input_format_context_0, &input_codec_context_0) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while opening file 1\n");
        exit(1);
    }
    // av_dump_format(input_format_context_0, 0, audio_input1, 0);
    
    if (open_input_file(audio_input2, &input_format_context_1, &input_codec_context_1) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while opening file 2\n");
        exit(1);
    }
    // av_dump_format(input_format_context_1, 0, audio_input2, 0);
    
    // Set up the filtergraph.
    err = init_filter_graph(&graph, &src0, &src1, &sink);
    printf("Init err = %d\n", err);

    remove(audio_output);
    
    av_log(NULL, AV_LOG_INFO, "Output file : %s\n", audio_output);
    
    err = open_output_file(audio_output, input_codec_context_0, &output_format_context, &output_codec_context);
    printf("open output file err : %d\n", err);
    av_dump_format(output_format_context, 0, audio_output, 1);
    
    if (write_output_file_header(output_format_context) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while writing header outputfile\n");
        exit(1);
    }

    process_all();
    
    if (write_output_file_trailer(output_format_context) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while writing header outputfile\n");
        exit(1);
    }
        
    printf("FINISHED\n");
    
    return 0;
}

