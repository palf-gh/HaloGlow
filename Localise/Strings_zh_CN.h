#ifndef STRINGS_ZH_CN_H
#define STRINGS_ZH_CN_H

#define AELOCALISE_HAS_ZH_CN

#include "LocKeys.h"

namespace AELocalise {
namespace ZH_CN {
inline const char *GetString(LocKey::Key key) {
  switch (key) {
  case LocKey::STR_GPU_STATUS_NAME:
    return "GPU 加速";
  case LocKey::STR_GPU_STATUS_ACTIVE:
    return "GPU 加速：已启用";
  case LocKey::STR_GPU_STATUS_READY:
    return "GPU 加速：已就绪";
  case LocKey::STR_GPU_STATUS_UNAVAILABLE:
    return "GPU 加速：不可用";
  case LocKey::STR_TOPIC_BASIC:
    return "基本";
  case LocKey::STR_INTENSITY:
    return "强度";
  case LocKey::STR_SIZE:
    return "大小";
  case LocKey::STR_THRESHOLD:
    return "阈值";
  case LocKey::STR_THRESHOLD_SOFTNESS:
    return "阈值柔和度";
  case LocKey::STR_TOPIC_SHAPE:
    return "形状";
  case LocKey::STR_WIDTH:
    return "宽度";
  case LocKey::STR_HEIGHT:
    return "高度";
  case LocKey::STR_FALLOFF:
    return "衰减";
  case LocKey::STR_TOPIC_TONE:
    return "颜色与色调";
  case LocKey::STR_GAMMA:
    return "伽马";
  case LocKey::STR_SATURATION:
    return "饱和度";
  case LocKey::STR_COLOUR_STRENGTH:
    return "着色强度";
  case LocKey::STR_COLOUR_1:
    return "颜色 1";
  case LocKey::STR_COLOUR_2:
    return "颜色 2";
  case LocKey::STR_LUMINANCE_SPACE:
    return "亮度色彩空间";
  case LocKey::STR_LUMINANCE_SPACE_ITEMS:
    return "自动|Rec.709 / sRGB|Display P3|ACEScg";
  case LocKey::STR_TOPIC_OUTPUT:
    return "输出";
  case LocKey::STR_GLOW_OPACITY:
    return "光晕不透明度";
  case LocKey::STR_SOURCE_OPACITY:
    return "源图像不透明度";
  case LocKey::STR_GLOW_ONLY:
    return "仅光晕";
  case LocKey::STR_GLOW_ONLY_CHECKBOX:
    return "开";
  case LocKey::STR_ALGORITHM_VERSION:
    return "算法版本";
  default:
    break;
  }
  return "";
}
} // namespace ZH_CN
} // namespace AELocalise

#endif // STRINGS_ZH_CN_H
