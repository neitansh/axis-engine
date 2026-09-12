// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

// Подключается только из lib/rmlui/Backends/RmlUi_Renderer_GL3.cpp через
// RMLUI_GL3_CUSTOM_LOADER. Бэкенд написан под глобальные gl*-функции и
// GL_*-константы, а движок ходит в OpenGL через загрузчик Irrlicht с полями
// GL.Name; макросы сводят одно к другому, чтобы бэкенд лежал в lib/ как есть.

#pragma once

#include <mt_opengl.h>

// Контекст у движка 3.2 compatibility (CIrrDeviceSDL), и строгая реализация
// откажет шейдеру с "#version 330". Ничего из 330 бэкенд не использует.
#undef RMLUI_SHADER_HEADER_VERSION
#define RMLUI_SHADER_HEADER_VERSION "#version 150\n"

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef char GLchar;
typedef khronos_ssize_t GLsizeiptr;
typedef khronos_intptr_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1

#define GL_ACTIVE_TEXTURE OpenGLProcedures::ACTIVE_TEXTURE
#define GL_ACTIVE_UNIFORMS OpenGLProcedures::ACTIVE_UNIFORMS
#define GL_ALWAYS OpenGLProcedures::ALWAYS
#define GL_ARRAY_BUFFER OpenGLProcedures::ARRAY_BUFFER
#define GL_BACK OpenGLProcedures::BACK
#define GL_BLEND OpenGLProcedures::BLEND
#define GL_BLEND_DST_ALPHA OpenGLProcedures::BLEND_DST_ALPHA
#define GL_BLEND_DST_RGB OpenGLProcedures::BLEND_DST_RGB
#define GL_BLEND_EQUATION_ALPHA OpenGLProcedures::BLEND_EQUATION_ALPHA
#define GL_BLEND_EQUATION_RGB OpenGLProcedures::BLEND_EQUATION_RGB
#define GL_BLEND_SRC_ALPHA OpenGLProcedures::BLEND_SRC_ALPHA
#define GL_BLEND_SRC_RGB OpenGLProcedures::BLEND_SRC_RGB
#define GL_CLAMP_TO_BORDER OpenGLProcedures::CLAMP_TO_BORDER
#define GL_CLAMP_TO_EDGE OpenGLProcedures::CLAMP_TO_EDGE
#define GL_COLOR_ATTACHMENT0 OpenGLProcedures::COLOR_ATTACHMENT0
#define GL_COLOR_BUFFER_BIT OpenGLProcedures::COLOR_BUFFER_BIT
#define GL_COLOR_CLEAR_VALUE OpenGLProcedures::COLOR_CLEAR_VALUE
#define GL_COLOR_WRITEMASK OpenGLProcedures::COLOR_WRITEMASK
#define GL_COMPILE_STATUS OpenGLProcedures::COMPILE_STATUS
#define GL_CONSTANT_COLOR OpenGLProcedures::CONSTANT_COLOR
#define GL_CULL_FACE OpenGLProcedures::CULL_FACE
#define GL_DEPTH24_STENCIL8 OpenGLProcedures::DEPTH24_STENCIL8
#define GL_DEPTH_STENCIL_ATTACHMENT OpenGLProcedures::DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_TEST OpenGLProcedures::DEPTH_TEST
#define GL_DRAW_FRAMEBUFFER OpenGLProcedures::DRAW_FRAMEBUFFER
#define GL_ELEMENT_ARRAY_BUFFER OpenGLProcedures::ELEMENT_ARRAY_BUFFER
#define GL_EQUAL OpenGLProcedures::EQUAL
#define GL_FLOAT OpenGLProcedures::FLOAT
#define GL_FRAGMENT_SHADER OpenGLProcedures::FRAGMENT_SHADER
#define GL_FRAMEBUFFER OpenGLProcedures::FRAMEBUFFER
#define GL_FRAMEBUFFER_COMPLETE OpenGLProcedures::FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_SRGB OpenGLProcedures::FRAMEBUFFER_SRGB
#define GL_FRONT OpenGLProcedures::FRONT
#define GL_FUNC_ADD OpenGLProcedures::FUNC_ADD
#define GL_INCR OpenGLProcedures::INCR
#define GL_INFO_LOG_LENGTH OpenGLProcedures::INFO_LOG_LENGTH
#define GL_INVALID_ENUM OpenGLProcedures::INVALID_ENUM
#define GL_INVALID_OPERATION OpenGLProcedures::INVALID_OPERATION
#define GL_INVALID_VALUE OpenGLProcedures::INVALID_VALUE
#define GL_KEEP OpenGLProcedures::KEEP
#define GL_LINEAR OpenGLProcedures::LINEAR
#define GL_LINK_STATUS OpenGLProcedures::LINK_STATUS
#define GL_MIRRORED_REPEAT OpenGLProcedures::MIRRORED_REPEAT
#define GL_NEAREST OpenGLProcedures::NEAREST
#define GL_NO_ERROR OpenGLProcedures::NO_ERROR
#define GL_ONE OpenGLProcedures::ONE
#define GL_ONE_MINUS_SRC_ALPHA OpenGLProcedures::ONE_MINUS_SRC_ALPHA
#define GL_OUT_OF_MEMORY OpenGLProcedures::OUT_OF_MEMORY
#define GL_READ_FRAMEBUFFER OpenGLProcedures::READ_FRAMEBUFFER
#define GL_RENDERBUFFER OpenGLProcedures::RENDERBUFFER
#define GL_REPEAT OpenGLProcedures::REPEAT
#define GL_REPLACE OpenGLProcedures::REPLACE
#define GL_RGBA OpenGLProcedures::RGBA
#define GL_RGBA16F OpenGLProcedures::RGBA16F
#define GL_RGBA8 OpenGLProcedures::RGBA8
#define GL_SCISSOR_BOX OpenGLProcedures::SCISSOR_BOX
#define GL_SCISSOR_TEST OpenGLProcedures::SCISSOR_TEST
#define GL_SRGB8_ALPHA8 OpenGLProcedures::SRGB8_ALPHA8
#define GL_STATIC_DRAW OpenGLProcedures::STATIC_DRAW
#define GL_STENCIL_BACK_FAIL OpenGLProcedures::STENCIL_BACK_FAIL
#define GL_STENCIL_BACK_FUNC OpenGLProcedures::STENCIL_BACK_FUNC
#define GL_STENCIL_BACK_PASS_DEPTH_FAIL OpenGLProcedures::STENCIL_BACK_PASS_DEPTH_FAIL
#define GL_STENCIL_BACK_PASS_DEPTH_PASS OpenGLProcedures::STENCIL_BACK_PASS_DEPTH_PASS
#define GL_STENCIL_BACK_REF OpenGLProcedures::STENCIL_BACK_REF
#define GL_STENCIL_BACK_VALUE_MASK OpenGLProcedures::STENCIL_BACK_VALUE_MASK
#define GL_STENCIL_BACK_WRITEMASK OpenGLProcedures::STENCIL_BACK_WRITEMASK
#define GL_STENCIL_BUFFER_BIT OpenGLProcedures::STENCIL_BUFFER_BIT
#define GL_STENCIL_CLEAR_VALUE OpenGLProcedures::STENCIL_CLEAR_VALUE
#define GL_STENCIL_FAIL OpenGLProcedures::STENCIL_FAIL
#define GL_STENCIL_FUNC OpenGLProcedures::STENCIL_FUNC
#define GL_STENCIL_PASS_DEPTH_FAIL OpenGLProcedures::STENCIL_PASS_DEPTH_FAIL
#define GL_STENCIL_PASS_DEPTH_PASS OpenGLProcedures::STENCIL_PASS_DEPTH_PASS
#define GL_STENCIL_REF OpenGLProcedures::STENCIL_REF
#define GL_STENCIL_TEST OpenGLProcedures::STENCIL_TEST
#define GL_STENCIL_VALUE_MASK OpenGLProcedures::STENCIL_VALUE_MASK
#define GL_STENCIL_WRITEMASK OpenGLProcedures::STENCIL_WRITEMASK
#define GL_TEXTURE0 OpenGLProcedures::TEXTURE0
#define GL_TEXTURE1 OpenGLProcedures::TEXTURE1
#define GL_TEXTURE_2D OpenGLProcedures::TEXTURE_2D
#define GL_TEXTURE_BORDER_COLOR OpenGLProcedures::TEXTURE_BORDER_COLOR
#define GL_TEXTURE_MAG_FILTER OpenGLProcedures::TEXTURE_MAG_FILTER
#define GL_TEXTURE_MIN_FILTER OpenGLProcedures::TEXTURE_MIN_FILTER
#define GL_TEXTURE_WRAP_S OpenGLProcedures::TEXTURE_WRAP_S
#define GL_TEXTURE_WRAP_T OpenGLProcedures::TEXTURE_WRAP_T
#define GL_TRIANGLES OpenGLProcedures::TRIANGLES
#define GL_UNSIGNED_BYTE OpenGLProcedures::UNSIGNED_BYTE
#define GL_UNSIGNED_INT OpenGLProcedures::UNSIGNED_INT
#define GL_VERTEX_SHADER OpenGLProcedures::VERTEX_SHADER
#define GL_VIEWPORT OpenGLProcedures::VIEWPORT
#define GL_ZERO OpenGLProcedures::ZERO

