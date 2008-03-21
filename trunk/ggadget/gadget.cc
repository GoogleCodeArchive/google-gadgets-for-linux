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

#include "gadget.h"
#include "contentarea_element.h"
#include "content_item.h"
#include "details_view_data.h"
#include "display_window.h"
#include "element_factory.h"
#include "file_manager_interface.h"
#include "file_manager_factory.h"
#include "file_manager_wrapper.h"
#include "localized_file_manager.h"
#include "gadget_consts.h"
#include "logger.h"
#include "main_loop_interface.h"
#include "menu_interface.h"
#include "host_interface.h"
#include "options_interface.h"
#include "script_context_interface.h"
#include "script_runtime_manager.h"
#include "scriptable_array.h"
#include "scriptable_framework.h"
#include "scriptable_helper.h"
#include "scriptable_menu.h"
#include "scriptable_options.h"
#include "scriptable_view.h"
#include "system_utils.h"
#include "view_host_interface.h"
#include "view.h"
#include "xml_parser_interface.h"
#include "xml_utils.h"
#include "extension_manager.h"

namespace ggadget {

class Gadget::Impl : public ScriptableHelperNativeOwnedDefault {
 public:
  DEFINE_CLASS_ID(0x6a3c396b3a544148, ScriptableInterface);

  /**
   * A class to bundles View, ScriptableView, ScriptContext, and
   * DetailsViewData together.
   */
  class ViewBundle {
   public:
    ViewBundle(ViewHostInterface *host,
               Gadget *gadget,
               ElementFactory *element_factory,
               ScriptableInterface *prototype,
               DetailsViewData *details,
               bool support_script)
      : host_(host),
        context_(NULL),
        view_(NULL),
        scriptable_(NULL),
        details_(details) {
      ASSERT(host_);
      if (support_script) {
        // Only xml based views have standalone script context.
        // FIXME: ScriptContext instance should be created on-demand, according
        // to the type of script files shipped in the gadget.
        // Or maybe we should add an option in gadget.gmanifest to specify what
        // ScriptRuntime implementation is required.
        // We may support multiple different script languages later.
        context_ = ScriptRuntimeManager::get()->CreateScriptContext("js");
      }

      view_ = new View(host_, gadget, element_factory, context_);

      if (details_)
        details_->Ref();
      if (context_)
        scriptable_ = new ScriptableView(view_, prototype, context_);
    }

    ~ViewBundle() {
      if (details_) {
        details_->Unref();
        details_ = NULL;
      }
      delete scriptable_;
      scriptable_ = NULL;
      delete view_;
      view_ = NULL;
      if (context_) {
        context_->Destroy();
        context_ = NULL;
      }
      if (host_) {
        host_->Destroy();
        host_ = NULL;
      }
    }

    ScriptContextInterface *context() { return context_; }
    View *view() { return view_; }
    ScriptableView *scriptable() { return scriptable_; }
    DetailsViewData *details() { return details_; }

   private:
    ViewHostInterface *host_;
    ScriptContextInterface *context_;
    View *view_;
    ScriptableView *scriptable_;
    DetailsViewData *details_;
  };

  Impl(Gadget *owner,
       HostInterface *host,
       const char *base_path,
       const char *options_name,
       int instance_id)
      : owner_(owner),
        host_(host),
        element_factory_(new ElementFactory()),
        extension_manager_(ExtensionManager::CreateExtensionManager()),
        file_manager_(new FileManagerWrapper()),
        options_(CreateOptions(options_name)),
        scriptable_options_(new ScriptableOptions(options_, false)),
        main_view_(NULL),
        options_view_(NULL),
        details_view_(NULL),
        old_details_view_(NULL),
        base_path_(base_path),
        instance_id_(instance_id),
        initialized_(false),
        has_options_xml_(false),
        plugin_flags_(0) {
    // Checks if necessary objects are created successfully.
    ASSERT(host_);
    ASSERT(element_factory_);
    ASSERT(extension_manager_);
    ASSERT(file_manager_);
    ASSERT(options_);
    ASSERT(scriptable_options_);
  }

