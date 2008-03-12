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

#include <cstdlib>
#include <gtk/gtk.h>
#include <locale.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ggadget/script_runtime_interface.h>
#include <ggadget/gtk/gadget_view_widget.h>
#include <ggadget/gtk/gtk_gadget_host.h>
#include <ggadget/gtk/gtk_view_host.h>
#include <ggadget/gtk/gtk_main_loop.h>
#include <ggadget/directory_provider_interface.h>
#include <ggadget/extension_manager.h>
#include <ggadget/script_runtime_manager.h>
#include <ggadget/ggadget.h>
#include <ggadget/gadget_consts.h>
#include <ggadget/file_manager_factory.h>
#include <ggadget/file_manager_wrapper.h>
#include <ggadget/localized_file_manager.h>

static double g_zoom = 1.;
static int g_debug_mode = 0;
static ggadget::gtk::GtkGadgetHost *g_gadget_host = NULL;
static gboolean g_composited = false;
static gboolean g_useshapemask = false;
static gboolean g_decorated = true;
static ggadget::gtk::GtkMainLoop g_main_loop;

static const char *kGlobalExtensions[] = {
// default framework must be loaded first, so that the default properties can
// be overrided.
  "default-framework",
  "libxml2-xml-parser",
  "default-options",
  "dbus-script-class",
  "gtk-edit-element",
  "gtkmoz-browser-element",
  "gtk-system-framework",
  "gst-audio-framework",
#ifdef GGL_HOST_LINUX
  "linux-system-framework",
#endif
  "smjs-script-runtime",
  "curl-xml-http-request",
  "google-gadget-manager",
  NULL
};

static const char *kGlobalResourcePaths[] = {
#ifdef GGL_RESOURCE_DIR
  GGL_RESOURCE_DIR "/ggl-resources.gg",
  GGL_RESOURCE_DIR "/ggl-resources",
#endif
  "ggl-resources.gg",
  "ggl-resources",
  NULL
};

static gboolean DeleteEventHandler(GtkWidget *widget,
                                   GdkEvent *event,
                                   gpointer data) {
  return FALSE;
}

static gboolean DestroyHandler(GtkWidget *widget,
                               gpointer data) {
  gtk_main_quit();
  return FALSE;
}

static bool CreateGadgetUI(GtkWindow *window, GtkBox *box,
                           const char *base_path) {
  g_gadget_host = new ggadget::gtk::GtkGadgetHost(g_composited,
                                                  g_useshapemask, g_zoom,
                                                  g_debug_mode);
  if (!g_gadget_host->LoadGadget(box, base_path)) {
    LOG("Failed to load gadget from: %s", base_path);
    return false;
  }

  ggadget::gtk::GtkViewHost *view_host =
      ggadget::down_cast<ggadget::gtk::GtkViewHost *>(
          g_gadget_host->GetGadget()->GetMainViewHost());
  GadgetViewWidget *gvw = view_host->GetWidget();
  gtk_box_pack_start(box, GTK_WIDGET(gvw), TRUE, TRUE, 0);

  // Setting min size here allows the window to resize below the size
  // request of the gadget view.
  GdkGeometry geometry;
  geometry.min_width = geometry.min_height = 100;
  gtk_window_set_geometry_hints(window, GTK_WIDGET(gvw),
                                &geometry, GDK_HINT_MIN_SIZE);
  return true;
}

static bool CreateGTKUI(const char *base_path) {
  GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window), "Google Gadgets");
  if (!g_decorated) {
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
  }
  g_signal_connect(G_OBJECT(window), "delete_event",
                   G_CALLBACK(DeleteEventHandler), NULL);
  g_signal_connect(G_OBJECT(window), "destroy",
                   G_CALLBACK(DestroyHandler), NULL);

  GdkScreen *screen = gtk_widget_get_screen(window);
#if GTK_CHECK_VERSION(2,10,0) // this line requires gtk 2.10
  g_composited = gdk_screen_is_composited(screen);
