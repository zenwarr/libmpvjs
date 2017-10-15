#include <mpv/client.h>
#include <mpv/opengl_cb.h>
#include <GL/gl.h>
#include <cstdio>
#include <functional>
#include <string>
#include <map>
#include <uv.h>
#include <locale>
#include <memory>
#include "mpv_player.h"
#include "helpers.h"

using namespace v8;
using namespace std;

/*************************************************************************************
 * MpvPlayerImpl
 *************************************************************************************/

const char *empty_string = "";

class MpvPlayerImpl {
public:
  MpvPlayerImpl(Isolate *isolate,
                Persistent<Object> *canvas,
                Persistent<Object> *renderingContext
  ) : _isolate(isolate), _canvas(canvas), _renderingContext(renderingContext) {
    _singleton = this;
  }
  
  ~MpvPlayerImpl() {
    if (_canvas) {
      _canvas->Reset();
      delete _canvas;
    }
    
    // unint mpv
    if (_mpv) {
      mpv_detach_destroy(_mpv);
    }
    if (_mpv_gl) {
      mpv_opengl_cb_set_update_callback(_mpv_gl, nullptr, nullptr);
      mpv_opengl_cb_uninit_gl(_mpv_gl);
    }
    
    // remove pointers to cached methods
    for (auto iter = _webgl_methods.begin(); iter != _webgl_methods.end(); ++iter) {
      delete iter->second;
    }
    
    _singleton = nullptr;
  }
  
  /** GL functions implementation **/
  
  const GLubyte *glGetString(GLenum name) {
    DEBUG("glGetString: %d\n", name);
    
    string result;
    if (name == GL_EXTENSIONS) {
      //todo: implement it
      result = "";
    } else {
      switch (name) {
        case GL_VERSION:
          // fake opengl version as mpv fails to understand the real one
          result = "OpenGL ES 2.0 Chromium";
          break;
          
        case GL_SHADING_LANGUAGE_VERSION:
          result = "OpenGL ES GLSL ES 1.0 Chromium";
          break;
          
        default: {
          result = string_to_cc(callMethod("getParameter", Number::New(_isolate, name)));
        }
      }
    }
    
    DEBUG("Resulting glGetString value: %s\n", result.c_str());
    
    if (result.size() > 0) {
      gl_props[name] = result;
      return (const GLubyte*)gl_props[name].c_str();
    } else {
      return nullptr;
    }
  }

  void glActiveTexture(GLenum texture) {
    DEBUG("glActiveTexture\n");
    
    callMethod("activeTexture", Integer::New(_isolate, texture));
  }
  
  GLuint glCreateProgram() {
    DEBUG("glCreateProgram\n");
    
    auto result = callMethod("createProgram");
    GLuint program_id = ++_last_program_id;
    _programs[program_id] = new Persistent<Value>(_isolate, result);
    return program_id;
  }
  
  void glDeleteProgram(GLuint program_id) {
    DEBUG("glDeleteProgram\n");
    
    auto prog_iter = _programs.find(program_id);
    if (prog_iter == _programs.end()) {
      return;
    }
    
    callMethod("deleteProgram", prog_iter->second->Get(_isolate));
  }
  
  void glGetProgramInfoLog(GLuint program_id, GLsizei max_length, GLsizei *length, GLchar *info_log) {
    DEBUG("glGetProgramInfoLog\n");
    
    if (!info_log) {
      return;
    }
    
    auto prog_iter = _programs.find(program_id);
    if (prog_iter == _programs.end()) {
      return;
    }
    
    auto result = callMethod("getProgramInfoLog", prog_iter->second->Get(_isolate))->ToString();
    int written = result->WriteUtf8((GLchar*)info_log, max_length - 1, nullptr, String::NO_NULL_TERMINATION);
    info_log[written + 1] = 0;
    if (length) {
      *length = written + 1;
    }
  }
  
