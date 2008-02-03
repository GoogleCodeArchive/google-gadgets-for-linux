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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <algorithm>
#include <vector>
#include <map>
#include "logger.h"
#include "module.h"
#include "common.h"
#include "extension_manager.h"

namespace ggadget {

ElementExtensionRegister::ElementExtensionRegister(ElementFactory *factory)
  : factory_(factory) {
}

bool ElementExtensionRegister::RegisterExtension(const Module *extension) {
  ASSERT(extension);
  RegisterElementExtensionFunc func =
      reinterpret_cast<RegisterElementExtensionFunc>(
          extension->GetSymbol(kElementExtensionSymbolName));

  return func ? func(factory_) : false;
}

ScriptExtensionRegister::ScriptExtensionRegister(ScriptContextInterface *context)
  : context_(context) {
}

bool ScriptExtensionRegister::RegisterExtension(const Module *extension) {
  ASSERT(extension);
  RegisterScriptExtensionFunc func =
      reinterpret_cast<RegisterScriptExtensionFunc>(
          extension->GetSymbol(kScriptExtensionSymbolName));

  return func ? func(context_) : false;
}

FrameworkExtensionRegister::FrameworkExtensionRegister(
    ScriptableInterface *framework,
    Gadget *gadget)
  : framework_(framework),
    gadget_(gadget) {
}

bool FrameworkExtensionRegister::RegisterExtension(const Module *extension) {
  ASSERT(extension);
  RegisterFrameworkExtensionFunc func =
      reinterpret_cast<RegisterFrameworkExtensionFunc>(
          extension->GetSymbol(kFrameworkExtensionSymbolName));

  return func ? func(framework_, gadget_) : false;
}

class MultipleExtensionRegisterWrapper::Impl {
 public:
  typedef std::vector<ExtensionRegisterInterface *> ExtRegisterVector;
  ExtRegisterVector ext_registers_;
};

MultipleExtensionRegisterWrapper::MultipleExtensionRegisterWrapper()
  : impl_(new Impl()) {
}

MultipleExtensionRegisterWrapper::~MultipleExtensionRegisterWrapper() {
  delete impl_;
}

bool MultipleExtensionRegisterWrapper::RegisterExtension(
    const Module *extension) {
  ASSERT(extension);

  bool result = false;

  Impl::ExtRegisterVector::iterator it = impl_->ext_registers_.begin();
  for (;it != impl_->ext_registers_.end(); ++it) {
    if ((*it)->RegisterExtension(extension))
      result = true;
  }

  return result;
}

void MultipleExtensionRegisterWrapper::AddExtensionRegister(
    ExtensionRegisterInterface *ext_register) {
  ASSERT(ext_register);
  impl_->ext_registers_.push_back(ext_register);
}

class ExtensionManager::Impl {
 public:
  Impl() : readonly_(false) {
  }

  ~Impl() {
    for (ExtensionMap::iterator it = extensions_.begin();
         it != extensions_.end(); ++it) {
      delete it->second;
    }
  }

  Module *LoadExtension(const char *name, bool resident) {
    ASSERT(name && *name);
    if (readonly_) {
      LOG("Can't load extension %s, into a readonly ExtensionManager.",
          name);
      return NULL;
    }

    if (name && *name) {
      std::string name_str(name);

      // If the module has already been loaded, then just return true.
      ExtensionMap::iterator it = extensions_.find(name_str);
      if (it != extensions_.end()) {
        if (!it->second->IsResident() && resident)
          it->second->MakeResident();
        return it->second;
      }

      Module *extension = new Module(name);
      if (!extension->IsValid()) {
        delete extension;
        return NULL;
      }

      if (resident)
        extension->MakeResident();

      extensions_[name_str] = extension;
      LOG("Extension %s was loaded successfully.", name);
      return extension;
    }
    return NULL;
  }

