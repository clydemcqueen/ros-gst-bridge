/* GStreamer
 * Copyright (C) 2020-2021 Brett Downing <brettrd@brettrd.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstrosbasesink
 *
 * The rosbasesink element, pipe audio data into ROS2.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v audiotestsrc ! rosbasesink node_name="gst_audio" topic="/audiotopic"
 * ]|
 * Streams test tones as ROS audio messages on topic.
 * </refsect2>
 */


#include <gst_bridge/rosbasesink.h>


GST_DEBUG_CATEGORY_STATIC (rosbasesink_debug_category);
#define GST_CAT_DEFAULT rosbasesink_debug_category

/* prototypes */


static void rosbasesink_set_property (GObject * object, guint property_id, const GValue * value, GParamSpec * pspec);
static void rosbasesink_get_property (GObject * object, guint property_id, GValue * value, GParamSpec * pspec);

static GstStateChangeReturn rosbasesink_change_state (GstElement * element, GstStateChange transition);
static void rosbasesink_init (RosBaseSink * rosbasesink);

static gboolean rosbasesink_setcaps (GstBaseSink * sink, GstCaps * caps);
static GstCaps * rosbasesink_getcaps (GstBaseSink * sink, GstCaps * caps);
static gboolean rosbasesink__query (GstBaseSink * sink, GstQuery * query);
static GstFlowReturn rosbasesink_render (GstBaseSink * sink, GstBuffer * buffer);


static gboolean rosbasesink_open (RosBaseSink * sink);
static gboolean rosbasesink_close (RosBaseSink * sink);

/*
  XXX provide a mechanism for ROS to provide a clock
*/


enum
{
  PROP_0,
  PROP_ROS_NAME,
  PROP_ROS_NAMESPACE,
};


/* pad templates */

static GstStaticPadTemplate rosbasesink_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (ROS_AUDIO_MSG_CAPS)
    );

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (RosBaseSink, rosbasesink, GST_TYPE_BASE_SINK,
    GST_DEBUG_CATEGORY_INIT (rosbasesink_debug_category, "rosbasesink", 0,
        "debug category for rosbasesink element"))

static void rosbasesink_class_init (RosBaseSinkClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *basesink_class = (GstBaseSinkClass *) klass;

  object_class->set_property = rosbasesink_set_property;
  object_class->get_property = rosbasesink_get_property;


  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_static_pad_template (element_class,
      &rosbasesink_sink_template);


  gst_element_class_set_static_metadata (element_class,
      "rosbasesink",
      "Sink",
      "a gstreamer sink class for handling boilerplate ROS2 interactions",
      "BrettRD <brettrd@brettrd.com>");

  g_object_class_install_property (object_class, PROP_ROS_NAME,
      g_param_spec_string ("ros-name", "node-name", "Name of the ROS node",
      "gst_base_sink_node",
      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS))
  );

  g_object_class_install_property (object_class, PROP_ROS_NAMESPACE,
      g_param_spec_string ("ros-namespace", "node-namespace", "Namespace for the ROS node",
      "",
      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS))
  );


  element_class->change_state = GST_DEBUG_FUNCPTR (rosbasesink_change_state); //use state change events to open and close publishers
  basesink_class->set_caps = GST_DEBUG_FUNCPTR (rosbasesink_setcaps);  //gstreamer informs us what caps we're using.
  basesink_class->get_caps = GST_DEBUG_FUNCPTR (rosbasesink_getcaps);  //requests a set of caps to choose from
  //basesink_class->event = GST_DEBUG_FUNCPTR (rosbasesink_event);  //flush events can cause discontinuities (flags exist in buffers)
  //basesink_class->wait_event = GST_DEBUG_FUNCPTR (rosbasesink_wait_event); //eos events, finish rendering the output then return
  //basesink_class->get_times = GST_DEBUG_FUNCPTR (rosbasesink_get_times); //asks us for start and stop times (?)
  //basesink_class->preroll = GST_DEBUG_FUNCPTR (rosbasesink_preroll); //hands us the first buffer
  basesink_class->render = GST_DEBUG_FUNCPTR (rosbasesink_render); // gives us a buffer to forward

}

