#include "Reader.hpp"

extern "C" {
#include <libavutil/error.h>
#include <libavutil/motion_vector.h>
#include <libavutil/timestamp.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>	
}

#include <string>
#include <stdexcept>

#ifdef av_err2str
#undef av_err2str

av_always_inline std::string av_err2string(int errnum) {
    char str[AV_ERROR_MAX_STRING_SIZE];
    return av_make_error_string(str, AV_ERROR_MAX_STRING_SIZE, errnum);
}
#define av_err2str(err) av_err2string(err)
#endif  // av_err2str

class MyReader : public Reader
{
public:
    MyReader(const char* path, HandleFrameCallback callback): path(path), callback(callback)
    {
        av_log_set_level(AV_LOG_ERROR);

        printf(".");
        init_decoder_1(path);
        printf(".");
        init_encoder_2();
        printf(".");
        init_decoder_3();
    }

    ~MyReader();

    void Start()
    {
        running = true;
        decode_loop_1();
    }

    void Stop()
    {
        running = false;
    }

    int GetNumFrames()
    {
        return video_stream_1->nb_frames;
    }

    int GetNumMs()
    {
        return av_q2d(video_stream_1->time_base) * 1000;
    }

    int CurrentFrame()
    {
        return frame_number;
    }

protected:
    void init_decoder_1(const char* src_filename);
    void init_encoder_2();
    void init_decoder_3();

    void decode_loop_1();
    void encode_loop_2();
    void decode_loop_3();

    bool running = false;
    const char* path;
    HandleFrameCallback callback;
    int frame_number = 0;

    AVFormatContext *fmt_ctx = NULL;
    int video_stream_idx = -1;

    // Decoder 1
    AVDictionary* opts_1 = NULL;
    AVCodecContext *dec_ctx_1 = NULL;
    AVCodec* dec_1 = NULL;
    AVStream *video_stream_1 = NULL;
    AVFrame *frame_1 = NULL;
    AVPacket* pkt_dec_1 = NULL;

    // Endoder 2
    AVCodecContext *enc_ctx_2 = NULL;
    const AVCodec* enc_2 = NULL;
    AVPacket* pkt_enc_2 = NULL;

    // Decoder 3
    AVCodecContext *dec_ctx_3 = NULL;
    const AVCodec* dec_3 = NULL;
    AVFrame *frame_3 = NULL;
    AVDictionary* opts_3 = NULL;
};

MyReader::~MyReader()
{
    if (fmt_ctx != NULL)
        avformat_close_input(&fmt_ctx);
    if(frame_1 != NULL)
        av_frame_free(&frame_1);
    if(dec_ctx_1 != NULL)
        avcodec_free_context(&dec_ctx_1);
    if(pkt_dec_1 != NULL)
        av_packet_free(&pkt_dec_1);


    if(enc_ctx_2 != NULL)
        avcodec_free_context(&enc_ctx_2);
    if(pkt_enc_2 != NULL)
        av_packet_free(&pkt_enc_2);


    if(frame_3 != NULL)
        av_frame_free(&frame_3);
    if(dec_ctx_3 != NULL)
        avcodec_free_context(&dec_ctx_3);  
}

// Initialization

void MyReader::init_decoder_1(const char* src_filename)
{
    int ret = 0;

    frame_1 = av_frame_alloc();
	if (!frame_1) {
        throw std::runtime_error("Could not allocate frame");
	}

    pkt_dec_1 = av_packet_alloc();
    if (!pkt_dec_1) {
        throw std::runtime_error("Could not allocate packet");
    }

    av_register_all();
    avcodec_register_all();

    ret = avformat_open_input(&fmt_ctx, src_filename, NULL, NULL);
    if (ret < 0) {
        throw std::runtime_error("Could not open source file: " + av_err2str(ret));
	}

    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
		throw std::runtime_error("Could not find stream information");
	}

	//av_dump_format(fmt_ctx, 0, src_filename, 0);

    video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec_1, 0);
	if (video_stream_idx < 0) {
        throw std::runtime_error("Could not find stream");
	}

    // dec_1 = avcodec_find_decoder_by_name("h264_cuvid");

    video_stream_1 = fmt_ctx->streams[video_stream_idx];

    dec_ctx_1 = avcodec_alloc_context3(dec_1);
    if (!dec_ctx_1) {
        throw std::runtime_error("Could not allocate a decoding context");
    }

    ret = avcodec_parameters_to_context(dec_ctx_1, video_stream_1->codecpar);
    if (ret < 0) {
        throw std::runtime_error("Could not copy codec parameters to decoder context");
    }

    dec_ctx_1->thread_count = 0;

    if (dec_1->capabilities & AV_CODEC_CAP_FRAME_THREADS)
        dec_ctx_1->thread_type = FF_THREAD_FRAME;
    else if (dec_1->capabilities & AV_CODEC_CAP_SLICE_THREADS)
        dec_ctx_1->thread_type = FF_THREAD_SLICE;
    else
        dec_ctx_1->thread_count = 1; //don't use multithreading

    av_dict_set(&opts_1, "flags2", "+export_mvs", 0);
    ret = avcodec_open2(dec_ctx_1, dec_1, &opts_1);
    av_dict_free(&opts_1);
    if (ret < 0) {
        throw std::runtime_error("Could not open codec");
    }
}

