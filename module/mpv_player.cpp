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
#include <cstring>
#include "mpv_player.h"
#include "helpers.h"
#include "mpv_node.h"

using namespace v8;
using namespace std;
using namespace node;

struct PlayerOptions {
  map<mpv_event_id, shared_ptr<Persistent<Function>>> event_handlers;
  string log_level;
};

map<string, mpv_event_id> handler_events = {
  { "onLog", MPV_EVENT_LOG_MESSAGE },
  { "onFileStart", MPV_EVENT_START_FILE },
  { "onFileEnd", MPV_EVENT_END_FILE },
  { "onFileLoaded", MPV_EVENT_FILE_LOADED },
  { "onIdle", MPV_EVENT_IDLE },
  { "onVideoReconfig", MPV_EVENT_VIDEO_RECONFIG },
  { "onAudioReconfig", MPV_EVENT_AUDIO_RECONFIG },
  { "onSeek", MPV_EVENT_SEEK },
  { "onPlaybackRestart", MPV_EVENT_PLAYBACK_RESTART },
  { "onQueueOverflow", MPV_EVENT_QUEUE_OVERFLOW }
};

template<class T>
struct PersistentDisposer {
  void operator()(Persistent<T> *p)const {
    DEBUG("disposing a persistent handle...\n");
    p->Reset();
    delete p;
  }
};

template<class T>
shared_ptr<Persistent<T>> pers_ptr(Persistent<T> *p) {
  return shared_ptr<Persistent<T>>(p, PersistentDisposer<T>());
}

/*************************************************************************************
 * MpvPlayerImpl
 *************************************************************************************/

#define MKI(I) Integer::New(_isolate, I)
#define MKN(N) Number::New(_isolate, N)

typedef map<GLuint, shared_ptr<Persistent<Value>>> ObjectStore;

class MPImpl {
public:
  MPImpl(Isolate *isolate,
         const shared_ptr<Persistent<Object>> &canvas,
         const shared_ptr<Persistent<Object>> &renderingContext,
         const PlayerOptions &opts
  ) : _isolate(isolate), _canvas(canvas), _renderingContext(renderingContext), _options(opts) {
    _singleton = this;
  }

  ~MPImpl() {
    // persistent handles being hold by us are automatically disposed by PersistentDisposer
    // we cannot un-initialize mpv here because accessing js is already forbidden now.
    // dispose method should be called explicitly by js code to uninit

    if (_mpv || _mpv_gl) {
      DEBUG("MpvPlayer object is removed by GC, but dispose function has not been called\n");
    }
  }

  void dispose() {
    DEBUG("removing MPImpl instance...\n");

    if (_mpv) {
      mpv_terminate_destroy(_mpv);
    }
    if (_mpv_gl) {
      mpv_opengl_cb_set_update_callback(_mpv_gl, nullptr, nullptr);
      mpv_opengl_cb_uninit_gl(_mpv_gl);
    }

    _singleton = nullptr;
    _mpv = nullptr;
    _mpv_gl = nullptr;
  }

  void handleEvent(const mpv_event *e) {
    auto it = _options.event_handlers.find(e->event_id);
    if (it != _options.event_handlers.end()) {
      if (e->event_id == MPV_EVENT_LOG_MESSAGE) {
        auto msg = static_cast<const mpv_event_log_message*>(e->data);
        Local<Value> args[] = { String::NewFromUtf8(_isolate, msg->text),
                                MKI(msg->log_level),
                                String::NewFromUtf8(_isolate, msg->prefix) };
        it->second->Get(_isolate)->CallAsFunction(_isolate->GetCurrentContext(),
                                                  _isolate->GetCurrentContext()->Global(), 3, args);
      } else if (e->event_id == MPV_EVENT_END_FILE) {
        auto end = static_cast<const mpv_event_end_file*>(e->data);
        Local<Value> args[] = { MKI(end->reason), MKI(end->error) };
        it->second->Get(_isolate)->CallAsFunction(_isolate->GetCurrentContext(),
                                                  _isolate->GetCurrentContext()->Global(), 2, args);
      } else {
        Local<Value> dummy;
        it->second->Get(_isolate)->CallAsFunction(_isolate->GetCurrentContext(),
                                                  _isolate->GetCurrentContext()->Global(), 0, &dummy);
      }
    }
  }

  void handlePropertyChange(const mpv_event_property *pd) {
    string prop_name(pd->name);
    auto range = _observers.equal_range(pd->name);
    Local<Value> args[] = { pd->data
                            ? mpv_node_to_v8_value(_isolate, static_cast<const mpv_node*>(pd->data))
                            : Null(_isolate).As<Value>() };
    for (auto it = range.first; it != range.second; ++it) {
      it->second->Get(_isolate)->CallAsFunction(_isolate->GetCurrentContext(),
                                                _isolate->GetCurrentContext()->Global(), 1, args);
    }
  }

  /** GL functions implementation **/

  const GLubyte *glGetString(GLenum name) {
    GL_DEBUG("glGetString: %d\n", name);

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

      default:
        result = string_to_cc(callMethod("getParameter", MKN(name)));
    }

