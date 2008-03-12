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

#ifndef GGADGET_ELEMENT_FACTORY_H__
#define GGADGET_ELEMENT_FACTORY_H__

#include <cstddef>
#include <map>
#include <ggadget/string_utils.h>

namespace ggadget {

class BasicElement;
class ViewInterface;
class View;

namespace internal {
class ElementFactoryImpl;
} // namespace internal

/**
 * Factory for creating an Element in the Gadget API.
 */
class ElementFactory {
 public:
  ElementFactory();
  virtual ~ElementFactory();

 public:
  /**
   * Creates an ElementInterface of the specified type.
   * @param tag_name the tag name name of the object.
   * @param parent the parent object of the newly created object.
   * @param view the top-level view object containing the current object.
   * @param name the name of the newly created element.
   * @return the pointer to the newly created object or @c NULL if failed.
   */
  BasicElement *CreateElement(const char *tag_name,
                              BasicElement *parent,
                              ViewInterface *view,
                              const char *name);

  /**
   * Used as the @c creator parameter in @c RegisterElementClass().
   */
  typedef BasicElement *(*ElementCreator)(BasicElement *parent,
                                          View *view,
                                          const char *name);

  /**
   * Registers a new subclass of ElementInterface.
   * @param tag_name the tag name name of the subclass.
   * @param creator the function pointer of the creator, which returns a new
   *     instance of an object of this tag name.
   * @return @c true if registered successfully, or @c false if the specified
   *     tag name already exists.
   */
  bool RegisterElementClass(const char *tag_name,
                            ElementCreator creator);

 private:
  internal::ElementFactoryImpl *impl_;
};

namespace internal {
// Declared here for unittest.

/**
 * Interface for creating an Element in the Gadget API.
 */
class ElementFactoryImpl {
 public:
  BasicElement *CreateElement(const char *tag_name,
                              BasicElement *parent,
                              ViewInterface *view,
                              const char *name);
  bool RegisterElementClass(const char *tag_name,
                            ElementFactory::ElementCreator creator);

  typedef std::map<const char *, ElementFactory::ElementCreator,
                   GadgetCharPtrComparator> CreatorMap;
  CreatorMap creators_;
};

} // namespace internal

} // namespace ggadget

#endif // GGADGET_ELEMENT_FACTORY_H__