# LaMoshPit
La Mosh Pit is a windows desktop application I am building from scratch for learning intentional and artistic data corruption of h264 files, any file the application works on will be a video that it encodes itself using the same codec everytime so that only one compression needs to be understood fully by the manipulation algorithms related to the user controls. I am developing it as a stand alone Windows app using Qt6, ffmpeg, and h264bitstream. 

## Features
- Real-time h.264 bitstream manipulation including live controls for macroblock level glitch effects on h264 videos which are rendered into the application. The application can hold one video at a time, as the video plays in the applications previewplayer, the user has access to controls which inject controlled corruption into macroblock level parameters live as the video plays. 

## Dependencies
- Qt6
- FFmpeg
- [h264bitstream](https://github.com/aizvorski/h264bitstream) (placed in `./h264bitstream/`)
- vcpkg

