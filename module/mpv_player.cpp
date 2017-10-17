#include <mpv/client.h>
#include <mpv/opengl_cb.h>
#include <GL/gl.h>
#include <cstdio>
#include <functional>
#include <sstream>
#include <map>
#include <uv.h>
#include <locale>
#include <memory>
#include "mpv_player.h"
#include "helpers.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

using namespace v8;
using namespace std;

/*************************************************************************************
 * MpvPlayerImpl
 *************************************************************************************/

#define MKI(I) Integer::New(_isolate, I)
#define MKN(N) Number::New(_isolate, N)

typedef map<GLuint, shared_ptr<Persistent<Value>>> ObjectStore;

class MpvPlayerImpl {
public:
  MpvPlayerImpl(Isolate *isolate,
                shared_ptr<Persistent<Object>> canvas,
                shared_ptr<Persistent<Object>> renderingContext
  ) : _isolate(isolate), _canvas(canvas), _renderingContext(renderingContext) {
    _singleton = this;
  }

  ~MpvPlayerImpl() {
    if (_canvas) {
      _canvas->Reset();
    }

    // unint mpv
    if (_mpv) {
      mpv_detach_destroy(_mpv);
    }
    if (_mpv_gl) {
      mpv_opengl_cb_set_update_callback(_mpv_gl, nullptr, nullptr);
      mpv_opengl_cb_uninit_gl(_mpv_gl);
    }

    _singleton = nullptr;
  }

  /** GL functions implementation **/

  const GLubyte *glGetString(GLenum name) {
    DEBUG("glGetString: %d\n", name);

    // check if we have already cached this property
    auto props_iter = gl_props.find(name);
    if (props_iter != gl_props.end()) {
      return reinterpret_cast<const GLubyte*>(props_iter->second.c_str());
    }

    string result;
    switch (name) {
      case GL_VERSION:
        // fake opengl version as mpv fails to understand the real one
        result = "OpenGL ES 2.0 Chromium";
        break;

      case GL_SHADING_LANGUAGE_VERSION:
        result = "OpenGL ES GLSL ES 1.0 Chromium";
        break;

      case GL_EXTENSIONS:
        result = "GL_ARB_framebuffer_object";
        break;

      case GL_RENDERER:
        result = "Software Rasterizer";
        break;

      default:
        result = string_to_cc(callMethod("getParameter", MKN(name)));
    }

    DEBUG("Resulting glGetString value: %s\n", result.c_str());

    if (!result.empty()) {
      gl_props[name] = result;
      return (const GLubyte*)gl_props[name].c_str();
    } else {
      return nullptr;
    }
  }

  void glActiveTexture(GLenum texture) {
    DEBUG("glActiveTexture\n");

    callMethod("activeTexture", MKI(texture));
  }

  GLuint glCreateProgram() {
    DEBUG("glCreateProgram\n");

    return storeObject(_programs, callMethod("createProgram"));
  }