    if (!result.empty()) {
      gl_props[name] = result;
      return reinterpret_cast<const GLubyte*>(gl_props[name].c_str());
    } else {
      return nullptr;
    }
  }

  void glActiveTexture(GLenum texture) {
    GL_DEBUG("glActiveTexture\n");

    callMethod("activeTexture", MKI(texture));
  }

  GLuint glCreateProgram() {
    GL_DEBUG("glCreateProgram\n");

    return storeObject(_programs, callMethod("createProgram"));
  }

  void glDeleteProgram(GLuint program_id) {
    GL_DEBUG("glDeleteProgram\n");

    auto prog_iter = _programs.find(program_id);
    if (prog_iter == _programs.end()) {
      // ignore silently
      return;
    }

    callMethod("deleteProgram", prog_iter->second->Get(_isolate));
  }

  void glGetProgramInfoLog(GLuint program_id, GLsizei max_length, GLsizei *length, GLchar *info_log) {
    GL_DEBUG("glGetProgramInfoLog\n");

    getObjectInfoLog("getProgramInfoLog", _programs, program_id, max_length, length, info_log);
  }

  void glGetProgramiv(GLuint program_id, GLenum pname, GLint *params) {
    GL_DEBUG("glGetProgramiv\n");

    getObjectiv("getProgramParameter", _programs, program_id, pname, params);
  }

  void glUseProgram(GLuint program_id) {
    GL_DEBUG("glUseProgram\n");

    auto prog_iter = _programs.find(program_id);
    if (prog_iter == _programs.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    callMethod("useProgram", prog_iter->second->Get(_isolate));
  }

  void glLinkProgram(GLuint program_id) {
    GL_DEBUG("glLinkProgram\n");

    auto prog_iter = _programs.find(program_id);
    if (prog_iter == _programs.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    callMethod("linkProgram", prog_iter->second->Get(_isolate));
  }

  GLuint glCreateShader(GLenum shader_type) {
    GL_DEBUG("glCreateShader\n");

    return storeObject(_shaders, callMethod("createShader", Number::New(_isolate, shader_type)));
  }

  void glDeleteShader(GLuint shader_id) {
    GL_DEBUG("glDeleteShader\n");

    auto sh_iter = _shaders.find(shader_id);
    if (sh_iter == _shaders.end()) {
      // ignore silently
      return;
    }

    callMethod("deleteShader", sh_iter->second->Get(_isolate));
  }

  void glAttachShader(GLuint program_id, GLuint shader_id) {
    GL_DEBUG("glAttachShader\n");

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
    GL_DEBUG("glCompileShader\n");

    auto sh_iter = _shaders.find(shader_id);
    if (sh_iter == _shaders.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    callMethod("compileShader", sh_iter->second->Get(_isolate));
  }

  void glShaderSource(GLuint shader_id, GLsizei count, const GLchar **string, const GLint *length) {
    GL_DEBUG("glShaderSource\n");

    auto sh_iter = _shaders.find(shader_id);
    if (sh_iter == _shaders.end() || count < 0) {
      // set GL_INVALID_VALUE
      return;
    }

    if (count == 0) {
      return;
    }

    if (count > 1) {
      _throw_js("glShaderSource: Multiple GLSL shader files are not supported");
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
    GL_DEBUG("glBindAttribLocation\n");

    auto prog_iter = _programs.find(program_id);
    if (prog_iter == _programs.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    callMethod("bindAttribLocation", { prog_iter->second->Get(_isolate), MKI(index),
                                       String::NewFromUtf8(_isolate, name) });
  }

  void glBindBuffer(GLenum target, GLuint buffer) {
    GL_DEBUG("glBindBuffer\n");

    if (target == GL_PIXEL_UNPACK_BUFFER) {
      GL_DEBUG("binding a buffer to GL_PIXEL_UNPACK_BUFFER");
    }

    if (buffer == 0) {
      callMethod("bindBuffer", { MKI(target), Null(_isolate) });
      return;
    }

    auto buffer_iter = _buffers.find(buffer);
    if (buffer_iter == _buffers.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    callMethod("bindBuffer", { MKI(target), buffer_iter->second->Get(_isolate) });
  }

  void glBindTexture(GLenum target, GLuint texture) {
    GL_DEBUG("glBindTexture\n");

    if (texture == 0) {
      callMethod("bindTexture", {MKI(target), Null(_isolate)});
      return;
    }

    auto texture_iter = _textures.find(texture);
    if (texture_iter == _textures.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    callMethod("bindTexture", { MKI(target), texture_iter->second->Get(_isolate) });
  }

  void glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAplha, GLenum dstAplha) {
    GL_DEBUG("glBlendFuncSeparate\n");

    callMethod("blendFuncSeparate", { MKI(srcRGB), MKI(dstRGB), MKI(srcAplha), MKI(dstAplha) });
  }

  void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage) {
    GL_DEBUG("glBufferData\n");
    
    if (size < 0) {
      // set GL_INVALID_VALUE
      return;
    }

    if (!data) {
      callMethod("bufferData", { MKI(target),
                                 Integer::NewFromUnsigned(_isolate, static_cast<uint32_t>(size)),
                                 MKI(usage) });
    } else {
      auto buf = ArrayBuffer::New(_isolate, static_cast<size_t>(size));
      memcpy(buf->GetContents().Data(), data, static_cast<size_t>(size));

      callMethod("bufferData", { MKI(target), buf, MKI(usage) });
    }
  }

  void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data) {
    GL_DEBUG("glBufferSubData\n");
    
    if (size < 0) {
      // set GL_INVALID_VALUE
      return;
    }
    
    if (size == 0) {
      return;
    }

    auto buf = ArrayBuffer::New(_isolate, static_cast<size_t>(size));
    memcpy(buf->GetContents().Data(), data, static_cast<size_t>(size));

    callMethod("bufferSubData", { MKI(target),
                                  Integer::NewFromUnsigned(_isolate, static_cast<uint32_t>(offset)),
                                  buf });
  }

  void glClear(GLbitfield mask) {
    GL_DEBUG("glClear\n");

    callMethod("clear", MKI(mask));
  }

  void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) {
    GL_DEBUG("glClearColor\n");

    callMethod("clearColor", { MKN(red), MKN(green), MKN(blue), MKN(alpha) });
  }

  void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
    GL_DEBUG("glDeleteBuffers\n");
    
    deleteObjects("deleteBuffer", _buffers, n, buffers);
  }

  void glDeleteTextures(GLsizei n, const GLuint *textures) {
    GL_DEBUG("glDeleteTextures\n");

    deleteObjects("deleteTexture", _textures, n, textures);
  }

  void glEnable(GLenum cap) {
    GL_DEBUG("glEnable\n");

    callMethod("enable", MKI(cap));
  }

  void glDisable(GLenum cap) {
    GL_DEBUG("glDisable\n");

    callMethod("disable", MKI(cap));
  }

  void glDisableVertexAttribArray(GLuint index) {
    GL_DEBUG("glDisableVertexAttribArray\n");

    callMethod("disableVertexAttribArray", MKI(index));
  }

  void glEnableVertexAttribArray(GLuint index) {
    GL_DEBUG("glEnableVertexAttribArray\n");

    callMethod("enableVertexAttribArray", MKI(index));
  }

  void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    GL_DEBUG("glDrawArrays\n");

    callMethod("drawArrays", { MKI(mode), MKI(first), MKI(count) });
  }

  void glFinish() {
    GL_DEBUG("glFinish\n");

    callMethod("finish");
  }

  void glFlush() {
    GL_DEBUG("glFlush\n");

    callMethod("flush");
  }

  void glGenBuffers(GLsizei n, GLuint *buffers) {
    GL_DEBUG("glGenBuffers\n");
    
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
    GL_DEBUG("glGenTextures\n");
    
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
    GL_DEBUG("glGetAttribLocation\n");

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
    GL_DEBUG("glGetError\n");

    auto result = callMethod("getError").As<Integer>();

    auto err_code = static_cast<GLenum>(result->IntegerValue());
    if (err_code != GL_NO_ERROR) {
      GL_DEBUG("glError result: %ld\n", result->IntegerValue());
    }
    return err_code;
  }

  void glGetIntegerv(GLenum pname, GLint *params) {
    GL_DEBUG("glGetIntegerv: %d\n", pname);

    if (!params) {
      return;
    }

    auto r = callMethod("getParameter", MKI(pname)).As<Integer>();
    if (r.IsEmpty()) {
      return;
    }

    switch (pname) {
      case GL_PIXEL_PACK_BUFFER_BINDING:
      case GL_PIXEL_UNPACK_BUFFER_BINDING:
      case GL_TRANSFORM_FEEDBACK_BUFFER_BINDING:
      case GL_UNIFORM_BUFFER_BINDING:
        // convert to buffer index
        *params = static_cast<GLint>(getIndexFromObject(_buffers, r));
        break;

      case GL_FRAMEBUFFER_BINDING:
        // convert to framebuffer index
        *params = static_cast<GLint>(getIndexFromObject(_framebuffers, r));
        break;

      case GL_CURRENT_PROGRAM:
        // convert to program index
        *params = static_cast<GLint>(getIndexFromObject(_programs, r));
        break;

      case GL_TEXTURE_BINDING_2D:
      case GL_TEXTURE_BINDING_CUBE_MAP:
        // convert to texture index
        *params = static_cast<GLint>(getIndexFromObject(_textures, r));
        break;

      default:
        if (r->IsBoolean()) {
          *params = static_cast<GLint>(r->BooleanValue());
        } else if (r->IsNumber()) {
          *params = static_cast<GLint>(r->IntegerValue());
        } else {
          _throw_js("glGetIntegerv: unexpected return type");
        }
    }
  }

  void glGetShaderInfoLog(GLuint shader_id, GLsizei max_length, GLsizei *length, GLchar *info_log) {
    GL_DEBUG("glGetShaderInfoLog\n");

    getObjectInfoLog("getShaderInfoLog", _shaders, shader_id, max_length, length, info_log);
  }

  void glGetShaderiv(GLuint shader_id, GLenum pname, GLint *params) {
    GL_DEBUG("glGetShaderiv\n");

    getObjectiv("getShaderParameter", _shaders, shader_id, pname, params);
  }

  GLint glGetUniformLocation(GLuint program_id, const GLchar *name) {
    GL_DEBUG("glGetUniformLocation\n");

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
    GL_DEBUG("glPixelStorei\n");

    if (pname == GL_UNPACK_ALIGNMENT) {
      GL_DEBUG("updaing unpack alignment, setting it to %d\n", param);
      unpack_alignment = param;
      // set GL_NO_ERROR
      return;
    } else if (pname == GL_PACK_ALIGNMENT) {
      GL_DEBUG("updating pack alignment, setting it to %d\n", param);
      pack_alignment = param;
      // set GL_NO_ERROR
      return;
    } else if (pname == GL_UNPACK_ROW_LENGTH) {
      _throw_js(("glPixelStorei called with unsupported pname = " + to_string(param)).c_str());
      return;
    }

    callMethod("pixelStorei", { MKI(pname), MKI(param) });
  }

  void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *data) {
    GL_DEBUG("glReadPixels\n");

    if (!data) {
      return;
    }

    //todo: handle bound buffer

    size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    size_t bytes_per_pixel = bytesPerPixel(type, format);

    Local<ArrayBuffer> buf = ArrayBuffer::New(_isolate, pixel_count * bytes_per_pixel);
    Local<Value> buf_view = bufForType(type, buf, pixel_count);
    if (buf_view.IsEmpty()) {
      _throw_js(("glReadPixels: unsupported data format = " + to_string(type)).c_str());
      return;
    }

    TryCatch try_catch(_isolate);
    auto r = callMethod("readPixels", { MKI(x), MKI(y), MKI(width), MKI(height), MKI(format), MKI(type), buf_view });
    if (try_catch.HasCaught()) {
      try_catch.ReThrow();
      return;
    }
    if (glGetError() != GL_NO_ERROR) {
      _throw_js("glReadPixels: error while reading pixels");
      return;
    }

    size_t row_bytes_aligned = alignToPackBoundary(width * bytes_per_pixel);
    if (row_bytes_aligned == width * bytes_per_pixel) {
      memcpy(data, buf->GetContents().Data(), buf->ByteLength());
    } else {
      auto buf_data = static_cast<const GLubyte*>(buf->GetContents().Data());
      auto dst_data = static_cast<GLubyte*>(data);

      for (size_t q = 0; q < height; ++q) {
        memcpy(dst_data, buf_data, width * bytes_per_pixel);
        buf_data += width * bytes_per_pixel;
        dst_data += row_bytes_aligned;
      }
    }
  }

  void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    GL_DEBUG("glScissor\n");

    callMethod("scissor", { MKI(x), MKI(y), MKI(width), MKI(height) });
  }

  void glTexImage2D(GLenum target, GLint level, GLint internal_format, GLsizei width,
                    GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *data) {
    GL_DEBUG("glTexImage2D\n");

    if (target != GL_TEXTURE_2D) {
      _throw_js(("glTexImage2D: unsupported target = " + to_string(target)).c_str());
      return;
    }

    //TODO: handle GL_PIXEL_UNPACK_BUFFER

    if (!data) {
      // data may be a null pointer. In this case, texture memory is allocated to accommodate
      // a texture of width width and height height.
      callMethod("texImage2D", { MKI(target), MKI(level), MKI(internal_format), MKI(width),
                                 MKI(height), MKI(border), MKI(format), MKI(type), Null(_isolate) });
    } else {
      auto bufs = getTexBuffers(type, format, width, height, data);
      if (bufs.second.IsEmpty()) {
        _throw_js(("glTexImage2D: unsupported type: " + to_string(type)).c_str());
        return;
      }

      callMethod("texImage2D", { MKI(target), MKI(level), MKI(internal_format), MKI(width),
                                 MKI(height), MKI(border), MKI(format), MKI(type), bufs.second, MKI(0) });
    }
  }

  void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    GL_DEBUG("glTexParameteri\n");

    callMethod("texParameteri", { MKI(target), MKI(pname), MKI(param) });
  }

  void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                       GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) {
    GL_DEBUG("glTexSubImage2D\n");

    //TODO: handle PIXEL_UNPACK_BUFFER

    auto bufs = getTexBuffers(type, format, width, height, pixels);
    if (bufs.second.IsEmpty()) {
      _throw_js(("glTexSubImage2D: unsupported type: " + to_string(type)).c_str());
      return;
    }

    callMethod("texSubImage2D", { MKI(target), MKI(level), MKI(xoffset), MKI(yoffset),
                                  MKI(width), MKI(height), MKI(format), MKI(type), bufs.second });
  }

  void glUniform1f(GLint location, GLfloat v0) {
    GL_DEBUG("glUniform1f\n");

    callLocationMethod("uniform1f", location, { Local<Value>(), MKN(v0) });
  }

  void glUniform2f(GLint location, GLfloat v0, GLfloat v1) {
    GL_DEBUG("glUniform2f\n");

    callLocationMethod("uniform2f", location, { Local<Value>(), MKN(v0), MKN(v1) });
  }

  void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
    GL_DEBUG("glUniform3f\n");

    callLocationMethod("uniform3f", location, { Local<Value>(), MKN(v0), MKN(v1), MKN(v2) });
  }

  void glUniform1i(GLint location, GLint v0) {
    GL_DEBUG("glUniform1i\n");

    callLocationMethod("uniform1i", location, { Local<Value>(), MKN(v0) });
  }

  void glUniformMatrix2fv(GLint location, GLsizei matrix_count, GLboolean transpose, const GLfloat *value) {
    GL_DEBUG("glUniformMatrix2fv\n");

    if (matrix_count != 1) {
      _throw_js(("glUniformMatrix2fv: unsupported parameter matrix_count = " + to_string(matrix_count)).c_str());
      return;
    }

    uniformMatrix("uniformMatrix2fv", 2, location, transpose, value);
  }

  void glUniformMatrix3fv(GLint location, GLsizei matrix_count, GLboolean transpose, const GLfloat *value) {
    GL_DEBUG("glUniformMatrix3fv\n");

    if (matrix_count != 1) {
      _throw_js(("glUniformMatrix3fv: unsupported parameter matrix_count = " + to_string(matrix_count)).c_str());
      return;
    }

    uniformMatrix("uniformMatrix3fv", 3, location, transpose, value);
  }

  void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                             GLsizei stride, const GLvoid *pointer) {
    GL_DEBUG("glVertexAttribPointer\n");

    auto low_pointer = static_cast<uint32_t>(reinterpret_cast<uint64_t>(pointer));

    callMethod("vertexAttribPointer", { MKI(index), MKI(size), MKI(type),
                                        Boolean::New(_isolate, normalized), MKI(stride),
                                        Integer::NewFromUnsigned(_isolate, low_pointer) });
  }

  void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    GL_DEBUG("glViewport\n");

    callMethod("viewport", { MKI(x), MKI(y), MKI(width), MKI(height) });
  }

  void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    GL_DEBUG("glBindFramebuffer\n");

    if (framebuffer == 0) {
      callMethod("bindFramebuffer", { MKI(target), Null(_isolate) });
      return;
    }

    auto fb_iter = _framebuffers.find(framebuffer);
    if (fb_iter == _framebuffers.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    callMethod("bindFramebuffer", { MKI(target), fb_iter->second->Get(_isolate) });
  }

  void glGenFramebuffers(GLsizei n, GLuint *ids) {
    GL_DEBUG("glGenFramebuffers\n");

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
    GL_DEBUG("glDeleteFramebuffers\n");

    deleteObjects("deleteFramebuffer", _framebuffers, n, framebuffers);
  }

  GLenum glCheckFramebufferStatus(GLenum target) {
    GL_DEBUG("glCheckFramebufferStatus\n");

    return static_cast<GLenum>(callMethod("checkFramebufferStatus", MKI(target))->IntegerValue());
  }

  void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    GL_DEBUG("glFramebufferTexture2D\n");

    auto tex_iter = _textures.find(texture);
    if (tex_iter == _textures.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    callMethod("framebufferTexture2D", { MKI(target), MKI(attachment), MKI(textarget),
                                         tex_iter->second->Get(_isolate), MKI(level) });
  }

  void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params) {
    GL_DEBUG("glGetFramebufferAttachmentParameteriv\n");

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
    if (method.IsEmpty() || method->IsNull() || method->IsUndefined()) {
      _throw_js(("failed to get rendering context method " + method_name).c_str());
      return shared_ptr<Persistent<Function>>();
    }
    auto pers = pers_ptr(new Persistent<Function>(_isolate, method));
    _webgl_methods[method_name] = pers;
    return pers;
  }

  Local<Value> callMethod(const string &method_name, const vector<Local<Value>> &args) {
    return localMethod(method_name)->Call(localContext(), static_cast<int>(args.size()),
                                          const_cast<Local<Value>*>(args.data()));
  }

  Local<Value> callMethod(const string &method_name, const Local<Value> &arg) {
    return localMethod(method_name)->Call(localContext(), 1, const_cast<Local<Value>*>(&arg));
  }

  Local<Value> callMethod(const string &method_name) {
    Local<Value> args[] = { };
    return localMethod(method_name)->Call(localContext(), 0, args);
  }

  template<class T>
  T storeObject(map<T, shared_ptr<Persistent<Value>>> &store, const Local<Value> &value) {
    T object_id = newId();
    store[object_id] = pers_ptr(new Persistent<Value>(_isolate, value));
    return object_id;
  }

  template<class T>
  T getIndexFromObject(map<T, shared_ptr<Persistent<Value>>> &store, const Local<Value> &value)const {
    for (auto obj_iter = store.begin(); obj_iter != store.end(); ++obj_iter) {
      if (value == *(obj_iter->second)) {
        return obj_iter->first;
      }
    }
    return 0;
  }

  size_t alignToUnpackBoundary(size_t value) {
    return alignToBoundary(value, unpack_alignment);
  }

  size_t alignToPackBoundary(size_t value) {
    return alignToBoundary(value, pack_alignment);
  }

  static size_t alignToBoundary(size_t value, size_t boundary) {
    return (value + boundary - 1) & ~(boundary - 1);
  }

  static size_t bytesPerPixel(GLenum type, GLenum format) {
    size_t type_c = 0, format_c = 0;

    switch (type) {
      case GL_UNSIGNED_INT_2_10_10_10_REV:        return 4;
      case GL_UNSIGNED_SHORT_5_6_5:               return 2;
      case GL_UNSIGNED_SHORT_8_8_APPLE:           return 2;
      case GL_UNSIGNED_SHORT_8_8_REV_APPLE:       return 2;
      default:                                    break;
    }

    switch (type) {
      case GL_UNSIGNED_BYTE:                      type_c = 1; break;
      case GL_UNSIGNED_SHORT:                     type_c = 2; break;
      case GL_FLOAT:                              type_c = 4; break;
      default:                                    return 0;
    }

    switch (format) {
      case GL_RED:
      case GL_RED_INTEGER:
      case GL_LUMINANCE:
        format_c = 1;
        break;

      case GL_RG:
      case GL_RG_INTEGER:
      case GL_LUMINANCE_ALPHA:
        format_c = 2;
        break;

      case GL_RGB:
      case GL_RGB_INTEGER:
        format_c = 3;
        break;

      case GL_RGBA:
      case GL_RGBA_INTEGER:
        format_c = 4;
        break;

      default:
        return 0;
    }

    return type_c * format_c;
  }

  Local<Value> bufForType(GLenum type, const Local<ArrayBuffer> &buf, size_t pixel_count) {
    switch (type) {
      case GL_UNSIGNED_BYTE:
        return Uint8Array::New(buf, 0, pixel_count);
        break;

      case GL_UNSIGNED_SHORT:
      case GL_UNSIGNED_SHORT_5_6_5:
      case GL_UNSIGNED_SHORT_4_4_4_4:
      case GL_UNSIGNED_SHORT_5_5_5_1:
      case GL_HALF_FLOAT:
        return Uint16Array::New(buf, 0, pixel_count);
        break;

      case GL_UNSIGNED_INT:
      case GL_UNSIGNED_INT_24_8:
        return Uint32Array::New(buf, 0, pixel_count);
        break;

      case GL_FLOAT:
        return Float32Array::New(buf, 0, pixel_count);
        break;

      default:
        return Local<Value>();
    }
  }

  pair<Local<ArrayBuffer>, Local<Value>> getTexBuffers(GLenum type, GLenum format, GLsizei width,
                                                             GLsizei height, const GLvoid *data) {
    size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    size_t bytes_per_pixel = bytesPerPixel(type, format);

    // Unfortunately, chromium does not accept externalized buffers for webgl
    // methods (externalized buffers in this case should be created by chromium itself), so we have to
    // copy entire data to the new buffer and give ownership to v8.
    Local<ArrayBuffer> buf = ArrayBuffer::New(_isolate, pixel_count * bytes_per_pixel);
    size_t row_bytes_aligned = alignToUnpackBoundary(width * bytes_per_pixel);
    if (row_bytes_aligned == width * bytes_per_pixel) {
      // smooth rows, without gaps between them, we can copy it with one call
      memcpy(buf->GetContents().Data(), data, buf->ByteLength());
    } else {
      // things got worse, there are alignment gaps between rows, and webgl does not support it.
      // we have to copy rows one-by-one and skip alignment gaps
      auto buf_data = static_cast<GLubyte*>(buf->GetContents().Data());
      auto src_data = static_cast<const GLubyte*>(data);

      for (size_t q = 0; q < height; ++q) {
        memcpy(buf_data, src_data, width * bytes_per_pixel);
        buf_data += width * bytes_per_pixel;
        src_data += row_bytes_aligned;
      }
    }

    return { buf, bufForType(type, buf, pixel_count) };
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

    if (pname == GL_INFO_LOG_LENGTH) {
      *params = 1024 * 10;
      GL_DEBUG("simulating GL_INFO_LOG_LENGTH parameter...\n");
      return;
    } else if (pname == GL_SHADER_SOURCE_LENGTH || pname == GL_ACTIVE_UNIFORM_MAX_LENGTH
               || pname == GL_ACTIVE_ATTRIBUTE_MAX_LENGTH) {
      _throw_js(("getting shader or program paratemer: unsupported pname value = " + to_string(pname)).c_str());
      return;
    }

    auto result = callMethod(webgl_method, { obj_iter->second->Get(_isolate), MKI(pname) } );
    if (result->IsNumber() || result->IsNumberObject()) {
      auto maybe_int = result->IntegerValue(_isolate->GetCurrentContext());
      if (maybe_int.IsJust()) {
        *params = static_cast<GLint>(maybe_int.ToChecked());
      }
    } else if (result->IsBoolean() || result->IsBooleanObject()) {
      auto maybe_bool = result->BooleanValue(_isolate->GetCurrentContext());
      if (maybe_bool.IsJust()) {
        *params = static_cast<GLint>(maybe_bool.ToChecked());
      }
    } else {
      _throw_js(("getting shader or program paramater: returned value is not an integer and not an boolean, pname = "
                 + to_string(pname)).c_str());
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

  void deleteObjects(const string &webgl_method, ObjectStore &store, GLsizei n, const GLuint *objects) {
    if (!objects || n == 0) {
      return;
    }

    if (n < 0) {
      // set GL_INVALID_VALUE
      return;
    }

    for (int j = 0; j < n; ++j) {
      auto object_iter = store.find(objects[j]);
      if (object_iter == store.end()) {
        continue;
      }

      callMethod(webgl_method, object_iter->second->Get(_isolate));
      if (glGetError() == GL_NO_ERROR) {
        store.erase(object_iter);
      }
    }
  }

  void callLocationMethod(const string &webgl_method, GLint location_id, vector<Local<Value>> &&args) {
    auto uni_iter = _uniforms.find(location_id);
    if (uni_iter == _uniforms.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    args[0] = uni_iter->second->Get(_isolate);
    callMethod(webgl_method, args);
  }

  void uniformMatrix(const string &method, int matrix_size, GLint location, GLboolean transpose,
                     const GLfloat *value) {
    auto loc_iter = _uniforms.find(location);
    if (loc_iter == _uniforms.end()) {
      // set GL_INVALID_VALUE
      return;
    }

    int matrix_elem_count = matrix_size * matrix_size;

    auto buf = ArrayBuffer::New(_isolate, matrix_elem_count * sizeof(GLfloat));
    memcpy(buf->GetContents().Data(), value, matrix_elem_count * sizeof(GLfloat));
    auto buf_view = Float32Array::New(buf, 0, static_cast<size_t>(matrix_elem_count));

    callMethod(method, { loc_iter->second->Get(_isolate), Boolean::New(_isolate, transpose), buf_view });
  }

  void _throw_js(const char *msg) {
    throw_js(_isolate, msg);
  }

  static MPImpl *singleton() {
    return _singleton;
  }

  mpv_opengl_cb_context *gl()const { return _mpv_gl; }
  mpv_handle *mpv()const { return _mpv; }

  /** Data members **/

  static MPImpl *_singleton;
  PlayerOptions _options;
  multimap<string, shared_ptr<Persistent<Object>>> _observers;
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
  int unpack_alignment = 1, pack_alignment = 1;
  
  GLuint newId() { return ++_last_id; }
};

MPImpl *MPImpl::_singleton = nullptr;

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
 * This function is called by libuv to process new lbimpv frames.
 * It is always called on the main thread.
 */
void do_update(uv_async_t *handle) {
  if (MPImpl::singleton()) {
    GL_DEBUG("mpv_opengl_cb_draw: drawing a frame...\n");
    HandleScope scope(MPImpl::singleton()->_isolate);
    mpv_opengl_cb_draw(MPImpl::singleton()->gl(), 0, 1280, -720);
  }
}

/**
 * This function is called by mpv itself when it has something to draw.
 * It can be called from any thread, so we should ask libuv to call the corresponding callback registered with uv_async_init.
 */
void mpv_async_update_cb(void *ctx) {
  uv_async_send(&async_handle);
}

/**
 * This function is called by libuv to process new libmpv events.
 * it is always called on the main thread.
 */
void do_wakeup(uv_async_t *handle) {
  if (MPImpl::singleton()) {
    while (true) {
      mpv_event *event = mpv_wait_event(MPImpl::singleton()->mpv(), 0);

      if (event->event_id == MPV_EVENT_NONE || event->event_id == MPV_EVENT_SHUTDOWN) {
        break;
      } else if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
        MPImpl::singleton()->handlePropertyChange(static_cast<const mpv_event_property*>(event->data));
      } else {
        MPImpl::singleton()->handleEvent(event);
      }
    }
  }
}

/**
 * This function is called by mpv itself when it has unprocessed events.
 * It can be called from any thread, so we should ask libuv to call the corresponding callback registered with uv_async_init.
 */
void mpv_async_wakeup_cb(void *ctx) {
  uv_async_send(&async_wakeup_handle);
}

/*************************************************************************************
 * Crazy bunch of wrappers around gl methods on MpvPlayerImpl class
 *************************************************************************************/

#define QUOTE(arg) #arg
#define DEF_FN(FUN_NAME) { QUOTE(gl##FUN_NAME), (void*)GlWrappers::gl##FUN_NAME }

namespace GlWrappers {
  void glActiveTexture(GLenum texture) { MPImpl::singleton()->glActiveTexture(texture); }

  const GLubyte *glGetString(GLenum name) { return MPImpl::singleton()->glGetString(name); }

  GLuint glCreateProgram() { return MPImpl::singleton()->glCreateProgram(); }

  void glDeleteProgram(GLuint program) { MPImpl::singleton()->glDeleteProgram(program); }

  void glGetProgramInfoLog(GLuint program, GLsizei max_length, GLsizei *length, GLchar *info_log) {
    MPImpl::singleton()->glGetProgramInfoLog(program, max_length, length, info_log);
  }

  void glGetProgramiv(GLuint program, GLenum pname, GLint *params) {
    MPImpl::singleton()->glGetProgramiv(program, pname, params);
  }

  void glLinkProgram(GLuint program) { MPImpl::singleton()->glLinkProgram(program); }

  void glUseProgram(GLuint program) { MPImpl::singleton()->glUseProgram(program); }

  GLuint glCreateShader(GLenum shader_type) { return MPImpl::singleton()->glCreateShader(shader_type); }

  void glDeleteShader(GLuint shader_id) { MPImpl::singleton()->glDeleteShader(shader_id); }

  void glAttachShader(GLuint program_id, GLuint shader_id) {
    MPImpl::singleton()->glAttachShader(program_id, shader_id);
  }

  void glCompileShader(GLuint shader_id) { MPImpl::singleton()->glCompileShader(shader_id); }

  void glBindAttribLocation(GLuint program_id, GLuint index, const GLchar *name) {
    MPImpl::singleton()->glBindAttribLocation(program_id, index, name);
  }

  void glBindBuffer(GLenum target, GLuint buffer) { MPImpl::singleton()->glBindBuffer(target, buffer); }

  void glBindTexture(GLenum target, GLuint texture) { MPImpl::singleton()->glBindTexture(target, texture); }

  void glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAplha, GLenum dstAplha) {
    MPImpl::singleton()->glBlendFuncSeparate(srcRGB, dstRGB, srcAplha, dstAplha);
  }

  void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data,
                        GLenum usage) { MPImpl::singleton()->glBufferData(target, size, data, usage); }

  void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                           const GLvoid *data) { MPImpl::singleton()->glBufferSubData(target, offset, size, data); }

  void glClear(GLbitfield mask) { MPImpl::singleton()->glClear(mask); }

  void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) {
    MPImpl::singleton()->glClearColor(red, green, blue, alpha);
  }

  void glDeleteBuffers(GLsizei n, const GLuint *buffers) { MPImpl::singleton()->glDeleteBuffers(n, buffers); }

  void glDeleteTextures(GLsizei n, const GLuint *textures) { MPImpl::singleton()->glDeleteTextures(n, textures); }

  void glEnable(GLenum cap) { MPImpl::singleton()->glEnable(cap); }

  void glDisable(GLenum cap) { MPImpl::singleton()->glDisable(cap); }

  void glDisableVertexAttribArray(GLuint index) { MPImpl::singleton()->glDisableVertexAttribArray(index); }

  void glEnableVertexAttribArray(GLuint index) { MPImpl::singleton()->glEnableVertexAttribArray(index); }

  void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    MPImpl::singleton()->glDrawArrays(mode, first, count);
  }

  void glFinish() { MPImpl::singleton()->glFinish(); }

  void glFlush() { MPImpl::singleton()->glFlush(); }

  void glGenBuffers(GLsizei n, GLuint *buffers) { MPImpl::singleton()->glGenBuffers(n, buffers); }

  void glGenTextures(GLsizei n, GLuint *textures) { MPImpl::singleton()->glGenTextures(n, textures); }

  GLint glGetAttribLocation(GLuint program_id, const GLchar *name) {
    return MPImpl::singleton()->glGetAttribLocation(program_id, name);
  }

  GLenum glGetError() { return MPImpl::singleton()->glGetError(); }

  void glGetIntegerv(GLenum pname, GLint *params) { MPImpl::singleton()->glGetIntegerv(pname, params); }

  void glGetShaderInfoLog(GLuint shader_id, GLsizei max_length, GLsizei *length, GLchar *info_log) {
    MPImpl::singleton()->glGetShaderInfoLog(shader_id, max_length, length, info_log);
  }

  void glGetShaderiv(GLuint shader_id, GLenum pname, GLint *params) {
    MPImpl::singleton()->glGetShaderiv(shader_id, pname, params);
  }

  GLint glGetUniformLocation(GLuint program, const GLchar *name) {
    return MPImpl::singleton()->glGetUniformLocation(program, name);
  }

  void glPixelStorei(GLenum pname, GLint param) { MPImpl::singleton()->glPixelStorei(pname, param); }

  void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                        GLvoid *data) { MPImpl::singleton()->glReadPixels(x, y, width, height, format, type, data); }

  void glShaderSource(GLuint shader_id, GLsizei count, const GLchar **str, const GLint *length) {
    MPImpl::singleton()->glShaderSource(shader_id, count, str, length);
  }

  void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    MPImpl::singleton()->glScissor(x, y, width, height);
  }

  void glTexImage2D(GLenum target, GLint level, GLint internal_format, GLsizei width, GLsizei height, GLint border,
                        GLenum format, GLenum type, const GLvoid *data) {
    MPImpl::singleton()->glTexImage2D(target, level, internal_format, width, height, border, format, type, data);
  }

  void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    MPImpl::singleton()->glTexParameteri(target, pname, param);
  }

  void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                           GLenum format, GLenum type, const GLvoid *pixels) {
    MPImpl::singleton()->glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
  }

  void glUniform1f(GLint location, GLfloat v0) { MPImpl::singleton()->glUniform1f(location, v0); }

  void glUniform2f(GLint location, GLfloat v0, GLfloat v1) { MPImpl::singleton()->glUniform2f(location, v0, v1); }

  void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
    MPImpl::singleton()->glUniform3f(location, v0, v1, v2);
  }

  void glUniform1i(GLint location, GLint v0) { MPImpl::singleton()->glUniform1i(location, v0); }

  void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    MPImpl::singleton()->glUniformMatrix2fv(location, count, transpose, value);
  }

  void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    MPImpl::singleton()->glUniformMatrix3fv(location, count, transpose, value);
  }

  void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,
                                 const GLvoid *pointer) {
    MPImpl::singleton()->glVertexAttribPointer(index, size, type, normalized, stride, pointer);
  }

  void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    MPImpl::singleton()->glViewport(x, y, width, height);
  }

  void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    MPImpl::singleton()->glBindFramebuffer(target, framebuffer);
  }

  void glGenFramebuffers(GLsizei n, GLuint *ids) { MPImpl::singleton()->glGenFramebuffers(n, ids); }

  void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) {
    MPImpl::singleton()->glDeleteFramebuffers(n, framebuffers);
  }

  GLenum glCheckFramebufferStatus(GLenum target) { return MPImpl::singleton()->glCheckFramebufferStatus(target); }

  void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    MPImpl::singleton()->glFramebufferTexture2D(target, attachment, textarget, texture, level);
  }

  void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params) {
    MPImpl::singleton()->glGetFramebufferAttachmentParameteriv(target, attachment, pname, params);
  }
}

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
  auto iter = gl_func_map.find(string(name));
  if (iter == gl_func_map.end()) {
    fprintf(stderr, "No WebGL wrapper for a function named %s\n", name);
    return nullptr;
  }
  return iter->second;
}

