#include "helpers.h"

using namespace v8;

void throw_js(Isolate *i, const char *msg) {
  i->ThrowException(Exception::Error(String::NewFromUtf8(i, msg)));
}

std::string string_to_cc(const Local<Value> &str) {
  String::Utf8Value v(str);
  if (*v == nullptr) {
    return std::string();
  } else {
    return std::string(*v, v.length());
  }
}

Local<Function> get_method(Isolate *i, Persistent<Object> *obj, const char *method_name) {
  return Local<Function>::Cast(obj->Get(i)->Get(String::NewFromUtf8(i, method_name)));
}
