#include "FlowLibShared.hpp"

#include "ReadFlow.h"
#include "NvDecoder.h"
#include "FFmpegDemuxer.h"

#include <opencv2/core.hpp>
#include <opencv2/core/cuda.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudaoptflow.hpp>
#include <opencv2/cudawarping.hpp>

#include <thread>
#include <algorithm>
#include <condition_variable>
// #include <format>

using namespace std;
using namespace cv;

struct Job {
    Job() {}
    Job(cuda::GpuMat lastFrame, cuda::GpuMat nextFrame, cuda::GpuMat out, FrameNumber frameNumber): lastFrame(lastFrame), nextFrame(nextFrame), out(out), frameNumber(frameNumber) {}

    cuda::GpuMat lastFrame;
    cuda::GpuMat nextFrame;
    cuda::GpuMat out;
    FrameNumber frameNumber;
};

#define NUM_POOLS 360/2
#define MAGNITUTE_THRESH 0.01f

class Runner : public FlowLibShared {
public:
    Runner(const char* video, FlowProperties& config):
        demuxer(video), config(config)
    {
        // Setup video reader
        cv::cuda::GpuMat temp(1, 1, CV_8UC1);
        temp.release();

        video_size = Size(
            demuxer.GetWidth(),
            demuxer.GetHeight()
        );

        resizeDim.w = video_size.width;
        resizeDim.h = video_size.height;

        dec = make_unique<NvDecoder>(true, FFmpeg2NvCodecId(demuxer.GetVideoCodec()), false, false, &cropDim, &resizeDim);
        dec->SetOperatingPoint(0, false);
        
        numFrames = demuxer.GetNumFrames();
        isFrameProcessed = vector<bool>(numFrames);
        fill(isFrameProcessed.begin(), isFrameProcessed.end(), false);

        if (frame_skip > 0) {
            numFrames = ceil(numFrames / (1 + frame_skip));
        }

        out = cuda::GpuMat(numFrames, NUM_POOLS, CV_32S);
        out.setTo({ 0 });

        // tracker_thread = thread(&Runner::TrackThread, this);
        // reader_thread = thread(&Runner::ReadThread, this);
    }

    ~Runner()
    {
        // running = false;

        // {
        //     lock_guard<mutex>lock(jobs_mutex);
        //     jobs.clear();
        // }

        // while(!tracker_thread_waiting){;}
        // condition.notify_all();

        // tracker_thread.join();
        // reader_thread.join();
    }

    void ReadThread(RunCallback callback, int callbackInterval)
    {
        string message = cv::format("[FlowLib] ReadThread start, frames %ld", numFrames);
        MY_LOG(message.c_str());

        int nVideoBytes = 0;
        int64_t pts = 0;
        int64_t frameNr = 0;
        uint8_t* pVideo = NULL;
        isReading = true;

        while(running)
        {
            // frame_seek need mutex?
            FrameNumber startPosition=frame_seek;
            for(FrameNumber f=startPosition; f<isFrameProcessed.size(); f++) {
                if(isFrameProcessed.at(f) == false) {
                    startPosition = f;
                    break;
                }
            }

            frame_seek = 0;
            frame_position = startPosition;
            // demuxer.SeekFrame(startPosition);
            lastGpuFrame = cuda::GpuMat();

            string message = cv::format("[FlowLib] reading from frame %ld", frame_position);
            MY_LOG(message.c_str());

            while(running)
            {
                bool demuxed = demuxer.Demux(&pVideo, &nVideoBytes, &pts, &frameNr);

                if(!demuxed) {
                    string message = cv::format("[FlowLib] demux end at %ld", frame_position);
                    MY_LOG(message.c_str());
                    break;
                }

                if(frame_seek > 0)
                    break;

                int nFrameReturned = dec->Decode(pVideo, nVideoBytes, 0, pts);

                while (jobs.size() > 1500) {
                    this_thread::sleep_for(chrono::milliseconds(1));
                }

                {
                    lock_guard<mutex>lock(jobs_mutex);

                    while(dec->NumFrames() > 0) {
                        if(!QueueFrame(dec->GetFrame())) {
                            string message = cv::format("[FlowLib] queue error at frame %ld", frame_position);
                            MY_LOG(message.c_str());
                            isReading = false;
                            return;
                        }

                        frame_position++;
                    }

                    if(callback && frame_position % callbackInterval == 0 && frame_position > 0) {
                        callback((FlowLibShared*)this, frame_position);
                    }
                }

                condition.notify_all();
            }

            if(frame_position == startPosition) {
                message = cv::format("[FlowLib] reading died at %ld", frame_position);
                MY_LOG(message.c_str());
                isReading = false;
                return;
            }

            bool allDone = true;
            
            for(FrameNumber f=0; f<isFrameProcessed.size(); f++) {
                if(isFrameProcessed.at(f) == false) {
                    message = cv::format("[FlowLib] frame %ld not done, looping (stopped at %ld)", f, frame_position);
                    MY_LOG(message.c_str());

                    allDone = false;
                    break;
                }
            }

            if(allDone) {
                MY_LOG("[FlowLib] ReadThread end (all done)");
                isReading = false;
                return;
            }
            
            MY_LOG("[FlowLib] ReadThread loop");
        }

        MY_LOG("[FlowLib] ReadThread end (exited)");
        isReading = false;
    }