MpvPlayer::MpvPlayer(Isolate *isolate,
                     const shared_ptr<Persistent<Object>> &canvas,
                     const shared_ptr<Persistent<Object>> &renderingContext,
                     const PlayerOptions &opts) :
    ObjectWrap(),
    d(new MPImpl(isolate, canvas, renderingContext, opts)) {

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
  NODE_SET_PROTOTYPE_METHOD(tpl, "getProperty", GetProperty);
  NODE_SET_PROTOTYPE_METHOD(tpl, "setProperty", SetProperty);
  NODE_SET_PROTOTYPE_METHOD(tpl, "observeProperty", ObserveProperty);
  NODE_SET_PROTOTYPE_METHOD(tpl, "dispose", Dispose);

  constructor.Reset(isolate, tpl->GetFunction());
  exports->Set(String::NewFromUtf8(isolate, MPV_PLAYER_CLASS), tpl->GetFunction());

  // initialize libuv async callbacks
  uv_async_init(uv_default_loop(), &async_handle, do_update);
  uv_async_init(uv_default_loop(), &async_wakeup_handle, do_wakeup);
}

/**
 * Mapped to MpvPlayer constructor function.
 * This function should only be called with `new` operator and get a single argument.
 * This argument should be an object representing a valid DOM canvas element.
 */
 void MpvPlayer::New(const FunctionCallbackInfo<Value> &args) {
  Isolate *isolate = args.GetIsolate();

  if (!args.IsConstructCall()) {
    // our constructor function is called as a function, without new
    isolate->ThrowException(String::NewFromUtf8(isolate, "MpvPlayer constructor should be called with new"));
    return;
  }

  if (args.Length() < 1) {
    isolate->ThrowException(
      Exception::TypeError(String::NewFromUtf8(isolate, "MpvPlayer: invalid number of arguments for constructor: canvas DOM element expected")));
    return;
  }

  Local<Value> arg = args[0];
  if (!arg->IsObject()) {
    isolate->ThrowException(
      Exception::TypeError(String::NewFromUtf8(isolate, "MpvPlayer: invalid argument, canvas DOM element expected")));
    return;
  }

  PlayerOptions opts;
  if (args.Length() > 1) {
    Local<Object> options = args[1].As<Object>();
    if (options.IsEmpty()) {
      throw_js(isolate, "MpvPlayer: invalid argument, option object expected");
      return;
    }

    // enumerate option properties
    Local<Array> op_props = options->GetOwnPropertyNames();
    for (uint32_t q = 0; q < op_props->Length(); ++q) {
      Local<String> prop_name = op_props->Get(q).As<String>();
      string prop_name_cc = string_to_cc(prop_name);
      if (prop_name_cc.size() > 2 && prop_name_cc.substr(0, 2) == "on") {
        // event handler
        auto he_iter = handler_events.find(prop_name_cc);
        if (he_iter == handler_events.end()) {
          throw_js(isolate, ("MpvPlayer: unknown event handler " + prop_name_cc).c_str());
          return;
        }

        Local<Function> prop_value = options->Get(prop_name).As<Function>();
        if (prop_name.IsEmpty()) {
          throw_js(isolate, ("MpvPlayer: invalid handler for " + prop_name_cc + ", not a function").c_str());
          return;
        }

        opts.event_handlers[he_iter->second] = pers_ptr(new Persistent<Function>(isolate, prop_value));
      } else if (prop_name_cc == "logLevel") {
        // log level
        Local<String> prop_value = options->Get(prop_name).As<String>();
        if (prop_value.IsEmpty()) {
          throw_js(isolate, "MpvPlayer: invalid argument type for option logLevel: string expected");
          return;
        }
        opts.log_level = string_to_cc(prop_value);
      }
    }
  }

  // extract object pointing to canvas
  auto canvas = pers_ptr(new Persistent<Object>(isolate, arg->ToObject()));

  // find getContext method on canvas object
  Local<Object> get_context_func = canvas->Get(isolate)->Get(String::NewFromUtf8(isolate, "getContext"))->ToObject();
  if (get_context_func.IsEmpty() || get_context_func->IsUndefined()) {
    throw_js(isolate, "MpvPlayer::create: failed to create WebGL rendering context");
    return;
  }

  // and call canvas.getContext to get webgl rendering context
  Local<Object> context_opts = Object::New(isolate);
  context_opts->Set(String::NewFromUtf8(isolate, "premultipliedAlpha"), Boolean::New(isolate, true));
  context_opts->Set(String::NewFromUtf8(isolate, "alpha"), Boolean::New(isolate, false));
  context_opts->Set(String::NewFromUtf8(isolate, "antialias"), Boolean::New(isolate, false));
  Local<Value> get_context_args[] = { String::NewFromUtf8(isolate, "webgl2"), context_opts };
  TryCatch try_catch(isolate);

  MaybeLocal<Value> maybe_context = get_context_func->CallAsFunction(isolate->GetCurrentContext(),
                                                                     canvas->Get(isolate), 1, get_context_args);

  if (try_catch.HasCaught()) {
    try_catch.ReThrow();
    return;
  }
  if (maybe_context.IsEmpty() ||
      (maybe_context.ToLocalChecked()->IsNull() || maybe_context.ToLocalChecked()->IsUndefined())) {
    throw_js(isolate, "MpvPlayer::create: failed to initialize WebGL");
    return;
  }

  // create C++ player object
  auto context_pers = pers_ptr(new Persistent<Object>(isolate, maybe_context.ToLocalChecked()->ToObject(isolate)));
  auto player_obj = new MpvPlayer(isolate, canvas, context_pers, opts);
  player_obj->Wrap(args.This());
  player_obj->Ref(); // do not GC this object if there are no handles, we are going to deref it inside dispose

  args.GetReturnValue().Set(args.This());
}

