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

#include <cmath>
#include <vector>
#include <algorithm>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <ggadget/scoped_ptr.h>
#include <ggadget/signals.h>
#include <ggadget/slot.h>
#include "cairo_graphics.h"
#include "cairo_canvas.h"
#include "cairo_font.h"

namespace ggadget {
namespace gtk {

const char *const kEllipsisText = "...";

static void SetPangoLayoutAttrFromTextFlags(PangoLayout *layout,
                                            int text_flags, double width) {
  PangoAttrList *attr_list = pango_attr_list_new();
  PangoAttribute *underline_attr = NULL;
  PangoAttribute *strikeout_attr = NULL;
  // Set the underline attribute
  if (text_flags & CanvasInterface::TEXT_FLAGS_UNDERLINE) {
    underline_attr = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
    // We want this attribute apply to all text.
    underline_attr->start_index = 0;
    underline_attr->end_index = 0xFFFFFFFF;
    pango_attr_list_insert(attr_list, underline_attr);
  }
  // Set the strikeout attribute.
  if (text_flags & CanvasInterface::TEXT_FLAGS_STRIKEOUT) {
    strikeout_attr = pango_attr_strikethrough_new(true);
    // We want this attribute apply to all text.
    strikeout_attr->start_index = 0;
    strikeout_attr->end_index = 0xFFFFFFFF;
    pango_attr_list_insert(attr_list, strikeout_attr);
  }
  // Set the wordwrap attribute.
  if (text_flags & CanvasInterface::TEXT_FLAGS_WORDWRAP) {
    pango_layout_set_width(layout, static_cast<int>(width) * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
  } else {
    // In pango, set width = -1 to set no wordwrap.
    pango_layout_set_width(layout, -1);
  }
  pango_layout_set_attributes(layout, attr_list);
  // This will also free underline_attr and strikeout_attr.
  pango_attr_list_unref(attr_list);
}

class CairoCanvas::Impl {
 public:
  Impl(const CairoGraphics *graphics, size_t w, size_t h, cairo_format_t fmt)
    : cr_(NULL), width_(w), height_(h), opacity_(1),
      zoom_(graphics->GetZoom()), format_(fmt) {
    cr_ = CreateContext(w, h, zoom_, fmt);

    if (!cr_)
      DLOG("Failed to create cairo context.");

    connection_ =
        graphics->ConnectOnZoom(NewSlot(this, &Impl::OnZoom));
  }

  Impl(double zoom, size_t w, size_t h, cairo_format_t fmt)
    : cr_(NULL), width_(w), height_(h), opacity_(1),
      zoom_(zoom), format_(fmt), connection_(NULL) {
    cr_ = CreateContext(w, h, zoom_, fmt);

    if (!cr_)
      DLOG("Failed to create cairo context.");
  }

  ~Impl() {
    if (cr_) cairo_destroy(cr_);
    if (connection_) connection_->Disconnect();
  }

  cairo_t *CreateContext(size_t w, size_t h, double zoom, cairo_format_t fmt) {
    // Only support ARGB32 and A8 format.
    if (fmt != CAIRO_FORMAT_ARGB32 && fmt != CAIRO_FORMAT_A8)
      return NULL;

    cairo_t *cr = NULL;

    size_t width = w;
    size_t height = h;

    if (zoom != 1) {
      width = static_cast<size_t>(round(w * zoom));
      height = static_cast<size_t>(round(h * zoom));
      if (!width) w = 1;
      if (!height) h = 1;
    }

    // create surface at native resolution after adjustment by scale
    cairo_surface_t *surface = cairo_image_surface_create(fmt, width, height);

    //DLOG("CreateContext(%zu,%zu) bytes=%zu", width, height,
    //     size_t(cairo_image_surface_get_stride(surface) * height));

    if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
      cr = cairo_create(surface);
      if (zoom != 1) {
        // divide again since x, y scale values may differ after
        // rounding from the initial multiplication
        cairo_scale(cr, width / (double)w, height / (double)h);
      }

      // Many CairoCanvas methods assume no existing path, so clear any
      // existing paths on construction.
      cairo_new_path(cr);

#if CAIRO_VERSION < CAIRO_VERSION_ENCODE(1,2,0)
      // Clear the surface in case cairo doesn't make it clear.
      // Hope that cairo >= 1.2.0 doesn't have such issue.
      cairo_operator_t op = cairo_get_operator(cr);
      cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
      cairo_paint(cr);
      cairo_set_operator(cr, op);
#endif

      // Likewise, save state first to allow ClearCanvas to clear the state.
      cairo_save(cr);
    }

    cairo_surface_destroy(surface);
    return cr;
  }

  // Recreate the cairo surface and context, copying the content from old
  // surface to the new one.
  void OnZoom(double zoom) {
    if (zoom_ == zoom) return;

    cairo_t *new_cr = CreateContext(width_, height_, zoom, format_);
    if (!new_cr) {
      DLOG("Failed to create new cairo context when changing zoom factor.");
      return;
    }

    // Copying old canvas into new one will consume lots of time.
    // Hopefully we do not need to do it if all elements can redraw themselves
    // after the zoom changed.
    // Just leave this code here for reference.
#if 0
    cairo_surface_t *old = GetSurface();

    if (old) {
      int sw = cairo_image_surface_get_width(old);
      int sh = cairo_image_surface_get_height(old);

      if (size_t(sw) == width_ && size_t(sh) == height_) {
        // no scaling needed
        cairo_set_source_surface(new_cr, old, 0, 0);
        cairo_paint(new_cr);
      } else {
        // CairoGraphics supports only uniform scaling in both X, Y,
        // but due to rounding differences, we need to compute the exact
        // scaling individually for X and Y.
        double cx = double(width_) / sw;
        double cy = double(height_) / sh;

        cairo_save(new_cr);
        cairo_scale(new_cr, cx, cy);
        cairo_set_source_surface(new_cr, old, 0, 0);
        cairo_paint(new_cr);
        cairo_restore(new_cr);
      }
    }
#endif

    if (cr_) cairo_destroy(cr_);
    cr_ = new_cr;
    zoom_ = zoom;
  }

  cairo_surface_t *GetSurface() const {
    if (cr_) {
      cairo_surface_t *s = cairo_get_target(cr_);
      cairo_surface_flush(s);
      return s;
    }
    return NULL;
  }

  bool DrawTextInternal(double x, double y, double width,
                        double height, const char *text,
                        const FontInterface *f,
                        Alignment align, VAlignment valign,
                        Trimming trimming, int text_flags) {
    if (text == NULL || f == NULL) return false;

    // If the text is blank, we need to do nothing.
    if (*text == 0) return true;

    cairo_save(cr_);
    // Restrict the output area.
    cairo_rectangle(cr_, x, y, x + width, y + height);
    cairo_clip(cr_);

    const CairoFont *font = down_cast<const CairoFont*>(f);
    PangoLayout *layout = pango_cairo_create_layout(cr_);
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_font_description(layout, font->GetFontDescription());
    SetPangoLayoutAttrFromTextFlags(layout, text_flags, width);
    // Pos is used to get glyph extents in pango.
    PangoRectangle pos;
    // real_x and real_y represent the real position of the layout.
    double real_x = x, real_y = y;

    // Set alignment. This is only effective when wordwrap is set
    // because when wordwrap is unset, the width has to be
    // -1, thus the alignment is useless.
    if (align == ALIGN_LEFT)
      pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    else if (align == ALIGN_CENTER)
      pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    else if (align == ALIGN_RIGHT)
      pango_layout_set_alignment(layout, PANGO_ALIGN_RIGHT);

    // Get the pixel extents(logical extents) of the layout.
    pango_layout_get_pixel_extents(layout, NULL, &pos);
    // Calculate number of all lines.
    int n_lines = pango_layout_get_line_count(layout);
    int line_height = pos.height / n_lines;
    // Calculate number of lines that could be displayed.
    // We should display one more line as long as there
    // are 5 pixels of blank left. This is only effective
    // when trimming exists.
    int displayed_lines = (static_cast<int>(height) - 5) / line_height + 1;
    if (displayed_lines > n_lines) displayed_lines = n_lines;

    if (trimming == TRIMMING_NONE || (pos.width <= width &&
          pango_layout_get_line_count(layout) <= displayed_lines)) {
      // When there is no trimming, we can directly show the layout.

      // Set vertical alignment.
      if (valign == VALIGN_MIDDLE)
        real_y = y + (height - pos.height) / 2;
      else if (valign == VALIGN_BOTTOM)
        real_y = y + height - pos.height;

      // When wordwrap is unset, we also have to do the horizontal alignment.
      if ((text_flags & TEXT_FLAGS_WORDWRAP) == 0) {
        if (align == ALIGN_CENTER)
          real_x = x + (width - pos.width) / 2;
        else if (align == ALIGN_RIGHT)
          real_x = x + width - pos.width;
      }

      // Show pango layout when there is no trimming.
      cairo_move_to(cr_, real_x, real_y);
      pango_cairo_show_layout(cr_, layout);

    } else {
      // We will use newtext as the content of the layout,
      // because we have to display the trimmed text.
      scoped_array<char> newtext(new char[strlen(text) + 4]);

      // Set vertical alignment.
      if (valign == VALIGN_MIDDLE)
        real_y = y + (height - line_height * displayed_lines) / 2;
      else if (valign == VALIGN_BOTTOM)
        real_y = y + height - line_height * displayed_lines;

      if (displayed_lines > 1) {
        // When there are multilines, we will show the above lines first,
        // because trimming will only occurs in the last line.
        PangoLayoutLine *line = pango_layout_get_line(layout,
                                                      displayed_lines - 2);
        int last_line_index = line->start_index + line->length;
        pango_layout_set_text(layout, text, last_line_index);
        cairo_move_to(cr_, real_x, real_y);
        pango_cairo_show_layout(cr_, layout);

        // The newtext contains the text that will be shown in the last line.
        strcpy(newtext.get(), text + last_line_index);
        real_y += line_height * (displayed_lines - 1);

      } else {
        // When there is only a single line, the newtext equals text.
        strcpy(newtext.get(), text);
      }
      // Set the newtext as the content of the layout.
      pango_layout_set_text(layout, newtext.get(), -1);

      // This record the width of the ellipsis text.
      int ellipsis_width = 0;

      if (trimming == TRIMMING_CHARACTER_ELLIPSIS) {
        // Pango has provided character-ellipsis trimming.
        // FIXME: when displaying arabic, the final layout width
        // may exceed the width we set before
        pango_layout_set_width(layout, static_cast<int>(width) * PANGO_SCALE);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

      } else if (trimming == TRIMMING_PATH_ELLIPSIS) {
        // Pango has provided path-ellipsis trimming.
        // FIXME: when displaying arabic, the final layout width
        // may exceed the width we set before
        pango_layout_set_width(layout, static_cast<int>(width) * PANGO_SCALE);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);

      } else {
        // We have to do other type of trimming ourselves, including
        // "character", "word" and "word-ellipsis".

        // We want every thing in a single line, so set no word wrap.
        pango_layout_set_width(layout, -1);
        pango_layout_get_pixel_extents(layout, NULL, &pos);
        if (trimming == TRIMMING_WORD_ELLIPSIS) {
          // Only in this condition should we calculate the ellipsis width.
          pango_layout_set_text(layout, kEllipsisText, -1);
          pango_layout_get_pixel_extents(layout, NULL, &pos);
          ellipsis_width = pos.width;
          pango_layout_set_text(layout, newtext.get(), -1);
        }

        // Figure out how many characters can be displayed.
        std::vector<int> cluster_index;
        PangoLayoutIter *it = pango_layout_get_iter(layout);
        // A cluster is the smallest linguistic unit that can be shaped.
        do {
          cluster_index.push_back(pango_layout_iter_get_index(it));
        } while (pango_layout_iter_next_cluster(it));
        cluster_index.push_back(strlen(newtext.get()));
        std::sort(cluster_index.begin(), cluster_index.end());

        std::vector<int>::iterator cluster_it = cluster_index.begin();
        for (; cluster_it != cluster_index.end(); ++cluster_it) {
          pango_layout_set_text(layout, newtext.get(), *cluster_it);
          pango_layout_get_pixel_extents(layout, NULL, &pos);
          if (pos.width > width - ellipsis_width)
            break;
        }

        // Use conceal_index to represent the first byte that won't be displayed.
        assert(cluster_it != cluster_index.begin());
        int conceal_index = *(--cluster_it);

        // Get the text that will finally be displayed.
        if (trimming == TRIMMING_CHARACTER) {
          // In "character", just show the characters before the index.
          pango_layout_set_text(layout, newtext.get(), conceal_index);

        } else {
          // In "word" or "word-ellipsis" trimming, we have to find out where
          // last word stops. If we can't find out a reasonable position, then
          // just do trimming as in "character".
          PangoLogAttr *log_attrs;
          int n_attrs;
          pango_layout_get_log_attrs(layout, &log_attrs, &n_attrs);
          int off = g_utf8_pointer_to_offset(newtext.get(),
                                             newtext.get() + conceal_index);
          while (off > 0 && !log_attrs[off].is_word_end &&
                 !log_attrs[off].is_word_start)
            --off;
          if (off > 0)
            conceal_index = g_utf8_offset_to_pointer(newtext.get(), off) -
                                                     newtext.get();
            newtext[conceal_index] = 0;

          // In word-ellipsis, we have to append the ellipsis manualy.
          if (trimming == TRIMMING_WORD_ELLIPSIS)
            strcpy(newtext.get() + conceal_index, kEllipsisText);

          pango_layout_set_text(layout, newtext.get(), -1);
        }

        // We also have to do the horizontal alignment.
        pango_layout_get_pixel_extents(layout, NULL, &pos);
        if (align == ALIGN_CENTER)
          real_x = x + (width - pos.width) / 2;
        else if (align == ALIGN_RIGHT)
          real_x = x + width - pos.width;
      }

      // Show the trimmed text.
      cairo_move_to(cr_, real_x, real_y);
      pango_cairo_show_layout(cr_, layout);
      pango_layout_get_pixel_extents(layout, NULL, &pos);
      PangoLayoutIter *it = pango_layout_get_iter(layout);
      do {
        pango_layout_iter_get_char_extents(it, &pos);
      } while(pango_layout_iter_next_char(it));
    }

    g_object_unref(layout);
    cairo_restore(cr_);

    return true;
  }


