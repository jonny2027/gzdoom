/*
 ** gl_wrapper.cpp
 **
 **---------------------------------------------------------------------------
 ** Copyright 2012-2014 Alexey Lysiuk
 ** All rights reserved.
 **
 ** Redistribution and use in source and binary forms, with or without
 ** modification, are permitted provided that the following conditions
 ** are met:
 **
 ** 1. Redistributions of source code must retain the above copyright
 **    notice, this list of conditions and the following disclaimer.
 ** 2. Redistributions in binary form must reproduce the above copyright
 **    notice, this list of conditions and the following disclaimer in the
 **    documentation and/or other materials provided with the distribution.
 ** 3. The name of the author may not be used to endorse or promote products
 **    derived from this software without specific prior written permission.
 **
 ** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 ** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 ** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 ** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 ** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 ** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 ** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 ** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 ** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 ** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **---------------------------------------------------------------------------
 **
 */

#include "doomstat.h"
#include "c_console.h"
#include "i_system.h"
#include "m_png.h"
#include "version.h"
#include "w_wad.h"
#include "i_rbopts.h"

#include "gl/renderer/gl_renderer.h"
#include "gl/system/gl_framebuffer.h"
#include "gl/system/gl_interface.h"
#include "gl/shaders/gl_shader.h"
#include "gl/textures/gl_hwtexture.h"
#include "gl/utility/gl_clock.h"


extern int paused;

extern bool g_isTicFrozen;

EXTERN_CVAR(Int, vid_vsync)

