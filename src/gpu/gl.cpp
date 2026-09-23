/**
 * @file gl.cpp
 * @brief Definitions of the OpenGL entry points and the loader.
 */
#include "gpu/gl.h"

namespace gl {

GLenum (CC_GL_APIENTRY* GetError)() = nullptr;
const GLubyte* (CC_GL_APIENTRY* GetString)(GLenum name) = nullptr;
void (CC_GL_APIENTRY* GetIntegerv)(GLenum pname, GLint* data) = nullptr;
void (CC_GL_APIENTRY* Viewport)(GLint x, GLint y, GLsizei width, GLsizei height) = nullptr;
void (CC_GL_APIENTRY* Scissor)(GLint x, GLint y, GLsizei width, GLsizei height) = nullptr;
void (CC_GL_APIENTRY* Enable)(GLenum cap) = nullptr;
void (CC_GL_APIENTRY* Disable)(GLenum cap) = nullptr;
void (CC_GL_APIENTRY* BlendFunc)(GLenum sfactor, GLenum dfactor) = nullptr;
void (CC_GL_APIENTRY* ClearColor)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) = nullptr;
void (CC_GL_APIENTRY* Clear)(GLbitfield mask) = nullptr;
void (CC_GL_APIENTRY* PixelStorei)(GLenum pname, GLint param) = nullptr;
void (CC_GL_APIENTRY* GenTextures)(GLsizei n, GLuint* textures) = nullptr;
void (CC_GL_APIENTRY* DeleteTextures)(GLsizei n, const GLuint* textures) = nullptr;
void (CC_GL_APIENTRY* BindTexture)(GLenum target, GLuint texture) = nullptr;
void (CC_GL_APIENTRY* ActiveTexture)(GLenum texture) = nullptr;
void (CC_GL_APIENTRY* TexParameteri)(GLenum target, GLenum pname, GLint param) = nullptr;
void (CC_GL_APIENTRY* TexImage2D)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels) = nullptr;
void (CC_GL_APIENTRY* TexSubImage2D)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* pixels) = nullptr;
GLuint (CC_GL_APIENTRY* CreateShader)(GLenum type) = nullptr;
void (CC_GL_APIENTRY* ShaderSource)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length) = nullptr;
void (CC_GL_APIENTRY* CompileShader)(GLuint shader) = nullptr;
void (CC_GL_APIENTRY* GetShaderiv)(GLuint shader, GLenum pname, GLint* params) = nullptr;
void (CC_GL_APIENTRY* GetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog) = nullptr;
void (CC_GL_APIENTRY* DeleteShader)(GLuint shader) = nullptr;
GLuint (CC_GL_APIENTRY* CreateProgram)() = nullptr;
void (CC_GL_APIENTRY* AttachShader)(GLuint program, GLuint shader) = nullptr;
void (CC_GL_APIENTRY* LinkProgram)(GLuint program) = nullptr;
void (CC_GL_APIENTRY* GetProgramiv)(GLuint program, GLenum pname, GLint* params) = nullptr;
void (CC_GL_APIENTRY* GetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog) = nullptr;
void (CC_GL_APIENTRY* UseProgram)(GLuint program) = nullptr;
void (CC_GL_APIENTRY* DeleteProgram)(GLuint program) = nullptr;
GLint (CC_GL_APIENTRY* GetUniformLocation)(GLuint program, const GLchar* name) = nullptr;
void (CC_GL_APIENTRY* Uniform1i)(GLint location, GLint v0) = nullptr;
void (CC_GL_APIENTRY* Uniform1f)(GLint location, GLfloat v0) = nullptr;
void (CC_GL_APIENTRY* Uniform2f)(GLint location, GLfloat v0, GLfloat v1) = nullptr;
void (CC_GL_APIENTRY* Uniform3f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) = nullptr;
void (CC_GL_APIENTRY* Uniform4f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) = nullptr;
void (CC_GL_APIENTRY* GenVertexArrays)(GLsizei n, GLuint* arrays) = nullptr;
void (CC_GL_APIENTRY* DeleteVertexArrays)(GLsizei n, const GLuint* arrays) = nullptr;
void (CC_GL_APIENTRY* BindVertexArray)(GLuint array) = nullptr;
void (CC_GL_APIENTRY* GenBuffers)(GLsizei n, GLuint* buffers) = nullptr;
void (CC_GL_APIENTRY* DeleteBuffers)(GLsizei n, const GLuint* buffers) = nullptr;
void (CC_GL_APIENTRY* BindBuffer)(GLenum target, GLuint buffer) = nullptr;
void (CC_GL_APIENTRY* BufferData)(GLenum target, GLsizeiptr size, const void* data, GLenum usage) = nullptr;
void (CC_GL_APIENTRY* EnableVertexAttribArray)(GLuint index) = nullptr;
void (CC_GL_APIENTRY* VertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer) = nullptr;
void (CC_GL_APIENTRY* DrawArrays)(GLenum mode, GLint first, GLsizei count) = nullptr;