  void glGetProgramiv(GLuint program_id, GLenum pname, GLint *params) {
    DEBUG("glGetProgramiv");
    
    if (!params) {
      return;
    }
    
    auto prog_iter = _programs.find(program_id);
    if (prog_iter == _programs.end()) {
      return;
    }
    
    auto result = callMethod("getProgramParameter", { prog_iter->second->Get(_isolate), Integer::New(_isolate, pname) } );
    if (result->IsBoolean() || result->IsBooleanObject()) {
      auto maybe_bool = result->BooleanValue(_isolate->GetCurrentContext());
      if (maybe_bool.IsJust()) {
        *params = static_cast<GLint>(maybe_bool.ToChecked());
      }
    } else {
      auto maybe_int = result->IntegerValue(_isolate->GetCurrentContext());
      if (maybe_int.IsJust()) {
        *params = static_cast<GLint>(maybe_int.ToChecked());
      }
    }
  }
  
  void glUseProgram(GLuint program_id) {
    DEBUG("glUseProgram");
    
    auto prog_iter = _programs.find(program_id);
    if (prog_iter == _programs.end()) {
      return;
    }
    
    callMethod("useProgram", prog_iter->second->Get(_isolate));
  }
  
  void glLinkProgram(GLuint program_id) {
    DEBUG("glLinkProgram");
    
    auto prog_iter = _programs.find(program_id);
    if (prog_iter == _programs.end()) {
      return;
    }
    
    callMethod("linkProgram", prog_iter->second->Get(_isolate));
  }
  
  GLuint glCreateShader(GLenum shader_type) {
    DEBUG("glCreateShader");
    
    auto result = callMethod("createShader", Number::New(_isolate, shader_type));
    GLuint shader_id = ++_last_shader_id;
    _shaders[shader_id] = new Persistent<Value>(_isolate, result);
    return shader_id;
  }
  
  void glDeleteShader(GLuint shader_id) {
    DEBUG("glDeleteShader");
    
    auto sh_iter = _shaders.find(shader_id);
    if (sh_iter == _shaders.end()) {
      return;
    }
    
    callMethod("deleteShader", sh_iter->second->Get(_isolate));
  }
  
  void glAttachShader(GLuint program_id, GLuint shader_id) {
    DEBUG("glAttachShader");
    
    auto prog_iter = _programs.find(program_id);
    if (prog_iter == _programs.end()) {
      return;
    }
    
    auto sh_iter = _shaders.find(shader_id);
    if (sh_iter == _shaders.end()) {
      return;
    }
    
    callMethod("attachShader", { prog_iter->second->Get(_isolate), sh_iter->second->Get(_isolate) });
  }
  
  void glCompileShader(GLuint shader_id) {
    DEBUG("glCompileShader");
    
    auto sh_iter = _shaders.find(shader_id);
    if (sh_iter == _shaders.end()) {
      return;
    }
    
    callMethod("compileShader", sh_iter->second->Get(_isolate));
  }
  
  void glShaderSource(GLuint shader_id, GLsizei count, const GLchar **string, const GLint *length) {
    
  }
  
  void glBindAttribLocation(GLuint program_di, GLuint index, const GLchar *name) {
    
  }
  
  void glBindBuffer(GLenum target, GLuint buffer) {
    
  }
  
  void glBindTexture(GLenum target, GLuint texture) {
    
  }
  