CVAR(Int, vid_vsync_auto_switch_frames, 10, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
CVAR(Int, vid_vsync_auto_fps, 60, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);

EXTERN_CVAR(Bool, gl_smooth_rendered)


// ---------------------------------------------------------------------------


namespace
{

class NonCopyable
{
protected:
	NonCopyable() { }
	~NonCopyable() { }

private:
	NonCopyable(const NonCopyable&);
	const NonCopyable& operator=(const NonCopyable&);
};


// ---------------------------------------------------------------------------


class RenderTarget : private NonCopyable
{
public:
	RenderTarget(const GLsizei width, const GLsizei height);
	~RenderTarget();

	void Bind();
	void Unbind();

	FHardwareTexture& GetColorTexture()
	{
		return m_texture;
	}

private:
	GLuint m_ID;
	GLuint m_oldID;

	FHardwareTexture m_texture;

	static GLuint GetBoundID();

}; // class RenderTarget


// ---------------------------------------------------------------------------


class PostProcess
{
public:
	explicit PostProcess(const RenderTarget* const sharedDepth = NULL);
	~PostProcess();

	void Init(const char* const shaderName, const GLsizei width, const GLsizei height);
	void Release();

	bool IsInitialized() const;

	void Start();
	void Finish();

private:
	GLsizei m_width;
	GLsizei m_height;

	RenderTarget*  m_renderTarget;
	FShader*       m_shader;

	const RenderTarget* m_sharedDepth;

}; // class PostProcess


// ---------------------------------------------------------------------------


struct CapabilityChecker
{
	CapabilityChecker();
};


// ---------------------------------------------------------------------------


class BackBuffer : public OpenGLFrameBuffer, private CapabilityChecker, private NonCopyable
{
	typedef OpenGLFrameBuffer Super;

public:
	BackBuffer(void* hMonitor, int width, int height, int bits, int refreshHz, bool fullscreen);
	~BackBuffer();

	virtual bool Lock(bool buffered);
	virtual void Update();

	virtual void GetScreenshotBuffer(const BYTE*& buffer, int& pitch, ESSType& color_type);


	static BackBuffer* GetInstance();

	PostProcess& GetPostProcess();


	void GetGammaTable(      uint16_t* red,       uint16_t* green,       uint16_t* blue);
	void SetGammaTable(const uint16_t* red, const uint16_t* green, const uint16_t* blue);

	void SetSmoothPicture(const bool smooth);

private:
	static BackBuffer*  s_instance;

	RenderTarget        m_renderTarget;
	FShader             m_gammaProgram;

	FHardwareTexture    m_gammaTexture;

	static const size_t GAMMA_TABLE_SIZE = 256;
	uint32_t            m_gammaTable[GAMMA_TABLE_SIZE];
	
	PostProcess         m_postProcess;

	uint32_t            m_frame;
	uint32_t            m_framesToSwitchVSync;
	uint32_t            m_lastFrame;
	uint32_t            m_lastFrameTime;

	void UpdateAutomaticVSync();

	void DrawRenderTarget();
	
}; // class BackBuffer
	

// ---------------------------------------------------------------------------


void BoundTextureSetFilter(const GLenum target, const GLint filter)
{
	glTexParameteri(target, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(target, GL_TEXTURE_MAG_FILTER, filter);

	glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void BoundTextureDraw2D(const GLsizei width, const GLsizei height)
{
	const bool flipX = width  < 0;
	const bool flipY = height < 0;

	const float u0 = flipX ? 1.0f : 0.0f;
	const float v0 = flipY ? 1.0f : 0.0f;
	const float u1 = flipX ? 0.0f : 1.0f;
	const float v1 = flipY ? 0.0f : 1.0f;

	const float x1 = 0.0f;
	const float y1 = 0.0f;
	const float x2 = abs(width );
	const float y2 = abs(height);

	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);

	glBegin(GL_QUADS);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glTexCoord2f(u0, v1);
	glVertex2f(x1, y1);
	glTexCoord2f(u1, v1);
	glVertex2f(x2, y1);
	glTexCoord2f(u1, v0);
	glVertex2f(x2, y2);
	glTexCoord2f(u0, v0);
	glVertex2f(x1, y2);
	glEnd();

	glEnable(GL_ALPHA_TEST);
	glEnable(GL_BLEND);
}

bool BoundTextureSaveAsPNG(const GLenum target, const char* const path)
{
	if (NULL == path)
	{
		return false;
	}

	GLint width  = 0;
	GLint height = 0;

	glGetTexLevelParameteriv(target, 0, GL_TEXTURE_WIDTH,  &width );
	glGetTexLevelParameteriv(target, 0, GL_TEXTURE_HEIGHT, &height);

	if (0 == width || 0 == height)
	{
		Printf("BoundTextureSaveAsPNG: invalid texture size %ix%i\n", width, height);

		return false;
	}

	static const int BYTES_PER_PIXEL = 4;

	const int imageSize = width * height * BYTES_PER_PIXEL;
	unsigned char* imageBuffer = static_cast<unsigned char*>(malloc(imageSize));

	if (NULL == imageBuffer)
	{
		Printf("BoundTextureSaveAsPNG: cannot allocate %i bytes\n", imageSize);

		return false;
	}

	glGetTexImage(target, 0, GL_BGRA, GL_UNSIGNED_BYTE, imageBuffer);

	const int lineSize = width * BYTES_PER_PIXEL;
	unsigned char lineBuffer[lineSize];

	for (GLint line = 0; line < height / 2; ++line)
	{
		void* frontLinePtr = &imageBuffer[line                * lineSize];
		void*  backLinePtr = &imageBuffer[(height - line - 1) * lineSize];

		memcpy(  lineBuffer, frontLinePtr, lineSize);
		memcpy(frontLinePtr,  backLinePtr, lineSize);
		memcpy( backLinePtr,   lineBuffer, lineSize);
	}

	FILE* file = fopen(path, "w");

	if (NULL == file)
	{
		Printf("BoundTextureSaveAsPNG: cannot open file %s\n", path);

		free(imageBuffer);

		return false;
	}

	const bool result =
		   M_CreatePNG(file, &imageBuffer[0], NULL, SS_BGRA, width, height, width * BYTES_PER_PIXEL)
		&& M_FinishPNG(file);

	fclose(file);

	free(imageBuffer);

	return result;
}


// ---------------------------------------------------------------------------


RenderTarget::RenderTarget(const GLsizei width, const GLsizei height)
: m_ID(0)
, m_oldID(0)
, m_texture(width, height, false, false, true, true)
{
	glGenFramebuffersEXT(1, &m_ID);

	Bind();
	m_texture.CreateTexture(NULL, width, height, false, 0, 0);
	m_texture.BindToFrameBuffer();
	Unbind();
}

RenderTarget::~RenderTarget()
{
	glDeleteFramebuffersEXT(1, &m_ID);
}


void RenderTarget::Bind()
{
	const GLuint boundID = GetBoundID();

	if (m_ID != boundID)
	{
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, m_ID);
		m_oldID = boundID;
	}
}

void RenderTarget::Unbind()
{
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, m_oldID);
	m_oldID = 0;
}


GLuint RenderTarget::GetBoundID()
{
	GLint result;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &result);

	return static_cast<GLuint>(result);
}