  ~Impl() {
    delete old_details_view_;
    old_details_view_ = NULL;
    delete details_view_;
    details_view_ = NULL;
    delete options_view_;
    options_view_ = NULL;
    delete main_view_;
    main_view_ = NULL;
    delete scriptable_options_;
    scriptable_options_ = NULL;
    delete options_;
    options_ = NULL;
    delete file_manager_;
    file_manager_ = NULL;
    if (extension_manager_) {
      extension_manager_->Destroy();
      extension_manager_ = NULL;
    }
    delete element_factory_;
    element_factory_ = NULL;
  }

  static bool ExtractFileFromFileManager(FileManagerInterface *fm,
                                         const char *file,
                                         std::string *path) {
    path->clear();
    if (fm->IsDirectlyAccessible(file, path))
      return true;

    path->clear();
    if (fm->ExtractFile(file, path))
      return true;

    return false;
  }

  // Do real initialize.
  bool Initialize() {
    if (!host_ || !element_factory_ || !file_manager_ || !options_ ||
        !scriptable_options_)
      return false;

    // Create gadget FileManager
    FileManagerInterface *fm = CreateGadgetFileManager(base_path_.c_str());
    if (fm == NULL) return false;
    file_manager_->RegisterFileManager("", fm);

    // Create system FileManager
    fm = CreateFileManager(kDirSeparatorStr);
    if (fm) file_manager_->RegisterFileManager(kDirSeparatorStr, fm);

    // Load strings and manifest.
    if (!ReadStringsAndManifest(file_manager_, &strings_map_,
                                &manifest_info_map_))
      return false;

    // TODO: Is it necessary to check the required fields in manifest?
    DLOG("Gadget min version: %s",
         GetManifestInfo(kManifestMinVersion).c_str());
    DLOG("Gadget id: %s", GetManifestInfo(kManifestId).c_str());
    DLOG("Gadget name: %s", GetManifestInfo(kManifestName).c_str());
    DLOG("Gadget description: %s",
         GetManifestInfo(kManifestDescription).c_str());

    // main view must be created before calling RegisterProperties();
    main_view_ = new ViewBundle(
        host_->NewViewHost(ViewHostInterface::VIEW_HOST_MAIN),
        owner_, element_factory_, &global_, NULL, true);
    ASSERT(main_view_);

    // Register scriptable properties.
    RegisterProperties();
    RegisterStrings(&strings_map_, &global_);
    RegisterStrings(&strings_map_, &strings_);

    // load fonts and objects
    for (StringMap::const_iterator i = manifest_info_map_.begin();
         i != manifest_info_map_.end(); ++i) {
      const std::string &key = i->first;
      if (SimpleMatchXPath(key.c_str(), kManifestInstallFontSrc)) {
        const char *font_name = i->second.c_str();
        std::string path;
        // ignore return, error not fatal
        if (ExtractFileFromFileManager(file_manager_, font_name, &path))
          host_->LoadFont(path.c_str());
      } else if (SimpleMatchXPath(key.c_str(), kManifestInstallObjectSrc) &&
                 extension_manager_) {
        const char *module_name = i->second.c_str();
        std::string path;
        if (ExtractFileFromFileManager(file_manager_, module_name, &path))
          extension_manager_->LoadExtension(path.c_str(), false);
      }
    }

    // Register extensions
    const ExtensionManager *global_manager =
        ExtensionManager::GetGlobalExtensionManager();
    MultipleExtensionRegisterWrapper register_wrapper;
    ElementExtensionRegister element_register(element_factory_);
    FrameworkExtensionRegister framework_register(&framework_, owner_);

    register_wrapper.AddExtensionRegister(&element_register);
    register_wrapper.AddExtensionRegister(&framework_register);

    if (global_manager)
      global_manager->RegisterLoadedExtensions(&register_wrapper);
    if (extension_manager_)
      extension_manager_->RegisterLoadedExtensions(&register_wrapper);

    // Initialize main view.
    std::string main_xml;
    if (!file_manager_->ReadFile(kMainXML, &main_xml)) {
      LOG("Failed to load main.xml.");
      return false;
    }

    main_view_->view()->SetCaption(GetManifestInfo(kManifestName).c_str());
    RegisterScriptExtensions(main_view_->context());

    if (!main_view_->scriptable()->InitFromXML(main_xml, kMainXML)) {
      LOG("Failed to setup the main view");
      return false;
    }

    has_options_xml_ = file_manager_->FileExists(kOptionsXML, NULL);

    return true;
  }