  void glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAplha, GLenum dstAplha) {
    
  }
  
  void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage) {
    
  }
  
  void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data) {
    
  }
  
  void glClear(GLbitfield mask) {
    
  }
  
  void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) {
    
  }
  
  void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
    
  }
  
  void glDeleteTextures(GLsizei n, const GLuint *textures) {
    
  }
  
  void glEnable(GLenum cap) {
    
  }
  
  void glDisable(GLenum cap) {
    
  }
  
  void glDisableVertexAttribArray(GLuint index) {
    
  }
  
  void glEnableVertexAttribArray(GLuint index) {
    
  }
  
  void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    
  }
  
  void glFinish() {
    
  }
  
  void glFlush() {
    
  }
  
  void glGenBuffers(GLsizei n, GLuint *buffers) {
    
  }
  
  void glGenTextures(GLsizei n, GLuint *textures) {
    
  }
  
  GLint glGetAttribLocation(GLuint program_id, const GLchar *name) {
    return -1;
  }
  
  GLenum glGetError() {
    return -1;
  }
  
  void glGetIntegerv(GLenum pname, GLint *params) {
    
  }
  
  void glGetShaderInfoLog(GLuint shader_id, GLsizei max_length, GLsizei *length, GLchar *info_log) {
    
  }
  
  void glGetShaderiv(GLuint shader_id, GLenum pname, GLint *params) {
    
  }
  
  GLint glGetUniformLocation(GLuint program, const GLchar *name) {
    return -1;
  }
  
  void glPixelStorei(GLenum pname, GLint param) {
    
  }
  
  void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *data) {
    
  }
  
  void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    
  }
  
  void glTexImage2D(GLenum target, GLint level, GLint internal_format, GLsizei width, GLsizei height, GLint border,
                     GLenum format, GLenum type, const GLvoid *data) {
    
  }
  
  void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    
  }
  
  void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                       GLenum format, GLenum type, const GLvoid *pixels) {
    
  }
  
  void glUniform1f(GLint location, GLfloat v0) {
    
  }
  
  void glUniform2f(GLint location, GLfloat v0, GLfloat v1) {
    
  }
  
  void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
    
  }
  
  void glUniform1i(GLint location, GLint v0) {
    
  }
  
  void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    
  }
  
  void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    
  }
  
  void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,
                             const GLvoid * pointer) {
    
  }
  
  void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    
  }
  
  void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    
  }
  
  void glGenFramebuffers(GLsizei n, GLuint *ids) {
    
  }
  
  void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) {
    
  }
  
  GLenum glCheckFramebufferStatus(GLenum target) {
    return -1;
  }
  
  void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    
  }
  
  void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params) {
    
  }
  
  /** Helper functions **/
  
  inline Local<Object> localContext()const { return _renderingContext->Get(_isolate); }
  
  inline Local<Function> localMethod(const string &method_name) { 
    auto m = method(method_name);
    return m ? m->Get(_isolate) : Local<Function>();
  }
  
  Persistent<Function> *method(const string &method_name) {
    if (!_renderingContext) {
      return nullptr;
    }
    
    // check if we have cached the requested method
    auto ex_iter = _webgl_methods.find(method_name);
    if (ex_iter != _webgl_methods.end()) {
      return ex_iter->second;
    }
    
    // if not, get the method from rendering context and cache it
    auto method = get_method(_isolate, _renderingContext, method_name.c_str());
    auto pers = new Persistent<Function>(_isolate, method);
    _webgl_methods[method_name] = pers;
    return pers;
  }
  
  Local<Value> callMethod(const string &method_name, const vector<Local<Value>> &args) {
    return localMethod(method_name)->Call(localContext(), args.size(), (Local<Value>*)args.data());
  }
  
  Local<Value> callMethod(const string &method_name, const Local<Value> &arg) {
    return localMethod(method_name)->Call(localContext(), 1, (Local<Value>*)&arg);
  }
  
  Local<Value> callMethod(const string &method_name) {
    Local<Value> args[] = { };
    return localMethod(method_name)->Call(localContext(), 0, args);
  }
  
  static MpvPlayerImpl *singleton() {
    return _singleton;
  }
  
  mpv_opengl_cb_context *gl()const { return _mpv_gl; }
  mpv_handle *mpv()const { return _mpv; }
  
  /** Data members **/
  
  static MpvPlayerImpl *_singleton;
  Isolate *_isolate;
  Persistent<Object> *_canvas = nullptr;
  Persistent<Object> *_renderingContext = nullptr;
  mpv_handle *_mpv = nullptr;
  mpv_opengl_cb_context *_mpv_gl = nullptr;
  map<int, string> gl_props;
  map<string, Persistent<Function>*> _webgl_methods;
  map<GLuint, Persistent<Value>*> _programs;
  map<GLuint, Persistent<Value>*> _shaders;
  GLuint _last_program_id = 0, _last_shader_id = 0;
};

MpvPlayerImpl *MpvPlayerImpl::_singleton = nullptr;

