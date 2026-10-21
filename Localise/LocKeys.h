#ifndef LOC_KEYS_H
#define LOC_KEYS_H

// Localisation string keys (spec §18.3). Popup "*_ITEMS" keys return one
// "Item1|Item2" string, the form PF_ADD_POPUP expects; item order is part of the
// persisted popup values and never changes between languages.
namespace LocKey {
enum Key {
  STR_TOPIC_BASIC,
  STR_INTENSITY,
  STR_SIZE,
  STR_THRESHOLD,
  STR_THRESHOLD_SOFTNESS,
  STR_TOPIC_SHAPE,
  STR_WIDTH,
  STR_HEIGHT,
  STR_FALLOFF,
  STR_TOPIC_TONE,
  STR_GAMMA,
  STR_SATURATION,
  STR_COLOUR_STRENGTH,
  STR_COLOUR_1,
  STR_COLOUR_2,
  STR_LUMINANCE_SPACE,
  STR_LUMINANCE_SPACE_ITEMS,
  STR_TOPIC_OUTPUT,
  STR_GLOW_OPACITY,
  STR_SOURCE_OPACITY,
  STR_GLOW_ONLY,
  STR_GLOW_ONLY_CHECKBOX,
  STR_ALGORITHM_VERSION,
  STR_COUNT
};
} // namespace LocKey

#endif // LOC_KEYS_H
