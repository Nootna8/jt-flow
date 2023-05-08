JTFlow is a library that can be used to create optical flow maps from video files.

There are 2 implementations, ffmpeg and cuda.

If you want to use this, build using cmake. Take out FlowLibUtil and your preferred implementation which you'll need to rename to FlowLib.dll/so.
So the choices would be FlowLibCuda or FlowLibLav (ffmpeg).

Then run FlowLibUtil video.mp4 flow.png