  // Register script extensions for a specified script context.
  // This method shall be called for all views' script contexts.
  void RegisterScriptExtensions(ScriptContextInterface *context) {
    ASSERT(context);
    const ExtensionManager *global_manager =
        ExtensionManager::GetGlobalExtensionManager();
    ScriptExtensionRegister script_register(context);

    if (global_manager)
      global_manager->RegisterLoadedExtensions(&script_register);
    if (extension_manager_)
      extension_manager_->RegisterLoadedExtensions(&script_register);
  }

  // Register all scriptable properties.
  void RegisterProperties() {
    RegisterConstant("debug", &debug_);
    RegisterConstant("storage", &storage_);

    // Register properties of gadget.debug.
    debug_.RegisterMethod("error", NewSlot(this, &Impl::DebugError));
    debug_.RegisterMethod("trace", NewSlot(this, &Impl::DebugTrace));
    debug_.RegisterMethod("warning", NewSlot(this, &Impl::DebugWarning));

    // Register properties of gadget.storage.
    storage_.RegisterMethod("extract", NewSlot(this, &Impl::ExtractFile));
    storage_.RegisterMethod("openText", NewSlot(this, &Impl::OpenTextFile));

    // Register properties of plugin.
    plugin_.RegisterProperty("plugin_flags", NULL, // No getter.
                NewSlot(this, &Impl::SetPluginFlags));
    plugin_.RegisterProperty("title", NULL, // No getter.
                NewSlot(main_view_->view(), &View::SetCaption));
    plugin_.RegisterProperty("window_width",
                NewSlot(main_view_->view(), &View::GetWidth), NULL);
    plugin_.RegisterProperty("window_height",
                NewSlot(main_view_->view(), &View::GetHeight), NULL);

    plugin_.RegisterMethod("RemoveMe",
                NewSlot(this, &Impl::RemoveMe));
    plugin_.RegisterMethod("ShowDetailsView",
                NewSlot(this, &Impl::ShowDetailsViewProxy));
    plugin_.RegisterMethod("CloseDetailsView",
                NewSlot(this, &Impl::CloseDetailsView));
    plugin_.RegisterMethod("ShowOptionsDialog",
                NewSlot(this, &Impl::ShowOptionsDialog));

    plugin_.RegisterSignal("onShowOptionsDlg",
                           &onshowoptionsdlg_signal_);
    plugin_.RegisterSignal("onAddCustomMenuItems",
                           &onaddcustommenuitems_signal_);
    plugin_.RegisterSignal("onCommand",
                           &oncommand_signal_);
    plugin_.RegisterSignal("onDisplayStateChange",
                           &ondisplaystatechange_signal_);
    plugin_.RegisterSignal("onDisplayTargetChange",
                           &ondisplaytargetchange_signal_);

    // Deprecated or unofficial properties and methods.
    plugin_.RegisterProperty("about_text", NULL, // No getter.
                             NewSlot(this, &Impl::SetAboutText));
    plugin_.RegisterMethod("SetFlags", NewSlot(this, &Impl::SetFlags));
    plugin_.RegisterMethod("SetIcons", NewSlot(this, &Impl::SetIcons));

    // Register properties and methods for content area.
    plugin_.RegisterProperty("contant_flags", NULL, // Write only.
                             NewSlot(this, &Impl::SetContentFlags));
    plugin_.RegisterProperty("max_content_items",
                             NewSlot(this, &Impl::GetMaxContentItems),
                             NewSlot(this, &Impl::SetMaxContentItems));
    plugin_.RegisterProperty("content_items",
                             NewSlot(this, &Impl::GetContentItems),
                             NewSlot(this, &Impl::SetContentItems));
    plugin_.RegisterProperty("pin_images",
                             NewSlot(this, &Impl::GetPinImages),
                             NewSlot(this, &Impl::SetPinImages));
    plugin_.RegisterMethod("AddContentItem",
                           NewSlot(this, &Impl::AddContentItem));
    plugin_.RegisterMethod("RemoveContentItem",
                           NewSlot(this, &Impl::RemoveContentItem));
    plugin_.RegisterMethod("RemoveAllContentItems",
                           NewSlot(this, &Impl::RemoveAllContentItems));

    // Register global properties.
    global_.RegisterConstant("gadget", this);
    global_.RegisterConstant("options", scriptable_options_);
    global_.RegisterConstant("strings", &strings_);
    global_.RegisterConstant("plugin", &plugin_);
    global_.RegisterConstant("pluginHelper", &plugin_);

    // As an unofficial feature, "gadget.debug" and "gadget.storage" can also
    // be accessed as "debug" and "storage" global objects.
    global_.RegisterConstant("debug", &debug_);
    global_.RegisterConstant("storage", &storage_);

    // Properties and methods of framework can also be accessed directly as
    // globals.
    global_.RegisterConstant("framework", &framework_);
    global_.SetInheritsFrom(&framework_);
  }