#define glActiveTexture GL.ActiveTexture
#define glAttachShader GL.AttachShader
#define glBindAttribLocation GL.BindAttribLocation
#define glBindBuffer GL.BindBuffer
#define glBindFramebuffer GL.BindFramebuffer
#define glBindRenderbuffer GL.BindRenderbuffer
#define glBindTexture GL.BindTexture
#define glBindVertexArray GL.BindVertexArray
#define glBlendColor GL.BlendColor
#define glBlendEquation GL.BlendEquation
#define glBlendEquationSeparate GL.BlendEquationSeparate
#define glBlendFunc GL.BlendFunc
#define glBlendFuncSeparate GL.BlendFuncSeparate
#define glBlitFramebuffer GL.BlitFramebuffer
#define glBufferData GL.BufferData
#define glCheckFramebufferStatus GL.CheckFramebufferStatus
#define glClear GL.Clear
#define glClearColor GL.ClearColor
#define glClearStencil GL.ClearStencil
#define glColorMask GL.ColorMask
#define glCompileShader GL.CompileShader
#define glCopyTexSubImage2D GL.CopyTexSubImage2D
#define glCreateProgram GL.CreateProgram
#define glCreateShader GL.CreateShader
#define glDeleteBuffers GL.DeleteBuffers
#define glDeleteFramebuffers GL.DeleteFramebuffers
#define glDeleteProgram GL.DeleteProgram
#define glDeleteRenderbuffers GL.DeleteRenderbuffers
#define glDeleteShader GL.DeleteShader
#define glDeleteTextures GL.DeleteTextures
#define glDeleteVertexArrays GL.DeleteVertexArrays
#define glDetachShader GL.DetachShader
#define glDisable GL.Disable
#define glDrawElements GL.DrawElements
#define glEnable GL.Enable
#define glEnableVertexAttribArray GL.EnableVertexAttribArray
#define glFramebufferRenderbuffer GL.FramebufferRenderbuffer
#define glFramebufferTexture2D GL.FramebufferTexture2D
#define glGenBuffers GL.GenBuffers
#define glGenFramebuffers GL.GenFramebuffers
#define glGenRenderbuffers GL.GenRenderbuffers
#define glGenTextures GL.GenTextures
#define glGenVertexArrays GL.GenVertexArrays
#define glGetActiveUniform GL.GetActiveUniform
#define glGetBooleanv GL.GetBooleanv
#define glGetError GL.GetError
#define glGetFloatv GL.GetFloatv
#define glGetIntegerv GL.GetIntegerv
#define glGetProgramInfoLog GL.GetProgramInfoLog
#define glGetProgramiv GL.GetProgramiv
#define glGetShaderInfoLog GL.GetShaderInfoLog
#define glGetShaderiv GL.GetShaderiv
#define glGetUniformLocation GL.GetUniformLocation
#define glIsEnabled GL.IsEnabled
#define glLinkProgram GL.LinkProgram
#define glRenderbufferStorageMultisample GL.RenderbufferStorageMultisample
#define glScissor GL.Scissor
#define glShaderSource GL.ShaderSource
#define glStencilFunc GL.StencilFunc
#define glStencilFuncSeparate GL.StencilFuncSeparate
#define glStencilMask GL.StencilMask
#define glStencilMaskSeparate GL.StencilMaskSeparate
#define glStencilOp GL.StencilOp
#define glStencilOpSeparate GL.StencilOpSeparate
#define glTexImage2D GL.TexImage2D
#define glTexParameterfv GL.TexParameterfv
#define glTexParameteri GL.TexParameteri
#define glUniform1f GL.Uniform1f
#define glUniform1fv GL.Uniform1fv
#define glUniform1i GL.Uniform1i
#define glUniform2f GL.Uniform2f
#define glUniform2fv GL.Uniform2fv
#define glUniform4fv GL.Uniform4fv
#define glUniformMatrix4fv GL.UniformMatrix4fv
#define glUseProgram GL.UseProgram
#define glVertexAttribPointer GL.VertexAttribPointer
#define glViewport GL.Viewport
