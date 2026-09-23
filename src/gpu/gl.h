/**
 * @file gl.h
 * @brief Minimal OpenGL 3.3 core loader: only the entry points the renderer
 * uses, resolved at run time through GLFW's glfwGetProcAddress.
 *
 * Everything lives in namespace gl so it never collides with whatever
 * <GL/gl.h> or glext.h declare on a platform.  The entry points are function
 * pointers because that is how OpenGL is linked on every platform; this is
 * the one place the Power of 10 rule against function pointers is set aside,
 * and every pointer is checked once, at load time, before any is called.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace gl {

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLubyte = unsigned char;
using GLfloat = float;
using GLchar = char;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;
using GLbitfield = unsigned int;

/** @brief The OpenGL enumerants the renderer uses (values from the Khronos registry). */
enum : GLenum
{
  TEXTURE_2D = 0x0DE1,
  TEXTURE0 = 0x84C0,
  TEXTURE_MIN_FILTER = 0x2801,
  TEXTURE_MAG_FILTER = 0x2800,
  TEXTURE_WRAP_S = 0x2802,
  TEXTURE_WRAP_T = 0x2803,
  NEAREST = 0x2600,
  LINEAR = 0x2601,
  CLAMP_TO_EDGE = 0x812F,
  RGBA = 0x1908,
  RGB = 0x1907,
  RED = 0x1903,
  RGBA8 = 0x8058,
  RGB8 = 0x8051,
  R32F = 0x822E,
  R8 = 0x8229,
  UNSIGNED_BYTE = 0x1401,
  FLOAT = 0x1406,
  UNPACK_ALIGNMENT = 0x0CF5,
  VERTEX_SHADER = 0x8B31,
  FRAGMENT_SHADER = 0x8B30,
  COMPILE_STATUS = 0x8B81,
  LINK_STATUS = 0x8B82,
  INFO_LOG_LENGTH = 0x8B84,
  ARRAY_BUFFER = 0x8892,
  STATIC_DRAW = 0x88E4,
  TRIANGLES = 0x0004,
  TRIANGLE_STRIP = 0x0005,
  BLEND = 0x0BE2,
  SRC_ALPHA = 0x0302,
  ONE_MINUS_SRC_ALPHA = 0x0303,
  SCISSOR_TEST = 0x0C11,
  COLOR_BUFFER_BIT = 0x00004000,
  NO_ERROR_ = 0,
  MAX_TEXTURE_SIZE = 0x0D33,
  VERSION = 0x1F02,
  RENDERER = 0x1F01,
  FALSE_ = 0,
  TRUE_ = 1,
};

#if defined(_WIN32) && !defined(APIENTRY)
#define CC_GL_APIENTRY __stdcall
#elif defined(_WIN32)
#define CC_GL_APIENTRY APIENTRY
#else
#define CC_GL_APIENTRY
#endif

/* ---- entry points (all null until load() succeeds) ---------------------- */
extern GLenum (CC_GL_APIENTRY* GetError)();
extern const GLubyte* (CC_GL_APIENTRY* GetString)(GLenum name);
extern void (CC_GL_APIENTRY* GetIntegerv)(GLenum pname, GLint* data);
extern void (CC_GL_APIENTRY* Viewport)(GLint x, GLint y, GLsizei width, GLsizei height);
extern void (CC_GL_APIENTRY* Scissor)(GLint x, GLint y, GLsizei width, GLsizei height);
extern void (CC_GL_APIENTRY* Enable)(GLenum cap);
extern void (CC_GL_APIENTRY* Disable)(GLenum cap);
extern void (CC_GL_APIENTRY* BlendFunc)(GLenum sfactor, GLenum dfactor);
extern void (CC_GL_APIENTRY* ClearColor)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
extern void (CC_GL_APIENTRY* Clear)(GLbitfield mask);
extern void (CC_GL_APIENTRY* PixelStorei)(GLenum pname, GLint param);
extern void (CC_GL_APIENTRY* GenTextures)(GLsizei n, GLuint* textures);
extern void (CC_GL_APIENTRY* DeleteTextures)(GLsizei n, const GLuint* textures);
extern void (CC_GL_APIENTRY* BindTexture)(GLenum target, GLuint texture);
extern void (CC_GL_APIENTRY* ActiveTexture)(GLenum texture);
extern void (CC_GL_APIENTRY* TexParameteri)(GLenum target, GLenum pname, GLint param);
extern void (CC_GL_APIENTRY* TexImage2D)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels);
extern void (CC_GL_APIENTRY* TexSubImage2D)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* pixels);
extern GLuint (CC_GL_APIENTRY* CreateShader)(GLenum type);
extern void (CC_GL_APIENTRY* ShaderSource)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
extern void (CC_GL_APIENTRY* CompileShader)(GLuint shader);
extern void (CC_GL_APIENTRY* GetShaderiv)(GLuint shader, GLenum pname, GLint* params);
extern void (CC_GL_APIENTRY* GetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
extern void (CC_GL_APIENTRY* DeleteShader)(GLuint shader);
extern GLuint (CC_GL_APIENTRY* CreateProgram)();
extern void (CC_GL_APIENTRY* AttachShader)(GLuint program, GLuint shader);
extern void (CC_GL_APIENTRY* LinkProgram)(GLuint program);
extern void (CC_GL_APIENTRY* GetProgramiv)(GLuint program, GLenum pname, GLint* params);
extern void (CC_GL_APIENTRY* GetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
extern void (CC_GL_APIENTRY* UseProgram)(GLuint program);
extern void (CC_GL_APIENTRY* DeleteProgram)(GLuint program);
extern GLint (CC_GL_APIENTRY* GetUniformLocation)(GLuint program, const GLchar* name);
extern void (CC_GL_APIENTRY* Uniform1i)(GLint location, GLint v0);
extern void (CC_GL_APIENTRY* Uniform1f)(GLint location, GLfloat v0);
extern void (CC_GL_APIENTRY* Uniform2f)(GLint location, GLfloat v0, GLfloat v1);
extern void (CC_GL_APIENTRY* Uniform3f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
extern void (CC_GL_APIENTRY* Uniform4f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
extern void (CC_GL_APIENTRY* GenVertexArrays)(GLsizei n, GLuint* arrays);
extern void (CC_GL_APIENTRY* DeleteVertexArrays)(GLsizei n, const GLuint* arrays);
extern void (CC_GL_APIENTRY* BindVertexArray)(GLuint array);
extern void (CC_GL_APIENTRY* GenBuffers)(GLsizei n, GLuint* buffers);
extern void (CC_GL_APIENTRY* DeleteBuffers)(GLsizei n, const GLuint* buffers);
extern void (CC_GL_APIENTRY* BindBuffer)(GLenum target, GLuint buffer);
extern void (CC_GL_APIENTRY* BufferData)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
extern void (CC_GL_APIENTRY* EnableVertexAttribArray)(GLuint index);
extern void (CC_GL_APIENTRY* VertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
extern void (CC_GL_APIENTRY* DrawArrays)(GLenum mode, GLint first, GLsizei count);

/** @brief Resolver signature: glfwGetProcAddress has this shape. */
using GetProcFn = void* (*)(const char* name);

/**
 * @brief Resolves every entry point above.
 * @param getproc The resolver (a current context must be bound).
 * @param missing Receives the name of the first entry point that could not be resolved; may be null.
 * @return true when every entry point was found.
 */
[[nodiscard]] bool load(GetProcFn getproc, const char** missing);

} // namespace gl