  void RemoveMe(bool save_data) {
    host_->RemoveGadget(instance_id_, save_data);
  }

  void RemoveMeMenuCallback(const char *) {
    RemoveMe(true);
  }

  void AboutMenuCallback(const char *) {
    host_->ShowGadgetAboutDialog(owner_);
  }

  void OptionsMenuCallback(const char *) {
    ShowOptionsDialog();
  }

  void OnAddCustomMenuItems(MenuInterface *menu) {
    ScriptableMenu scriptable_menu(menu);
    onaddcustommenuitems_signal_(&scriptable_menu);
    if (HasOptionsDialog())
      menu->AddItem("Options...", 0, NewSlot(this, &Impl::OptionsMenuCallback));
    menu->AddItem("Remove Me", 0, NewSlot(this, &Impl::RemoveMeMenuCallback));
    menu->AddItem("About...", 0, NewSlot(this, &Impl::AboutMenuCallback));
  }

  void SetPluginFlags(int flags) {
    bool changed = (flags != plugin_flags_);
    plugin_flags_ = flags;
    if (changed)
      onpluginflagschanged_signal_(flags);
  }

  void SetFlags(int plugin_flags, int content_flags) {
    SetPluginFlags(plugin_flags);
    SetContentFlags(content_flags);
  }

  void SetIcons(const Variant &param1, const Variant &param2) {
    LOG("pluginHelper.SetIcons is no longer supported. "
        "Please specify icons in the manifest file.");
  }

  void SetContentFlags(int flags) {
    ContentAreaElement *content_area =
        main_view_->view()->GetContentAreaElement();
    if (content_area) content_area->SetContentFlags(flags);
  }

  size_t GetMaxContentItems() {
    ContentAreaElement *content_area =
        main_view_->view()->GetContentAreaElement();
    return content_area ? content_area->GetMaxContentItems() : 0;
  }

  void SetMaxContentItems(size_t max_content_items) {
    ContentAreaElement *content_area =
        main_view_->view()->GetContentAreaElement();
    if (content_area) content_area->SetMaxContentItems(max_content_items);
  }

  ScriptableArray *GetContentItems() {
    ContentAreaElement *content_area =
        main_view_->view()->GetContentAreaElement();
    return content_area ? content_area->ScriptGetContentItems() : NULL;
  }

  void SetContentItems(ScriptableInterface *array) {
    ContentAreaElement *content_area =
        main_view_->view()->GetContentAreaElement();
    if (content_area) content_area->ScriptSetContentItems(array);
  }

  ScriptableArray *GetPinImages() {
    ContentAreaElement *content_area =
        main_view_->view()->GetContentAreaElement();
    return content_area ? content_area->ScriptGetPinImages() : NULL;
  }

  void SetPinImages(ScriptableInterface *array) {
    ContentAreaElement *content_area =
        main_view_->view()->GetContentAreaElement();
    if (content_area) content_area->ScriptSetPinImages(array);
  }

  void AddContentItem(ContentItem *item,
                      ContentAreaElement::DisplayOptions options) {
    ContentAreaElement *content_area =
        main_view_->view()->GetContentAreaElement();
    if (content_area) content_area->AddContentItem(item, options);
  }

  void RemoveContentItem(ContentItem *item) {
    ContentAreaElement *content_area =
        main_view_->view()->GetContentAreaElement();
    if (content_area) content_area->RemoveContentItem(item);
  }

  void RemoveAllContentItems() {
    ContentAreaElement *content_area =
        main_view_->view()->GetContentAreaElement();
    if (content_area) content_area->RemoveAllContentItems();
  }

  void SetAboutText(const char *about_text) {
    manifest_info_map_[kManifestAboutText] = about_text;
  }