    bool QueueFrame(cuda::GpuMat nextFrame)
    {
        if(lastGpuFrame.empty()) {
            lastGpuFrame = nextFrame;
            isFrameProcessed.at(frame_position) = true;
            
            return true;
        }
        
        if(frame_position > isFrameProcessed.size()-1)
            return false;

        if(isFrameProcessed.at(frame_position) == true) {
            return true;
        }

        isFrameProcessed.at(frame_position) = true;

   //     if (frame_skip_counter < 1) {
            jobs.emplace_back(lastGpuFrame, nextFrame, out.row(frame_position-1), frame_position);
            frame_skip_counter = frame_skip;
            lastGpuFrame = nextFrame;
            //return true;
  //      }

//        frame_skip_counter -= 1;

        return true;
    }

    void TrackThread()
    {
        MY_LOG("[FlowLib] TrackThread start");

        cv::Ptr<cv::cuda::NvidiaOpticalFlow_2_0> flow = cuda::NvidiaOpticalFlow_2_0::create(
            video_size,
            cv::cuda::NvidiaOpticalFlow_2_0::NV_OF_PERF_LEVEL_SLOW
        );

        Size flowSize = video_size / flow->getGridSize();
        cuda::GpuMat flow_frame = cuda::GpuMat(flowSize, CV_16SC2);

        Job job;

        while (running) {
            std::unique_lock<std::mutex> lock{ jobs_mutex };
            while (jobs.empty() && running) {
                tracker_thread_waiting = true;
                condition.wait(lock);
                tracker_thread_waiting = false;
            }

            if (jobs.empty())
                continue;

            job = jobs.front();
            jobs.pop_front();
            lock.unlock();
            
            flow->calc(job.nextFrame, job.lastFrame, flow_frame);
            runMatPool(flow_frame, job.out, NUM_POOLS, MAGNITUTE_THRESH);
        }
        
        MY_LOG("[FlowLib] TrackThread finishing up");
        tracker_thread_waiting = true;

        while (!jobs.empty()) {
            job = jobs.front();
            jobs.pop_front();
            
            flow->calc(job.nextFrame, job.lastFrame, flow_frame);
            runMatPool(flow_frame, job.out, NUM_POOLS, MAGNITUTE_THRESH);
        }

        MY_LOG("[FlowLib] TrackThread end");
    }

