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
#include <algorithm>
#include "elements.h"
#include "basic_element.h"
#include "element_factory_interface.h"
#include "graphics_interface.h"
#include "logger.h"
#include "math_utils.h"
#include "scriptable_helper.h"
#include "view.h"
#include "xml_utils.h"
#include "event.h"

namespace ggadget {

class Elements::Impl {
 public:
  Impl(ElementFactoryInterface *factory, BasicElement *owner, View *view)
      : factory_(factory), owner_(owner), view_(view),
        width_(.0), height_(.0),
        canvas_(NULL),
        count_changed_(true),
        has_popup_(false),
        scrollable_(false) {
    ASSERT(factory);
    ASSERT(view);
  }

  ~Impl() {
    RemoveAllElements();
    if (canvas_) {
      canvas_->Destroy();
      canvas_ = NULL;
    }
  }

  int GetCount() {
    return children_.size();
  }

  ElementInterface *AppendElement(const char *tag_name, const char *name) {
    BasicElement *e = down_cast<BasicElement *>(
        factory_->CreateElement(tag_name, owner_, view_, name));
    if (e == NULL)
      return NULL;
    if (view_->OnElementAdd(e)) {
      children_.push_back(e);
      count_changed_ = true;
    } else {
      delete e;
      e = NULL;
    }
    return e;
  }

  ElementInterface *InsertElement(const char *tag_name,
                                  const ElementInterface *before,
                                  const char *name) {
    BasicElement *e = down_cast<BasicElement *>(
        factory_->CreateElement(tag_name, owner_, view_, name));
    if (e == NULL)
      return NULL;
    if (view_->OnElementAdd(e)) {
      Children::iterator ite = std::find(children_.begin(), children_.end(),
                                         before);
      children_.insert(ite, e);
      count_changed_ = true;
    } else {
      delete e;
      e = NULL;
    }
    return e;
  }

  bool RemoveElement(ElementInterface *element) {
    Children::iterator ite = std::find(children_.begin(), children_.end(),
                                       down_cast<BasicElement *>(element));
    if (ite == children_.end())
      return false;
    view_->OnElementRemove(*ite);
    delete *ite;
    children_.erase(ite);
    count_changed_ = true;
    return true;
  }

  void RemoveAllElements() {
    for (Children::iterator ite = children_.begin();
         ite != children_.end(); ++ite) {
      view_->OnElementRemove(*ite);
      delete *ite;
    }
    Children v;
    children_.swap(v);
    count_changed_ = true;
  }

  ElementInterface *GetItem(const Variant &index_or_name) {
    switch (index_or_name.type()) {
      case Variant::TYPE_INT64:
        return GetItemByIndex(VariantValue<int>()(index_or_name));
      case Variant::TYPE_STRING:
        return GetItemByName(VariantValue<const char *>()(index_or_name));
      default:
        return NULL;
    }
  }

  ElementInterface *GetItemByIndex(int index) {
    if (index >= 0 && index < static_cast<int>(children_.size()))
      return children_[index];
    return NULL;
  }

  ElementInterface *GetItemByName(const char *name) {
    return GetItemByIndex(GetIndexByName(name));
  }

  Variant GetItemByNameVariant(const char *name) {
    ElementInterface *result = GetItemByName(name);
    return result ? Variant(result) : Variant();
  }

  int GetIndexByName(const char *name) {
    if (name == NULL || strlen(name) == 0)
      return -1;
    for (Children::const_iterator ite = children_.begin();
         ite != children_.end(); ++ite) {
      if (GadgetStrCmp((*ite)->GetName().c_str(), name) == 0)
        return ite - children_.begin();
    }
    return -1;
  }

  void MapChildPositionEvent(const PositionEvent &org_event,
                             BasicElement *child,
                             PositionEvent *new_event) {
    double child_x, child_y;
    if (owner_) {
      owner_->SelfCoordToChildCoord(child, org_event.GetX(), org_event.GetY(),
                                    &child_x, &child_y);
    } else {
      ParentCoordToChildCoord(org_event.GetX(), org_event.GetY(),
                              child->GetPixelX(), child->GetPixelY(),
                              child->GetPixelPinX(), child->GetPixelPinY(),
                              DegreesToRadians(child->GetRotation()),
                              &child_x, &child_y);
    }

    new_event->SetX(child_x);
    new_event->SetY(child_y);
  }

