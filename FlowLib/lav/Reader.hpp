#pragma once

#include <memory>
#include <functional>
#include <opencv2/core.hpp>

struct AVFrame;

typedef std::function<void(AVFrame* frame, int frame_number)> HandleFrameCallback;

class Reader
{
public:
    virtual ~Reader() = default;
    virtual void Start() = 0;

    virtual int CurrentFrame() = 0;
    virtual int GetNumFrames() = 0;
    virtual int GetNumMs() = 0;
    cv::Size GetVideoSize();
};

std::unique_ptr<Reader> CreateReader(const char* path, HandleFrameCallback callback);