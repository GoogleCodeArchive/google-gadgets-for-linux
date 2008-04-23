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

#ifndef GGADGET_BUTTON_ELEMENT_H__
#define GGADGET_BUTTON_ELEMENT_H__

#include <stdlib.h>
#include <ggadget/basic_element.h>

namespace ggadget {

class MouseEvent;
class TextFrame;

class ButtonElement : public BasicElement {
 public:
  DEFINE_CLASS_ID(0xb6fb01fd48134377, BasicElement);

  ButtonElement(BasicElement *parent, View *view, const char *name);
  virtual ~ButtonElement();

 protected:
  virtual void DoRegister();

 public:
  /** Lets this button use default images. */
  void UseDefaultImages();

  /** Gets and sets the file name of default button image. */
  Variant GetImage() const;
  void SetImage(const Variant &img);

  /** Gets and sets the file name of disabled button image. */
  Variant GetDisabledImage() const;
  void SetDisabledImage(const Variant &img);

  /** Gets and sets the file name of mouse over button image. */
  Variant GetOverImage() const;
  void SetOverImage(const Variant &img);

  /** Gets and sets the file name of mouse down button image. */
  Variant GetDownImage() const;
  void SetDownImage(const Variant &img);

  /** Gets the text frame containing the caption of this button. */
  TextFrame *GetTextFrame();
  const TextFrame *GetTextFrame() const;

  /**
   * Gets and sets whether the image is stretched normally or stretched only
   * the middle area.
   */
  bool IsStretchMiddle() const;
  void SetStretchMiddle(bool stretch_middle);

 public:
  static BasicElement *CreateInstance(BasicElement *parent, View *view,
                                      const char *name);

 protected:
  virtual void DoDraw(CanvasInterface *canvas);
  virtual EventResult HandleMouseEvent(const MouseEvent &event);
  virtual void GetDefaultSize(double *width, double *height) const;

 public:
  virtual bool HasOpaqueBackground() const;

 private:
  DISALLOW_EVIL_CONSTRUCTORS(ButtonElement);

  class Impl;
  Impl *impl_;
};

} // namespace ggadget

#endif // GGADGET_BUTTON_ELEMENT_H__
