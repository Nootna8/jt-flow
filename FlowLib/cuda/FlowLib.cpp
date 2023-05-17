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

    void ReadThread()
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
        isTracking = true;

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
            last_frame_done = job.frameNumber;
        }

        MY_LOG("[FlowLib] Finishing up");

        while(!jobs.empty()) {
            job = jobs.front();
            jobs.pop_front();
            
            flow->calc(job.nextFrame, job.lastFrame, flow_frame);
            runMatPool(flow_frame, job.out, NUM_POOLS, MAGNITUTE_THRESH);
            last_frame_done = job.frameNumber;
        }
        
        tracker_thread_waiting = true;

        MY_LOG("[FlowLib] TrackThread end");

        isTracking = false;
    }

    void Run(RunCallback callback, int callbackInterval)
    {
        running = true;
        last_frame_done = 0;

        tracker_thread = thread(&Runner::TrackThread, this);
        reader_thread = thread(&Runner::ReadThread, this);
        int callbackRuns = std::ceil(numFrames / callbackInterval);
        int callbackRun = -1;

        while(isReading) {
            for(FrameNumber f=0; f<last_frame_done; f++) {
                int myCallbackRun = std::floor(f / callbackInterval);
                if(myCallbackRun <= callbackRun)
                    continue;

                FrameNumber toFrame = (myCallbackRun+1) * callbackInterval;
                if(toFrame >= last_frame_done || toFrame >= numFrames)
                    continue;

                callback(this, toFrame);
                callbackRun = myCallbackRun;
            }

            this_thread::sleep_for(chrono::milliseconds(500));
        }

        running = false;
        while(!tracker_thread_waiting){;}
        condition.notify_all();

        while(isTracking) {
            for(FrameNumber f=0; f<last_frame_done; f++) {
                int myCallbackRun = std::floor(f / callbackInterval);
                if(myCallbackRun <= callbackRun)
                    continue;

                FrameNumber toFrame = (myCallbackRun+1) * callbackInterval;
                if(toFrame >= last_frame_done || toFrame >= numFrames)
                    continue;

                callback(this, toFrame);
                callbackRun = myCallbackRun;
            }
            
            this_thread::sleep_for(chrono::milliseconds(500));
        }

        reader_thread.join();
        tracker_thread.join();
        MY_LOG("[FlowLib] finished");
    }

    FrameNumber CurrentFrame()
    {
        return last_frame_done;
    }

    FrameNumber GetNumFrames()
    {
        return numFrames;
    }

    FrameNumber GetNumMs()
    {
        return demuxer.GetDuration();
    }

    bool GetMat(FrameRange range, cv::Mat& buffer)
    {
        out.rowRange(range.fromFrame, range.toFrame).download(buffer);
        
        return true;
    }

    cv::Size GetVideoSize()
    {
        return video_size;
    }

protected:
    bool isReading = true;
    bool isTracking = true;
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
    FrameNumber last_frame_done = 0;

    int frame_skip = 0;
    int frame_skip_counter = 0;

    cuda::GpuMat lastGpuFrame;
    cuda::GpuMat out;

    deque<Job> jobs;
    mutex jobs_mutex;
    bool running = true;
    bool tracker_thread_waiting = false;
    thread tracker_thread;
    thread reader_thread;
    std::condition_variable condition = {};
    Mat drawBuffer;
    Mat waveBuffer;
};

FlowLibShared* CreateFlowLib(const char* videoPath, FlowProperties* properties)
{
    return new Runner(videoPath, *properties);
}