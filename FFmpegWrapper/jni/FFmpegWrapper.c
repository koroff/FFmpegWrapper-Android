#include <jni.h>
#include <android/log.h>
#include <string.h>
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"

#define LOG_TAG "FFmpegWrapper"
#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Current output
const char *outputPath;
//const char *outputFormatName = "mpegts";
const char *outputFormatName = "mp4";
const char *videoCodecName = "h264";
const char *audioCodecName = "aac";
AVFormatContext *outputFormatContext;
AVStream *audioStream;
AVStream *videoStream;
AVCodec *audioCodec;
AVCodec *videoCodec;

AVPacket *packet; // recycled across calls to writeAVPacketFromEncodedData

// Example h264 file:
const char *sampleFilePath = "/sdcard/output.mp4";

// FFmpeg Utilities

void init(){
    av_register_all();
    avformat_network_init();
    avcodec_register_all();
}

char* stringForAVErrorNumber(int errorNumber){
    char *errorBuffer = malloc(sizeof(char) * AV_ERROR_MAX_STRING_SIZE);

    int strErrorResult = av_strerror(errorNumber, errorBuffer, AV_ERROR_MAX_STRING_SIZE);
    if (strErrorResult != 0) {
        LOGE("av_strerror error: %d", strErrorResult);
        return NULL;
    }
    return errorBuffer;
}

void copyAVFormatContext(AVFormatContext **dest, AVFormatContext **source){
    int numStreams = (*source)->nb_streams;
    LOGI("copyAVFormatContext source has %d streams", numStreams);
    LOGI("codec_id note: AAC : %d, H264 : %d", CODEC_ID_AAC, CODEC_ID_H264);
    int i;
    for (i = 0; i < numStreams; i++) {
        // Get input stream
        AVStream *inputStream = (*source)->streams[i];
        AVCodecContext *inputCodecContext = inputStream->codec;

        // Add new stream to output with codec from input stream
        AVStream *outputStream = avformat_new_stream(*dest, inputCodecContext->codec);
        AVCodecContext *outputCodecContext = outputStream->codec;

        // Copy input stream's codecContext for output stream's codecContext
        avcodec_copy_context(outputCodecContext, inputCodecContext);
        LOGI("copyAVFormatContext Copying stream %d with codec_id %i", i, inputCodecContext->codec_id);
    }
}

// FFInputFile functions
// Using these to deduce codec parameters from test file

AVFormatContext* avFormatContextForInputPath(const char *inputPath, const char *inputFormatString){
    // You can override the detected input format
    AVFormatContext *inputFormatContext = NULL;
    AVInputFormat *inputFormat = NULL;
    //AVDictionary *inputOptions = NULL;

    if (inputFormatString) {
        inputFormat = av_find_input_format(inputFormatString);
        LOGI("avFormatContextForInputPath got inputFormat from string");
    }
    LOGI("avFormatContextForInputPath post av_Find_input_format");
    // It's possible to send more options to the parser
    // av_dict_set(&inputOptions, "video_size", "640x480", 0);
    // av_dict_set(&inputOptions, "pixel_format", "rgb24", 0);
    // av_dict_free(&inputOptions); // Don't forget to free

    LOGI("avFormatContextForInputPath pre avformat_open_input path: %s format: %s", inputPath, inputFormatString);
    int openInputResult = avformat_open_input(&inputFormatContext, inputPath, inputFormat, /*&inputOptions*/ NULL);
    LOGI("avFormatContextForInputPath avformat_open_input result: %d", openInputResult);
    if (openInputResult != 0) {
        LOGE("avformat_open_input failed: %s", stringForAVErrorNumber(openInputResult));
        avformat_close_input(&inputFormatContext);
        return NULL;
    }

    int streamInfoResult = avformat_find_stream_info(inputFormatContext, NULL);
    LOGI("avFormatContextForInputPath avformat_find_stream_info result: %d", streamInfoResult);
    if (streamInfoResult < 0) {
        avformat_close_input(&inputFormatContext);
        LOGE("avformat_find_stream_info failed: %s", stringForAVErrorNumber(openInputResult));
        return NULL;
    }
    return inputFormatContext;
}

// FFOutputFile functions

AVFormatContext* avFormatContextForOutputPath(const char *path, const char *formatName){
    AVFormatContext *outputFormatContext;
    LOGI("avFormatContextForOutputPath format: %s path: %s", formatName, path);
    int openOutputValue = avformat_alloc_output_context2(&outputFormatContext, NULL, formatName, path);
    if (openOutputValue < 0) {
        avformat_free_context(outputFormatContext);
    }
    return outputFormatContext;
}

int openFileForWriting(AVFormatContext *avfc, const char *path){
    if (!(avfc->oformat->flags & AVFMT_NOFILE)) {
        return avio_open(&avfc->pb, path, AVIO_FLAG_WRITE);
    }
    return -42;
}

int writeFileHeader(AVFormatContext *avfc){
    AVDictionary *options = NULL;

    // Write header for output file
    int writeHeaderResult = avformat_write_header(avfc, &options);
    if (writeHeaderResult < 0) {
        av_dict_free(&options);
    }
    av_dict_free(&options);
    return writeHeaderResult;
}