  void glDeleteProgram(GLuint program_id) {
    DEBUG("glDeleteProgram\n");

    auto prog_iter = _programs.find(program_id);
    if (prog_iter == _programs.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    callMethod("deleteProgram", prog_iter->second->Get(_isolate));
  }

  void glGetProgramInfoLog(GLuint program_id, GLsizei max_length, GLsizei *length, GLchar *info_log) {
    DEBUG("glGetProgramInfoLog\n");

    getObjectInfoLog("getProgramInfoLog", _programs, program_id, max_length, length, info_log);
  }

  void glGetProgramiv(GLuint program_id, GLenum pname, GLint *params) {
    DEBUG("glGetProgramiv\n");

    getObjectiv("getProgramParameter", _programs, program_id, pname, params);
  }

  void glUseProgram(GLuint program_id) {
    DEBUG("glUseProgram\n");

    auto prog_iter = _programs.find(program_id);
    if (prog_iter == _programs.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    callMethod("useProgram", prog_iter->second->Get(_isolate));
  }

  void glLinkProgram(GLuint program_id) {
    DEBUG("glLinkProgram\n");

    auto prog_iter = _programs.find(program_id);
    if (prog_iter == _programs.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    callMethod("linkProgram", prog_iter->second->Get(_isolate));
  }

  GLuint glCreateShader(GLenum shader_type) {
    DEBUG("glCreateShader\n");

    return storeObject(_shaders, callMethod("createShader", Number::New(_isolate, shader_type)));
  }

  void glDeleteShader(GLuint shader_id) {
    DEBUG("glDeleteShader\n");

    auto sh_iter = _shaders.find(shader_id);
    if (sh_iter == _shaders.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    callMethod("deleteShader", sh_iter->second->Get(_isolate));
  }

  void glAttachShader(GLuint program_id, GLuint shader_id) {
    DEBUG("glAttachShader\n");

    auto prog_iter = _programs.find(program_id);
    if (prog_iter == _programs.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    auto sh_iter = _shaders.find(shader_id);
    if (sh_iter == _shaders.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    callMethod("attachShader", { prog_iter->second->Get(_isolate), sh_iter->second->Get(_isolate) });
  }

  void glCompileShader(GLuint shader_id) {
    DEBUG("glCompileShader\n");

    auto sh_iter = _shaders.find(shader_id);
    if (sh_iter == _shaders.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    callMethod("compileShader", sh_iter->second->Get(_isolate));
  }

  void glShaderSource(GLuint shader_id, GLsizei count, const GLchar **string, const GLint *length) {
    DEBUG("glShaderSource\n");

    auto sh_iter = _shaders.find(shader_id);
    if (sh_iter == _shaders.end() || count < 0) {
      // set GL_INVALID_VALUE
      return;
    }

    if (count == 0) {
      return;
    }

    if (count > 1) {
      DEBUG("Multiple GLSL shader files are not supported\n");
      return;
    }

    Local<String> shader_source;
    if (length) {
      shader_source = String::NewFromUtf8(_isolate, string[0], String::kNormalString, length[0]);
    } else {
      shader_source = String::NewFromUtf8(_isolate, string[0]);
    }

    callMethod("shaderSource", { sh_iter->second->Get(_isolate), shader_source });
  }

  void glBindAttribLocation(GLuint program_id, GLuint index, const GLchar *name) {
    DEBUG("glBindAttribLocation\n");

    auto prog_iter = _programs.find(program_id);
    if (prog_iter == _programs.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    callMethod("bindAttribLocation", { prog_iter->second->Get(_isolate), MKI(index),
                                       String::NewFromUtf8(_isolate, name) });
  }

  void glBindBuffer(GLenum target, GLuint buffer) {
    DEBUG("glBindBuffer\n");
  
    auto buffer_iter = _buffers.find(buffer);
    if (buffer_iter == _buffers.end()) {
      // set GL_INVALID_VALUE
      return;
    }
    
    callMethod("bindBuffer", { MKI(target), buffer_iter->second->Get(_isolate) });
  }

  void glBindTexture(GLenum target, GLuint texture) {
    DEBUG("glBindTexture\n");
    
    auto texture_iter = _textures.find(texture);
    if (texture_iter == _textures.end()) {
      // set GL_INVALID_VALUE
      return;
    }
    
    callMethod("bindTexture", { MKI(target), texture_iter->second->Get(_isolate) });
  }

  void glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAplha, GLenum dstAplha) {
    DEBUG("glBlendFuncSeparate\n");

    callMethod("blendFuncSeparate", { MKI(srcRGB), MKI(dstRGB), MKI(srcAplha), MKI(dstAplha) });
  }

  void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage) {
    DEBUG("glBufferData\n");
    
    if (size < 0) {
      // set GL_INVALID_VALUE
      return;
    }
    
    if (size == 0) {
      return;
    }
    
    callMethod("bufferData", { MKI(target), Integer::NewFromUnsigned(_isolate, static_cast<uint32_t>(size)),
                               ArrayBuffer::New(_isolate, (void*)data, static_cast<size_t>(size)),
                               MKI(usage), MKI(0) });
  }

  void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data) {
    DEBUG("glBufferSubData\n");
    
    if (size < 0) {
      // set GL_INVALID_VALUE
      return;
    }
    
    if (size == 0) {
      return;
    }
    
    callMethod("bufferSubData", { MKI(target), Integer::NewFromUnsigned(_isolate, static_cast<uint32_t>(size)),
                                  ArrayBuffer::New(_isolate, (void*)data, static_cast<size_t>(size)),
                                  MKI(0) });
  }

  void glClear(GLbitfield mask) {
    DEBUG("glClear\n");

    callMethod("clear", MKI(mask));
  }

  void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) {
    DEBUG("glClearColor\n");

    callMethod("clearColor", { MKN(red), MKN(green), MKN(blue), MKN(alpha) });
  }

  void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
    DEBUG("glDeleteBuffers\n");
    
    if (!buffers || n == 0) {
      return;
    }
    
    if (n < 0) {
      // set GL_INVALID_VALUE
      return;
    }
    
    for (int j = 0; j < n; ++j) {
      auto buffer_iter = _buffers.find(buffers[j]);
      if (buffer_iter == _buffers.end()) {
        continue;
      }
      
      callMethod("deleteBuffer", buffer_iter->second->Get(_isolate));
      if (glGetError() == GL_NO_ERROR) {
        _buffers.erase(buffer_iter);
      }
    }
  }

  void glDeleteTextures(GLsizei n, const GLuint *textures) {
    DEBUG("glDeleteTextures\n");
    
    if (!textures || n == 0) {
      return;
    }
    
    if (n < 0) {
      // set GL_INVALID_VALUE
      return;
    }
    
    for (int j = 0; j < n; ++j) {
      auto texture_iter = _textures.find(textures[j]);
      if (texture_iter == _textures.end()) {
        continue;
      }
      
      callMethod("deleteTexture", texture_iter->second->Get(_isolate));
      if (glGetError() == GL_NO_ERROR) {
        _textures.erase(texture_iter);
      }
    }
  }

  void glEnable(GLenum cap) {
    DEBUG("glEnable\n");

    callMethod("enable", MKI(cap));
  }

  void glDisable(GLenum cap) {
    DEBUG("glDisable\n");

    callMethod("disable", MKI(cap));
  }

  void glDisableVertexAttribArray(GLuint index) {
    DEBUG("glDisableVertexAttribArray\n");

    callMethod("disableVertexAttribArray", MKI(index));
  }

  void glEnableVertexAttribArray(GLuint index) {
    DEBUG("glEnableVertexAttribArray\n");

    callMethod("enableVertexAttribArray", MKI(index));
  }

  void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    DEBUG("glDrawArrays\n");

    callMethod("drawArrays", { MKI(mode), MKI(first), MKI(count) });
  }

  void glFinish() {
    DEBUG("glFinish\n");

    callMethod("finish");
  }

  void glFlush() {
    DEBUG("glFlush\n");

    callMethod("flush");
  }

  void glGenBuffers(GLsizei n, GLuint *buffers) {
    DEBUG("glGenBuffers\n");
    
    if (!buffers || n == 0) {
      return;
    }
    
    if (n < 0) {
      // set GL_INVALID_VALUE
      return;
    }
    
    for (int j = 0; j < n; ++j) {
      auto r = callMethod("createBuffer");
      buffers[j] = r.IsEmpty() ? 0 : storeObject(_buffers, r);
    }
  }

  void glGenTextures(GLsizei n, GLuint *textures) {
    DEBUG("glGenTextures\n");
    
    if (!textures || n == 0) {
      return;
    }
    
    if (n < 0) {
      // set GL_INVALID_VALUE
      return;
    }
    
    for (int j = 0; j < n; ++j) {
      auto r = callMethod("createTexture");
      textures[j] = r.IsEmpty() ? 0 : storeObject(_textures, r);
    }
  }

  GLint glGetAttribLocation(GLuint program_id, const GLchar *name) {
    DEBUG("glGetAttribLocation\n");

    auto prog_iter = _programs.find(program_id);
    if (prog_iter == _programs.end()) {
      // set GL_INVALID_OPERATION
      return -1;
    }

    auto r = callMethod("getAttribLocation", { prog_iter->second->Get(_isolate),
                                               String::NewFromUtf8(_isolate, name) });
    return r.IsEmpty() ? -1 : static_cast<GLint>(r.As<Integer>()->IntegerValue());
  }

  GLenum glGetError() {
    DEBUG("glGetError\n");

    auto result = callMethod("getError").As<Integer>();

    DEBUG("glError result: %ld\n", result->IntegerValue());

    return static_cast<GLenum>(result->IntegerValue());
  }

  void glGetIntegerv(GLenum pname, GLint *params) {
    DEBUG("glGetIntegerv\n");

    if (!params) {
      return;
    }

    auto r = callMethod("getParameter", MKI(pname)).As<Integer>();
    if (!r.IsEmpty()) {
      *params = static_cast<GLint>(r->IntegerValue());
    }
  }

  void glGetShaderInfoLog(GLuint shader_id, GLsizei max_length, GLsizei *length, GLchar *info_log) {
    DEBUG("glGetShaderInfoLog\n");

    getObjectInfoLog("getShaderInfoLog", _shaders, shader_id, max_length, length, info_log);
  }

  void glGetShaderiv(GLuint shader_id, GLenum pname, GLint *params) {
    DEBUG("glGetShaderiv\n");

    getObjectiv("getShaderParameter", _shaders, shader_id, pname, params);
  }

  GLint glGetUniformLocation(GLuint program_id, const GLchar *name) {
    DEBUG("glGetUniformLocation\n");

    auto prog_iter = _programs.find(program_id);
    if (prog_iter == _programs.end()) {
      // set GL_INVALID_VALUE
      return -1;
    }

    auto r = callMethod("getUniformLocation", { prog_iter->second->Get(_isolate),
                                                String::NewFromUtf8(_isolate, name) });
    return r.IsEmpty() ? -1 : storeObject(_uniforms, r);
  }

  void glPixelStorei(GLenum pname, GLint param) {
    DEBUG("glPixelStorei\n");

    callMethod("pixelStorei", { MKI(pname), MKI(param) });
  }

  void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *data) {
    DEBUG("glReadPixels\n");

    if (!data) {
      return;
    }

    Local<ArrayBuffer> shared_buf;
    Local<Value> buf_view;

    switch (type) {
      case GL_UNSIGNED_BYTE:
        shared_buf = ArrayBuffer::New(_isolate, data, width * height * sizeof(GLubyte));
        buf_view = Uint8Array::New(shared_buf, 0, static_cast<size_t>(width * height));
        break;

      case GL_UNSIGNED_SHORT:
      case GL_UNSIGNED_SHORT_5_6_5:
      case GL_UNSIGNED_SHORT_4_4_4_4:
      case GL_UNSIGNED_SHORT_5_5_5_1:
        shared_buf = ArrayBuffer::New(_isolate, data, width * height * sizeof(GLushort));
        buf_view = Uint16Array::New(shared_buf, 0, static_cast<size_t>(width * height));
        break;

      case GL_FLOAT:
        shared_buf = ArrayBuffer::New(_isolate, data, width * height * sizeof(GLfloat));
        buf_view = Float32Array::New(shared_buf, 0, static_cast<size_t>(width * height));
        break;

      default:
        DEBUG("glReadPixels: unsupported data format %d", type);
        return;
    }

    callMethod("readPixels", { MKI(x), MKI(y), MKI(width), MKI(height), MKI(format), MKI(type), buf_view });
  }

  void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    DEBUG("glScissor\n");

    callMethod("scissor", { MKI(x), MKI(y), MKI(width), MKI(height) });
  }

