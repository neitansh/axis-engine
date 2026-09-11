// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
// Copyright (C) 2017 numzero, Lobachevskiy Vitaliy <numzer0@yandex.ru>

#pragma once
#include "pipeline.h"
#include <string>

/**
 * Подпись на закрытых глазах — большая, посередине кадра.
 *
 * Рисуется в свою текстуру в начале кадра, а сведение (second_stage)
 * кладёт её на кадр само: так она мягкая, как всё, что видно сквозь веки,
 * и гаснет вместе с ними. Поверх кадра вместе с HUD она осталась бы чёткой.
 */
class ScreenCaptionStep : public RenderStep
{
public:
	ScreenCaptionStep() = default;

	void setRenderSource(RenderSource *) override {}
	void setRenderTarget(RenderTarget *target) override { m_target = target; }
	void reset(PipelineContext &context) override {}
	void run(PipelineContext &context) override;

private:
	RenderTarget *m_target {nullptr};
};

/**
 *  Step to apply post-processing filter to the rendered image
 */
class PostProcessingStep : public RenderStep
{
public:
	/**
	 * Construct a new PostProcessingStep object
	 *
	 * @param shader_id ID of the shader in IShaderSource
	 * @param texture_map Map of textures to be chosen from the render source
	 */
	PostProcessingStep(u32 shader_id, const std::vector<u8> &texture_map,
			const std::string &label = {});

	std::string getStepLabel() const override { return m_label; }


	void setRenderSource(RenderSource *source) override;
	void setRenderTarget(RenderTarget *target) override;
	void reset(PipelineContext &context) override;
	void run(PipelineContext &context) override;

	/**
	 * Configure bilinear filtering for a specific texture layer
	 *
	 * @param index Index of the texture layer
	 * @param value true to enable the bilinear filter, false to disable
	 */
	void setBilinearFilter(u8 index, bool value);
private:
	u32 shader_id;
	std::string m_label;
	std::vector<u8> texture_map;
	RenderSource *source { nullptr };
	RenderTarget *target { nullptr };
	video::SMaterial material;

	void configureMaterial();
};


class ResolveMSAAStep : public TrivialRenderStep
{
public:
	ResolveMSAAStep(TextureBufferOutput *_msaa_fbo, TextureBufferOutput *_target_fbo) :
			msaa_fbo(_msaa_fbo), target_fbo(_target_fbo) {};
	void run(PipelineContext &context) override;

private:
	TextureBufferOutput *msaa_fbo;
	TextureBufferOutput *target_fbo;
};


RenderStep *addPostProcessing(RenderPipeline *pipeline, RenderStep *previousStep, v2f scale, Client *client,
		ScreenCaptionStep *caption);