  cairo_t *cr_;
  size_t width_, height_;
  double opacity_;
  double zoom_;
  cairo_format_t format_;
  Connection *connection_;
  std::stack<double> opacity_stack_;
};

CairoCanvas::CairoCanvas(const CairoGraphics *graphics,
                         size_t w, size_t h, cairo_format_t fmt)
  : impl_(new Impl(graphics, w, h, fmt)) {
}

CairoCanvas::CairoCanvas(double zoom, size_t w, size_t h, cairo_format_t fmt)
  : impl_(new Impl(zoom, w, h, fmt)) {
}

CairoCanvas::~CairoCanvas() {
  delete impl_;
  impl_ = NULL;
}

void CairoCanvas::Destroy() {
  delete this;
}

bool CairoCanvas::ClearCanvas() {
  // Clear the surface.
  ASSERT(impl_->cr_);
  cairo_operator_t op = cairo_get_operator(impl_->cr_);
  cairo_set_operator(impl_->cr_, CAIRO_OPERATOR_CLEAR);
  cairo_paint(impl_->cr_);
  cairo_set_operator(impl_->cr_, op);

  // Reset state.
  cairo_reset_clip(impl_->cr_);
  impl_->opacity_ = 1.;
  impl_->opacity_stack_ = std::stack<double>();

  cairo_restore(impl_->cr_);
  cairo_save(impl_->cr_);

  return true;
}

bool CairoCanvas::PopState() {
  ASSERT(impl_->cr_);
  if (impl_->opacity_stack_.empty()) {
    return false;
  }

  impl_->opacity_ = impl_->opacity_stack_.top();
  impl_->opacity_stack_.pop();
  cairo_restore(impl_->cr_);
  return true;
}

bool CairoCanvas::PushState() {
  ASSERT(impl_->cr_);
  impl_->opacity_stack_.push(impl_->opacity_);
  cairo_save(impl_->cr_);
  return true;
}

bool CairoCanvas::MultiplyOpacity(double opacity) {
  if (opacity >= 0.0 && opacity <= 1.0) {
    impl_->opacity_ *= opacity;
    return true;
  }
  return false;
}

bool CairoCanvas::DrawLine(double x0, double y0, double x1, double y1,
                             double width, const Color &c) {
  ASSERT(impl_->cr_);
  if (width < 0.0) {
    return false;
  }

  cairo_set_line_width(impl_->cr_, width);
  cairo_set_source_rgba(impl_->cr_, c.red, c.green, c.blue, impl_->opacity_);
  cairo_move_to(impl_->cr_, x0, y0);
  cairo_line_to(impl_->cr_, x1, y1);
  cairo_stroke(impl_->cr_);

  return true;
}

void CairoCanvas::RotateCoordinates(double radians) {
  ASSERT(impl_->cr_);
  cairo_rotate(impl_->cr_, radians);
}

void CairoCanvas::TranslateCoordinates(double dx, double dy) {
  ASSERT(impl_->cr_);
  cairo_translate(impl_->cr_, dx, dy);
}

void CairoCanvas::ScaleCoordinates(double cx, double cy) {
  ASSERT(impl_->cr_);
  cairo_scale(impl_->cr_, cx, cy);
}

bool CairoCanvas::DrawFilledRect(double x, double y,
                                 double w, double h, const Color &c) {
  ASSERT(impl_->cr_);
  if (w <= 0.0 || h <= 0.0) {
    return false;
  }

  cairo_set_source_rgba(impl_->cr_, c.red, c.green, c.blue, impl_->opacity_);
  cairo_rectangle(impl_->cr_, x, y, w, h);
  cairo_fill(impl_->cr_);
  return true;
}

bool CairoCanvas::IntersectRectClipRegion(double x, double y,
                                          double w, double h) {
  if (w <= 0.0 || h <= 0.0) {
    return false;
  }

  cairo_rectangle(impl_->cr_, x, y, w, h);
  cairo_clip(impl_->cr_);
  return true;
}

bool CairoCanvas::DrawCanvas(double x, double y, const CanvasInterface *img) {
  if (!img) return false;

  const CairoCanvas *cimg = down_cast<const CairoCanvas *>(img);
  cairo_surface_t *s = cimg->GetSurface();
  int sheight = cairo_image_surface_get_height(s);
  int swidth = cairo_image_surface_get_width(s);
  size_t w = cimg->GetWidth();
  size_t h = cimg->GetHeight();
  if (size_t(sheight) == h && size_t(swidth) == w) {
    // no scaling needed
    cairo_set_source_surface(impl_->cr_, s, x, y);
    cairo_paint_with_alpha(impl_->cr_, impl_->opacity_);
  } else {
    // CairoGraphics supports only uniform scaling in both X, Y, but due to
    // rounding differences, we need to compute the exact scaling individually
    // for X and Y.
    double cx = double(w) / swidth;
    double cy = double(h) / sheight;

    cairo_save(impl_->cr_);
    cairo_scale(impl_->cr_, cx, cy);
    cairo_set_source_surface(impl_->cr_, s, x / cx, y / cy);
    cairo_paint_with_alpha(impl_->cr_, impl_->opacity_);
    cairo_restore(impl_->cr_);
  }

  return true;
}

bool CairoCanvas::DrawFilledRectWithCanvas(double x, double y,
                                           double w, double h,
                                           const CanvasInterface *img) {
  if (!img || w <= 0.0 || h <= 0.0) return false;

  const CairoCanvas *cimg = down_cast<const CairoCanvas *>(img);
  cairo_surface_t *s = cimg->GetSurface();
  cairo_save(impl_->cr_);
  cairo_rectangle(impl_->cr_, x, y, w, h);
  cairo_clip(impl_->cr_);

  int sheight = cairo_image_surface_get_height(s);
  int swidth = cairo_image_surface_get_width(s);
  size_t sw = cimg->GetWidth();
  size_t sh = cimg->GetHeight();
  if (static_cast<size_t>(sheight) != sh || static_cast<size_t>(swidth) != sw) {
    // CairoGraphics supports only uniform scaling in both X, Y, but due to
    // rounding differences, we need to compute the exact scaling individually
    // for X and Y.
    double cx = sw / swidth;
    double cy = sh / sheight;
    cairo_scale(impl_->cr_, cx, cy);
  }

  cairo_pattern_t *pattern = cairo_pattern_create_for_surface(s);
  cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
  cairo_set_source(impl_->cr_, pattern);
  cairo_paint_with_alpha(impl_->cr_, impl_->opacity_);
  cairo_pattern_destroy(pattern);
  cairo_restore(impl_->cr_);
  return true;
}

bool CairoCanvas::DrawCanvasWithMask(double x, double y,
                                    const CanvasInterface *img,
                                    double mx, double my,
                                    const CanvasInterface *mask) {
  if (!img || !mask) return false;

  const CairoCanvas *cmask =  down_cast<const CairoCanvas *>(mask);
  const CairoCanvas *cimg =  down_cast<const CairoCanvas *>(img);
  // In this implementation, only non-mask canvases may have surface dimensions
  // different from the canvas dimensions, so we only need to check img.
  cairo_surface_t *simg = cimg->GetSurface();
  cairo_surface_t *smask = cmask->GetSurface();
  int sheight = cairo_image_surface_get_height(simg);
  int swidth = cairo_image_surface_get_width(simg);
  size_t w = cimg->GetWidth();
  size_t h = cimg->GetHeight();
  if (size_t(sheight) == h && size_t(swidth) == w) {
    // no scaling needed
    cairo_set_source_surface(impl_->cr_, simg, x, y);
    cairo_mask_surface(impl_->cr_, smask, mx, my);
  } else {
    double cx = double(w) / swidth;
    double cy = double(h) / sheight;

    cairo_save(impl_->cr_);
    cairo_scale(impl_->cr_, cx, cy);
    cairo_set_source_surface(impl_->cr_, simg, x / cx, y / cy);
    cairo_scale(impl_->cr_, 1 / cx, 1 / cy);
    cairo_mask_surface(impl_->cr_, smask, mx, my);
    cairo_restore(impl_->cr_);
  }

  return true;
}

bool CairoCanvas::DrawText(double x, double y, double width, double height,
                           const char *text, const FontInterface *f,
                           const Color &c, Alignment align, VAlignment valign,
                           Trimming trimming,  int text_flags) {

  cairo_set_source_rgba(impl_->cr_, c.red, c.green, c.blue, impl_->opacity_);

  return impl_->DrawTextInternal(x, y, width, height, text, f, align,
                                 valign, trimming, text_flags);
}


bool CairoCanvas::DrawTextWithTexture(double x, double y, double width,
                                      double height, const char *text,
                                      const FontInterface *f,
                                      const CanvasInterface *texture,
                                      Alignment align, VAlignment valign,
                                      Trimming trimming, int text_flags) {
  const CairoCanvas *cimg = down_cast<const CairoCanvas *>(texture);
  cairo_surface_t *s = cimg->GetSurface();
  cairo_pattern_t *pattern = cairo_pattern_create_for_surface(s);
  if (!pattern) {
    return false;
  }
  cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);