  EventResult OnMouseEvent(const MouseEvent &event,
                           BasicElement **fired_element,
                           BasicElement **in_element) {
    // The following event types are processed directly in the view.
    ASSERT(event.GetType() != Event::EVENT_MOUSE_OVER &&
           event.GetType() != Event::EVENT_MOUSE_OUT);

    *in_element = NULL;
    *fired_element = NULL;
    MouseEvent new_event(event);
    // Iterate in reverse since higher elements are listed last.
    for (Children::reverse_iterator ite = children_.rbegin();
         ite != children_.rend(); ++ite) {
      if (!(*ite)->IsVisible())
        continue;

      MapChildPositionEvent(event, *ite, &new_event);
      if ((*ite)->IsPointIn(new_event.GetX(), new_event.GetY())) {
        BasicElement *child = (*ite);
        BasicElement *descendant_in_element = NULL;
        ScopedDeathDetector death_detector(view_, &child);
        ScopedDeathDetector death_detector1(view_, &descendant_in_element);

        EventResult result = child->OnMouseEvent(new_event, false,
                                                 fired_element,
                                                 &descendant_in_element);
        // The child has been removed by some event handler, can't continue.
        if (!child)
          return result;
        if (!*in_element)
          *in_element = descendant_in_element ? descendant_in_element : child;
        if (*fired_element)
          return result;
      }
    }
    return EVENT_RESULT_UNHANDLED;
  }

  EventResult OnDragEvent(const DragEvent &event,
                          BasicElement **fired_element) {
    // Only the following event type is dispatched along the element tree.
    ASSERT(event.GetType() == Event::EVENT_DRAG_MOTION);

    *fired_element = NULL;
    DragEvent new_event(event);
    // Iterate in reverse since higher elements are listed last.
    for (Children::reverse_iterator ite = children_.rbegin();
         ite != children_.rend(); ++ite) {
      BasicElement *child = (*ite);
      if (!child->IsVisible())
        continue;

      MapChildPositionEvent(event, child, &new_event);
      if (child->IsPointIn(new_event.GetX(), new_event.GetY())) {
        ScopedDeathDetector death_detector(view_, &child);
        EventResult result = (*ite)->OnDragEvent(new_event, false,
                                                 fired_element);
        // The child has been removed by some event handler, can't continue.
        if (!child || *fired_element)
          return result;
      }
    }
    return EVENT_RESULT_UNHANDLED;
  }

  // Update the maximum children extent.
  void UpdateChildExtent(BasicElement *child,
                         double *extent_width, double *extent_height) {
    double x = child->GetPixelX();
    double y = child->GetPixelY();
    double pin_x = child->GetPixelPinX();
    double pin_y = child->GetPixelPinY();
    double width = child->GetPixelWidth();
    double height = child->GetPixelHeight();
    // Estimate the biggest possible extent with low cost.
    double est_maximum_extent = std::max(pin_x, width - pin_x) +
                                std::max(pin_y, height - pin_y);
    double child_extent_width = x + est_maximum_extent;
    double child_extent_height = y + est_maximum_extent;
    // Calculate the actual extent only if the estimated value is bigger than
    // current extent.
    if (child_extent_width > *extent_width ||
        child_extent_height > *extent_height) {
      GetChildExtentInParent(x, y, pin_x, pin_y, width, height,
                             DegreesToRadians(child->GetRotation()),
                             &child_extent_width, &child_extent_height);
      if (child_extent_width > *extent_width)
        *extent_width = child_extent_width;
      if (child_extent_height > *extent_height)
        *extent_height = child_extent_height;
    }
  }

