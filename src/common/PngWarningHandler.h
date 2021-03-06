////////////////////////////////////////////////////////////////////////
//
// This file is part of gmic-8bf, a filter plug-in module that
// interfaces with G'MIC-Qt.
//
// Copyright (c) 2020, 2021 Nicholas Hayes
//
// This file is licensed under the MIT License.
// See LICENSE.txt for complete licensing and attribution information.
//
////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef PNGWARNINGHANDLER_H
#define PNGWARNINGHANDLER_H

#include <png.h>

#if DEBUG_BUILD
void PngWarningHandler(png_structp png, png_const_charp errorDescription);
#endif // DEBUG_BUILD

#endif // !PNGWARNINGHANDLER_H
