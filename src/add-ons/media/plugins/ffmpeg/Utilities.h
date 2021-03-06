/*
 * Copyright 2009, Stephan Aßmus <superstippi@gmx.de>
 * Copyright 2014, Colin Günther <coling@gmx.de>
 * All rights reserved. Distributed under the terms of the GNU L-GPL license.
 */
#ifndef UTILITIES_H
#define UTILITIES_H


/*! \brief This file contains functions to convert and calculate values from
		FFmpeg to Media Kit and vice versa.
*/


#include <assert.h>

#include <GraphicsDefs.h>

extern "C" {
	#include "avcodec.h"
}


/*! \brief Converts FFmpeg notation of video aspect ratio into the Media Kits
		notation.

	\param contextIn An AVCodeContext structure of FFmpeg containing the values
		needed to calculate the Media Kit video aspect ratio.
		The following fields are used for the calculation:
			- AVCodecContext.sample_aspect_ratio.num (optional)
			- AVCodecContext.sample_aspect_ratio.den (optional)
			- AVCodecContext.width (must)
			- AVCodecContext.height (must)
	\param pixelWidthAspectOut On return contains Media Kits notation of the
		video aspect ratio width. E.g. 16:9 -> 16 is returned here
	\param pixelHeightAspectOut On return contains Media Kits notation of the
		video aspect ratio height. E.g. 16:9 -> 9 is returned here
*/
inline void
ConvertAVCodecContextToVideoAspectWidthAndHeight(AVCodecContext& contextIn,
	uint16& pixelWidthAspectOut, uint16& pixelHeightAspectOut)
{
	assert(contextIn.sample_aspect_ratio.num >= 0);
	assert(contextIn.sample_aspect_ratio.den > 0);
	assert(contextIn.width > 0);
	assert(contextIn.height > 0);

	// The following code is based on code originally located in
	// AVFormatReader::Stream::Init() and thus should be copyrighted to Stephan
	// Aßmus
	AVRational pixelAspectRatio;

	if (contextIn.sample_aspect_ratio.num == 0) {
		// AVCodecContext doesn't contain a video aspect ratio, so calculate it
		// ourselve based solely on the video dimensions
		av_reduce(&pixelAspectRatio.num, &pixelAspectRatio.den, contextIn.width,
			contextIn.height, 1024 * 1024);
	
		pixelWidthAspectOut = static_cast<int16>(pixelAspectRatio.num);
		pixelHeightAspectOut = static_cast<int16>(pixelAspectRatio.den);
		return;
	}

	// AVCodecContext contains a video aspect ratio, so use it
	av_reduce(&pixelAspectRatio.num, &pixelAspectRatio.den,
		contextIn.width * contextIn.sample_aspect_ratio.num,
		contextIn.height * contextIn.sample_aspect_ratio.den,
		1024 * 1024);

	pixelWidthAspectOut = static_cast<int16>(pixelAspectRatio.num);
	pixelHeightAspectOut = static_cast<int16>(pixelAspectRatio.den);
}


/*! \brief Calculates bytes per row for a video frame.

	\param colorSpace The Media Kit color space the video frame uses.
	\param videoWidth The width of the video frame.

	\returns bytes per video frame row
	\returns Zero, when bytes per video frame cannot be calculated.
*/
inline uint32
CalculateBytesPerRowWithColorSpaceAndVideoWidth(color_space colorSpace, int videoWidth)
{
	assert(videoWidth >= 0);

	const uint32 kBytesPerRowUnknown = 0;
	size_t bytesPerPixel;
	size_t rowAlignment;

	if (get_pixel_size_for(colorSpace, &bytesPerPixel, &rowAlignment, NULL) != B_OK)
		return kBytesPerRowUnknown;

	uint32 bytesPerRow = bytesPerPixel * videoWidth;
	uint32 numberOfUnalignedBytes = bytesPerRow % rowAlignment;

	if (numberOfUnalignedBytes == 0)
		return bytesPerRow;

	uint32 numberOfBytesNeededForAlignment = rowAlignment - numberOfUnalignedBytes;
	bytesPerRow += numberOfBytesNeededForAlignment;

	return bytesPerRow;
}


/*! \brief Converts FFmpeg notation of video frame rate into the Media Kits
		notation.

	\param contextIn An AVCodeContext structure of FFmpeg containing the values
		needed to calculate the Media Kit video frame rate.
		The following fields are used for the calculation:
			- AVCodecContext.time_base.num (must)
			- AVCodecContext.time_base.den (must)
			- AVCodecContext.ticks_per_frame (must)
	\param frameRateOut On return contains Media Kits notation of the video
		frame rate.
*/
inline void
ConvertAVCodecContextToVideoFrameRate(AVCodecContext& contextIn, float& frameRateOut)
{
	// assert that av_q2d(contextIn.time_base) > 0 and computable
	assert(contextIn.time_base.num > 0);
	assert(contextIn.time_base.den > 0);

	// The following code is based on private get_fps() function of FFmpeg's
	// ratecontrol.c:
	// https://lists.ffmpeg.org/pipermail/ffmpeg-cvslog/2012-April/049280.html
	double possiblyInterlacedFrameRate = 1.0 / av_q2d(contextIn.time_base);
	double numberOfInterlacedFramesPerFullFrame = FFMAX(contextIn.ticks_per_frame, 1);

	frameRateOut
		= possiblyInterlacedFrameRate / numberOfInterlacedFramesPerFullFrame;
}

#endif // UTILITIES_H