/*************************************************************************************
 * Some helpers
 *************************************************************************************/

Persistent<Function> MpvPlayer::constructor;

static const char *MPV_PLAYER_CLASS = "MpvPlayer";

static uv_async_t async_handle, async_wakeup_handle;

/*************************************************************************************
 * MpvPlayer
 *************************************************************************************/

/**
 * This function is called by libuv when mpv signalizes it has something to draw.
 * It is always called on the main thread.
 */
void async_cb(uv_async_t *handle) {
  // we should draw some frames!
  if (MpvPlayerImpl::singleton()) {
    mpv_opengl_cb_draw(MpvPlayerImpl::singleton()->gl(), 0, 300, 200);
  }
}

/**
 * This function is called by mpv itself when it has something to draw.
 * It can be called from any thread, so we should ask libuv to call the corresponding callback registered with uv_async_init.
 */
void mpv_update_callback(void *ctx) {
  uv_async_send(&async_handle);
}

/**
 * This function is called by libuv when mpv signalizes it has unprocessed events.
 * it is always called on the main thread.
 */
void async_wakeup_cb(uv_async_t *handle) {
  if (MpvPlayerImpl::singleton()) {
    while (true) {
      mpv_event *event = mpv_wait_event(MpvPlayerImpl::singleton()->mpv(), 0);
      
      if (event->event_id == MPV_EVENT_NONE || event->event_id == MPV_EVENT_SHUTDOWN) {
        break;
      } else if (event->event_id == MPV_EVENT_LOG_MESSAGE) {
        mpv_event_log_message *log_msg = reinterpret_cast<mpv_event_log_message*>(event->data);
        DEBUG("mpv log: %s\n", log_msg->text);
      }
    }
  }
}

/**
 * This function is called by mpv itself when it has unprocessed events.
 * It can be called from any thread, so we should ask libuv to call the corresponding callback registered with uv_async_init.
 */
void mpv_wakeup_callback(void *ctx) {
  uv_async_send(&async_wakeup_handle);
}

typedef void(*FUNC_PTR)(GLenum);

#define QUOTE(arg) #arg
#define DEF_FN(FUN_NAME) { QUOTE(gl##FUN_NAME), (void*)web_gl##FUN_NAME }

