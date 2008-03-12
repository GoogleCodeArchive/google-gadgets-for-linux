/*
  Copyright 2007 Google Inc.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef GGADGET_GADGET_HOST_INTERFACE_H__
#define GGADGET_GADGET_HOST_INTERFACE_H__

#include <vector>
#include <string>

namespace ggadget {

template <typename R, typename P1> class Slot1;
class GadgetInterface;
class MainLoopInterface;
class OptionsInterface;
class ScriptableInterface;
class Signal;
class ViewHostInterface;
class ViewInterface;

/**
 * Interface for providing host services to the gadgets.
 * The @c GadgetHostInterface implementation should depend on the host.
 */
class GadgetHostInterface {
 public:
  virtual ~GadgetHostInterface() { }

  /** Returns the @c OptionsInterface instance for this gadget. */
  virtual OptionsInterface *GetOptions() = 0;

  /** Returns the hosted gadget. */
  virtual GadgetInterface *GetGadget() = 0;

  enum ViewType {
    VIEW_MAIN,
    VIEW_OPTIONS,
    /** Old style options dialog that uses @c ggadget::DisplayWindow. */
    VIEW_OLD_OPTIONS,
    VIEW_DETAILS,
  };

  /**
   * Creates a new @c ViewHostInterface for a view.
   * Once the ViewHost is created, the given ViewInterface is owned by that
   * ViewHost and will be freed by that ViewHost.
   * @param type type of view.
   * @param view View object to attach this host to
   */
  virtual ViewHostInterface *NewViewHost(ViewType type,
                                         ViewInterface *view) = 0;

  enum PluginFlags {
    PLUGIN_FLAG_NONE = 0,
    /** Adds a "back" button in the plugin toolbar. */
    PLUGIN_FLAG_TOOLBAR_BACK = 1,
    /** Adds a "forward" button in the plugin toolbar. */
    PLUGIN_FLAG_TOOLBAR_FORWARD = 2,
  };

  /**
   * @param plugin_flags combination of PluginFlags.
   */
  virtual void SetPluginFlags(int plugin_flags) = 0;

  /**
   * Requests that the gadget be removed from the container (e.g. sidebar).
   * @param save_data if @c true, the gadget's state is saved before the gadget
   *     is removed.
   */
  virtual void RemoveMe(bool save_data) = 0;

  enum DebugLevel {
    DEBUG_TRACE,
    DEBUG_WARNING,
    DEBUG_ERROR,
  };

  /** Output a debug string to the debug console or other places. */
  virtual void DebugOutput(DebugLevel level, const char *message) const = 0;

  /** Open the given URL in the user's default web browser. */
  virtual bool OpenURL(const char *url) const = 0;

  /** Temporarily install a given font on the system. */
  virtual bool LoadFont(const char *filename) = 0;
};

} // namespace ggadget

#endif // GGADGET_GADGET_HOST_INTERFACE_H__