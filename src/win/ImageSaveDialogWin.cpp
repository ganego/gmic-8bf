////////////////////////////////////////////////////////////////////////
//
// This file is part of gmic-8bf, a filter plug-in module that
// interfaces with G'MIC-Qt.
//
// Copyright (c) 2020 Nicholas Hayes
//
// This file is licensed under the MIT License.
// See LICENSE.txt for complete licensing and attribution information.
//
////////////////////////////////////////////////////////////////////////

#include "ImageSaveDialogWin.h"
#include "resource.h"
#include <windows.h>
#include <Uxtheme.h>
#include <ShlObj.h>
#include <ShObjIdl.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

namespace
{
    bool UseVistaStyleDialogs()
    {
        DWORD resultFlags = GetThemeAppProperties();

        if ((resultFlags & STAP_ALLOW_CONTROLS) != 0)
        {
            // Use the classic dialog if the OS is in safe mode.
            return GetSystemMetrics(SM_CLEANBOOT) == 0;
        }

        return false;
    }

    OSErr GetSaveFileNameVista(HWND owner, boost::filesystem::path& saveFilePath)
    {
        // The client GUID is used to allow this dialog to persist its state independently of the other file dialogs in
        // the host application.
        // {71709F93-0429-45D2-97E2-FE69937BE9E8}
        static const GUID ClientGuid = { 0x71709f93, 0x429, 0x45d2, { 0x97, 0xe2, 0xfe, 0x69, 0x93, 0x7b, 0xe9, 0xe8 } };

        OSErr err = noErr;

        try
        {
            wchar_t titleBuffer[256] = {};
            wchar_t filterNameBuffer[256] = {};

            const int titleLength = LoadStringW(wil::GetModuleInstanceHandle(),
                                                IMAGE_SAVE_DIALOG_TITLE,
                                                titleBuffer,
                                                _countof(titleBuffer));
            const int filterNameLength = LoadStringW(wil::GetModuleInstanceHandle(),
                                                     IMAGE_SAVE_DIALOG_FILTER_NAME,
                                                     filterNameBuffer,
                                                     _countof(filterNameBuffer));

            if (titleLength > 0 && filterNameLength > 0)
            {
                auto comCleanup = wil::CoInitializeEx(COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

                auto pfd = wil::CoCreateInstance<IFileSaveDialog>(CLSID_FileSaveDialog);

                DWORD dwOptions;
                THROW_IF_FAILED(pfd->GetOptions(&dwOptions));

                THROW_IF_FAILED(pfd->SetOptions(dwOptions | FOS_DONTADDTORECENT | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT));
                THROW_IF_FAILED(pfd->SetTitle(titleBuffer));
                THROW_IF_FAILED(pfd->SetClientGuid(ClientGuid));
                THROW_IF_FAILED(pfd->SetDefaultExtension(L"png"));

                COMDLG_FILTERSPEC filter = { filterNameBuffer, L"*.png" };

                THROW_IF_FAILED(pfd->SetFileTypes(1, &filter));

                THROW_IF_FAILED(pfd->Show(owner));

                wil::com_ptr<IShellItem> psi;
                THROW_IF_FAILED(pfd->GetResult(&psi));

                wil::unique_cotaskmem_string pszPath;

                THROW_IF_FAILED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath));

                saveFilePath = pszPath.get();
            }
            else
            {
                err = ioErr;
            }
        }
        catch (const std::bad_alloc&)
        {
            err = memFullErr;
        }
        catch (const wil::ResultException& e)
        {
            switch (e.GetErrorCode())
            {
            case E_OUTOFMEMORY:
                err = memFullErr;
                break;
            case HRESULT_FROM_WIN32(ERROR_CANCELLED):
                err = userCanceledErr;
                break;
            default:
                err = ioErr;
                break;
            }
        }
        catch (...)
        {
            err = ioErr;
        }

