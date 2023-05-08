// #include <opencv2/core/utils/logger.hpp>
#include "FlowLibShared.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

LoggingCallback logger = nullptr;

FlowHandle FlowCreateHandle(const char* videoPath, FlowProperties* config)
{
    FlowLibShared* handle = CreateFlowLib(videoPath, config);
    MY_LOG("[FlowLib] handle created");
    return (FlowHandle)handle;
}

void FlowDestroyHandle(FlowHandle handlePtr)
{
    FlowLibShared* handle = (FlowLibShared*)handlePtr;
    delete handle;
    MY_LOG("[FlowLib] handle destroyed");
}

FrameNumber FlowGetLength(FlowHandle handlePtr)
{
    FlowLibShared* handle = (FlowLibShared*)handlePtr;
    return handle->GetNumFrames();
}

void FlowDrawRange(FlowHandle handlePtr, FrameRange range, DrawCallback callback, void* userData)
{
    // Runner* runner = (Runner*)handle;
    // runner->FlowDrawRange(range, callback, userData);
}

void FlowCalcWave(FlowHandle handlePtr, FrameRange range, DrawCallback callback, void* userData)
{
    // Runner* runner = (Runner*)handle;
    // runner->CalcWave(range, callback, userData);
}

void FlowSetLogger(LoggingCallback callback)
{
    logger = callback;
    MY_LOG("[FlowLib] logger attached");
}

void FlowSave(FlowHandle handlePtr, const char* path)
{
    FlowLibShared* handle = (FlowLibShared*)handlePtr;
    cv::Mat mat = handle->GetMat();
    mat.convertTo(mat, CV_8UC1);
    cv::imwrite(path, mat);
    
    // FlowDrawRange(handle, {0, FlowGetLength(handle)}, [](void* data, int width, int height, void* userData) {
    //     Mat mat(height, width, CV_32FC1, data);
    //     Mat m2;
    //     mat.convertTo(m2, CV_8UC1, 255.0);
    //     imwrite((const char *) userData, m2);
    // }, (void*)path);
}

float FlowProgress(FlowHandle handlePtr)
{
    FlowLibShared* handle = (FlowLibShared*)handlePtr;
    return handle->CurrentFrame() / handle->GetNumFrames();
}

void FlowRun(FlowHandle handlePtr, FlowRunCallback callback)
{
    FlowLibShared* handle = (FlowLibShared*)handlePtr;
    handle->Run(callback);
}