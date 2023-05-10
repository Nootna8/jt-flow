#include "FlowLibShared.hpp"

#include "Reader.hpp"
// #include "BS_thread_pool.hpp"

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
        try {
            InitOpencl();
            useOpenCL = true;
        } catch (std::exception& e) {
            printf("OpenCL not available: %s\n", e.what());
        }
        // printf(".");

        flowOutput = cv::UMat(reader->GetNumFrames(), FLOW_HEIGHT, CV_32SC1, cv::ACCESS_WRITE, cv::USAGE_ALLOCATE_DEVICE_MEMORY);
        cv::Mat flowOutputZero = cv::Mat(reader->GetNumFrames(), FLOW_HEIGHT, CV_32SC1, cv::Scalar(0, 0, 0));
        flowOutputZero.copyTo(flowOutput);

        // flowOutput = cv::Mat(reader->GetNumFrames(), FLOW_HEIGHT, CV_32SC1, cv::Scalar(0, 0, 0));
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
        // return flowOutput;
    }

    void Run(RunCallback cb, int callbackInterval)
    {
        callback = cb;
        reader->Start();
    }

protected:
    void HandleFrame(AVFrame* frame, int frame_number);
    void HandleVectorData(AVFrameSideData* sd, int frame_number);
    void InitOpencl();
    void process_vector(AVMotionVector* vector, int frame_number, cv::Mat writeMat);

    cv::ocl::Program vectorFrame;
    cv::ocl::Context clContext;
    bool useOpenCL = false;

    std::unique_ptr<Reader> reader;
    RunCallback callback;

    cv::UMat flowOutput;
    // cv::Mat flowOutput;
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

    if(callback && frame_number > 0 && frame_number % 120 == 0) {
        callback((FlowLibShared*)this, frame_number);
    }
}

#define PI_F 3.141592654f

void cartesian_to_polar(float x, float y, float* magnitude, float* angle_degrees) {
    *magnitude = sqrt(x * x + y * y);
    *angle_degrees = atan(y / x) * 180.0f / PI_F;
    if (x < 0) {
        *angle_degrees += 180.0f;
    }
    else if (y < 0) {
        *angle_degrees += 360.0f;
    }
}

void FlowLib::process_vector(AVMotionVector* vector, int frame_number, cv::Mat writeMat)
{
    float magnitude, angle;
    cartesian_to_polar(vector->motion_x, vector->motion_y, &magnitude, &angle);
    if (magnitude < MAGNITUDE_THRESHOLD) {
        return;
    }

    int myAngle = (int)angle / 2;
    if (myAngle < 0 || myAngle >= 180) {
        return;
    }

    writeMat.at<int>(frame_number, myAngle) += 1;
}

void FlowLib::HandleVectorData(AVFrameSideData* sd, int frame_number)
{
    size_t numVectors = sd->size / sizeof(AVMotionVector);

    // BS::thread_pool pool;

    if(!useOpenCL) {
        cv::Mat writeMat = flowOutput.getMat(cv::ACCESS_WRITE);
        for(int v=0; v<numVectors; v++) {
            AVMotionVector* vector = (AVMotionVector*)(sd->data + v * sizeof(AVMotionVector));
            process_vector(vector, frame_number, writeMat);
            // pool.push_task(&FlowLib::process_vector, this, vector, frame_number);
        }
        // pool.wait_for_tasks();
    } else {
        cl::Context theContext((cl_context)clContext.ptr());

        cv::ocl::Kernel kernel("vectorFrame", vectorFrame);
        cl::Buffer vectorBuffer(theContext, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sd->size, sd->data);

        kernel.args(
            MAGNITUDE_THRESHOLD,
            vectorBuffer,
            cv::ocl::KernelArg::ReadWrite(flowOutput.row(frame_number))
        );

        size_t globalThreads[1] = { numVectors };
        bool success = kernel.run(1, globalThreads, NULL, true);
        if (!success){
            throw std::runtime_error("Failed running the kernel...");
        }
    }

    

    
}

FlowLibShared* CreateFlowLib(const char* videoPath, FlowProperties* properties)
{
    return new FlowLib(videoPath);
}