void web_glActiveTexture(GLenum texture) { MpvPlayerImpl::singleton()->glActiveTexture(texture); }
const GLubyte *web_glGetString(GLenum name) { return MpvPlayerImpl::singleton()->glGetString(name); }
GLuint web_glCreateProgram() { return MpvPlayerImpl::singleton()->glCreateProgram(); }
void web_glDeleteProgram(GLuint program) { MpvPlayerImpl::singleton()->glDeleteProgram(program); }
void web_glGetProgramInfoLog(GLuint program, GLsizei max_length, GLsizei *length, GLchar *info_log) {
  MpvPlayerImpl::singleton()->glGetProgramInfoLog(program, max_length, length, info_log);
}
void web_glGetProgramiv(GLuint program, GLenum pname, GLint *params) {
  MpvPlayerImpl::singleton()->glGetProgramiv(program, pname, params);
}
void web_glLinkProgram(GLuint program) { MpvPlayerImpl::singleton()->glLinkProgram(program); }
void web_glUseProgram(GLuint program) { MpvPlayerImpl::singleton()->glUseProgram(program); }
GLuint web_glCreateShader(GLenum shader_type) { return MpvPlayerImpl::singleton()->glCreateShader(shader_type); }
void web_glDeleteShader(GLuint shader_id) { MpvPlayerImpl::singleton()->glDeleteShader(shader_id); }
void web_glAttachShader(GLuint program_id, GLuint shader_id) {
  MpvPlayerImpl::singleton()->glAttachShader(program_id, shader_id);
}
void web_glCompileShader(GLuint shader_id) { MpvPlayerImpl::singleton()->glCompileShader(shader_id); }
void web_glBindAttribLocation(GLuint program_id, GLuint index, const GLchar *name) { MpvPlayerImpl::singleton()->glBindAttribLocation(program_id, index, name); }
void web_glBindBuffer(GLenum target, GLuint buffer) { MpvPlayerImpl::singleton()->glBindBuffer(target, buffer); }
void web_glBindTexture(GLenum target, GLuint texture) { MpvPlayerImpl::singleton()->glBindTexture(target, texture); }
void web_glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAplha, GLenum dstAplha) { MpvPlayerImpl::singleton()->glBlendFuncSeparate(srcRGB, dstRGB, srcAplha, dstAplha); }
void web_glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage) { MpvPlayerImpl::singleton()->glBufferData(target, size, data, usage); }
void web_glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data) { MpvPlayerImpl::singleton()->glBufferSubData(target, offset, size, data); }
void web_glClear(GLbitfield mask) { MpvPlayerImpl::singleton()->glClear(mask); }
void web_glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) { MpvPlayerImpl::singleton()->glClearColor(red, green, blue, alpha); }
void web_glDeleteBuffers(GLsizei n, const GLuint *buffers) { MpvPlayerImpl::singleton()->glDeleteBuffers(n, buffers); }
void web_glDeleteTextures(GLsizei n, const GLuint *textures) { MpvPlayerImpl::singleton()->glDeleteTextures(n, textures); }
void web_glEnable(GLenum cap) { MpvPlayerImpl::singleton()->glEnable(cap); }
void web_glDisable(GLenum cap) { MpvPlayerImpl::singleton()->glDisable(cap); }
void web_glDisableVertexAttribArray(GLuint index) { MpvPlayerImpl::singleton()->glDisableVertexAttribArray(index); }
void web_glEnableVertexAttribArray(GLuint index) { MpvPlayerImpl::singleton()->glEnableVertexAttribArray(index); }
void web_glDrawArrays(GLenum mode, GLint first, GLsizei count) { MpvPlayerImpl::singleton()->glDrawArrays(mode, first, count); }
void web_glFinish() { MpvPlayerImpl::singleton()->glFinish(); }
void web_glFlush() { MpvPlayerImpl::singleton()->glFlush(); }
void web_glGenBuffers(GLsizei n, GLuint *buffers) { MpvPlayerImpl::singleton()->glGenBuffers(n, buffers); }
void web_glGenTextures(GLsizei n, GLuint *textures) { MpvPlayerImpl::singleton()->glGenTextures(n, textures); }
GLint web_glGetAttribLocation(GLuint program_id, const GLchar *name) { return MpvPlayerImpl::singleton()->glGetAttribLocation(program_id, name); }
GLenum web_glGetError() { return MpvPlayerImpl::singleton()->glGetError(); }
void web_glGetIntegerv(GLenum pname, GLint *params) { MpvPlayerImpl::singleton()->glGetIntegerv(pname, params); }
void web_glGetShaderInfoLog(GLuint shader_id, GLsizei max_length, GLsizei *length, GLchar *info_log) { MpvPlayerImpl::singleton()->glGetShaderInfoLog(shader_id, max_length, length, info_log); }
void web_glGetShaderiv(GLuint shader_id, GLenum pname, GLint *params) { MpvPlayerImpl::singleton()->glGetShaderiv(shader_id, pname, params); }
GLint web_glGetUniformLocation(GLuint program, const GLchar *name) { return MpvPlayerImpl::singleton()->glGetUniformLocation(program, name); }
void web_glPixelStorei(GLenum pname, GLint param) { MpvPlayerImpl::singleton()->glPixelStorei(pname, param); }
void web_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *data) { MpvPlayerImpl::singleton()->glReadPixels(x, y, width, height, format, type, data); }
void web_glShaderSource(GLuint shader_id, GLsizei count, const GLchar **str, const GLint *length) {
  MpvPlayerImpl::singleton()->glShaderSource(shader_id, count, str, length); 
}
void web_glScissor(GLint x, GLint y, GLsizei width, GLsizei height) { MpvPlayerImpl::singleton()->glScissor(x, y, width, height); }
void web_glTexImage2D(GLenum target, GLint level, GLint internal_format, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *data) { MpvPlayerImpl::singleton()->glTexImage2D(target, level, internal_format, width, height, border, format, type, data); }
void web_glTexParameteri(GLenum target, GLenum pname, GLint param) { MpvPlayerImpl::singleton()->glTexParameteri(target, pname, param); }
void web_glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) { MpvPlayerImpl::singleton()->glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels); }
void web_glUniform1f(GLint location, GLfloat v0) { MpvPlayerImpl::singleton()->glUniform1f(location, v0); }
void web_glUniform2f(GLint location, GLfloat v0, GLfloat v1) { MpvPlayerImpl::singleton()->glUniform2f(location, v0, v1); }
void web_glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) { MpvPlayerImpl::singleton()->glUniform3f(location, v0, v1, v2); }
void web_glUniform1i(GLint location, GLint v0) { MpvPlayerImpl::singleton()->glUniform1i(location, v0); }
void web_glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { MpvPlayerImpl::singleton()->glUniformMatrix2fv(location, count, transpose, value); }
void web_glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { MpvPlayerImpl::singleton()->glUniformMatrix3fv(location, count, transpose, value); }
void web_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid * pointer) { MpvPlayerImpl::singleton()->glVertexAttribPointer(index, size, type, normalized, stride, pointer); }
void web_glViewport(GLint x, GLint y, GLsizei width, GLsizei height) { MpvPlayerImpl::singleton()->glViewport(x, y, width, height); }
void web_glBindFramebuffer(GLenum target, GLuint framebuffer) { MpvPlayerImpl::singleton()->glBindFramebuffer(target, framebuffer); }
void web_glGenFramebuffers(GLsizei n, GLuint *ids) { MpvPlayerImpl::singleton()->glGenFramebuffers(n, ids); }
void web_glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) { MpvPlayerImpl::singleton()->glDeleteFramebuffers(n, framebuffers); }
GLenum web_glCheckFramebufferStatus(GLenum target) { return MpvPlayerImpl::singleton()->glCheckFramebufferStatus(target); }
void web_glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) { MpvPlayerImpl::singleton()->glFramebufferTexture2D(target, attachment, textarget, texture, level); }
void web_glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params) { MpvPlayerImpl::singleton()->glGetFramebufferAttachmentParameteriv(target, attachment, pname, params); }