  void Layout() {
    int child_count = children_.size();
    for (int i = 0; i < child_count; i++) {
      children_[i]->Layout();
    }

    if (scrollable_) {
      // If scrollable, the canvas size is the max extent of the children.
      bool need_update_extents = false;
      for (int i = 0; i < child_count; i++) {
        if (children_[i]->IsPositionChanged() ||
            children_[i]->IsSizeChanged()) {
          need_update_extents = true;
          break;
        }
      }

      if (need_update_extents || !canvas_) {
        width_ = height_ = 0;
        for (int i = 0; i < child_count; i++) {
          UpdateChildExtent(children_[i], &width_, &height_);
        }
      }
    } else if (owner_) {
      // If not scrollable, the canvas size is the same as the parent.
      width_ = static_cast<size_t>(ceil(owner_->GetPixelWidth()));
      height_ = static_cast<size_t>(ceil(owner_->GetPixelHeight()));
    } else {
      width_ = view_->GetWidth();
      height_ = view_->GetHeight();
    }
  }

  const CanvasInterface *Draw(bool *changed) {
    *changed = count_changed_;
    count_changed_ = false;

    if (children_.empty())
      return NULL;

    size_t new_canvas_width = static_cast<size_t>(ceil(width_));
    size_t new_canvas_height = static_cast<size_t>(ceil(height_));
    if (!canvas_ || new_canvas_width != canvas_->GetWidth() ||
        new_canvas_height != canvas_->GetHeight()) {
      *changed = true;
      if (canvas_) {
        canvas_->Destroy();
        canvas_ = NULL;
      }

      if (new_canvas_width == 0 || new_canvas_height == 0)
        return NULL;

      const GraphicsInterface *gfx = view_->GetGraphics();
      canvas_ = gfx->NewCanvas(new_canvas_width, new_canvas_height);
      if (!canvas_) {
        DLOG("Error: unable to create canvas.");
        return NULL;
      }
    }

    // Draw children into temp array.
    int child_count = children_.size();
    const CanvasInterface **children_canvas =
        new const CanvasInterface*[child_count];

    BasicElement *popup = view_->GetPopupElement();
    for (int i = 0; i < child_count; i++) {
      BasicElement *element = children_[i];
      bool has_popup = (element == popup);
      if (has_popup) {
        children_canvas[i] = NULL; // Skip the popup element.
      } else {
        bool child_changed = false;
        children_canvas[i] = element->Draw(&child_changed);
        if (element->IsPositionChanged()) {
          element->ClearPositionChanged();
          child_changed = true;
        }
        *changed = *changed || child_changed;
      }

      if (has_popup != has_popup_) {
        *changed = true;
        has_popup_ = has_popup;
      }
    }

    if (*changed) {
      canvas_->ClearCanvas();
      canvas_->IntersectRectClipRegion(0., 0., width_, height_);
      for (int i = 0; i < child_count; i++) {
        if (children_canvas[i]) {
          canvas_->PushState();

          BasicElement *element = children_[i];
          if (element->GetRotation() == .0) {
            canvas_->TranslateCoordinates(
                element->GetPixelX() - element->GetPixelPinX(),
                element->GetPixelY() - element->GetPixelPinY());
          } else {
            canvas_->TranslateCoordinates(element->GetPixelX(),
                                          element->GetPixelY());
            canvas_->RotateCoordinates(
                DegreesToRadians(element->GetRotation()));
            canvas_->TranslateCoordinates(-element->GetPixelPinX(),
                                          -element->GetPixelPinY());
          }

          const CanvasInterface *mask = element->GetMaskCanvas();
          if (mask) {
            canvas_->DrawCanvasWithMask(.0, .0, children_canvas[i],
                                        .0, .0, mask);
          } else {
            canvas_->DrawCanvas(.0, .0, children_canvas[i]);
          }

          canvas_->PopState();
        }
      }

      if (view_->GetDebugMode() > 0) {
        // Draw bounding box for debug.
        double w = canvas_->GetWidth();
        double h = canvas_->GetHeight();
        canvas_->DrawLine(0, 0, 0, h, 1, Color(0, 0, 0));
        canvas_->DrawLine(0, 0, w, 0, 1, Color(0, 0, 0));
        canvas_->DrawLine(w, h, 0, h, 1, Color(0, 0, 0));
        canvas_->DrawLine(w, h, w, 0, 1, Color(0, 0, 0));
        canvas_->DrawLine(0, 0, w, h, 1, Color(0, 0, 0));
        canvas_->DrawLine(w, 0, 0, h, 1, Color(0, 0, 0));
      }
    }

    delete[] children_canvas;
    children_canvas = NULL;
    return canvas_;
  }

