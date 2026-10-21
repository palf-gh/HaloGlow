#ifndef STRINGS_EN_US_H
#define STRINGS_EN_US_H

#define AELOCALISE_HAS_EN_US

#include "LocKeys.h"

namespace AELocalise {
namespace EN_US {
inline const char *GetString(LocKey::Key key) {
  switch (key) {
  case LocKey::STR_GPU_STATUS_NAME:
    return "GPU Acceleration";
  case LocKey::STR_GPU_STATUS_ACTIVE:
    return "GPU acceleration active";
  case LocKey::STR_GPU_STATUS_READY:
    return "GPU acceleration ready";
  case LocKey::STR_GPU_STATUS_UNAVAILABLE:
    return "GPU acceleration unavailable";
  case LocKey::STR_TOPIC_BASIC:
    return "Basic";
  case LocKey::STR_INTENSITY:
    return "Intensity";
  case LocKey::STR_SIZE:
    return "Size";
  case LocKey::STR_THRESHOLD:
    return "Threshold";
  case LocKey::STR_THRESHOLD_SOFTNESS:
    return "Threshold Softness";
  case LocKey::STR_TOPIC_SHAPE:
    return "Shape";
  case LocKey::STR_WIDTH:
    return "Width";
  case LocKey::STR_HEIGHT:
    return "Height";
  case LocKey::STR_FALLOFF:
    return "Falloff";
  case LocKey::STR_TOPIC_TONE:
    return "Colour & Tone";
  case LocKey::STR_GAMMA:
    return "Gamma";
  case LocKey::STR_SATURATION:
    return "Saturation";
  case LocKey::STR_COLOUR_STRENGTH:
    return "Colour Strength";
  case LocKey::STR_COLOUR_1:
    return "Colour 1";
  case LocKey::STR_COLOUR_2:
    return "Colour 2";
  case LocKey::STR_LUMINANCE_SPACE:
    return "Luminance Space";
  case LocKey::STR_LUMINANCE_SPACE_ITEMS:
    return "Auto|Rec.709 / sRGB|Display P3|ACEScg";
  case LocKey::STR_TOPIC_OUTPUT:
    return "Output";
  case LocKey::STR_GLOW_OPACITY:
    return "Glow Opacity";
  case LocKey::STR_SOURCE_OPACITY:
    return "Source Opacity";
  case LocKey::STR_GLOW_ONLY:
    return "Glow Only";
  case LocKey::STR_GLOW_ONLY_CHECKBOX:
    return "On";
  case LocKey::STR_ALGORITHM_VERSION:
    return "Algorithm Version";
  default:
    break;
  }
  return "";
}
} // namespace EN_US
} // namespace AELocalise

#endif // STRINGS_EN_US_H