  bool UnloadExtension(const char *name) {
    ASSERT(name && *name);
    if (readonly_) {
      LOG("Can't unload extension %s, from a readonly ExtensionManager.", name);
      return false;
    }

    if (name && *name) {
      std::string name_str(name);

      ExtensionMap::iterator it = extensions_.find(name_str);
      if (it != extensions_.end()) {
        if (it->second->IsResident()) {
          LOG("Can't unload extension %s, it's resident.", name);
          return false;
        }
        delete it->second;
        extensions_.erase(it);
        return true;
      }
    }
    return false;
  }

  bool EnumerateLoadedExtensions(
      Slot2<bool, const char *, const char *> *callback) {
    ASSERT(callback);

    bool result = false;
    for (ExtensionMap::const_iterator it = extensions_.begin();
         it != extensions_.end(); ++it) {
      result = (*callback)(it->first.c_str(), it->second->GetName().c_str());
      if (!result) break;
    }

    delete callback;
    return result;
  }

  bool RegisterExtension(const char *name, ExtensionRegisterInterface *reg) {
    ASSERT(name && *name && reg);
    Module *extension = LoadExtension(name, false);
    if (extension && extension->IsValid()) {
      return reg->RegisterExtension(extension);
    }
    return false;
  }

  bool RegisterLoadedExtensions(ExtensionRegisterInterface *reg) {
    ASSERT(reg);
    if (extensions_.size()) {
      bool ret = true;
      for (ExtensionMap::const_iterator it = extensions_.begin();
           it != extensions_.end(); ++it) {
        if (!reg->RegisterExtension(it->second))
          ret = false;
      }
      return ret;
    }
    return false;
  }

  void MarkAsGlobal() {
    // Make all loaded extensions resident.
    for (ExtensionMap::iterator it = extensions_.begin();
         it != extensions_.end(); ++it) {
      it->second->MakeResident();
    }
    readonly_ = true;
  }

 public:
  static bool SetGlobalExtensionManager(ExtensionManager *manager) {
    if (!global_manager_ && manager) {
      global_manager_ = manager;
      global_manager_->impl_->MarkAsGlobal();
      return true;
    }
    return false;
  }

  static ExtensionManager *GetGlobalExtensionManager() {
    return global_manager_;
  }

  static ExtensionManager *global_manager_;

 private:
  typedef std::map<std::string, Module*> ExtensionMap;

  ExtensionMap extensions_;
  bool readonly_;
};

ExtensionManager* ExtensionManager::Impl::global_manager_ = NULL;

ExtensionManager::ExtensionManager()
  : impl_(new Impl()) {
}

ExtensionManager::~ExtensionManager() {
  delete impl_;
}

bool ExtensionManager::Destroy() {
  if (this && this != Impl::global_manager_) {
    delete this;
    return true;
  }

  DLOG("Try to destroy %s ExtensionManager object.",
       (this == NULL ? "an invalid" : "the global"));

  return false;
}

bool ExtensionManager::LoadExtension(const char *name, bool resident) {
  return impl_->LoadExtension(name, resident) != NULL;
}

bool ExtensionManager::UnloadExtension(const char *name) {
  return impl_->UnloadExtension(name);
}

bool ExtensionManager::EnumerateLoadedExtensions(
    Slot2<bool, const char *, const char *> *callback) const {
  return impl_->EnumerateLoadedExtensions(callback);
}

bool ExtensionManager::RegisterExtension(const char *name,
                                         ExtensionRegisterInterface *reg) const {
  return impl_->RegisterExtension(name, reg);
}

bool ExtensionManager::RegisterLoadedExtensions(
    ExtensionRegisterInterface *reg) const {
  return impl_->RegisterLoadedExtensions(reg);
}

const ExtensionManager *ExtensionManager::GetGlobalExtensionManager() {
  return Impl::GetGlobalExtensionManager();
}

bool ExtensionManager::SetGlobalExtensionManager(ExtensionManager *manager) {
  return Impl::SetGlobalExtensionManager(manager);
}

ExtensionManager *
ExtensionManager::CreateExtensionManager() {
  ExtensionManager *manager = new ExtensionManager();
  return manager;
}

} // namespace ggadget