// ---------------------------------------------------------------------------


PostProcess::PostProcess(const RenderTarget* const sharedDepth)
: m_width       (0   )
, m_height      (0   )
, m_renderTarget(NULL)
, m_shader      (NULL)
, m_sharedDepth (sharedDepth)
{

}

PostProcess::~PostProcess()
{
	Release();
}


void PostProcess::Init(const char* const shaderName, const GLsizei width, const GLsizei height)
{
	assert(NULL != shaderName);
	assert(width  > 0);
	assert(height > 0);

	Release();

	m_width  = width;
	m_height = height;

	m_renderTarget = new RenderTarget(m_width, m_height);

	m_shader = new FShader();
	m_shader->Load("PostProcessing", "shaders/glsl/main.vp", shaderName, NULL, "");

	const GLuint program = m_shader->GetHandle();

	glUseProgram(program);
	glUniform1i(glGetUniformLocation(program, "sampler0"), 0);
	glUniform2f(glGetUniformLocation(program, "resolution"),
		static_cast<GLfloat>(width), static_cast<GLfloat>(height));
	glUseProgram(0);
}

void PostProcess::Release()
{
	if (NULL != m_shader)
	{
		delete m_shader;
		m_shader = NULL;
	}

	if (NULL != m_renderTarget)
	{
		delete m_renderTarget;
		m_renderTarget = NULL;
	}

	m_width  = 0;
	m_height = 0;
}


bool PostProcess::IsInitialized() const
{
	// TODO: check other members?
	return NULL != m_renderTarget;
}


void PostProcess::Start()
{
	assert(NULL != m_renderTarget);

	m_renderTarget->Bind();
}

void PostProcess::Finish()
{
	m_renderTarget->Unbind();

	m_renderTarget->GetColorTexture().Bind(0, 0);

	m_shader->Bind(0.0f);
	BoundTextureDraw2D(m_width, m_height);
	glUseProgram(0);
}


// ---------------------------------------------------------------------------


PostProcess* GetPostProcess()
{
	BackBuffer* backBuffer = BackBuffer::GetInstance();

	return NULL == backBuffer
		? NULL
		: &backBuffer->GetPostProcess();
}


CUSTOM_CVAR(Int, gl_postprocess, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	PostProcess* const postProcess = GetPostProcess();

	if (NULL != postProcess)
	{
		postProcess->Release();
	}
}


bool IsPostProcessActive()
{
	return NULL != GetPostProcess() && gl_postprocess > 0;
}

void StartPostProcess()
{
	if (!IsPostProcessActive())
	{
		return;
	}

	PostProcess* const postProcess = GetPostProcess();

	if (!postProcess->IsInitialized())
	{
		postProcess->Init("shaders/glsl/fxaa.fp", SCREENWIDTH, SCREENHEIGHT);
	}
	
	postProcess->Start();
}

void EndPostProcess()
{
	if (IsPostProcessActive())
	{
		GetPostProcess()->Finish();
	}
}


// ---------------------------------------------------------------------------


CapabilityChecker::CapabilityChecker()
{
	static const char ERROR_MESSAGE[] =
		"The graphics hardware in your system does not support %s.\n"
		"It is required to run this version of " GAMENAME ".\n"
		"You can try to use SDL-based version where this feature is not mandatory.";

	if (!(gl.flags & RFL_FRAMEBUFFER))
	{
		I_FatalError(ERROR_MESSAGE, "Frame Buffer Object (FBO)");
	}
}


// ---------------------------------------------------------------------------


BackBuffer* BackBuffer::s_instance;


const uint32_t GAMMA_TABLE_ALPHA = 0xFF000000;