#endif
  DLOG("Composited screen? %d", static_cast<int>(g_composited));
  DLOG("Use shape mask? %d", static_cast<int>(g_useshapemask));

  if (g_composited) {
    GdkColormap *rgba;
    rgba = gdk_screen_get_rgba_colormap (screen);
    gtk_widget_set_colormap (window, rgba);
  }

  GtkBox *vbox = GTK_BOX(gtk_vbox_new(FALSE, 0));
  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(vbox));

  GtkWidget *exit_button = gtk_button_new_with_label("Exit");
  gtk_box_pack_end(vbox, exit_button, FALSE, FALSE, 0);
  g_signal_connect_swapped(G_OBJECT(exit_button), "clicked",
                           G_CALLBACK(gtk_widget_destroy), G_OBJECT(window));

  GtkWidget *separator = gtk_hseparator_new();
  gtk_box_pack_end(vbox, separator, FALSE, FALSE, 5);

  if (!CreateGadgetUI(GTK_WINDOW(window), vbox, base_path)) {
    return false;
  }

  //gtk_widget_realize(window);
  //gtk_window_set_opacity(GTK_WINDOW(window), .0);
  gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ALWAYS);
  gtk_widget_show_all(window);

  return true;
}

static void DestroyUI() {
  delete g_gadget_host;
  g_gadget_host = NULL;
}

class DirectoryProvider : public ggadget::DirectoryProviderInterface {
 public:
  virtual std::string GetProfileDirectory() { return ""; }
  virtual std::string GetResourceDirectory() { return ""; }
};
static DirectoryProvider g_directory_provider;

int main(int argc, char* argv[]) {
  gtk_init(&argc, &argv);

  // set locale according to env vars
  setlocale(LC_ALL, "");

  if (argc < 2) {
    LOG("Error: not enough arguments. Gadget base path required.");
    return -1;
  }

  if (argc >= 3) {
    sscanf(argv[2], "%lg", &g_zoom);
    if (g_zoom <= 0 || g_zoom > 5) {
      LOG("Zoom level invalid, resetting to 1");
      g_zoom = 1.;
    }
  }

  if (argc >= 4) {
    sscanf(argv[3], "%d", &g_debug_mode);
    if (g_debug_mode < 0 || g_debug_mode > 2) {
      LOG("Debug mode invalid, resetting to 0");
      g_debug_mode = 0;
    }
  }

  if (argc >= 5) {
    int useshapemask;
    sscanf(argv[4], "%d", &useshapemask);
    g_useshapemask = (useshapemask != 0);
  }

  if (argc >= 6) {
    int decorated;
    sscanf(argv[5], "%d", &decorated);
    g_decorated = (decorated != 0);
  }

  // Set global main loop
  ggadget::SetGlobalMainLoop(&g_main_loop);
  ggadget::SetDirectoryProvider(&g_directory_provider);

  // Set global file manager.
  ggadget::FileManagerWrapper *fm_wrapper = new ggadget::FileManagerWrapper();
  ggadget::FileManagerInterface *fm;

  for (size_t i = 0; kGlobalResourcePaths[i]; ++i) {
    fm = ggadget::CreateFileManager(kGlobalResourcePaths[i]);
    if (fm) {
      fm_wrapper->RegisterFileManager(ggadget::kGlobalResourcePrefix,
                                      new ggadget::LocalizedFileManager(fm));
      break;
    }
  }

  if ((fm = ggadget::CreateFileManager(ggadget::kDirSeparatorStr)) != NULL)
    fm_wrapper->RegisterFileManager(ggadget::kDirSeparatorStr, fm);
  // TODO: Proper profile directory.
  if ((fm = ggadget::CreateFileManager(".")) != NULL)
    fm_wrapper->RegisterFileManager(ggadget::kProfilePrefix, fm);

  ggadget::SetGlobalFileManager(fm_wrapper);

  // Load global extensions.
  ggadget::ExtensionManager *ext_manager =
      ggadget::ExtensionManager::CreateExtensionManager();
  ggadget::ExtensionManager::SetGlobalExtensionManager(ext_manager);

  // Ignore errors when loading extensions.
  for (size_t i = 0; kGlobalExtensions[i]; ++i)
    ext_manager->LoadExtension(kGlobalExtensions[i], false);

  // Register JavaScript runtime.
  ggadget::ScriptRuntimeManager *manager = ggadget::ScriptRuntimeManager::get();
  ggadget::ScriptRuntimeExtensionRegister script_runtime_register(manager);
  ext_manager->RegisterLoadedExtensions(&script_runtime_register);

  // Make the global extension manager readonly to avoid the potential
  // danger that a bad gadget register local extensions into the global
  // extension manager.
  ext_manager->SetReadonly();

  if (!CreateGTKUI(argv[1])) {
    LOG("Error: unable to create UI");
    return -1;
  }

  gtk_main();

  DestroyUI();

  return 0;
}