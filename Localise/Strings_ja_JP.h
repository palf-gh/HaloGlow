#ifndef STRINGS_JA_JP_H
#define STRINGS_JA_JP_H

#define AELOCALISE_HAS_JA_JP

#include "LocKeys.h"

namespace AELocalise {
namespace JA_JP {
inline const char *GetString(LocKey::Key key) {
  switch (key) {
  case LocKey::STR_GPU_STATUS_NAME:
    return "GPU アクセラレーション";
  case LocKey::STR_GPU_STATUS_ACTIVE:
    return "GPU アクセラレーション: 有効";
  case LocKey::STR_GPU_STATUS_READY:
    return "GPU アクセラレーション: 準備完了";
  case LocKey::STR_GPU_STATUS_UNAVAILABLE:
    return "GPU アクセラレーション: 利用不可";
  case LocKey::STR_TOPIC_BASIC:
    return "基本";
  case LocKey::STR_INTENSITY:
    return "強度";
  case LocKey::STR_SIZE:
    return "サイズ";
  case LocKey::STR_THRESHOLD:
    return "しきい値";
  case LocKey::STR_THRESHOLD_SOFTNESS:
    return "しきい値の柔らかさ";
  case LocKey::STR_TOPIC_SHAPE:
    return "形状";
  case LocKey::STR_WIDTH:
    return "幅";
  case LocKey::STR_HEIGHT:
    return "高さ";
  case LocKey::STR_FALLOFF:
    return "減衰";
  case LocKey::STR_TOPIC_TONE:
    return "色・階調";
  case LocKey::STR_GAMMA:
    return "ガンマ";
  case LocKey::STR_SATURATION:
    return "彩度";
  case LocKey::STR_COLOUR_STRENGTH:
    return "着色の強さ";
  case LocKey::STR_COLOUR_1:
    return "カラー 1";
  case LocKey::STR_COLOUR_2:
    return "カラー 2";
  case LocKey::STR_LUMINANCE_SPACE:
    return "輝度の色空間";
  case LocKey::STR_LUMINANCE_SPACE_ITEMS:
    return "自動|Rec.709 / sRGB|Display P3|ACEScg";
  case LocKey::STR_TOPIC_OUTPUT:
    return "出力";
  case LocKey::STR_GLOW_OPACITY:
    return "グローの不透明度";
  case LocKey::STR_SOURCE_OPACITY:
    return "元画像の不透明度";
  case LocKey::STR_GLOW_ONLY:
    return "グローのみ";
  case LocKey::STR_GLOW_ONLY_CHECKBOX:
    return "オン";
  case LocKey::STR_ALGORITHM_VERSION:
    return "アルゴリズムの版";
  default:
    break;
  }
  return "";
}
} // namespace JA_JP
} // namespace AELocalise

#endif // STRINGS_JA_JP_H