  void DebugError(const char *message) {
    host_->DebugOutput(HostInterface::DEBUG_ERROR, message);
  }

  void DebugTrace(const char *message) {
    host_->DebugOutput(HostInterface::DEBUG_TRACE, message);
  }

  void DebugWarning(const char *message) {
    host_->DebugOutput(HostInterface::DEBUG_WARNING, message);
  }

  std::string ExtractFile(const char *filename) {
    std::string extracted_file;
    return file_manager_->ExtractFile(filename, &extracted_file) ?
        extracted_file : "";
  }

  std::string OpenTextFile(const char *filename) {
    std::string data;
    std::string result;
    if (file_manager_->ReadFile(filename, &data) &&
        !DetectAndConvertStreamToUTF8(data, &result, NULL))
      LOG("gadget.storage.openText() failed to read text file: %s", filename);
    return result;
  }

  std::string GetManifestInfo(const char *key) {
    GadgetStringMap::const_iterator it = manifest_info_map_.find(key);
    if (it == manifest_info_map_.end())
      return std::string();
    return it->second;
  }

  bool HasOptionsDialog() {
    return has_options_xml_ || onshowoptionsdlg_signal_.HasActiveConnections();
  }

  void OptionsDialogCallback(int flag) {
    if (options_view_) {
      SimpleEvent event((flag == ViewInterface::OPTIONS_VIEW_FLAG_OK) ?
                        Event::EVENT_OK : Event::EVENT_CANCEL);
      options_view_->view()->OnOtherEvent(event);
    }
  }

  bool ShowOptionsDialog() {
    bool ret = false;
    int flag = ViewInterface::OPTIONS_VIEW_FLAG_OK |
               ViewInterface::OPTIONS_VIEW_FLAG_CANCEL;

    if (onshowoptionsdlg_signal_.HasActiveConnections()) {
      options_view_ = new ViewBundle(
          host_->NewViewHost(ViewHostInterface::VIEW_HOST_OPTIONS),
          owner_, element_factory_, NULL, NULL, false);
      DisplayWindow *window = new DisplayWindow(options_view_->view());
      Variant result = onshowoptionsdlg_signal_(window);
      if ((result.type() != Variant::TYPE_BOOL ||
           VariantValue<bool>()(result)) && window->AdjustSize()) {
        options_view_->view()->SetResizable(ViewInterface::RESIZABLE_FALSE);
        ret = options_view_->view()->ShowView(
            true, flag, NewSlot(this, &Impl::OptionsDialogCallback));
      } else {
        LOG("gadget cancelled the options dialog.");
      }
      delete window;
      delete options_view_;
      options_view_ = NULL;
    } else if (has_options_xml_) {
      std::string xml;
      if (file_manager_->ReadFile(kOptionsXML, &xml)) {
        options_view_ = new ViewBundle(
            host_->NewViewHost(ViewHostInterface::VIEW_HOST_OPTIONS),
            owner_, element_factory_, &global_, NULL, true);
        RegisterScriptExtensions(options_view_->context());
        std::string full_path = file_manager_->GetFullPath(kOptionsXML);
        if (options_view_->scriptable()->InitFromXML(xml, full_path.c_str())) {
          options_view_->view()->SetResizable(ViewInterface::RESIZABLE_FALSE);
          ret = options_view_->view()->ShowView(
              true, flag, NewSlot(this, &Impl::OptionsDialogCallback));
        } else {
          LOG("Failed to setup the options view");
        }
        delete options_view_;
        options_view_ = NULL;
      } else {
        LOG("Failed to load options.xml file from gadget package.");
      }
    } else {
      LOG("Failed to show options dialog because there is neither options.xml"
          "nor OnShowOptionsDlg handler");
    }
    return ret;
  }

  bool ShowDetailsViewProxy(DetailsViewData *details_view_data,
                            const char *title, int flags,
                            Slot *callback) {
    Slot1<void, int> *feedback_handler =
        callback ? new SlotProxy1<void, int>(callback) : NULL;
    return ShowDetailsView(details_view_data, title, flags, feedback_handler);
  }