  bool result;
  int sheight = cairo_image_surface_get_height(s);
  int swidth = cairo_image_surface_get_width(s);
  size_t sw = cimg->GetWidth();
  size_t sh = cimg->GetHeight();
  cairo_save(impl_->cr_);
  if (static_cast<size_t>(sheight) != sh || static_cast<size_t>(swidth) != sw) {
    // CairoGraphics supports only uniform scaling in both X, Y, but due to
    // rounding differences, we need to compute the exact scaling individually
    // for X and Y.
    double cx = sw / swidth;
    double cy = sh / sheight;
    cairo_scale(impl_->cr_, cx, cy);
    cairo_set_source(impl_->cr_, pattern);
    cairo_scale(impl_->cr_, 1 / cx, 1 / cy);
  } else {
    cairo_set_source(impl_->cr_, pattern);
  }

  result =  impl_->DrawTextInternal(x, y, width, height, text, f, align,
                                    valign, trimming, text_flags);
  cairo_pattern_destroy(pattern);
  cairo_restore(impl_->cr_);
  return result;
}

bool CairoCanvas::GetTextExtents(const char *text, const FontInterface *f,
                                 int text_flags, double in_width,
                                 double *width, double *height) {
  if (text == NULL || f == NULL) {
    *width = *height = 0;
    return false;
  }

  // If the text is blank, we need to do nothing.
  if (!*text) {
    *width = *height = 0;
    return true;
  }

  const CairoFont *font = down_cast<const CairoFont*>(f);
  PangoLayout *layout = pango_cairo_create_layout(impl_->cr_);
  pango_layout_set_text(layout, text, -1);
  pango_layout_set_font_description(layout, font->GetFontDescription());

  if (in_width <= 0) {
    text_flags &= ~TEXT_FLAGS_WORDWRAP;
  }
  SetPangoLayoutAttrFromTextFlags(layout, text_flags, in_width);

  // Get the pixel extents(logical extents) of the layout.
  int w, h;
  pango_layout_get_pixel_size(layout, &w, &h);
  *width = w;
  *height = h;

  g_object_unref(layout);
  return true;
}