/**
 * Mapped to MpvPlayer.create function.
 * Initializes MPV player instance.
 */
void MpvPlayer::Create(const FunctionCallbackInfo<Value> &args) {
  Isolate *isolate = args.GetIsolate();
  auto self = ObjectWrap::Unwrap<MpvPlayer>(args.Holder());

  if (self->d->_mpv) {
    throw_js(isolate, "MpvPlayer::create: player already created, cannot call this method twice on the same object");
    return;
  }

  // create mpv object
  self->d->_mpv = mpv_create();
  if (!self->d->_mpv) {
    throw_js(isolate, "MpvPlayer::create: failed to initialize mpv");
    return;
  }

  // to speed up things (in electron, yeah) we disable events js code hasn't subscribed to
  const map<mpv_event_id, shared_ptr<Persistent<Function>>> &eh = self->d->_options.event_handlers;
  for (int q = MPV_EVENT_LOG_MESSAGE; q <= MPV_EVENT_QUEUE_OVERFLOW; ++q) {
    auto it = eh.find((mpv_event_id)q); // so hacky
    if (it == eh.end() && q != MPV_EVENT_PROPERTY_CHANGE
#ifdef BUILD_DEBUG
      && q != MPV_EVENT_LOG_MESSAGE
#endif
        ) {
      mpv_request_event(self->d->_mpv, (mpv_event_id)q, false);
    }
  }
  // wow, such fast

  // set default logging params
  const char *log_level = self->d->_options.log_level.empty() ? "warn" : self->d->_options.log_level.c_str();
  mpv_request_log_messages(self->d->_mpv, log_level);
  mpv_set_wakeup_callback(self->d->_mpv, mpv_async_wakeup_cb, self);

  // initialize mpv
  if (mpv_initialize(self->d->_mpv) < 0) {
    throw_js(isolate, "MpvPlayer::create: failed to initialize mpv");
    return;
  }

  // set default options
  mpv_set_option_string(self->d->_mpv, "vo", "opengl-cb");
  mpv_set_option_string(self->d->_mpv, "hwdec", "auto");
  mpv_set_option_string(self->d->_mpv, "sub-auto", "no");
  mpv_set_option_string(self->d->_mpv, "input-vo-keyboard", "no");

  // initialize opengl callback api
  self->d->_mpv_gl = static_cast<mpv_opengl_cb_context*>(mpv_get_sub_api(self->d->_mpv, MPV_SUB_API_OPENGL_CB));
  if (!self->d->_mpv_gl) {
    throw_js(isolate, "MpvPlayer::create: falied to initialize opengl subapi");
    return;
  }

  if (mpv_opengl_cb_init_gl(self->d->_mpv_gl, nullptr, get_proc_address, self) < 0) {
    throw_js(isolate, "MpvPlayer::create: falied to initialize WebGL functions");
    return;
  }

  mpv_opengl_cb_set_update_callback(self->d->_mpv_gl, mpv_async_update_cb, nullptr);
}

