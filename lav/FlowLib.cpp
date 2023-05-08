#include "FlowLibShared.hpp"

#include "Reader.hpp"

extern "C" {
#include <libavutil/error.h>
#include <libavutil/motion_vector.h>
#include <libavformat/avformat.h>	
}


#include <opencv2/opencv.hpp>
#include <opencv2/core/ocl.hpp>
#include <CL/cl.hpp>

#include <stdexcept>
#include <fstream>
#include <string>

class FlowLib : public FlowLibShared {
public:
    FlowLib(const char* path)
    {
        reader = CreateReader(path, [this](AVFrame* frame, int frame_number) { HandleFrame(frame, frame_number); });
        printf(".");
        InitOpencl();
        printf(".");

        flowOutput = cv::UMat(FLOW_HEIGHT, reader->GetNumFrames(), CV_32SC1, cv::ACCESS_WRITE, cv::USAGE_ALLOCATE_DEVICE_MEMORY);
        cv::Mat flowOutputZero = cv::Mat(FLOW_HEIGHT, reader->GetNumFrames(), CV_32SC1, cv::Scalar(0, 0, 0));
        flowOutputZero.copyTo(flowOutput);
    }

    FrameNumber CurrentFrame()
    {
        return reader->CurrentFrame();
    }

    FrameNumber GetNumFrames()
    {
        return reader->GetNumFrames();
    }

    FrameNumber GetNumMs()
    {
        return reader->GetNumMs();
    }

    cv::Mat GetMat()
    {
        cv::Mat buf;
        flowOutput.copyTo(buf);
        return buf;
    }

    void Run(RunCallback cb)
    {
        callback = cb;
        reader->Start();
    }

protected:
    void HandleFrame(AVFrame* frame, int frame_number);
    void HandleVectorData(AVFrameSideData* sd, int frame_number);
    void InitOpencl();

    cv::ocl::Program vectorFrame;
    cv::ocl::Context clContext;

    std::unique_ptr<Reader> reader;
    RunCallback callback;

    cv::UMat flowOutput;
    int FLOW_HEIGHT = 180;
    float MAGNITUDE_THRESHOLD = 0.5;
};

void FlowLib::InitOpencl()
{
    if (!cv::ocl::haveOpenCL())
    {
        throw std::runtime_error("OpenCL is not avaiable...");
    }
    
    if (!clContext.create(cv::ocl::Device::TYPE_GPU))
    {
        throw std::runtime_error("Failed creating the context...");
    }

    std::cout << clContext.ndevices() << " GPU devices are detected." << std::endl;
    for (int i = 0; i < clContext.ndevices(); i++)
    {
        cv::ocl::Device device = clContext.device(i);
        std::cout << "name                 : " << device.name() << std::endl;
        std::cout << "available            : " << device.available() << std::endl;
        std::cout << "imageSupport         : " << device.imageSupport() << std::endl;
        std::cout << "OpenCL_C_Version     : " << device.OpenCL_C_Version() << std::endl;
        std::cout << std::endl;
    }

    // Select the first device
    cv::ocl::Device(clContext.device(0));

    // Compile the kernel code
    std::ifstream ifs("vectorFrame.ocl");
    if (ifs.fail()) {
        throw std::runtime_error("vectorFrame.ocl not found");
    }
    std::string kernelSource((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    cv::ocl::ProgramSource programSource(kernelSource);

    cv::String errmsg;
    cv::String buildopt = "";
    vectorFrame = clContext.getProg(programSource, buildopt, errmsg);
    if(errmsg.length() > 0) {
        throw std::runtime_error("OCL Error: " + errmsg);
    }
}

void FlowLib::HandleFrame(AVFrame* frame, int frame_number)
{
    AVFrameSideData* sd = av_frame_get_side_data(frame, AV_FRAME_DATA_MOTION_VECTORS);
    if(sd) {
        HandleVectorData(sd, frame_number);
    }

    if(callback) {
        callback((FlowLibShared*)this, frame_number);
    }
}

void FlowLib::HandleVectorData(AVFrameSideData* sd, int frame_number)
{
    size_t numVectors = sd->size / sizeof(AVMotionVector);

    cl::Context theContext((cl_context)clContext.ptr());

    cv::ocl::Kernel kernel("vectorFrame", vectorFrame);
    cl::Buffer vectorBuffer(theContext, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sd->size, sd->data);

    kernel.args(
        MAGNITUDE_THRESHOLD,
        vectorBuffer,
        cv::ocl::KernelArg::ReadWrite(flowOutput.col(frame_number))
    );

    size_t globalThreads[1] = { numVectors };
    bool success = kernel.run(1, globalThreads, NULL, true);
    if (!success){
        throw std::runtime_error("Failed running the kernel...");
    }
}

FlowLibShared* CreateFlowLib(const char* videoPath, FlowProperties* properties)
{
    return new FlowLib(videoPath);
}