  void glTexImage2D(GLenum target, GLint level, GLint internal_format, GLsizei width,
                    GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *data) {
    DEBUG("glTexImage2D\n");

    if (target != GL_TEXTURE_2D) {
      DEBUG("glTexImage2D called with unsupported target parameter: %d\n", type);
      return;
    }

    GLint bound_buf = 0;
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &bound_buf);
    if (bound_buf >= 1) {
      // If a non-zero named buffer object is bound to the GL_PIXEL_UNPACK_BUFFER target
      // while a texture image is specified, data is treated as a byte offset into the buffer object's data store.
      DEBUG("glTextImage2D called with unsupported offset parameter\n");
      return;
    }

    if (!data) {
      // data may be a null pointer. In this case, texture memory is allocated to accommodate
      // a texture of width width and height height.
      callMethod("texImage2D", { MKI(target), MKI(target), MKI(internal_format), MKI(width),
                                 MKI(height), MKI(border), MKI(format), MKI(type) });
    } else {
      auto bufs = getTexBuffers(type, width, height, data);

      DEBUG("calling webgl method\n");
      callMethod("texImage2D", { MKI(target), MKI(target), MKI(internal_format), MKI(width),
                                 MKI(height), MKI(border), MKI(format), MKI(type), bufs.second });
    }
  }

  void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    DEBUG("glTexParameteri\n");

    callMethod("texParameteri", { MKI(target), MKI(pname), MKI(param) });
  }

  void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                       GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) {
    DEBUG("glTexSubImage2D\n");

    auto bufs = getTexBuffers(type, width, height, pixels);

    callMethod("texSubImage2D", { MKI(target), MKI(level), MKI(xoffset), MKI(yoffset),
                                  MKI(width), MKI(height), MKI(format), MKI(type), bufs.second });
  }

  void glUniform1f(GLint location, GLfloat v0) {
    DEBUG("glUniform1f\n");

    callLocationMethod("uniform1f", location, { MKN(v0) });
  }

  void glUniform2f(GLint location, GLfloat v0, GLfloat v1) {
    DEBUG("glUniform2f\n");

    callLocationMethod("uniform2f", location, { MKN(v0), MKN(v1) });
  }

  void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
    DEBUG("glUniform3f\n");

    callLocationMethod("uniform3d", location, { MKN(v0), MKN(v1), MKN(v2) });
  }

  void glUniform1i(GLint location, GLint v0) {
    DEBUG("glUniform1i\n");

    callLocationMethod("uniform1i", location, { MKN(v0) });
  }

  void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    DEBUG("glUniformMatrix2fv\n");

    uniformMatrix(location, count, transpose, value);
  }

  void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    DEBUG("glUniformMatrix3fv\n");

    uniformMatrix(location, count, transpose, value);
  }

  void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                             GLsizei stride, const GLvoid *pointer) {
    DEBUG("glVertexAttribPointer\n");

    auto low_pointer = static_cast<uint32_t>(reinterpret_cast<uint64_t>(pointer));

    callMethod("vertexAttribPointer", { MKI(index), MKI(size), MKI(type),
                                        Boolean::New(_isolate, normalized), MKI(stride),
                                        Integer::NewFromUnsigned(_isolate, low_pointer) });
  }

  void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    DEBUG("glViewport\n");

    callMethod("viewport", { MKI(x), MKI(y), MKI(width), MKI(height) });
  }

  void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    DEBUG("glBindFramebuffer\n");

    auto fb_iter = _framebuffers.find(framebuffer);
    if (fb_iter == _framebuffers.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    callMethod("bindFramebuffer", { MKI(target), fb_iter->second->Get(_isolate) });
  }

  void glGenFramebuffers(GLsizei n, GLuint *ids) {
    DEBUG("glGenFramebuffers\n");

    if (!ids || n == 0) {
      return;
    }

    if (n < 0) {
      // set GL_INVALID_VALUE
      return;
    }

    for (int j = 0; j < n; ++j) {
      auto r = callMethod("createFramebuffer");
      ids[j] = r.IsEmpty() ? 0 : storeObject(_framebuffers, r);
    }
  }

  void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) {
    DEBUG("glDeleteFramebuffers\n");

    if (!framebuffers || n == 0) {
      return;
    }

    if (n < 0) {
      // set GL_INVALID_VALUE
      return;
    }

    for (int j = 0; j < n; ++j) {
      auto buffer_iter = _framebuffers.find(framebuffers[j]);
      if (buffer_iter == _framebuffers.end()) {
        continue;
      }

      callMethod("deleteBuffer", buffer_iter->second->Get(_isolate));
      if (glGetError() == GL_NO_ERROR) {
        _framebuffers.erase(buffer_iter);
      }
    }
  }

  GLenum glCheckFramebufferStatus(GLenum target) {
    DEBUG("glCheckFramebufferStatus\n");

    return static_cast<GLenum>(callMethod("checkFramebufferStatus", MKI(target))->IntegerValue());
  }

  void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    DEBUG("glFramebufferTexture2D\n");

    auto tex_iter = _textures.find(texture);
    if (tex_iter == _textures.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    callMethod("framebufferTexture2D", { MKI(target), MKI(attachment), MKI(textarget),
                                         tex_iter->second->Get(_isolate), MKI(level) });
  }

  void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params) {
    DEBUG("glGetFramebufferAttachmentParameteriv\n");

    if (!params) {
      return;
    }

    auto r = callMethod("getFramebufferAttachmentParameter", { MKI(target), MKI(pname), MKI(pname) }).As<Integer>();
    if (!r.IsEmpty()) {
      *params = static_cast<GLint>(r->IntegerValue());
    }
  }

  /** Helper functions **/

  inline Local<Object> localContext()const { return _renderingContext->Get(_isolate); }

  inline Local<Function> localMethod(const string &method_name) {
    auto m = method(method_name);
    return m ? m->Get(_isolate) : Local<Function>();
  }

  shared_ptr<Persistent<Function>> method(const string &method_name) {
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
    auto pers = make_shared<Persistent<Function>>(_isolate, method);
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

  template<class T>
  T storeObject(map<T, shared_ptr<Persistent<Value>>> &store, const Local<Value> &value) {
    T object_id = newId();
    store[object_id] = make_shared<Persistent<Value>>(_isolate, value);
    return object_id;
  }

  pair<Local<ArrayBuffer>, Local<Value>> getTexBuffers(GLenum type, GLsizei width,
                                                             GLsizei height, const GLvoid *data) {
    Local<ArrayBuffer> shared_buf;
    Local<Value> buf_view;

    DEBUG("calling getTexBuffers: type = %d, width = %d, height = %d, data = %p\n", type, width, height, data);

    switch (type) {
      case GL_UNSIGNED_BYTE:
        DEBUG("creating a shared array buffer\n");
        shared_buf = ArrayBuffer::New(_isolate, const_cast<void*>(data), width * height * sizeof(GLubyte));
        DEBUG("creating a buffer view\n");
        buf_view = Uint8Array::New(shared_buf, 0, static_cast<size_t>(width * height));
        DEBUG("done!\n");
        break;

      case GL_UNSIGNED_SHORT:
      case GL_UNSIGNED_SHORT_5_6_5:
      case GL_UNSIGNED_SHORT_4_4_4_4:
      case GL_UNSIGNED_SHORT_5_5_5_1:
      case GL_HALF_FLOAT:
        shared_buf = ArrayBuffer::New(_isolate, const_cast<void*>(data), width * height * sizeof(GLushort));
        buf_view = Uint16Array::New(shared_buf, 0, static_cast<size_t>(width * height));
        break;

      case GL_UNSIGNED_INT:
      case GL_UNSIGNED_INT_24_8:
        shared_buf = ArrayBuffer::New(_isolate, const_cast<void*>(data), width * height * sizeof(GLuint));
        buf_view = Uint32Array::New(shared_buf, 0, static_cast<size_t>(width * height));
        break;

      case GL_FLOAT:
        shared_buf = ArrayBuffer::New(_isolate, const_cast<void*>(data), width * height * sizeof(GLfloat));
        buf_view = Float32Array::New(shared_buf, 0, static_cast<size_t>(width * height));
        break;

      default:
        DEBUG("glTextImage2D: unsupported data format %d", type);
    }

    return { shared_buf, buf_view };
  }

  void getObjectiv(const char *webgl_method, const ObjectStore &store, GLuint object_id,
                   GLenum pname, GLint *params) {
    if (!params) {
      return;
    }

    auto obj_iter = store.find(object_id);
    if (obj_iter == store.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    auto result = callMethod(webgl_method, { obj_iter->second->Get(_isolate), MKI(pname) } );
    if (result->IsNumber() || result->IsNumberObject()) {
      auto maybe_int = result->IntegerValue(_isolate->GetCurrentContext());
      if (maybe_int.IsJust()) {
        *params = static_cast<GLint>(maybe_int.ToChecked());
      }
    } else {
      DEBUG("getProgramParameter with pname = %d: returned value is not an integer\n", pname);
    }
  }

  void getObjectInfoLog(const char *webgl_method, const ObjectStore &store, GLuint object_id,
                        GLsizei max_length, GLsizei *length, GLchar *info_log) {
    if (!info_log || max_length == 0) {
      return;
    }

    if (max_length < 0) {
      // set GL_INVALID_VALUE
      return;
    }

    auto obj_iter = store.find(object_id);
    if (obj_iter == store.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    auto result = callMethod(webgl_method, obj_iter->second->Get(_isolate))->ToString();
    int written = result->WriteUtf8(info_log, max_length - 1, nullptr, String::NO_NULL_TERMINATION);
    info_log[written + 1] = 0;
    if (length) {
      *length = written + 1;
    }
  }

  void callLocationMethod(const string &webgl_method, GLint location_id, const vector<Local<Value>> &args) {
    auto uni_iter = _uniforms.find(location_id);
    if (uni_iter == _uniforms.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    vector<Local<Value>> m_args = args;
    m_args.insert(m_args.begin(), uni_iter->second->Get(_isolate));
    callMethod(webgl_method, m_args);
  }

  void uniformMatrix(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    auto loc_iter = _uniforms.find(location);
    if (loc_iter == _uniforms.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    auto shared_buf = ArrayBuffer::New(_isolate, static_cast<void*>(const_cast<GLfloat*>(value)), count * sizeof(GLfloat));
    auto buf_view = Float32Array::New(shared_buf, 0, static_cast<size_t>(count));

    callMethod("uniformMatrix2fv", { loc_iter->second->Get(_isolate), Boolean::New(_isolate, transpose), buf_view });
  }

  static MpvPlayerImpl *singleton() {
    return _singleton;
  }

  mpv_opengl_cb_context *gl()const { return _mpv_gl; }
  mpv_handle *mpv()const { return _mpv; }

  /** Data members **/

  static MpvPlayerImpl *_singleton;
  Isolate *_isolate;
  shared_ptr<Persistent<Object>> _canvas = nullptr;
  shared_ptr<Persistent<Object>> _renderingContext = nullptr;
  mpv_handle *_mpv = nullptr;
  mpv_opengl_cb_context *_mpv_gl = nullptr;
  map<int, string> gl_props;
  map<string, shared_ptr<Persistent<Function>>> _webgl_methods;
  ObjectStore _programs;
  ObjectStore _shaders;
  ObjectStore _buffers;
  ObjectStore _textures;
  ObjectStore _framebuffers;
  map<GLint, shared_ptr<Persistent<Value>>> _uniforms;
  GLuint _last_id = 0;
  
  GLuint newId() { return ++_last_id; }
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
    DEBUG("mpv_opengl_cb_draw: drawing a frame...");
    mpv_opengl_cb_draw(MpvPlayerImpl::singleton()->gl(), 0, 300, 200);
  }
}

/**
 * This function is called by mpv itself when it has something to draw.
 * It can be called from any thread, so we should ask libuv to call the corresponding callback registered with uv_async_init.
 */
void mpv_update_callback(void *ctx) {
  DEBUG("mpv reported it has some new frames!\n");
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
        auto log_msg = reinterpret_cast<mpv_event_log_message*>(event->data);
        DEBUG("mpv log: %s", log_msg->text);
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
                     shared_ptr<Persistent<Object>> canvas,
                     shared_ptr<Persistent<Object>> renderingContext) : ObjectWrap() {
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
    auto canvas = shared_ptr<Persistent<Object>>(new Persistent<Object>(isolate, arg->ToObject()));

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
    auto webglContext = shared_ptr<Persistent<Object>>(new Persistent<Object>(isolate, getContextResult->ToObject()));

    auto playerObject = new MpvPlayer(isolate, canvas, webglContext);
    playerObject->Wrap(args.This());
    args.GetReturnValue().Set(args.This());
  } else {
    // our constructor function is called as a function, without new
    isolate->ThrowException(String::NewFromUtf8(isolate, "MpvPlayer constructor should be called with new"));
  }

  avcodec_register_all();

  // enumerate codecs supported by libav
  DEBUG("Enumerating ffmpeg codecs...\n");
  AVCodec *cur_codec = nullptr;
  do {
    cur_codec = av_codec_next(cur_codec);
    if (cur_codec) {
      DEBUG("Codec: %s\n", cur_codec->name);
    }
  } while (cur_codec);
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

  mpv_set_option_string(self->d->_mpv, "opengl-hwdec-interop", "auto");
  mpv_set_option_string(self->d->_mpv, "vo", "opengl-cb");
  mpv_set_option_string(self->d->_mpv, "hwdec", "no");
  mpv_set_option_string(self->d->_mpv, "sub-auto", "no");
  mpv_set_option_string(self->d->_mpv, "log-file", "~/electron-mpv.log");

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

  mpv_opengl_cb_set_update_callback(self->d->_mpv_gl, mpv_update_callback, nullptr);
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
    if (!args[j]->IsString()) {
      throw_js(i, "MpvPlayer::command: invalid argument types, expected to be strings");
      return;
    }
    mpv_args_c[j] = string_to_cc(args[j]);
    mpv_args[j] = mpv_args_c[j].c_str();
  }
  mpv_args[args.Length()] = nullptr;

  int mpv_result = mpv_command(self->d->_mpv, mpv_args);
  if (mpv_result != MPV_ERROR_SUCCESS) {
    throw_js(i, mpv_error_string(mpv_result));
  }
}