static map<string, void*> gl_func_map = {
  DEF_FN(ActiveTexture),
  DEF_FN(AttachShader),
  DEF_FN(BindAttribLocation),
  DEF_FN(BindBuffer),
  DEF_FN(BindTexture),
  DEF_FN(BlendFuncSeparate),
  DEF_FN(BufferData),
  DEF_FN(BufferSubData),
  DEF_FN(Clear),
  DEF_FN(ClearColor),
  DEF_FN(CompileShader),
  DEF_FN(CreateProgram),
  DEF_FN(CreateShader),
  DEF_FN(DeleteBuffers),
  DEF_FN(DeleteProgram),
  DEF_FN(DeleteShader),
  DEF_FN(DeleteTextures),
  DEF_FN(Disable),
  DEF_FN(DisableVertexAttribArray),
  DEF_FN(DrawArrays),
  DEF_FN(Enable),
  DEF_FN(EnableVertexAttribArray),
  DEF_FN(Finish),
  DEF_FN(Flush),
  DEF_FN(GenBuffers),
  DEF_FN(GenTextures),
  DEF_FN(GetAttribLocation),
  DEF_FN(GetError),
  DEF_FN(GetIntegerv),
  DEF_FN(GetProgramInfoLog),
  DEF_FN(GetProgramiv),
  DEF_FN(GetShaderInfoLog),
  DEF_FN(GetShaderiv),
  DEF_FN(GetString),
  DEF_FN(GetUniformLocation),
  DEF_FN(LinkProgram),
  DEF_FN(PixelStorei),
  DEF_FN(ReadPixels),
  DEF_FN(Scissor),
  DEF_FN(ShaderSource),
  DEF_FN(TexImage2D),
  DEF_FN(TexParameteri),
  DEF_FN(TexSubImage2D),
  DEF_FN(Uniform1f),
  DEF_FN(Uniform2f),
  DEF_FN(Uniform3f),
  DEF_FN(Uniform1i),
  DEF_FN(UniformMatrix2fv),
  DEF_FN(UniformMatrix3fv),
  DEF_FN(UseProgram),
  DEF_FN(VertexAttribPointer),
  DEF_FN(Viewport),
  DEF_FN(BindFramebuffer),
  DEF_FN(GenFramebuffers),
  DEF_FN(DeleteFramebuffers),
  DEF_FN(CheckFramebufferStatus),
  DEF_FN(FramebufferTexture2D),
  DEF_FN(GetFramebufferAttachmentParameteriv),
};

