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

#include <vector>
#include <sys/utsname.h>

#include "machine.h"
#include <ggadget/common.h>

namespace ggadget {
namespace framework {

static const char* kKeysInMachineInfo[] = { "cpu family", "model", "stepping",
    "vendor_id", "model name", "cpu MHz" };

// Represents the file names for reading CPU info.
static const char* kCPUInfoFile = "/proc/cpuinfo";

Machine::Machine() {
  InitArchInfo();
  InitProcInfo();
}

const char *Machine::GetBiosSerialNumber() const {
  return "to be continued...";
}

const char *Machine::GetMachineManufacturer() const {
  return "to be continued...to be continued...\";";
}

const char *Machine::GetMachineModel() const {
  return "to be continued...";
}

const char *Machine::GetProcessorArchitecture() const {
  return sysinfo_[CPU_ARCH].c_str();
}

int Machine::GetProcessorCount() const {
  return cpu_count_;
}

int Machine::GetProcessorFamily() const {
  return strtol(sysinfo_[CPU_FAMILY].c_str(), NULL, 10);
}

int Machine::GetProcessorModel() const {
  return strtol(sysinfo_[CPU_MODEL].c_str(), NULL, 10);
}

const char* Machine::GetProcessorName() const {
  return sysinfo_[CPU_NAME].c_str();
}

int Machine::GetProcessorSpeed() const {
  return strtol(sysinfo_[CPU_SPEED].c_str(), NULL, 10);
}

int Machine::GetProcessorStepping() const {
  return strtol(sysinfo_[CPU_STEPPING].c_str(), NULL, 10);
}

const char *Machine::GetProcessorVendor() const {
  return sysinfo_[CPU_VENDOR].c_str();
}

void Machine::InitArchInfo() {
  utsname name;
  if (uname(&name) == -1) { // indicates error when -1 is returned.
    sysinfo_[CPU_ARCH] = "";
    return;
  }

  sysinfo_[CPU_ARCH] = std::string(name.machine);
}

void Machine::InitProcInfo() {
  FILE* fp = fopen(kCPUInfoFile, "r");
  if (fp == NULL)
    return;

  char line[1001] = { 0 };
  cpu_count_ = 0;
  std::string key, value;

  // get the processor count
  while (fgets(line, sizeof(line) - 1, fp)) {
    if (!SplitString(line, ":", &key, &value))
      continue;

    key = TrimString(key);
    value = TrimString(value);
    
    if (key == "processor") {
      cpu_count_ ++;
      continue;
    }

    if (cpu_count_ > 1) continue;

    for (size_t i = 0; i < arraysize(kKeysInMachineInfo); ++i) {
      if (key == kKeysInMachineInfo[i]) {
        sysinfo_[i] = value;
        break;
      }
    }
  }

  fclose(fp);
}

} // namespace framework
} // namespace ggadget
