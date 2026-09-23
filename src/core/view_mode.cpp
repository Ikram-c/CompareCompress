/**
 * @file view_mode.cpp
 * @brief Configuration keys of the view modes.
 */
#include "core/view_mode.h"

namespace cc {

const char* view_mode_key(ViewMode m)
{
  switch(m)
  {
    case ViewMode::Before: return "before";
    case ViewMode::After: return "after";
    case ViewMode::Heat: return "heat";
    case ViewMode::Overlay: return "overlay";
  }
  return "";
}

bool view_mode_from_key(const std::string& key, ViewMode& out)
{
  const ViewMode modes[] = {ViewMode::Before, ViewMode::After, ViewMode::Heat, ViewMode::Overlay};
  for(const ViewMode m : modes)
  {
    if(key == view_mode_key(m))
    {
      out = m;
      return true;
    }
  }
  return false;
}

} // namespace cc