void MpvPlayer::Command(const FunctionCallbackInfo<Value> &args) {
  Isolate *i = args.GetIsolate();
  auto self = ObjectWrap::Unwrap<MpvPlayer>(args.Holder());

  if (!self->d->_mpv) {
    throw_js(i, "MpvPlayer::command: player object is not initialized");
    return;
  }

  if (args.Length() < 1) {
    throw_js(i, "MpvPlayer::command: not enough arguments, at least one is expected");
    return;
  }

  if (!args[0]->IsString() && !args[0]->IsStringObject()) {
    throw_js(i, "MpvPlayer::command: invalid arguments, first arguments should be a command name");
    return;
  }

  AutoForeignMpvNode mpv_result;
  int err_code;
  if (args.Length() == 1) {
    string command_name = string_to_cc(args[0]);
    err_code = mpv_command_string(self->d->_mpv, command_name.c_str());
  } else {
    AutoMpvNode mpv_args(args, 0);
    if (!mpv_args.valid()) {
      throw_js(i, "MpvPlayer::command: invalid arguments");
      return;
    }

    err_code = mpv_command_node(self->d->_mpv, mpv_args.ptr(), &mpv_result.node);
  }

  if (err_code != MPV_ERROR_SUCCESS) {
    throw_js(i, mpv_error_string(err_code));
    return;
  }

  args.GetReturnValue().Set(mpv_node_to_v8_value(i, &mpv_result.node));
}

