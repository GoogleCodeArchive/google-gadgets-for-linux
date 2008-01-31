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

#include "unittest/gunit.h"
#include "ggadget/element_factory.h"
#include "mocked_element.h"
#include "mocked_view_host.h"

class ElementFactoryTest : public testing::Test {
 protected:
  virtual void SetUp() {
  }

  virtual void TearDown() {
  }
};

TEST_F(ElementFactoryTest, TestRegister) {
  ggadget::internal::ElementFactoryImpl impl;
  ASSERT_TRUE(impl.RegisterElementClass("muffin", Muffin::CreateInstance));
  ASSERT_FALSE(impl.RegisterElementClass("muffin", Muffin::CreateInstance));
  ASSERT_TRUE(impl.RegisterElementClass("pie", Pie::CreateInstance));
  ASSERT_FALSE(impl.RegisterElementClass("pie", Pie::CreateInstance));
}

TEST_F(ElementFactoryTest, TestCreate) {
  ggadget::ElementFactory factory;
  MockedViewHost vh(&factory);
  factory.RegisterElementClass("muffin", Muffin::CreateInstance);
  factory.RegisterElementClass("pie", Pie::CreateInstance);

  ggadget::BasicElement *e1 = factory.CreateElement("muffin",
                                                    NULL,
                                                    vh.GetViewInternal(),
                                                    NULL);
  ASSERT_TRUE(e1 != NULL);
  ASSERT_STREQ("muffin", e1->GetTagName().c_str());

  ggadget::BasicElement *e2 = factory.CreateElement("pie",
                                                    e1,
                                                    vh.GetViewInternal(),
                                                    NULL);
  ASSERT_TRUE(e2 != NULL);
  ASSERT_STREQ("pie", e2->GetTagName().c_str());

  ggadget::BasicElement *e3 = factory.CreateElement("bread",
                                                    e2,
                                                    vh.GetViewInternal(),
                                                    NULL);
  ASSERT_TRUE(e3 == NULL);
  delete ggadget::down_cast<Muffin *>(e1);
  delete ggadget::down_cast<Pie *>(e2);
}

int main(int argc, char *argv[]) {
  testing::ParseGUnitFlags(&argc, argv);
  return RUN_ALL_TESTS();
}