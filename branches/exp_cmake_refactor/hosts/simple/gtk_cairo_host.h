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

#ifndef HOSTS_SIMPLE_GTK_CAIRO_HOST_H__
#define HOSTS_SIMPLE_GTK_CAIRO_HOST_H__

#include <set>

#include <gtk/gtk.h>
#include "ggadget/common.h"
#include "ggadget/host_interface.h"

namespace ggadget {
template <typename R, typename P1> class Slot1;
}
class GadgetViewWidget;

/**
 * An implementation of HostInterface for the simple gadget host. 
 * In this implementation, there is one instance of GtkCairoHost per view, 
 * and one instance of GraphicsInterface per GtkCairoHost.
 */
class GtkCairoHost : public ggadget::HostInterface {
 public:
  GtkCairoHost(GadgetViewWidget *gvw, int debug_mode);
  virtual ~GtkCairoHost();

  /** 
   * Sets the GraphicsInterface object associated with the host. Once it is set
   * the object is owned by this HostInterface and should not be freed manually.
   */
  void SetGraphics(ggadget::GraphicsInterface *gfx);
  
  /**
   * Switches the GadgetViewWidget associated with this host. 
   * When this is done, the original GadgetViewWidget will no longer have a 
   * valid GtkCairoHost object. The new GadgetViewWidget is responsible for
   * freeing this host.
   */
  void SwitchWidget(GadgetViewWidget *new_gvw, int debug_mode);
      
  virtual const ggadget::GraphicsInterface *GetGraphics() const {
    return gfx_;
  }

  virtual void QueueDraw();
   
  virtual bool GrabKeyboardFocus();

  virtual bool DetachFromView();

  virtual void SetResizeable();  
  virtual void SetCaption(const char *caption); 
  virtual void SetShowCaptionAlways(bool always);
  
  virtual int RegisterTimer(unsigned ms, void *data);
  virtual bool RemoveTimer(int token);
  virtual uint64_t GetCurrentTime() const;

  virtual ggadget::XMLHttpRequestInterface *NewXMLHttpRequest();

  typedef ggadget::Slot1<void, int> IOWatchCallback;
  int RegisterReadWatch(int fd, IOWatchCallback *callback);
  int RegisterWriteWatch(int fd, IOWatchCallback *callback);
  bool RemoveIOWatch(int token);

  int GetDebugMode() const { return debug_mode_; }

 private:
  class CallbackData;
  class IOWatchCallbackData;
  int RegisterIOWatch(bool read_or_write, int fd, IOWatchCallback *callback);
  bool RemoveCallback(int token);

  static gboolean DispatchTimer(gpointer data);
  static gboolean DispatchIOWatch(GIOChannel *source,
                                  GIOCondition cond,
                                  gpointer data);

  GadgetViewWidget *gvw_;
  ggadget::GraphicsInterface *gfx_;
  typedef std::map<int, CallbackData *> CallbackMap;
  CallbackMap callbacks_;
  int debug_mode_;

  DISALLOW_EVIL_CONSTRUCTORS(GtkCairoHost);
};

#endif // HOSTS_SIMPLE_GTK_CAIRO_HOST_H__
