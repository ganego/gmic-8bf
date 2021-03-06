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

#include "stdafx.h"
#include "GmicPlugin.h"
#include "GmicIOSettings.h"
#include <vector>
#include "FileUtil.h"
#include "resource.h"
#include <stdexcept>
#include <wil/resource.h>
#include <wil/result.h>
#include <boost/process.hpp>

OSErr DoParameters(FilterRecord* filterRecord);
OSErr DoPrepare(FilterRecord* filterRecord) noexcept;
OSErr DoStart(FilterRecord* filterRecord);
OSErr DoContinue() noexcept;
OSErr DoFinish();

namespace
{
    bool ShowUI(FilterRecordPtr filterRecord)
    {
        bool showUI = false;

        FilterParameters* parameters = LockParameters(filterRecord);

        if (parameters != nullptr)
        {
            showUI = parameters->showUI;

            UnlockParameters(filterRecord);
        }

        return showUI;
    }

    OSErr ExecuteGmicQt(
        const boost::filesystem::path& indexFilePath,
        const boost::filesystem::path& outputDir,
        FilterRecordPtr filterRecord)
    {
        OSErr err = noErr;

        try
        {
            boost::filesystem::path gmicExecutablePath;

            err = GetGmicQtPath(gmicExecutablePath);

            if (err == noErr)
            {
                boost::filesystem::path reapply = ShowUI(filterRecord) ? "" : "reapply";

                boost::process::child child(gmicExecutablePath, indexFilePath, outputDir, reapply);

                child.wait();

                int exitCode = child.exit_code();

                if (exitCode != 0)
                {
                    if (exitCode == 5)
                    {
                        err = userCanceledErr;
                    }
                    else
                    {
                        err = ioErr;
                    }
                }
            }
        }
        catch (const std::bad_alloc&)
        {
            err = memFullErr;
        }
        catch (const boost::process::process_error& e)
        {
            err = ShowErrorMessage(e.what(), filterRecord, ioErr);
        }

        return err;
    }

    // Determines whether the filter can precess the current document.
    OSErr CanProcessDocument(const FilterRecord* filterRecord) noexcept
    {
        OSErr result = noErr;

        if (filterRecord->imageMode != plugInModeRGBColor &&
            filterRecord->imageMode != plugInModeGrayScale &&
            filterRecord->imageMode != plugInModeRGB48 &&
            filterRecord->imageMode != plugInModeGray16)
        {
            DebugOut("Unsupported imageMode: %d", filterRecord->imageMode);

            result = filterBadMode;
        }

        return result;
    }

    OSErr CreateParameters(FilterRecordPtr filterRecord)
    {
#if _MSC_VER
        // Suppress the 'C4366: The result of the unary '&' operator may be unaligned' warning.
        // The FilterRecord structure is properly aligned.
#pragma warning(disable: 4366)
#endif
        return NewPIHandle(filterRecord, static_cast<int32>(sizeof(FilterParameters)), &filterRecord->parameters);

#if _MSC_VER
#pragma warning(default: 4366)
#endif
    }
}

//-------------------------------------------------------------------------------
DLLExport MACPASCAL void Gmic_Entry_Point(
    const int16 selector,
    FilterRecordPtr filterRecord,
    intptr_t * data,
    int16 * result)
{
    UNREFERENCED_PARAMETER(data);

    DebugOut("%s selector: %d", __FUNCTION__, selector);

    try
    {
        switch (selector)
        {
        case filterSelectorAbout:
            *result = DoAbout(reinterpret_cast<AboutRecord*>(filterRecord));
            break;
        case filterSelectorParameters:
            *result = DoParameters(filterRecord);
            break;
        case filterSelectorPrepare:
            *result = DoPrepare(filterRecord);
            break;
        case filterSelectorStart:
            *result = DoStart(filterRecord);
            break;
        case filterSelectorContinue:
            *result = DoContinue();
            break;
        case filterSelectorFinish:
            *result = DoFinish();
            break;
        default:
            *result = filterBadParameters;
            break;
        }
    }
    catch (const OSErr e)
    {
        *result = e;
    }
    catch (const std::bad_alloc&)
    {
        *result = memFullErr;
    }
    catch (const std::length_error&)
    {
        // The std::*string classes can throw this if the string
        // exceeds the implementation-defined max size.
        *result = memFullErr;
    }
    catch (...)
    {
        *result = paramErr;
    }

    DebugOut("%s selector: %d, result: %d", __FUNCTION__, selector, *result);
}