/**
 * The function used by MPV to get addresses for opengl functions.
 */
static void *get_proc_address(void *ctx, const char *name) {
  DEBUG("Requested GL method: %s\n", name);
  
  auto iter = gl_func_map.find(string(name));
  if (iter == gl_func_map.end()) {
    fprintf(stderr, "No WebGL wrapper for a function named %s\n", name);
    return nullptr;
  }
  return iter->second;
}

MpvPlayer::MpvPlayer(Isolate *isolate,
                     Persistent<Object> *canvas,
                     Persistent<Object> *renderingContext) : ObjectWrap() {
  d = new MpvPlayerImpl(isolate, canvas, renderingContext);
}

MpvPlayer::~MpvPlayer() {
  delete d;
}

/**
 * Initialize the native module
 */
void MpvPlayer::Init(Local<Object> exports) {
  Isolate *isolate = exports->GetIsolate();

  Local<FunctionTemplate> tpl = FunctionTemplate::New(isolate, New);
  tpl->SetClassName(String::NewFromUtf8(isolate, MPV_PLAYER_CLASS));
  tpl->InstanceTemplate()->SetInternalFieldCount(1);

  NODE_SET_PROTOTYPE_METHOD(tpl, "create", Create);
  NODE_SET_PROTOTYPE_METHOD(tpl, "command", Command);

  constructor.Reset(isolate, tpl->GetFunction());
  exports->Set(String::NewFromUtf8(isolate, MPV_PLAYER_CLASS), tpl->GetFunction());
  
  uv_async_init(uv_default_loop(), &async_handle, async_cb);
  uv_async_init(uv_default_loop(), &async_wakeup_handle, async_wakeup_cb);
}

/**
 * Mapped to MpvPlayer constructor function.
 * This function should only be called with `new` operator and get a single argument.
 * This argument should be an object representing a valid DOM canvas element.
 */
 void MpvPlayer::New(const FunctionCallbackInfo<Value> &args) {
  Isolate *isolate = args.GetIsolate();

  if (args.IsConstructCall()) {
    if (args.Length() < 1) {
      isolate->ThrowException(
        Exception::TypeError(String::NewFromUtf8(isolate, "MpvPlayer: invalid number of arguments for constructor")));
      return;
    }
    
    Local<Value> arg = args[0];
    if (!arg->IsObject()) {
      isolate->ThrowException(
        Exception::TypeError(String::NewFromUtf8(isolate, "MpvPlayer: invalid argument, canvas DOM element expected")));
      return;
    }
        
    // extract object pointing to canvas
    Persistent<Object> *canvas = new Persistent<Object>(isolate, arg->ToObject());
    
    // create rendering context on our canvas
    Local<Object> fGetContext = canvas->Get(isolate)->Get(String::NewFromUtf8(isolate, "getContext"))->ToObject();
    if (fGetContext.IsEmpty() || fGetContext->IsNull() || fGetContext->IsUndefined()) {
      throw_js(isolate, "MpvPlayer::create: failed to create WebGL rendering context");
      return;
    }
    
    // and call canvas.getContext to get webgl rendering context
    Local<Value> getContextArgs[] = {
      String::NewFromUtf8(isolate, "webgl")
    };
    TryCatch tryCatch;
    Local<Value> getContextResult = Local<Function>::Cast(fGetContext)->Call(canvas->Get(isolate), 1, getContextArgs);
    
    if (tryCatch.HasCaught()) {
      tryCatch.ReThrow();
      return;
    }
    
    if (getContextResult.IsEmpty() || getContextResult->IsNull() || getContextResult->IsUndefined()) {
      throw_js(isolate, "MpvPlayer::create: failed to initialize WebGL");
      return;
    }
    auto webglContext = new Persistent<Object>(isolate, getContextResult->ToObject());
    
    Local<Function> webgl_method = get_method(isolate, webglContext, "getParameter");
    
    auto playerObject = new MpvPlayer(isolate, canvas, webglContext);
    
    playerObject->Wrap(args.This());
    args.GetReturnValue().Set(args.This());
  } else {
    // our constructor function is called as a function, without new
    isolate->ThrowException(String::NewFromUtf8(isolate, "MpvPlayer constructor should be called with new"));
  }
}