void MpvPlayer::GetProperty(const FunctionCallbackInfo<Value> &args) {
  Isolate *i = args.GetIsolate();
  auto self = ObjectWrap::Unwrap<MpvPlayer>(args.Holder());

  if (!self || !self->d->_mpv) {
    throw_js(i, "MpvPlayer::getProperty: player object is not initialized");
    return;
  }

  if (args.Length() != 1) {
    throw_js(i, "MpvPlayer::getProperty: incorrect number of arguments, a single property name expected");
    return;
  }

  Local<String> arg = args[0]->ToString();
  if (arg.IsEmpty()) {
    throw_js(i, "MpvPlayer::getProperty: incorrect arguments, a single property name expected");
    return;
  }

  string arg_c = string_to_cc(arg);
  if (arg_c.empty()) {
    throw_js(i, "MpvPlayer::getProperty: fail");
    return;
  }

  AutoForeignMpvNode node;
  auto err_code = mpv_get_property(self->d->_mpv, arg_c.c_str(), MPV_FORMAT_NODE, &node.node);
  if (err_code != MPV_ERROR_SUCCESS) {
    throw_js(i, mpv_error_string(err_code));
    return;
  }

  args.GetReturnValue().Set(mpv_node_to_v8_value(i, &node.node));
}

void MpvPlayer::SetProperty(const FunctionCallbackInfo<Value> &args) {
  Isolate *i = args.GetIsolate();
  auto self = ObjectWrap::Unwrap<MpvPlayer>(args.Holder());

  if (!self || !self->d->_mpv) {
    throw_js(i, "MpvPlayer::setProperty: player object is not initialized");
    return;
  }

  if (args.Length() != 2) {
    throw_js(i, "MpvPlayer::setProperty: incorrect number of arguments, two arguments are expected");
    return;
  }

  if (!args[0]->IsString() && !args[0]->IsStringObject()) {
    throw_js(i, "MpvPlayer::setProperty: first argument is incorrect, a string expected");
    return;
  }

  string prop_name_c = string_to_cc(args[0]);
  if (prop_name_c.empty()) {
    throw_js(i, "MpvPlayer::setProperty: fail");
    return;
  }

  AutoMpvNode anode(i, args[1]);
  if (!anode.valid()) {
    throw_js(i, "MpvPlayer::setProperty: failed to convert js value to one of mpv_node formats");
    return;
  }

  auto err_code = mpv_set_property(self->d->_mpv, prop_name_c.c_str(), MPV_FORMAT_NODE, anode.ptr());
  if (err_code != MPV_ERROR_SUCCESS) {
    throw_js(i, mpv_error_string(err_code));
    return;
  }
}