  bool ShowDetailsView(DetailsViewData *details_view_data,
                       const char *title, int flags,
                       Slot1<void, int> *feedback_handler) {
    CloseDetailsView();
    details_view_ = new ViewBundle(
        host_->NewViewHost(ViewHostInterface::VIEW_HOST_DETAILS),
        owner_, element_factory_, &global_, details_view_data, true);
    ScriptContextInterface *context = details_view_->context();
    ScriptableOptions *scriptable_data = details_view_->details()->GetData();
    OptionsInterface *data = scriptable_data->GetOptions();

    // Register script extensions.
    RegisterScriptExtensions(context);

    // Set up the detailsViewData variable in the opened details view.
    context->AssignFromNative(NULL, "", "detailsViewData",
                              Variant(scriptable_data));

    std::string xml;
    std::string xml_file;
    if (details_view_data->GetContentIsHTML() ||
        !details_view_data->GetContentIsView()) {
      if (details_view_data->GetContentIsHTML()) {
        xml_file = kHTMLDetailsView;
        ScriptableInterface *ext_obj = details_view_data->GetExternalObject();
        context->AssignFromNative(NULL, "", "external", Variant(ext_obj));
        data->PutValue("contentType", Variant("text/html"));
      } else {
        xml_file = kTextDetailsView;
        data->PutValue("contentType", Variant("text/plain"));
      }
      data->PutValue("content", Variant(details_view_data->GetText()));
      GetGlobalFileManager()->ReadFile(xml_file.c_str(), &xml);
    } else {
      xml_file = details_view_data->GetText();
      file_manager_->ReadFile(xml_file.c_str(), &xml);
    }

    if (xml.empty() ||
        !details_view_->scriptable()->InitFromXML(xml, xml_file.c_str())) {
      LOG("Failed to load details view from %s", xml_file.c_str());
      delete details_view_;
      details_view_ = NULL;
      return false;
    }

    // For details view, the caption set in xml file will be discarded.
    if (title && *title)
      details_view_->view()->SetCaption(title);

    details_view_->view()->ShowView(title, flags, feedback_handler);
    return true;
  }

  void CloseDetailsView() {
    delete old_details_view_;

    if (details_view_)
      details_view_->view()->CloseView();

    old_details_view_ = details_view_;
    details_view_ = NULL;
  }

  Connection* ConnectOnPluginFlagsChanged(Slot1<void, int> *handler) {
    return onpluginflagschanged_signal_.Connect(handler);
  }

  static void RegisterStrings(const StringMap *strings,
                              ScriptableHelperNativeOwnedDefault *scriptable) {
    for (StringMap::const_iterator it = strings->begin();
         it != strings->end(); ++it) {
      scriptable->RegisterConstant(it->first.c_str(), it->second);
    }
  }

  static bool ReadStringsAndManifest(FileManagerInterface *file_manager,
                                     StringMap *strings_map,
                                     StringMap *manifest_info_map) {
    // Load string table.
    std::string strings_data;
    if (file_manager->ReadFile(kStringsXML, &strings_data)) {
      std::string full_path = file_manager->GetFullPath(kStringsXML);
      if (!GetXMLParser()->ParseXMLIntoXPathMap(strings_data, NULL,
                                                full_path.c_str(),
                                                kStringsTag,
                                                NULL, kEncodingFallback,
                                                strings_map)) {
        return false;
      }
    }

    std::string manifest_contents;
    if (!file_manager->ReadFile(kGadgetGManifest, &manifest_contents))
      return false;

    std::string manifest_path = file_manager->GetFullPath(kGadgetGManifest);
    if (!GetXMLParser()->ParseXMLIntoXPathMap(manifest_contents,
                                              strings_map,
                                              manifest_path.c_str(),
                                              kGadgetTag,
                                              NULL, kEncodingFallback,
                                              manifest_info_map)) {
      return false;
    }
    return true;
  }

  static FileManagerInterface *CreateGadgetFileManager(const char *base_path) {
    std::string path, filename;
    SplitFilePath(base_path, &path, &filename);

    // Uses the parent path of base_path if it refers to a manifest file.
    if (filename.length() <= strlen(kGManifestExt) ||
        strcasecmp(filename.c_str() + filename.size() - strlen(kGManifestExt),
                   kGManifestExt) != 0) {
      path = base_path;
    }

    FileManagerInterface *fm = CreateFileManager(path.c_str());
    return fm ? new LocalizedFileManager(fm) : NULL;
  }