/**
 * Mapped to MpvPlayer.create function.
 * Initializes MPV player instance.
 */
void MpvPlayer::Create(const FunctionCallbackInfo<Value> &args) {
  Isolate *isolate = args.GetIsolate();
  MpvPlayer *self = ObjectWrap::Unwrap<MpvPlayer>(args.Holder());
  
  self->d->_mpv = mpv_create();
  if (!self->d->_mpv) {
    isolate->ThrowException(
      Exception::Error(String::NewFromUtf8(isolate, "MpvPlayer::create: falied to initialize mpv"))
    );
    return;
  }
  
  mpv_request_log_messages(self->d->_mpv, "debug");
  mpv_set_wakeup_callback(self->d->_mpv, (void (*)(void*))mpv_wakeup_callback, self);
  
  if (mpv_initialize(self->d->_mpv) < 0) {
    isolate->ThrowException(
      Exception::Error(String::NewFromUtf8(isolate, "MpvPlayer::create: falied to initialize mpv"))
    );
    return;
  }
  
  mpv_set_option_string(self->d->_mpv, "vo", "opengl-cb");
  mpv_set_option_string(self->d->_mpv, "hwdec", "auto");
  
  self->d->_mpv_gl = static_cast<mpv_opengl_cb_context*>(mpv_get_sub_api(self->d->_mpv, MPV_SUB_API_OPENGL_CB));
  if (!self->d->_mpv_gl) {
    isolate->ThrowException(
      Exception::Error(String::NewFromUtf8(isolate, "MpvPlayer::create: falied to initialize opengl subapi"))
    );
    return;
  }
  
  int res = mpv_opengl_cb_init_gl(self->d->_mpv_gl, nullptr, get_proc_address, self);
  if (res < 0) {
    isolate->ThrowException(
      Exception::Error(String::NewFromUtf8(isolate, "MpvPlayer::create: falied to initialize WebGL functions"))
    );
    return;
  }
}

void MpvPlayer::Command(const FunctionCallbackInfo<Value> &args) {
  Isolate *i = args.GetIsolate();
  MpvPlayer *self = ObjectWrap::Unwrap<MpvPlayer>(args.Holder());
  
  if (!self->d->_mpv) {
    throw_js(i, "MpvPlayer: calling a method on unintialized instance");
    return;
  }
  
  if (args.Length() < 1) {
    throw_js(i, "MpvPlayer::command: not enough arguments, at least one is expected");
    return;
  }
  
  const char *mpv_args[args.Length() + 1];
  string mpv_args_c[args.Length()];
  for (int j = 0; j < args.Length(); ++j) {
    if (!args[0]->IsString()) {
      throw_js(i, "MpvPlayer::command: invalid argument types, expected to be strings");
      return;
    }
    mpv_args_c[j] = string_to_cc(args[0]);
    mpv_args[j] = mpv_args_c[j].c_str();
  }
  mpv_args[args.Length()] = nullptr;
  
  int mpv_result = mpv_command(self->d->_mpv, mpv_args);
  if (mpv_result != MPV_ERROR_SUCCESS) {
    throw_js(i, mpv_error_string(mpv_result));
  }
}