BackBuffer::BackBuffer(void* hMonitor, int width, int height, int bits, int refreshHz, bool fullscreen)
: OpenGLFrameBuffer(hMonitor, width, height, bits, refreshHz, fullscreen)
, m_renderTarget(width, height)
, m_gammaTexture(GAMMA_TABLE_SIZE, 1, false, false, true, true)
, m_postProcess(&m_renderTarget)
, m_frame(0)
, m_framesToSwitchVSync(0)
, m_lastFrame(0)
, m_lastFrameTime(0)
{
	s_instance = this;

	SetSmoothPicture(gl_smooth_rendered);

	// Create gamma correction texture

	for (size_t i = 0; i < GAMMA_TABLE_SIZE; ++i)
	{
		m_gammaTable[i] = GAMMA_TABLE_ALPHA + (i << 16) + (i << 8) + i;
	}

	m_gammaTexture.CreateTexture(
		reinterpret_cast<unsigned char*>(m_gammaTable),
		GAMMA_TABLE_SIZE, 1, false, 1, 0);

	// Setup uniform samplers for gamma correction shader

	m_gammaProgram.Load("GammaCorrection", "shaders/glsl/main.vp",
		"shaders/glsl/gamma_correction.fp", NULL, "");

	const GLuint program = m_gammaProgram.GetHandle();

	glUseProgram(program);
	glUniform1i(glGetUniformLocation(program, "backbuffer"), 0);
	glUniform1i(glGetUniformLocation(program, "gammaTable"), 1);
	glUseProgram(0);

	// Fill render target with black color

	m_renderTarget.Bind();
	glClear(GL_COLOR_BUFFER_BIT);
	m_renderTarget.Unbind();

	// Post-processing setup

	GLRenderer->beforeRenderView = StartPostProcess;
	GLRenderer->afterRenderView  = EndPostProcess;
}

BackBuffer::~BackBuffer()
{
	s_instance = NULL;
}


bool BackBuffer::Lock(bool buffered)
{
	if (0 == m_Lock)
	{
		m_renderTarget.Bind();
	}

	return Super::Lock(buffered);
}

void BackBuffer::Update()
{
	UpdateAutomaticVSync();

	if (!CanUpdate())
	{
		GLRenderer->Flush();
		return;
	}

	Begin2D(false);

	DrawRateStuff();
	GLRenderer->Flush();

	DrawRenderTarget();

	Swap();
	Unlock();

	CheckBench();
}


void BackBuffer::GetScreenshotBuffer(const BYTE*& buffer, int& pitch, ESSType& color_type)
{
	m_renderTarget.Bind();

	Super::GetScreenshotBuffer(buffer, pitch, color_type);

	m_renderTarget.Unbind();
}


BackBuffer* BackBuffer::GetInstance()
{
	return s_instance;
}

PostProcess& BackBuffer::GetPostProcess()
{
	return m_postProcess;
}


bool IsVSyncEnabled()
{
#if defined (__APPLE__)
	GLint result = 0;

	CGLGetParameter(CGLGetCurrentContext(), kCGLCPSwapInterval, &result);

	return result;
#else // !__APPLE__
	return false; // Not implemented
#endif // __APPLE__
}

void BackBuffer::UpdateAutomaticVSync()
{
	++m_frame;

	// Check and update vertical synchromization state of OpenGL context
	// if automatic VSync is not active or game is not running (menu, console, pause etc)

	const bool isVSync = IsVSyncEnabled();
	const bool isGameRunning = GS_LEVEL == gamestate
		&& !paused
		&& !g_isTicFrozen
		&& MENU_Off == menuactive
		&& c_up == ConsoleState;

	if (2 != vid_vsync || !isGameRunning)
	{
		m_framesToSwitchVSync = 0;
		m_lastFrame           = 0;
		m_lastFrameTime       = 0;

		if (0 == vid_vsync && isVSync)
		{
			SetVSync(0);
		}
		else if (vid_vsync > 0 && !isVSync)
		{
			SetVSync(1);
		}

		return;
	}

	// Update automatic VSync state
	// and change corresponding OpenGL context parameter if needed

	const uint32_t frameTime = I_MSTime();

	if (0 != m_lastFrameTime && frameTime != m_lastFrameTime)
	{
		const uint32_t fps = 1000 / (frameTime - m_lastFrameTime);

		if (   ( isVSync && fps <  vid_vsync_auto_fps)
			|| (!isVSync && fps >= vid_vsync_auto_fps) )

		{
			if (m_frame == m_lastFrame + 1)
			{
				++m_framesToSwitchVSync;
			}
			else
			{
				m_framesToSwitchVSync = 0;
			}

			m_lastFrame = m_frame;
		}
		else
		{
			m_framesToSwitchVSync = 0;
		}
	}

	if (m_framesToSwitchVSync >= vid_vsync_auto_switch_frames)
	{
		SetVSync(isVSync ? 0 : 1);
		
		m_framesToSwitchVSync = 0;
	}
	
	m_lastFrameTime = frameTime;
}