bool CairoCanvas::GetPointValue(double x, double y,
                                Color *color, double *opacity) const {
#if CAIRO_VERSION >= CAIRO_VERSION_ENCODE(1,2,0)
#define BYTE_TO_DOUBLE(x) (static_cast<double>(x)/255.0)
  cairo_surface_t *surface = GetSurface();

  // Only support Image surface for now.
  if (!surface || cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE)
    return false;

  int width = cairo_image_surface_get_width(surface);
  int height = cairo_image_surface_get_height(surface);

  cairo_user_to_device(impl_->cr_, &x, &y);
  int xi = static_cast<int>(round(x));
  int yi = static_cast<int>(round(y));

  // The pixel is outside the canvas.
  if (xi < 0 || xi >= width || yi < 0 || yi >= height)
    return false;

  cairo_format_t format = cairo_image_surface_get_format(surface);
  unsigned char *data = cairo_image_surface_get_data(surface);
  int stride = cairo_image_surface_get_stride(surface);
  double red, green, blue, op;
  red = green = blue = op = 0;
  if (format == CAIRO_FORMAT_ARGB32 || format == CAIRO_FORMAT_RGB24) {
    uint32_t *ptr = reinterpret_cast<uint32_t *>(data + (stride * yi) + xi * 4);
    uint32_t cell = *ptr;
    red = BYTE_TO_DOUBLE((cell >> 16) & 0xFF);
    green = BYTE_TO_DOUBLE((cell >> 8) & 0xFF);
    blue = BYTE_TO_DOUBLE(cell & 0xFF);
    if (format == CAIRO_FORMAT_ARGB32) {
      op = BYTE_TO_DOUBLE((cell >> 24) & 0xFF);
      if (op != 0) {
        red /= op;
        green /= op;
        blue /= op;
        if (red > 1) red = 1;
        if (green > 1) green = 1;
        if (blue > 1) blue = 1;
      }
    } else {
      op = 1.0;
    }
  } else if (format == CAIRO_FORMAT_A8) {
    unsigned char *ptr = data + (stride * yi) + xi;
    op = BYTE_TO_DOUBLE(*ptr);
  } else if (format == CAIRO_FORMAT_A1) {
    uint32_t *ptr =
        reinterpret_cast<uint32_t *>(data + (stride * yi) + (xi / 32) * 4);
    uint32_t cell = *ptr;
#ifdef GGL_BIG_ENDIAN
    op = static_cast<double>((cell >> (31 - (xi % 32))) & 1);
#else
    op = static_cast<double>((cell >> (xi % 32)) & 1);
#endif
  } else {
    return false;
  }
  if (color) {
    color->red = red;
    color->green = green;
    color->blue = blue;
  }
  if (opacity) *opacity = op;
  return true;
#undef BYTE_TO_DOUBLE
#else
  return false;
#endif
}

