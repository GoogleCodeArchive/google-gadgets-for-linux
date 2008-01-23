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

#include "scriptable_image.h"
#include "image_interface.h"

namespace ggadget {

class ScriptableImage::Impl {
 public:
  Impl(ImageInterface *image) : image_(image) {
  }

  ~Impl() {
    ::ggadget::DestroyImage(image_);
  }

  ImageInterface *image_;
};

ScriptableImage::ScriptableImage(ImageInterface *image)
    : impl_(new Impl(image)) {
  RegisterProperty("width", NewSlot(image, &ImageInterface::GetWidth), NULL);
  RegisterProperty("height", NewSlot(image, &ImageInterface::GetHeight), NULL);
}

ScriptableImage::~ScriptableImage() {
  delete impl_;
}

const ImageInterface *ScriptableImage::GetImage() const {
  return impl_->image_;
}

void ScriptableImage::DestroyImage() {
  ::ggadget::DestroyImage(impl_->image_);
  impl_->image_ = NULL;
}

} // namespace ggadget