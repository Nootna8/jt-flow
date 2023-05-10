#include <iostream>
#include <thread>
#include <chrono>

extern "C" {
#include "FlowLib.h"
}

int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::cout << "Usage: FlowLibUtil.exe <input video> <output file>\n";
        return 0;
    }

    FlowProperties properties = {
        360/2, // numberOfPools
        0.02f, // maxValue
        false, // overlayHalf
        0.5f, // focusPoint
        0.5f, // focusSize
        0.5f // waveSmoothing1
    };


    try {

        FlowHandle handle = FlowCreateHandle(argv[1], &properties);

        std::cout << "Length frames: " << FlowGetLength(handle) << "\n";
        std::cout << "Length ms: " << FlowGetLengthMs(handle) << "\n";

        clock_t start = clock();
        FlowRun(handle, [](FlowHandle handle, int frame_number) {
            FrameNumber length = FlowGetLength(handle);
            std::cout << frame_number << " / " << length << "\n";
        }, 120);
        clock_t end = clock();
        double elapsed_time = (double)(end - start) / CLOCKS_PER_SEC;
	    fprintf(stderr, "Elapsed time: %f seconds\n", elapsed_time);

        FlowSave(handle, argv[2]);
        FlowDestroyHandle(handle);
    } catch (const std::runtime_error& e) {
        std::cout << "Exception: " << e.what() << "\n";
    }

    // float progress = 0.0f;
    // do {
    //     progress = FlowProgress(handle);

    //     // print progress in percent with 2 decimal places
    //     std::cout << "\r" << (int)(progress * 10000.0f) / 100.0f << "%";
        
    //     std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // } while (progress < 1.0f);

    // FlowSave(handle, argv[2]);

    std::cout << "Done\n";

    return 0;
}