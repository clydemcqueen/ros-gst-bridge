/*
(BSD License) to go with ROS2

*/

#ifndef GST_BRIDGE__GST_BRIDGE_H_
#define GST_BRIDGE__GST_BRIDGE_H_

#include <gst/gst.h>

#include <gst/video/video-format.h>
#include <gst/audio/audio-format.h>

#include <rclcpp/rclcpp.hpp>

#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <audio_msgs/msg/audio.hpp>

#define GST_BRIDGE_GST_VIDEO_FORMAT_LIST "{ GRAY8, GRAY16_LE, RGB, BGR, RGBA, BGRA }"
#define GST_BRIDGE_GST_AUDIO_FORMAT_LIST "{ S8, U8, S16LE, U16LE, S32LE, U32LE, F32LE, F64LE }"    // only well behaved formats

// The following audio formats are theoretically ok, but might be more throuble than they're worth.
// 
// these formats need endian conversion on popular platforms
//     S16BE, U16BE, S24_32BE, U24_32BE, S32BE, U32BE, S24BE, U24BE, S20BE, U20BE, S18BE, U18BE, F32BE, F64BE
// these formats have odd packing and need thorough testing
//     S24_32LE, U24_32LE, S24LE, U24LE, S20LE, U20LE, S18LE, U18LE, 


#define ROS_IMAGE_MSG_CAPS                            \
  "video/x-raw, "                                     \
  "format = " GST_BRIDGE_GST_VIDEO_FORMAT_LIST ", "   \
  "framerate = " GST_VIDEO_FPS_RANGE ", "             \
  "width = " GST_VIDEO_SIZE_RANGE ", "                \
  "height = " GST_VIDEO_SIZE_RANGE " "

#define ROS_AUDIO_MSG_CAPS                            \
  "audio/x-raw, "                                     \
  "format = " GST_BRIDGE_GST_AUDIO_FORMAT_LIST ", "   \
  "rate = " GST_AUDIO_RATE_RANGE ", "                 \
  "channels = " GST_AUDIO_CHANNELS_RANGE ","          \
  "layout = interleaved"

//support rpicamsrc compressed feeds over DDS?
#define H264_CAPS                                     \
  "video/x-h264, "                                    \
  "width = " GST_VIDEO_SIZE_RANGE ", "                \
  "height = " GST_VIDEO_SIZE_RANGE ", "               \
  "framerate = " GST_VIDEO_FPS_RANGE ", "             \
  "stream-format = (string) byte-stream, "            \
  "alignment = (string) nal, "                        \
  "profile = (string) { constrained-baseline, baseline, main, high }"

// XXX support source from "text/plain" for pocketsphinx
// XXX support sink to "text/x-raw,{ (string)pango-markup, (string)utf8 }" for textoverlay
// XXX support src and sink "ANY" like filesink and filesrc, (emit a stamped byte string, with a gst caps string as meta)



namespace gst_bridge
{
//measure the difference between ROS and GST time
//raw sampling of the clocks seems to be stable within about 10uS
GstClockTimeDiff sample_clock_offset(GstClock* gst_clock, rclcpp::Time stream_start);

// convert between ROS and GST types
GstVideoFormat getGstVideoFormat(const std::string & encoding);
GstAudioFormat getGstAudioFormat(const std::string & encoding);

std::string getRosEncoding(GstVideoFormat);
std::string getRosEncoding(GstAudioFormat);

audio_msgs::msg::Audio gst_audio_info_to_audio_msg(GstAudioInfo * audio_info);

/*
// convert between GST and CV
// these should cover the edge cases that ROS doesn't know about
GstVideoFormat getGstVideoFormatFromCvType(int cv_type);
GstAudioFormat getGstAudioFormatFromCvType(int cv_type);

int getCvTypeFromGstVideoFormat(GstVideoFormat);
int getCvTypeFromGstAudioFormat(GstAudioFormat);


// audio specific conversions from ROS to CV
int getCvTypeFromRosEncoding(const std::string & encoding);
*/

}

#endif //GST_BRIDGE__GST_BRIDGE_H_