static void rosbasesink_init (RosBaseSink * sink)
{
  // Don't register the node or the publisher just yet,
  // wait for rosbasesink_open()
  // XXX set defaults elsewhere to keep gst-inspect consistent
  sink->node_name = g_strdup("gst_base_sink_node");
  sink->node_namespace = g_strdup("");
}

void rosbasesink_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  RosBaseSink *sink = GST_ROS_BASE_SINK (object);

  GST_DEBUG_OBJECT (sink, "set_property");

  switch (property_id) {
    case PROP_ROS_NAME:
      if(sink->node)
      {
        RCLCPP_ERROR(sink->logger, "can't change node name once openned");
      }
      else
      {
        g_free(sink->node_name);
        sink->node_name = g_value_dup_string(value);
      }
      break;

    case PROP_ROS_NAMESPACE:
      if(sink->node)
      {
        RCLCPP_ERROR(sink->logger, "can't change node namespace once openned");
      }
      else
      {
        g_free(sink->node_namespace);
        sink->node_namespace = g_value_dup_string(value);
      }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void rosbasesink_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  RosBaseSink *sink = GST_ROS_BASE_SINK (object);

  GST_DEBUG_OBJECT (sink, "get_property");
  switch (property_id) {
    case PROP_ROS_NAME:
      g_value_set_string(value, sink->node_name);
      break;

    case PROP_ROS_NAMESPACE:
      g_value_set_string(value, sink->node_namespace);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


static GstStateChangeReturn rosbasesink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  RosBaseSink *sink = GST_ROS_BASE_SINK (element);

  switch (transition)
  {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
      if (!rosbasesink_open(sink))
      {
        GST_DEBUG_OBJECT (sink, "open failed");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
      sink->ros_clock_offset = gst_bridge::sample_clock_offset(GST_ELEMENT_CLOCK(sink), sink->clock);
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (rosbasesink_parent_class)->change_state (element, transition);

  switch (transition)
  {
    case GST_STATE_CHANGE_READY_TO_NULL:
      rosbasesink_close(sink);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    default:
      break;
  }

  return ret;

}

/* open the device with given specs */
static gboolean rosbasesink_open (RosBaseSink * sink)
{
  RosBaseSinkClass *sink_class = GST_ROS_BASE_SINK_GET_CLASS (sink);
  gboolean result = TRUE;
  GST_DEBUG_OBJECT (sink, "open");

  sink->ros_context = std::make_shared<rclcpp::Context>();
  sink->ros_context->init(0, NULL);    // XXX should expose the init arg list
  rclcpp::NodeOptions opts = rclcpp::NodeOptions();
  opts.context(sink->ros_context); //set a context to generate the node in
  sink->node = std::make_shared<rclcpp::Node>(std::string(sink->node_name), std::string(sink->node_namespace), opts);
  //rclcpp::QoS qos = rclcpp::SensorDataQoS().reliable();  //XXX add a parameter for overrides
  //XXX add an executor and get sink->node->spin() running on a thread so reconf callbacks respond
  
  // allow sub-class to create publishers on sink->node
  if(sink_class->open)
    result = sink_class->open(sink);
  
  // XXX do something with result
  sink->logger = sink->node->get_logger();
  sink->clock = sink->node->get_clock();
  return result;
}

/* close the device */
static gboolean rosbasesink_close (RosBaseSink * sink)
{
  RosBaseSinkClass *sink_class = GST_ROS_BASE_SINK_GET_CLASS (sink);
  gboolean result = TRUE;

  GST_DEBUG_OBJECT (sink, "close");

  sink->clock.reset();

  //allow sub-class to clean up before destroying ros context
  if(sink_class->close)
    result = sink_class->close(sink);
  
  // XXX do something with result
  sink->node.reset();
  sink->ros_context->shutdown("gst closing rosbasesink");
  return result;
}


/*
XXX fixate only applies to pull mode, delete this chunk
static GstCaps * rosbasesink_fixate (GstBaseSink * base_sink, GstCaps * caps)
{
  //XXX check init_caps and fixate to that
  GstStructure *s;
  gint width, depth;
  RosBaseSink *sink = GST_ROS_BASE_SINK (base_sink);

  GST_DEBUG_OBJECT (sink, "fixate");

  caps = gst_caps_make_writable (caps);

  s = gst_caps_get_structure (caps, 0);

  // fields for all formats 
  gst_structure_fixate_field_nearest_int (s, "rate", 44100);
  gst_structure_fixate_field_nearest_int (s, "channels", 2);
  gst_structure_fixate_field_nearest_int (s, "width", 16);

  // fields for int 
  if (gst_structure_has_field (s, "depth")) {
    gst_structure_get_int (s, "width", &width);
    // round width to nearest multiple of 8 for the depth
    depth = GST_ROUND_UP_8 (width);
    gst_structure_fixate_field_nearest_int (s, "depth", depth);
  }
  if (gst_structure_has_field (s, "signed"))
    gst_structure_fixate_field_boolean (s, "signed", TRUE);
  if (gst_structure_has_field (s, "endianness"))
    gst_structure_fixate_field_nearest_int (s, "endianness", G_BYTE_ORDER);

  caps = GST_BASE_SINK_CLASS (rosbasesink_parent_class)->fixate (base_sink, caps);

  return caps;
}
*/


// event triggered when caps change
// XXX rosbasesink does not  need to shim into here
static gboolean rosbasesink_setcaps (GstBaseSink * base_sink, GstCaps * caps)
{
  gboolean result = FALSE;

  RosBaseSink *sink = GST_ROS_BASE_SINK (base_sink);
  RosBaseSinkClass *sink_class = GST_ROS_BASE_SINK_GET_CLASS (sink);

  if(sink_class->set_caps)
    result = sink_class->set_caps(sink, caps);
  
  return result;
}

// return a caps filter to gstreamer
static GstCaps* rosbasesink_getcaps (GstBaseSink * base_sink, GstCaps * filter)
{
  RosBaseSink *sink = GST_ROS_BASE_SINK (base_sink);
  RosBaseSinkClass *sink_class = GST_ROS_BASE_SINK_GET_CLASS (sink);

  if(sink_class->get_caps)
    sink_class->get_caps(sink, filter);
  
  return filter;
}

static gboolean
gst_alsasink_query (GstBaseSink * base_sink, GstQuery * query)
{
  RosBaseSink *sink = GST_ROS_BASE_SINK (base_sink);
  RosBaseSinkClass *sink_class = GST_ROS_BASE_SINK_GET_CLASS (sink);

  gboolean res;

  if (sink_class->query)
    res = sink_class->query (sink, query);
  else
    res = FALSE;

  return res;
}

static GstFlowReturn rosbasesink_render (GstBaseSink * base_sink, GstBuffer * buf)
{
  rclcpp::Time msg_time;
  GstClockTimeDiff base_time;

  RosBaseSink *sink = GST_ROS_BASE_SINK (base_sink);
  RosBaseSinkClass *sink_class = GST_ROS_BASE_SINK_GET_CLASS (sink);

  GST_DEBUG_OBJECT (sink, "render");

  // XXX look at the base sink clock synchronising features
  base_time = gst_element_get_base_time(GST_ELEMENT(sink));
  msg_time = rclcpp::Time(GST_BUFFER_PTS(buf) + base_time + sink->ros_clock_offset, sink->clock->get_clock_type());

  if(NULL != sink_class->render)
    return sink_class->render(sink, buf, msg_time);
  
  if(sink->node)
    RCLCPP_WARN(sink->logger, "rosbasesink render function not set, dropping buffer");

  return GST_FLOW_OK;
}