namespace {

/**
 * @brief Resolves one entry point.
 * @tparam Fn The pointer type of the entry point.
 * @param getproc The platform resolver (glfwGetProcAddress).
 * @param name Entry point name including the `gl` prefix.
 * @param fn Receives the pointer.
 * @param ok Cleared when the resolver returns null; left alone otherwise.
 * @param missing Receives `name` when the resolver returns null.
 */
template <typename Fn>
void resolve(GetProcFn getproc, const char* name, Fn& fn, bool& ok, const char** missing)
{
  if(!ok)
  {
    return;
  }
  fn = reinterpret_cast<Fn>(getproc(name));
  if(fn == nullptr)
  {
    ok = false;
    if(missing != nullptr)
    {
      *missing = name;
    }
  }
}

/** @brief Resolves the state entry points. */
void load_state(GetProcFn getproc, bool& ok, const char** missing)
{
  resolve(getproc, "glGetError", GetError, ok, missing);
  resolve(getproc, "glGetString", GetString, ok, missing);
  resolve(getproc, "glGetIntegerv", GetIntegerv, ok, missing);
  resolve(getproc, "glViewport", Viewport, ok, missing);
  resolve(getproc, "glScissor", Scissor, ok, missing);
  resolve(getproc, "glEnable", Enable, ok, missing);
  resolve(getproc, "glDisable", Disable, ok, missing);
  resolve(getproc, "glBlendFunc", BlendFunc, ok, missing);
  resolve(getproc, "glClearColor", ClearColor, ok, missing);
  resolve(getproc, "glClear", Clear, ok, missing);
  resolve(getproc, "glPixelStorei", PixelStorei, ok, missing);
}

/** @brief Resolves the textures entry points. */
void load_textures(GetProcFn getproc, bool& ok, const char** missing)
{
  resolve(getproc, "glGenTextures", GenTextures, ok, missing);
  resolve(getproc, "glDeleteTextures", DeleteTextures, ok, missing);
  resolve(getproc, "glBindTexture", BindTexture, ok, missing);
  resolve(getproc, "glActiveTexture", ActiveTexture, ok, missing);
  resolve(getproc, "glTexParameteri", TexParameteri, ok, missing);
  resolve(getproc, "glTexImage2D", TexImage2D, ok, missing);
  resolve(getproc, "glTexSubImage2D", TexSubImage2D, ok, missing);
}

/** @brief Resolves the shaders entry points. */
void load_shaders(GetProcFn getproc, bool& ok, const char** missing)
{
  resolve(getproc, "glCreateShader", CreateShader, ok, missing);
  resolve(getproc, "glShaderSource", ShaderSource, ok, missing);
  resolve(getproc, "glCompileShader", CompileShader, ok, missing);
  resolve(getproc, "glGetShaderiv", GetShaderiv, ok, missing);
  resolve(getproc, "glGetShaderInfoLog", GetShaderInfoLog, ok, missing);
  resolve(getproc, "glDeleteShader", DeleteShader, ok, missing);
  resolve(getproc, "glCreateProgram", CreateProgram, ok, missing);
  resolve(getproc, "glAttachShader", AttachShader, ok, missing);
  resolve(getproc, "glLinkProgram", LinkProgram, ok, missing);
  resolve(getproc, "glGetProgramiv", GetProgramiv, ok, missing);
  resolve(getproc, "glGetProgramInfoLog", GetProgramInfoLog, ok, missing);
  resolve(getproc, "glUseProgram", UseProgram, ok, missing);
  resolve(getproc, "glDeleteProgram", DeleteProgram, ok, missing);
  resolve(getproc, "glGetUniformLocation", GetUniformLocation, ok, missing);
  resolve(getproc, "glUniform1i", Uniform1i, ok, missing);
  resolve(getproc, "glUniform1f", Uniform1f, ok, missing);
  resolve(getproc, "glUniform2f", Uniform2f, ok, missing);
  resolve(getproc, "glUniform3f", Uniform3f, ok, missing);
  resolve(getproc, "glUniform4f", Uniform4f, ok, missing);
}

/** @brief Resolves the buffers entry points. */
void load_buffers(GetProcFn getproc, bool& ok, const char** missing)
{
  resolve(getproc, "glGenVertexArrays", GenVertexArrays, ok, missing);
  resolve(getproc, "glDeleteVertexArrays", DeleteVertexArrays, ok, missing);
  resolve(getproc, "glBindVertexArray", BindVertexArray, ok, missing);
  resolve(getproc, "glGenBuffers", GenBuffers, ok, missing);
  resolve(getproc, "glDeleteBuffers", DeleteBuffers, ok, missing);
  resolve(getproc, "glBindBuffer", BindBuffer, ok, missing);
  resolve(getproc, "glBufferData", BufferData, ok, missing);
  resolve(getproc, "glEnableVertexAttribArray", EnableVertexAttribArray, ok, missing);
  resolve(getproc, "glVertexAttribPointer", VertexAttribPointer, ok, missing);
  resolve(getproc, "glDrawArrays", DrawArrays, ok, missing);
}

} // namespace

bool load(GetProcFn getproc, const char** missing)
{
  if(missing != nullptr)
  {
    *missing = nullptr;
  }
  if(getproc == nullptr)
  {
    if(missing != nullptr)
    {
      *missing = "(no resolver)";
    }
    return false;
  }
  bool ok = true;
  load_state(getproc, ok, missing);
  load_textures(getproc, ok, missing);
  load_shaders(getproc, ok, missing);
  load_buffers(getproc, ok, missing);
  return ok;
}

} // namespace gl
