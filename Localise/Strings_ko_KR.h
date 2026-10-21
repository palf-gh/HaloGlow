#ifndef STRINGS_KO_KR_H
#define STRINGS_KO_KR_H

#define AELOCALISE_HAS_KO_KR

#include "LocKeys.h"

namespace AELocalise {
namespace KO_KR {
inline const char *GetString(LocKey::Key key) {
  switch (key) {
  case LocKey::STR_TOPIC_BASIC:
    return "기본";
  case LocKey::STR_INTENSITY:
    return "강도";
  case LocKey::STR_SIZE:
    return "크기";
  case LocKey::STR_THRESHOLD:
    return "임계값";
  case LocKey::STR_THRESHOLD_SOFTNESS:
    return "임계값 부드러움";
  case LocKey::STR_TOPIC_SHAPE:
    return "모양";
  case LocKey::STR_WIDTH:
    return "너비";
  case LocKey::STR_HEIGHT:
    return "높이";
  case LocKey::STR_FALLOFF:
    return "감쇠";
  case LocKey::STR_TOPIC_TONE:
    return "색상 및 톤";
  case LocKey::STR_GAMMA:
    return "감마";
  case LocKey::STR_SATURATION:
    return "채도";
  case LocKey::STR_COLOUR_STRENGTH:
    return "색상 강도";
  case LocKey::STR_COLOUR_1:
    return "색상 1";
  case LocKey::STR_COLOUR_2:
    return "색상 2";
  case LocKey::STR_LUMINANCE_SPACE:
    return "휘도 색 공간";
  case LocKey::STR_LUMINANCE_SPACE_ITEMS:
    return "자동|Rec.709 / sRGB|Display P3|ACEScg";
  case LocKey::STR_TOPIC_OUTPUT:
    return "출력";
  case LocKey::STR_GLOW_OPACITY:
    return "글로우 불투명도";
  case LocKey::STR_SOURCE_OPACITY:
    return "원본 불투명도";
  case LocKey::STR_GLOW_ONLY:
    return "글로우만";
  case LocKey::STR_GLOW_ONLY_CHECKBOX:
    return "켜기";
  case LocKey::STR_ALGORITHM_VERSION:
    return "알고리즘 버전";
  default:
    break;
  }
  return "";
}
} // namespace KO_KR
} // namespace AELocalise

#endif // STRINGS_KO_KR_H
