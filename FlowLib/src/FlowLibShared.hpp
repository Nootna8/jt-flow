#pragma once

extern "C" {
#include "FlowLib.h"
};

#include <functional>

namespace cv {
    class Mat;
};

class FlowLibShared;
typedef std::function<void(FlowLibShared* handle, int frame_number)> RunCallback;

class FlowLibShared
{
public:
    virtual ~FlowLibShared() = default;
    
    virtual FrameNumber CurrentFrame() = 0;
    virtual FrameNumber GetNumFrames() = 0;
    virtual FrameNumber GetNumMs() = 0;
    virtual cv::Mat GetMat() = 0;

    virtual void Run(RunCallback callback, int callbackInterval) = 0;
};

FlowLibShared* CreateFlowLib(const char* videoPath, FlowProperties* properties);

extern LoggingCallback logger;
// #define MY_LOG(message) if(logger) { logger(0, message); } else { CV_LOG_INFO(NULL, message); }
#define MY_LOG(message) printf(message); printf("\n");