    cuda::GpuMat PrepareFrame(FrameRange range)
    {
        config.numberOfPools = NUM_POOLS;

        // Create a pointer to the data frames we want
        FrameNumber numDrawFrames = range.toFrame - range.fromFrame;
        cuda::GpuMat outCopy(numDrawFrames, config.numberOfPools, CV_32S);
        outCopy.setTo({0});
        cuda::GpuMat outPtr = out.rowRange(range.fromFrame, range.toFrame);
        outPtr.copyTo(outCopy);

        // Normallize the values to 0 and 1
        float maxVal = video_size.width * video_size.height * config.maxValue;
        cuda::threshold(outCopy, outCopy, maxVal, 0, THRESH_TRUNC);
        cuda::GpuMat outCopyNormed(numDrawFrames, config.numberOfPools, CV_32FC1);
        cuda::normalize(outCopy, outCopyNormed, 0.0f, 1.0f, NORM_MINMAX, CV_32FC1);
        outCopy = outCopyNormed;
        
        // Apply position mirror
        if(config.overlayHalf) {
            int poolsHalf = config.numberOfPools / 2;
            cuda::GpuMat outCopyHalfed(numDrawFrames, poolsHalf, CV_32FC1);
            outCopyHalfed.setTo({1.0f});

            cuda::add(outCopyHalfed, outCopy.colRange(0, poolsHalf), outCopyHalfed);
            cuda::subtract(outCopyHalfed, outCopy.colRange(poolsHalf, config.numberOfPools), outCopyHalfed);
            outCopyHalfed.convertTo(outCopyHalfed, CV_32FC1, 0.5);

            outCopy = outCopyHalfed;
        }

/*
        if(config.rollOffset > 0) {
            cuda::GpuMat outCopyTmp = outCopy.clone();

            int offset = outCopy.cols / 2;
            offset = max(0, offset, min(outCopy.cols, offset));
            outCopy.colRange(0, offset).copyTo(outCopyTmp.colRange(offset, outCopyTmp.cols));
            outCopy.colRange(offset, outCopyTmp.cols).copyTo(outCopyTmp.colRange(0, offset));
            outCopy = outCopyTmp;
        }
*/

        return outCopy;
    }

    void FlowDrawRange(FrameRange range, DrawCallback callback, void* userData)
    {
        config.numberOfPools = NUM_POOLS;
        
        bool needsQueue = false;
        
        if(frame_position >= range.fromFrame && frame_position <= range.toFrame) {
            needsQueue = false;
        } else {
            for(FrameNumber f=range.fromFrame; f<range.toFrame; f++) {
                if(isFrameProcessed.at(f) == false) {
                    needsQueue = true;
                    break;
                }
            }
        }

        if (needsQueue) {
            //frame_seek = range.fromFrame;
        }

        
        
        // Apply re-pooling

/*
        int step = NUM_POOLS/config.numberOfPools;
        for(int p=0; p<config.numberOfPools; p++) {
            int fromPool = p*step;
            int toPool = min(NUM_POOLS-1, fromPool+step);

            cuda::reduce(outPtr.colRange(fromPool, toPool), outCopy.col(p), 1, REDUCE_SUM, CV_32S);
        }
        */

        cuda::GpuMat outCopy = PrepareFrame(range);

        drawBuffer = Mat(outCopy.rows, config.numberOfPools, CV_32FC1);
        outCopy.download(drawBuffer);
        rotate(drawBuffer, drawBuffer, ROTATE_90_COUNTERCLOCKWISE);

        callback(drawBuffer.data, drawBuffer.cols, drawBuffer.rows, userData);
    }

    void CalcWave(FrameRange range, DrawCallback callback, void* userData)
    {
        cuda::GpuMat outCopy = PrepareFrame(range);
        
        int fromPool = (config.focusPoint - (config.focusSize / 2.0f)) * outCopy.cols;
        fromPool = max(0, fromPool);

        int toPool = (config.focusPoint + (config.focusSize / 2.0f)) * outCopy.cols;
        toPool = min(outCopy.cols, toPool);

        outCopy = outCopy.colRange(fromPool, toPool);
        cuda::resize(outCopy, outCopy, Size(1, outCopy.rows));
        outCopy.download(waveBuffer);
        
        rotate(waveBuffer, waveBuffer, ROTATE_90_COUNTERCLOCKWISE);
        //if(config.waveSmoothing1 > 0) {
        //    GaussianBlur(waveBuffer, waveBuffer, Size(0, 0), config.waveSmoothing1);
        //}

        callback(waveBuffer.data, waveBuffer.cols, waveBuffer.rows, userData);
    }

