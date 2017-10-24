#pragma once

#include <node.h>
#include <string>
#include <memory>

#define BUILD_DEBUG
//#define BUILD_GL_DEBUG

#ifdef BUILD_DEBUG
#define DEBUG(...) std::fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG(...) do { } while (false)
#endif

#ifdef BUILD_GL_DEBUG
#define GL_DEBUG(...) std::fprintf(stderr, __VA_ARGS__)
#else
#define GL_DEBUG(...) do { } while (false)
#endif

/**
 * Throws an exception with given message inside the given v8::Isolate
 */
void throw_js(v8::Isolate *i, const char *msg);

/**
 * Converts v8::String to std::string
 */
std::string string_to_cc(const v8::Local<v8::Value> &str);

v8::Local<v8::String> make_string(v8::Isolate *i, const char *text);
inline v8::Local<v8::String> make_string(v8::Isolate *i, const std::string &text) { return make_string(i, text.c_str()); }

/**
 * Gets a method with given name from v8::Object
 */
v8::Local<v8::Function> get_method(v8::Isolate *isolate, std::shared_ptr<v8::Persistent<v8::Object>> obj,
                                   const char *method_name);

void js_name_for_mpv(std::string &name);
void js_name_for_mpv(char *name);
void mpv_name_for_js(std::string &name);

template<class T, class F>
inline v8::Local<T> CastLocal(const v8::Local<F> &value) {
  return v8::Local<T>::Cast(value);
}