void BackBuffer::DrawRenderTarget()
{
	m_renderTarget.Unbind();

	m_renderTarget.GetColorTexture().Bind(0, 0);
	m_gammaTexture.Bind(1, 0);

	if (rbOpts.dirty)
	{
		// TODO: Figure out why the following glClear() call is needed
		// to avoid drawing of garbage in fullscreen mode when
		// in-game's aspect ratio is different from display one
		glClear(GL_COLOR_BUFFER_BIT);
		
		rbOpts.dirty = false;
	}

	glViewport(rbOpts.shiftX, rbOpts.shiftY, rbOpts.width, rbOpts.height);

	m_gammaProgram.Bind(0.0f);
	BoundTextureDraw2D(Width, Height);
	glUseProgram(0);

	glViewport(0, 0, Width, Height);
}


void BackBuffer::GetGammaTable(uint16_t* red, uint16_t* green, uint16_t* blue)
{
	for (size_t i = 0; i < GAMMA_TABLE_SIZE; ++i)
	{
		const uint32_t r = (m_gammaTable[i] & 0x000000FF);
		const uint32_t g = (m_gammaTable[i] & 0x0000FF00) >> 8;
		const uint32_t b = (m_gammaTable[i] & 0x00FF0000) >> 16;
		
		// Convert 8 bits colors to 16 bits by multiplying on 256
		
		red  [i] = Uint16(r << 8);
		green[i] = Uint16(g << 8);
		blue [i] = Uint16(b << 8);
	}	
}

void BackBuffer::SetGammaTable(const uint16_t* red, const uint16_t* green, const uint16_t* blue)
{
	for (size_t i = 0; i < GAMMA_TABLE_SIZE; ++i)
	{
		// Convert 16 bits colors to 8 bits by dividing on 256
		
		const uint32_t r =   red[i] >> 8;
		const uint32_t g = green[i] >> 8;
		const uint32_t b =  blue[i] >> 8;
		
		m_gammaTable[i] = GAMMA_TABLE_ALPHA + (b << 16) + (g << 8) + r;
	}

	m_gammaTexture.CreateTexture(
		reinterpret_cast<unsigned char*>(m_gammaTable),
		GAMMA_TABLE_SIZE, 1, false, 1, 0);
}

void BackBuffer::SetSmoothPicture(const bool smooth)
{
	FHardwareTexture& texture = m_renderTarget.GetColorTexture();
	texture.Bind(0, 0);
	BoundTextureSetFilter(GL_TEXTURE_2D, smooth ? GL_LINEAR : GL_NEAREST);
}

} // unnamed namespace


// ---------------------------------------------------------------------------


CUSTOM_CVAR(Bool, gl_smooth_rendered, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (BackBuffer* backBuffer = BackBuffer::GetInstance())
	{
		backBuffer->SetSmoothPicture(self);
	}
}


// ---------------------------------------------------------------------------


extern "C"
{

int SDL_GetGammaRamp(uint16_t* red, uint16_t* green, uint16_t* blue)
{
	BackBuffer* frameBuffer = BackBuffer::GetInstance();

	if (NULL != frameBuffer)
	{
		frameBuffer->GetGammaTable(red, green, blue);
	}

	return 0;
}

int SDL_SetGammaRamp(const uint16_t* red, const uint16_t* green, const uint16_t* blue)
{
	BackBuffer* frameBuffer = BackBuffer::GetInstance();

	if (NULL != frameBuffer)
	{
		frameBuffer->SetGammaTable(red, green, blue);
	}

	return 0;
}

}


// ---------------------------------------------------------------------------


#define OpenGLFrameBuffer BackBuffer

#include "sdlglvideo.cpp"

#undef OpenGLFrameBuffer