    float GetProgress()
    {
        // count the number of completed frames in isFrameProcessed and divide by numFrames, then return the result

        int completedFrames = 0;
        for(int i=0; i<isFrameProcessed.size(); i++) {
            if(isFrameProcessed.at(i)) {
                completedFrames++;
            }
        }

        float ret = (float)completedFrames / (float)numFrames;
        ret -= 0.01;

        if(!isReading) {
            ret = 1.0f;
        }

        return ret;
    }

    void Run(RunCallback callback, int callbackInterval)
    {
        tracker_thread = thread(&Runner::TrackThread, this);

        ReadThread(callback, callbackInterval);

        running = false;
        while(!tracker_thread_waiting){;}
        condition.notify_all();

        tracker_thread.join();
    }

    FrameNumber CurrentFrame()
    {
        return frame_position;
    }

    FrameNumber GetNumFrames()
    {
        return numFrames;
    }

    FrameNumber GetNumMs()
    {
        return demuxer.GetDuration();
    }

    cv::Mat GetMat()
    {
        cv::Mat buf;

        out.download(buf);
        
        // rotate(buf, buf, ROTATE_90_COUNTERCLOCKWISE);
        // float maxVal = video_size.width * video_size.height * ;
        // buf.convertTo(buf, CV_32FC1, config.maxValue);
        
        return buf;
    }

protected:
    bool isReading = true;
    bool had_update = false;
    vector<bool> isFrameProcessed;
    FlowProperties& config;
    
    FFmpegDemuxer demuxer;
    unique_ptr<NvDecoder> dec;

    Size video_size;
    ::Rect cropDim = {};
    Dim resizeDim = {};

    FrameNumber frame_seek = 0;
    FrameNumber numFrames;
    FrameNumber frame_position = 0;
    int frame_skip = 0;
    int frame_skip_counter = 0;

    cuda::GpuMat lastGpuFrame;
    cuda::GpuMat out;

    deque<Job> jobs;
    mutex jobs_mutex;
    bool running = true;
    bool tracker_thread_waiting = false;
    thread tracker_thread;
    // thread reader_thread;
    std::condition_variable condition = {};
    Mat drawBuffer;
    Mat waveBuffer;
};

FlowLibShared* CreateFlowLib(const char* videoPath, FlowProperties* properties)
{
    return new Runner(videoPath, *properties);
}

// FlowHandle FlowCreateHandle(const char* videoPath, FlowProperties* config)
// {
//     Runner* runner = new Runner(videoPath, *config);
//     MY_LOG("[FlowLib] handle created");
//     return (FlowHandle)runner;
// }

// void FlowDestroyHandle(FlowHandle handle)
// {
//     Runner* runner = (Runner*)handle;
//     delete runner;
//     MY_LOG("[FlowLib] handle destroyed");
// }

// FrameNumber FlowGetLength(FlowHandle handle)
// {
//     Runner* runner = (Runner*)handle;
//     return runner->GetNumFrames();
// }

// void FlowDrawRange(FlowHandle handle, FrameRange range, DrawCallback callback, void* userData)
// {
//     Runner* runner = (Runner*)handle;
//     runner->FlowDrawRange(range, callback, userData);
// }

// void FlowCalcWave(FlowHandle handle, FrameRange range, DrawCallback callback, void* userData)
// {
//     Runner* runner = (Runner*)handle;
//     runner->CalcWave(range, callback, userData);
// }

// void FlowSetLogger(LoggingCallback callback)
// {
//     logger = callback;
//     MY_LOG("[FlowLib] logger attached");
// }

// void FlowSave(FlowHandle handle, const char* path)
// {
//     FlowDrawRange(handle, {0, FlowGetLength(handle)}, [](void* data, int width, int height, void* userData) {
//         Mat mat(height, width, CV_32FC1, data);
//         Mat m2;
//         mat.convertTo(m2, CV_8UC1, 255.0);
//         imwrite((const char *) userData, m2);
//     }, (void*)path);
// }

// float FlowProgress(FlowHandle handle)
// {
//     Runner* runner = (Runner*)handle;
//     return runner->GetProgress();
// }