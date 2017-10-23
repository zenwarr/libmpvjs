#include "helpers.h"

using namespace v8;
using namespace std;

void throw_js(Isolate *i, const char *msg) {
  i->ThrowException(Exception::Error(String::NewFromUtf8(i, msg)));
}

std::string string_to_cc(const Local<Value> &str) {
  String::Utf8Value v(str);
  if (*v == nullptr) {
    return std::string();
  } else {
    return std::string(*v, static_cast<unsigned long>(v.length()));
  }
}

Local<Function> get_method(Isolate *i, shared_ptr<Persistent<Object>> obj, const char *method_name) {
  return Local<Function>::Cast(obj->Get(i)->Get(String::NewFromUtf8(i, method_name)));
}

Local<String> make_string(Isolate *i, const char *text) {
  return String::NewFromUtf8(i, text, NewStringType::kNormal).ToLocalChecked();
}

void js_name_for_mpv(string &name) {
  for (auto cit = name.begin(); cit != name.end(); ++cit) {
    if (*cit == '_') {
      *cit = '-';
    }
  }
}

void js_name_for_mpv(char *name) {
  if (!name) {
    return;
  }

  for (; *name != 0; ++name) {
    if (*name == '_') {
      *name = '-';
    }
  }
}

void mpv_name_for_js(string &name) {
  for (auto cit = name.begin(); cit != name.end(); ++cit) {
    if (*cit == '-') {
      *cit = '_';
    }
  }
}