void MpvPlayer::ObserveProperty(const FunctionCallbackInfo<Value> &args) {
  Isolate *i = args.GetIsolate();
  auto self = ObjectWrap::Unwrap<MpvPlayer>(args.Holder());

  if (!self || !self->d->_mpv) {
    throw_js(i, "MpvPlayer::observeProperty: player object is not initialized");
    return;
  }

  if (args.Length() != 2) {
    throw_js(i, "MpvPlayer::observeProperty: incorrect number of arguments, two arguments are expected");
    return;
  }

  if (!args[0]->IsString() && !args[0]->IsStringObject()) {
    throw_js(i, "MpvPlayer::observeProperty: first argument is incorrect, a string expected");
    return;
  }

  string prop_name = string_to_cc(args[0]);
  if (prop_name.empty()) {
    throw_js(i, "MpvPlayer::setProperty: fail");
    return;
  }

  Local<Object> handler = args[1].As<Object>();
  if (handler.IsEmpty() || !handler->IsCallable()) {
    throw_js(i, "MpvPlayer::observeProperty: second argument is invalid, a callable expected");
    return;
  }

  int err_code = mpv_observe_property(self->d->_mpv, 0, prop_name.c_str(), MPV_FORMAT_NODE);
  if (err_code != MPV_ERROR_SUCCESS) {
    throw_js(i, mpv_error_string(err_code));
    return;
  }

  self->d->_observers.insert({ prop_name, pers_ptr(new Persistent<Object>(i, handler)) });
}

void MpvPlayer::Dispose(const FunctionCallbackInfo<Value> &args) {
  Isolate *i = args.GetIsolate();
  auto self = ObjectWrap::Unwrap<MpvPlayer>(args.Holder());

  if (!self || !self->d->_mpv) {
    throw_js(i, "MpvPlayer::dispose: player object is not initialized");
    return;
  }

  if (args.Length()) {
    throw_js(i, "MpvPlayer::dispose: incorrect number of arguments, should be 0");
    return;
  }

  self->d->dispose();
  self->Unref(); // now v8 is free to remove this object
}