void MyReader::init_encoder_2()
{
    int ret = 0;

    pkt_enc_2 = av_packet_alloc();
    if (!pkt_enc_2) {
        throw std::runtime_error("Could not allocate packet");
    }

    enc_2 = avcodec_find_encoder_by_name("libx264");
    if(!enc_2) {
        throw std::runtime_error("Could not find encoder");
    }

    enc_ctx_2 = avcodec_alloc_context3(enc_2);
    if (!enc_ctx_2) {
        throw std::runtime_error("Could not allocate an encoding context");
    }

    enc_ctx_2->width = dec_ctx_1->width;
    enc_ctx_2->height = dec_ctx_1->height;
    enc_ctx_2->pix_fmt = dec_ctx_1->pix_fmt;

    enc_ctx_2->time_base = {1, 25};
    enc_ctx_2->framerate = {25, 1};
    enc_ctx_2->framerate = video_stream_1->avg_frame_rate;

    enc_ctx_2->max_b_frames = 0;
    enc_ctx_2->gop_size = 100000;

    // av_opt_set(enc_ctx_2->priv_data, "preset", "slow", 0);

    ret = avcodec_open2(enc_ctx_2, enc_2, NULL);
    if (ret < 0) {
        throw std::runtime_error("Could not open encoder codec");
    }
}

void MyReader::init_decoder_3()
{
    int ret = 0;

    frame_3 = av_frame_alloc();
	if (!frame_3) {
        throw std::runtime_error("Could not allocate frame");
	}

    dec_ctx_3 = avcodec_alloc_context3(dec_3);
    if (!dec_ctx_3) {
        throw std::runtime_error("Could not allocate a decoding context");
    }

    dec_3 = avcodec_find_decoder_by_name("h264");
    if(!dec_3) {
        throw std::runtime_error("Could not find decoder");
    }

    dec_ctx_3->thread_count = 0;

    if (dec_3->capabilities & AV_CODEC_CAP_FRAME_THREADS)
        dec_ctx_3->thread_type = FF_THREAD_FRAME;
    else if (dec_3->capabilities & AV_CODEC_CAP_SLICE_THREADS)
        dec_ctx_3->thread_type = FF_THREAD_SLICE;
    else
        dec_ctx_3->thread_count = 1; //don't use multithreading

    av_dict_set(&opts_3, "flags2", "+export_mvs", 0);
    ret = avcodec_open2(dec_ctx_3, dec_3, &opts_3);
    av_dict_free(&opts_3);

    if (ret < 0) {
        throw std::runtime_error("Could not open codec");
    }
}

// Reading loop

void MyReader::decode_loop_1()
{
    int ret = 0;

    while (av_read_frame(fmt_ctx, pkt_dec_1) >= 0 && running) {
		if (pkt_dec_1->stream_index != video_stream_idx) {
            av_packet_unref(pkt_dec_1);
            continue;
        }

        ret = avcodec_send_packet(dec_ctx_1, pkt_dec_1);
        if (ret < 0) {
            throw std::runtime_error("Error while sending a packet to the decoder (1) " + av_err2str(ret));
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(dec_ctx_1, frame_1);
            
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            
            if (ret < 0) {
                throw std::runtime_error("Error while receiving a frame from the decoder (1)");
            }

            encode_loop_2();

            av_frame_unref(frame_1);
        }
            
		av_packet_unref(pkt_dec_1);
	}
}

void MyReader::encode_loop_2()
{
    int ret = 0;

    ret = avcodec_send_frame(enc_ctx_2, frame_1);
    if (ret < 0) {
        throw std::runtime_error("Error sending a frame for encoding (2)");
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx_2, pkt_enc_2);
        
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }

        if (ret < 0) {
            throw std::runtime_error("Error during encoding (2)");
        }

        decode_loop_3();
        
        av_packet_unref(pkt_enc_2);
    }
}

void MyReader::decode_loop_3()
{
    int ret = 0;

    ret = avcodec_send_packet(dec_ctx_3, pkt_enc_2);
    if (ret < 0) {
        throw std::runtime_error("Error while sending a packet to the decoder (3)");
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx_3, frame_3);
        
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        
        if (ret < 0) {
            throw std::runtime_error("Error while receiving a frame from the decoder (3)");
        }

        callback(frame_3, frame_number);
        frame_number ++;
        
        av_frame_unref(frame_3);
    }
}

std::unique_ptr<Reader> CreateReader(const char* path, HandleFrameCallback callback)
{
    return std::make_unique<MyReader>(path, callback);
}