        return err;
    }

    wil::unique_cotaskmem_ptr<wchar_t[]> BulidClassicSaveDialogFilterString(LPCWSTR filterName, size_t filterNameLength)
    {
        static constexpr const wchar_t* fileExtensionFilter = L"*.png";
        constexpr size_t fileExtensionFilterLength = std::char_traits<wchar_t>::length(fileExtensionFilter);

        // The filter uses embedded NUL characters as a separator, with double termination for the last item.
        // The final string will have the following format: "PNG Images\0*.png\0\0".

        const size_t filterStringLength = filterNameLength + 1 + fileExtensionFilterLength + 1 + 1;
        const size_t filterStringLengthInBytes = filterStringLength * sizeof(wchar_t);

        wil::unique_cotaskmem_ptr<wchar_t[]> filter = wil::make_unique_cotaskmem<wchar_t[]>(filterStringLengthInBytes);

        wchar_t* buffer = filter.get();

        wcsncpy_s(
            buffer,
            filterStringLength,
            filterName,
            filterNameLength);

        wcsncpy_s(
            buffer + filterNameLength + 1,
            filterStringLength,
            fileExtensionFilter,
            fileExtensionFilterLength);

        buffer[filterStringLength - 1] = L'\0';

        return filter;
    }

    OSErr GetSaveFileNameClassic(HWND owner, boost::filesystem::path& outputFilePath)
    {
        OSErr err = noErr;

        try
        {
            wchar_t titleBuffer[256] = {};
            wchar_t filterNameBuffer[256] = {};

            const int titleLength = LoadStringW(wil::GetModuleInstanceHandle(),
                                                IMAGE_SAVE_DIALOG_TITLE,
                                                titleBuffer,
                                                _countof(titleBuffer));
            const int filterNameLength = LoadStringW(wil::GetModuleInstanceHandle(),
                                                     IMAGE_SAVE_DIALOG_FILTER_NAME,
                                                     filterNameBuffer,
                                                     _countof(filterNameBuffer));

            if (titleLength > 0 && filterNameLength > 0)
            {
                auto comCleanup = wil::CoInitializeEx(COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

                auto filterStr = BulidClassicSaveDialogFilterString(filterNameBuffer, filterNameLength);

                constexpr int fileNameBufferLength = 8192;

                auto fileNameBuffer = wil::make_unique_cotaskmem<wchar_t[]>(fileNameBufferLength);

                memset(fileNameBuffer.get(), 0, fileNameBufferLength * sizeof(wchar_t));


                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = owner;
                ofn.lpstrDefExt = L"png";
                ofn.lpstrTitle = titleBuffer;
                ofn.lpstrFilter = filterStr.get();
                ofn.nFilterIndex = 1;
                ofn.lpstrFile = fileNameBuffer.get();
                ofn.nMaxFile = fileNameBufferLength;
                ofn.Flags = OFN_DONTADDTORECENT | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

                if (GetSaveFileNameW(&ofn))
                {
                    outputFilePath = fileNameBuffer.get();
                }
                else
                {
                    err = CommDlgExtendedError() == 0 ? userCanceledErr : ioErr;
                }
            }
            else
            {
                err = ioErr;
            }
        }
        catch (const std::bad_alloc&)
        {
            err = memFullErr;
        }
        catch (const wil::ResultException& e)
        {
            err = e.GetErrorCode() == E_OUTOFMEMORY ? memFullErr : ioErr;
        }
        catch (...)
        {
            err = ioErr;
        }

        return err;
    }
}

OSErr GetNewImageFileNameNative(const FilterRecordPtr filterRecord, boost::filesystem::path& outputFileName)
{
    PlatformData* platformData = static_cast<PlatformData*>(filterRecord->platformData);

    HWND owner = platformData != nullptr ? reinterpret_cast<HWND>(platformData->hwnd) : nullptr;

    if (UseVistaStyleDialogs())
    {
        return GetSaveFileNameVista(owner, outputFileName);
    }
    else
    {
        return GetSaveFileNameClassic(owner, outputFileName);
    }
}