  void SetScrollable(bool scrollable) {
    scrollable_ = scrollable;
  }

  void MarkRedraw() {
    Children::iterator it = children_.begin();
    for (; it != children_.end(); ++it)
      (*it)->MarkRedraw();
  }

  ElementFactoryInterface *factory_;
  BasicElement *owner_;
  View *view_;
  typedef std::vector<BasicElement *> Children;
  Children children_;
  double width_;
  double height_;
  CanvasInterface *canvas_;
  bool count_changed_;
  bool has_popup_;
  bool scrollable_;
};

Elements::Elements(ElementFactoryInterface *factory,
                   BasicElement *owner, View *view)
    : impl_(new Impl(factory, owner, view)) {
  RegisterProperty("count", NewSlot(impl_, &Impl::GetCount), NULL);
  RegisterMethod("item", NewSlot(impl_, &Impl::GetItem));
  // Register the "default" method, allowing this object be called directly
  // as a function.
  RegisterMethod("", NewSlot(impl_, &Impl::GetItem));
  // Disable the following for now, because they are not in the public
  // API document.
  // SetArrayHandler(NewSlot(impl_, &Impl::GetItemByIndex), NULL);
  // SetDynamicPropertyHandler(NewSlot(impl_, &Impl::GetItemByNameVariant),
  //                           NULL);
}

Elements::~Elements() {
  delete impl_;
}

int Elements::GetCount() const {
  ASSERT(impl_);
  return impl_->GetCount();
}

ElementInterface *Elements::GetItemByIndex(int child) {
  ASSERT(impl_);
  return impl_->GetItemByIndex(child);
}

ElementInterface *Elements::GetItemByName(const char *child) {
  ASSERT(impl_);
  return impl_->GetItemByName(child);
}

const ElementInterface *Elements::GetItemByIndex(int child) const {
  ASSERT(impl_);
  return impl_->GetItemByIndex(child);
}

const ElementInterface *Elements::GetItemByName(const char *child) const {
  ASSERT(impl_);
  return impl_->GetItemByName(child);
}

ElementInterface *Elements::AppendElement(const char *tag_name,
                                          const char *name) {
  ASSERT(impl_);
  return impl_->AppendElement(tag_name, name);
}

ElementInterface *Elements::InsertElement(const char *tag_name,
                                          const ElementInterface *before,
                                          const char *name) {
  ASSERT(impl_);
  return impl_->InsertElement(tag_name, before, name);
}

ElementInterface *Elements::AppendElementFromXML(const char *xml) {
  return ::ggadget::AppendElementFromXML(impl_->view_, this, xml);
}

ElementInterface *Elements::InsertElementFromXML(
    const char *xml, const ElementInterface *before) {
  return ::ggadget::InsertElementFromXML(impl_->view_, this, xml, before);
}

bool Elements::RemoveElement(ElementInterface *element) {
  return impl_->RemoveElement(element);
}

void Elements::RemoveAllElements() {
  impl_->RemoveAllElements();
}

void Elements::Layout() {
  impl_->Layout();
}

const CanvasInterface *Elements::Draw(bool *changed) {
  return impl_->Draw(changed);
}

EventResult Elements::OnMouseEvent(const MouseEvent &event,
                                   BasicElement **fired_element,
                                   BasicElement **in_element) {
  return impl_->OnMouseEvent(event, fired_element, in_element);
}

EventResult Elements::OnDragEvent(const DragEvent &event,
                                  BasicElement **fired_element) {
  return impl_->OnDragEvent(event, fired_element);
}

void Elements::SetScrollable(bool scrollable) {
  impl_->SetScrollable(scrollable);
}

void Elements::GetChildrenExtents(double *width, double *height) {
  ASSERT(width && height);
  *width = impl_->width_;
  *height = impl_->height_;
}

void Elements::MarkRedraw() {
  impl_->MarkRedraw();
}

} // namespace ggadget
