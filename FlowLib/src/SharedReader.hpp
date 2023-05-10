#pragma once

extern "C" {
#include <libavformat/avformat.h>
}

#include <map>
#include <cmath>

struct StreamProgram {
    AVStream* videoStream = nullptr;
    AVStream* audioStream = nullptr;
    AVStream* dataStream = nullptr;
    int64_t bitRate = 0;

    int64_t GetLengthFrames()
    {
        int64_t frames = videoStream->nb_frames;

        if (frames == 0) {
            if (dataStream == nullptr) {
                return 0;
            }
            if (dataStream->duration == 0) {
                return 0;
            }

            double data_duration_seconds = (double)dataStream->duration * av_q2d(dataStream->time_base);
            double frame_rate = av_q2d(videoStream->avg_frame_rate);
            frames = std::ceil(data_duration_seconds * frame_rate);
        }

        return frames;
    }

    int64_t GetLengthMs()
    {
        AVStream* infoStream = dataStream;
        if(infoStream == nullptr) {
            infoStream = videoStream;
        }

        int64_t duration = infoStream->duration * av_q2d(infoStream->time_base) * 1000;
        return duration;
    }
};

StreamProgram GetStreamProgram(AVFormatContext* fmt_ctx)
{
    std::map<int, StreamProgram> programMap;
    int64_t maxBitRate = 0;

    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream* stream = fmt_ctx->streams[i];

        int64_t bitRate = stream->codecpar->bit_rate;
        if (bitRate == 0) {
            AVDictionaryEntry* entry = av_dict_get(stream->metadata, "variant_bitrate", nullptr, AV_DICT_IGNORE_SUFFIX);
            if (entry == nullptr) {
                continue;
            }
            bitRate = std::stoi(entry->value);
        }
        if(bitRate > maxBitRate) {
            maxBitRate = bitRate;
        }

        if(programMap.find(bitRate) == programMap.end()) {
            programMap[bitRate] = StreamProgram();
            programMap[bitRate].bitRate = bitRate;
        }

        if(stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            programMap[bitRate].videoStream = stream;
        }
        else if(stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            programMap[bitRate].audioStream = stream;
        }
        else if(stream->codecpar->codec_type == AVMEDIA_TYPE_DATA) {
            programMap[bitRate].dataStream = stream;
        }
    }

    return programMap[maxBitRate];
}