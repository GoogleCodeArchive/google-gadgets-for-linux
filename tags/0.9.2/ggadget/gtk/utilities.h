/*
  Copyright 2008 Google Inc.

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

#ifndef GGADGET_GTK_UTILITIES_H__
#define GGADGET_GTK_UTILITIES_H__

#include <string>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <ggadget/view_interface.h>
#include <ggadget/slot.h>

namespace ggadget {

class Gadget;
namespace gtk {

/**
 * Displays a message box containing the message string.
 *
 * @param title tile of the alert window.
 * @param message the alert message.
 */
void ShowAlertDialog(const char *title, const char *message);

/**
 * Displays a dialog containing the message string and Yes and No buttons.
 *
 * @param title tile of the alert window.
 * @param message the message string.
 * @return @c true if Yes button is pressed, @c false if not.
 */
bool ShowConfirmDialog(const char *title, const char *message);

/**
 * Displays a dialog asking the user to enter text.
 *
 * @param title tile of the alert window.
 * @param message the message string displayed before the edit box.
 * @param default_value the initial default value dispalyed in the edit box.
 * @return the user inputted text, or an empty string if user canceled the
 *     dialog.
 */
std::string ShowPromptDialog(const char *title, const char *message,
                             const char *default_value);

/**
 * Shows an about dialog for a specified gadget.
 */
void ShowGadgetAboutDialog(Gadget *gadget);

/** Open the given URL in the user's default web browser. */
bool OpenURL(const char *url);

/** Load a given font into the application. */
bool LoadFont(const char *filename);

/**
 * Loads a GdkPixbuf object from raw image data.
 * @param data A string object containing the raw image data.
 * @return NULL on failure, a GdkPixbuf object otherwise.
 */
GdkPixbuf *LoadPixbufFromData(const std::string &data);

/**
 * Creates a GdkCursor for a specified cursor type.
 *
 * @param type the cursor type, see @c ViewInterface::CursorType.
 * @param hittest Current hit test value, used to match the cursor when there
 *        is no suitable cursor for the specified type.
 * @return a new  GdkCursor instance when succeeded, otherwize NULL.
 */
GdkCursor *CreateCursor(int type, ViewInterface::HitTest hittest);

/**
 * Disables the background of a widget.
 *
 * This function can only take effect when the Window system supports RGBA
 * visual. In another word, a window manager that supports composition must be
 * available.
 *
 * @param widget the GtkWidget of which the background to be disabled.
 * @return true if succeeded.
 */
bool DisableWidgetBackground(GtkWidget *widget);

/**
 * Checks if the window system supports composite drawing mode for a specific
 * window.
 *
 * If it returns true, then it means that the window can have transparent
 * background.
 *
 * If the window is NULL, then checks if the default screen support composite
 * drawing mode.
 */
bool SupportsComposite(GtkWidget *window);

/**
 * Talk to the window manager to maximize the window.
 *
 * @param window the gtk window you want to be maximized.
 * @param maximize_vert @c true if window should be maximized vertically
 * @param maximize_horz @c true if window should be maximized horizenly.
 * @return @true if the action succeed.
 */
bool MaximizeWindow(GtkWidget *window, bool maximize_vert, bool maximize_horz);

/**
 * Gets the geometry of the screen work area containing the specified window.
 *
 * @param window The specified gtk window in the work area.
 * @param[out] workarea Returns the geometry of the work area in the screen.
 */
void GetWorkAreaGeometry(GtkWidget *window, GdkRectangle *workarea);

/*
 * Monitor changes of the screen work area containing the specified window.
 *
 * The specified slot will be destroyed when the specified gtk window is
 * destroyed.
 *
 * Only one monitor can be attached to the specified window, and setting a
 * monitor with NULL slot will remove the old monitor.
 *
 * @param window The specified gtk window in the work area to be monitored.
 * @param slot The slot to be called when the work area changed.
 * @return true if success.
 */
bool MonitorWorkAreaChange(GtkWidget *window, Slot0<void> *slot);

} // namespace gtk
} // namespace ggadget

#endif // GGADGET_GTK_UTILITIES_H__