OSErr DoParameters(FilterRecord* filterRecord)
{
    PrintFunctionName();

#if DEBUG_BUILD
    std::string sig = FourCCToString(filterRecord->hostSig);

    DebugOut("Host signature: 0x%X (%s)", filterRecord->hostSig, sig.c_str());
#endif // DEBUG_BUILD

    OSErr err = noErr;

    if (HostMeetsRequirements(filterRecord))
    {
        if (filterRecord->parameters == nullptr)
        {
            err = CreateParameters(filterRecord);

            if (err == noErr)
            {
               FilterParameters* parameters = LockParameters(filterRecord);
               if (parameters != nullptr)
               {
                   parameters->lastSelectorWasParameters = true;
                   parameters->showUI = true;

                   UnlockParameters(filterRecord);
               }
            }
        }
        else
        {
            FilterParameters* parameters = LockParameters(filterRecord);
            if (parameters != nullptr)
            {
                parameters->lastSelectorWasParameters = true;
                parameters->showUI = true;

                UnlockParameters(filterRecord);
            }
        }
    }
    else
    {
        err = errPlugInHostInsufficient;
    }

    return err;
}

OSErr DoPrepare(FilterRecord* filterRecord) noexcept
{
    PrintFunctionName();

    OSErr err = noErr;

    // Take half of the available space.
    filterRecord->maxSpace /= 2;

    if (filterRecord->parameters == nullptr)
    {
        if (HostMeetsRequirements(filterRecord))
        {
            err = CreateParameters(filterRecord);

            if (err == noErr)
            {
                FilterParameters* parameters = LockParameters(filterRecord);
                if (parameters != nullptr)
                {
                    parameters->lastSelectorWasParameters = false;
                    parameters->showUI = false;

                    UnlockParameters(filterRecord);
                }
            }
        }
        else
        {
            err = errPlugInHostInsufficient;
        }
    }
    else
    {
        FilterParameters* parameters = LockParameters(filterRecord);

        if (parameters != nullptr)
        {
            if (parameters->lastSelectorWasParameters)
            {
                parameters->showUI = true;
                parameters->lastSelectorWasParameters = false;
            }
            else
            {
                parameters->showUI = false;
            }

            UnlockParameters(filterRecord);
        }
    }

    return err;
}

OSErr DoStart(FilterRecord* filterRecord)
{
    PrintFunctionName();

    if (filterRecord->bigDocumentData != nullptr)
    {
        filterRecord->bigDocumentData->PluginUsing32BitCoordinates = true;
    }

    OSErr err = CanProcessDocument(filterRecord);

    if (err == noErr)
    {
        GmicIOSettings settings;
        boost::filesystem::path settingsPath;

        // Try to load the settings file, if present.
        // If the settings file cannot be loaded the plug-in will still be launched.
        if (GetIOSettingsPath(settingsPath) == noErr)
        {
            settings.Load(settingsPath);
        }

        boost::filesystem::path inputDir;
        boost::filesystem::path outputDir;

        err = GetInputDirectory(inputDir);

        if (err == noErr)
        {
            err = GetOutputDirectory(outputDir);

            if (err == noErr)
            {
                boost::filesystem::path indexFilePath;

                err = WriteGmicFiles(inputDir, indexFilePath, filterRecord, settings);
                DebugOut("After WriteGmicFiles err=%d", err);

                if (err == noErr)
                {
                    err = ExecuteGmicQt(indexFilePath, outputDir, filterRecord);
                    DebugOut("After ExecuteGmicQt err=%d", err);

                    if (err == noErr)
                    {
                        err = ReadGmicOutput(outputDir, filterRecord, settings);
                        DebugOut("After ReadGmicOutput err=%d", err);
                    }
                }
            }
        }
    }

    return err;
}

OSErr DoContinue() noexcept
{
    PrintFunctionName();

    return noErr;
}

OSErr DoFinish()
{
    PrintFunctionName();

    return noErr;
}

FilterParameters* LockParameters(FilterRecordPtr filterRecord)
{
    return reinterpret_cast<FilterParameters*>(LockPIHandle(filterRecord, filterRecord->parameters, false));
}

void UnlockParameters(FilterRecordPtr filterRecord)
{
    UnlockPIHandle(filterRecord, filterRecord->parameters);
}