cairo_surface_t *CairoCanvas::GetSurface() const {
  return impl_->GetSurface();
}

size_t CairoCanvas::GetWidth() const {
  return impl_->width_;
}

size_t CairoCanvas::GetHeight() const {
  return impl_->height_;
}

cairo_t *CairoCanvas::GetContext() const {
  return impl_->cr_;
}

void CairoCanvas::MultiplyColor(const Color &color) {
  cairo_surface_t *surface = impl_->GetSurface();

  // Color multiply can only be done on ARGB32 surface.
  if (color != Color::kWhite && surface &&
      impl_->format_ == CAIRO_FORMAT_ARGB32) {
    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    int stride = cairo_image_surface_get_stride(surface);
    unsigned char *bytes = cairo_image_surface_get_data(surface);
    uint32_t rm = static_cast<uint32_t>(color.red * 256);
    uint32_t gm = static_cast<uint32_t>(color.green * 256);
    uint32_t bm = static_cast<uint32_t>(color.blue * 256);

    // We are sure that the surface format is CAIRO_FORMAT_ARGB32.
    for (int x = 0; x < width; x++) {
      for (int y = 0; y < height; y++) {
        uint32_t *ptr =
            reinterpret_cast<uint32_t *>(bytes + (stride * y) + x * 4);
        uint32_t cell = *ptr;
        cell = (cell & 0xFF000000) |
               ((((cell & 0xFF) * bm) >> 8) & 0xFF) |
               ((((cell & 0xFF00) * gm) >> 8) & 0xFF00) |
               ((((cell & 0xFF0000) * rm) >> 8) & 0xFF0000);
        *ptr = cell;
      }
    }
  }
}

bool CairoCanvas::IsValid() const {
  return impl_->cr_ != NULL;
}

} // namespace gtk
} // namespace ggadget