int writeFileTrailer(AVFormatContext *avfc){
    return av_write_trailer(avfc);
}

// FFOuputStream
/* Olde testing
int prepareStream(AVCodec **codec, AVStream **stream, AVFormatContext *avfc, const char *codecName){
    AVCodecContext *cc;

    *codec = avcodec_find_encoder_by_name(codecName);
    if(!(*codec)){ LOGE("find_encoder %s failed", codecName); }
    *stream = avformat_new_stream(avfc, *codec);
    if(!(*stream)){ LOGE("avformat_new_stream failed"); }

    cc = (*stream)->codec;

    // read codec params from file
    avcodec_get_context_defaults3(cc, *codec);

    // test manually providing codec params
    if(codecName == videoCodecName){
        LOGI("setting video cc params");
        cc->codec_id = CODEC_ID_H264;
        cc->bit_rate = 1000000;
        cc->width = 640;
        cc->height = 480;
        cc->time_base.den = 30;
        cc->time_base.num = 1;
    }else if(codecName == audioCodecName){
        LOGI("setting audio cc params");
        cc->codec_id = CODEC_ID_AAC;
        cc->sample_fmt  = AV_SAMPLE_FMT_S16;
        cc->bit_rate    = 128000;
        cc->sample_rate = 44100;
        cc->channels    = 1;
    }

    if (avfc->oformat->flags & AVFMT_GLOBALHEADER)
            cc->flags |= CODEC_FLAG_GLOBAL_HEADER;

}
*/

  /////////////////////
  //  JNI FUNCTIONS  //
  /////////////////////

void Java_net_openwatch_ffmpegwrapper_FFmpegWrapper_prepareAVFormatContext(JNIEnv *env, jobject obj, jstring jOutputPath){
    init();

    AVFormatContext *inputFormatContext;
    outputPath = (*env)->GetStringUTFChars(env, jOutputPath, NULL);

    outputFormatContext = avFormatContextForOutputPath(outputPath, outputFormatName);
    LOGI("post avFormatContextForOutputPath");
    inputFormatContext = avFormatContextForInputPath(sampleFilePath, outputFormatName);
    LOGI("post avFormatContextForInputPath");
    copyAVFormatContext(&outputFormatContext, &inputFormatContext);
    LOGI("post copyAVFormatContext");

    int result = openFileForWriting(outputFormatContext, outputPath);
    if(result < 0){
        LOGE("openFileForWriting error: %d", result);
    }

    writeFileHeader(outputFormatContext);

    // will prepare streams by copying from inputFormatContext
    //prepareStream(&audioCodec, &audioStream, outputFormatContext, audioCodecName);
    //prepareStream(&videoCodec, &videoStream, outputFormatContext, videoCodecName);
}

void Java_net_openwatch_ffmpegwrapper_FFmpegWrapper_writeAVPacketFromEncodedData(JNIEnv *env, jobject obj, jobject jData, jint jIsVideo, jint jOffset, jint jSize, jint jFlags, jlong jFrameCount){
    if(packet == NULL){
        packet = av_malloc(sizeof(AVPacket));
    }

    av_init_packet(packet);
    uint8_t *data = (*env)->GetDirectBufferAddress(env, jData);
    LOGI("writeAVPacketFromEncodedData video: %d length %d", (int) jIsVideo, (int) jSize);
    int avPacketFromDataResult = av_packet_from_data(packet, data, (int) jSize);
    if(avPacketFromDataResult < 0){
        LOGE("av_packet_from_data error: %s",stringForAVErrorNumber(avPacketFromDataResult));
    }

    /*
    packet->data = data;
    packet->size = (int) jSize;
    packet->pts = (int64_t) ((long) jFrameCount);
    packet->dts = (int64_t) ((long) jFrameCount);

    // unknown params:
    packet->duration = 0;
    packet->pos = -1;
    */

    if( ((int) jIsVideo) == JNI_TRUE){
        LOGI("pre write_frame video");
        packet->stream_index = 0; // TODO
        // Apply bitstream filter
    }else{
        LOGI("pre write_frame audio");
        packet->stream_index = 1; // TODO
    }

    int writeFrameResult = av_interleaved_write_frame(outputFormatContext, packet);
    if(writeFrameResult < 0){
        LOGE("av_interleaved_write_frame error: %s", stringForAVErrorNumber(writeFrameResult));
    }
    LOGI("post write_frame");
    // Create AVPacket from encoded data
    // apply bitstream filter
    // av_rescale_q
    // Write packet to AVFormatContext (av_interleaved_write_frame)
}

void Java_net_openwatch_ffmpegwrapper_FFmpegWrapper_finalizeAVFormatContext(JNIEnv *env, jobject obj){
    LOGI("finalizeAVFormatContext");
    // Write file trailer (av_write_trailer)
    int writeTrailerResult = writeFileTrailer(outputFormatContext);
    if(writeTrailerResult < 0){
        LOGE("av_write_trailer error: %d", writeTrailerResult);
    }
}