  NativeOwnedScriptable global_;
  NativeOwnedScriptable debug_;
  NativeOwnedScriptable storage_;
  NativeOwnedScriptable plugin_;
  NativeOwnedScriptable framework_;
  NativeOwnedScriptable strings_;

  Signal1<Variant, DisplayWindow *> onshowoptionsdlg_signal_;
  Signal1<void, ScriptableMenu *> onaddcustommenuitems_signal_;
  Signal1<void, int> oncommand_signal_;
  Signal1<void, int> ondisplaystatechange_signal_;
  Signal1<void, int> ondisplaytargetchange_signal_;

  Signal1<void, int> onpluginflagschanged_signal_;

  StringMap manifest_info_map_;
  StringMap strings_map_;

  Gadget *owner_;
  HostInterface *host_;
  ElementFactory *element_factory_;
  ExtensionManager *extension_manager_;
  FileManagerWrapper *file_manager_;
  OptionsInterface *options_;
  ScriptableOptions *scriptable_options_;

  ViewBundle *main_view_;
  ViewBundle *options_view_;
  ViewBundle *details_view_;
  ViewBundle *old_details_view_;

  std::string base_path_;
  int instance_id_;
  bool initialized_;
  bool has_options_xml_;
  int plugin_flags_;
};

Gadget::Gadget(HostInterface *host,
               const char *base_path,
               const char *options_name,
               int instance_id)
    : impl_(new Impl(this, host, base_path, options_name, instance_id)) {
  impl_->initialized_ = impl_->Initialize();
}

Gadget::~Gadget() {
  delete impl_;
  impl_ = NULL;
}

HostInterface *Gadget::GetHost() const {
  return impl_->host_;
}

void Gadget::RemoveMe(bool save_data) {
  impl_->RemoveMe(save_data);
}

bool Gadget::IsValid() const {
  return impl_->initialized_;
}

int Gadget::GetInstanceID() const {
  return impl_->instance_id_;
}

int Gadget::GetPluginFlags() const {
  return impl_->plugin_flags_;
}

FileManagerInterface *Gadget::GetFileManager() const {
  return impl_->file_manager_;
}

OptionsInterface *Gadget::GetOptions() const {
  return impl_->options_;
}

View *Gadget::GetMainView() const {
  return impl_->main_view_->view();
}

std::string Gadget::GetManifestInfo(const char *key) const {
  return impl_->GetManifestInfo(key);
}

const StringMap &Gadget::GetStrings() const {
  return impl_->strings_map_;
}

bool Gadget::ShowMainView() {
  ASSERT(IsValid());
  return impl_->main_view_->view()->ShowView(false, 0, NULL);
}

void Gadget::CloseMainView() {
  impl_->main_view_->view()->CloseView();
}

bool Gadget::HasOptionsDialog() const {
  return impl_->HasOptionsDialog();
}

bool Gadget::ShowOptionsDialog() {
  return impl_->ShowOptionsDialog();
}

bool Gadget::ShowDetailsView(DetailsViewData *details_view_data,
                             const char *title, int flags,
                             Slot1<void, int> *feedback_handler) {
  return impl_->ShowDetailsView(details_view_data, title, flags,
                                feedback_handler);
}

void Gadget::CloseDetailsView() {
  return impl_->CloseDetailsView();
}

void Gadget::OnAddCustomMenuItems(MenuInterface *menu) {
  impl_->OnAddCustomMenuItems(menu);
}

void Gadget::OnCommand(Command command) {
  impl_->oncommand_signal_(command);
}

void Gadget::OnDisplayStateChange(DisplayState display_state) {
  impl_->ondisplaystatechange_signal_(display_state);
}

void Gadget::OnDisplayTargetChange(DisplayTarget display_target) {
  impl_->ondisplaytargetchange_signal_(display_target);
}

Connection *Gadget::ConnectOnPluginFlagsChanged(Slot1<void, int> *handler) {
  return impl_->ConnectOnPluginFlagsChanged(handler);
}

// static methods
bool Gadget::GetGadgetManifest(const char *base_path, StringMap *data) {
  ASSERT(base_path);
  ASSERT(data);

  FileManagerInterface *file_manager = Impl::CreateGadgetFileManager(base_path);
  if (!file_manager)
    return false;

  StringMap strings_map;
  bool result = Impl::ReadStringsAndManifest(file_manager, &strings_map, data);
  delete file_manager;
  return result;
}

} // namespace ggadget