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

#include <QtScript/QScriptValue>
#include <QtCore/QStringList>
#include <ggadget/scoped_ptr.h>
#include <ggadget/string_utils.h>
#include "js_function_slot.h"
#include "js_script_context.h"
#include "converter.h"

#if 1
#undef DLOG
#define DLOG  true ? (void) 0 : LOG
#endif

namespace ggadget {
namespace qt {

extern JSScriptContext *GetEngineContext(QScriptEngine *engine);

static int i = 0;
JSFunctionSlot::JSFunctionSlot(const Slot* prototype,
                               QScriptEngine *engine,
                               const char *script,
                               const char *file_name,
                               int lineno)
    : prototype_(prototype),
      engine_(engine),
      code_(true),
      script_(QString::fromUtf8(script)),
      file_name_(file_name ? file_name: ""),
      line_no_(lineno) {
  i++;
  LOG("New JSFunctionSlot:#%d", i);
}

JSFunctionSlot::JSFunctionSlot(const Slot* prototype,
                               QScriptEngine *engine, QScriptValue function)
    : prototype_(prototype),
      engine_(engine),
      code_(false),
      function_(function) {
  i++;
  LOG("New JSFunctionSlot:#%d", i);
}

JSFunctionSlot::~JSFunctionSlot() {
  DLOG("JSFunctionSlot deleted");
  i--;
  LOG("Delete JSFunctionSlot:#%d", i);
}

ResultVariant JSFunctionSlot::Call(ScriptableInterface *object,
                                   int argc, const Variant argv[]) const {
  ScopedLogContext log_context(GetEngineContext(engine_));
  Variant return_value(GetReturnType());
  QScriptValue qval;
  if (code_) {
    DLOG("JSFunctionSlot::Call: %s", script_.toUtf8().data());
    qval = engine_->evaluate(script_, file_name_.c_str(), line_no_);
  } else {
    DLOG("JSFunctionSlot::Call function");
    QScriptValue fun(function_);
    QScriptValueList args;

    for (int i = 0; i < argc; i++) {
      QScriptValue qval;
      if (!ConvertNativeToJS(engine_, argv[i], &qval)) {
        LOGE("Failed to convert native parameter %d to QScriptValue", i);
        engine_->currentContext()->throwError(
            QString("Failed to convert native parameter %1 to QScriptValue")
            .arg(i));
        return ResultVariant(return_value);
      }
      args << qval;
    }

    qval = fun.call(QScriptValue(), args);
  }
  if (engine_->hasUncaughtException()) {
    QStringList bt = engine_->uncaughtExceptionBacktrace();
    LOGE("Backtrace:");
    for (int i = 0; i < bt.size(); i++) {
      LOGE("\t%s", bt[i].toStdString().c_str());
    }
  }

  if (!ConvertJSToNative(engine_, return_value, qval, &return_value)) {
    LOGE("Failed to convert returned value to native");
    engine_->currentContext()->throwError(
        "Failed to convert returned value to native");
  }
  return ResultVariant(return_value);
}

} // namespace qt
} // namespace ggadget
