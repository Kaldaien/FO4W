/**
* This file is part of Batman "Fix".
*
* Batman "Fix" is free software : you can redistribute it and / or modify
* it under the terms of the GNU General Public License as published by
* The Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Batman "Fix" is distributed in the hope that it will be useful,
* But WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Batman "Fix". If not, see <http://www.gnu.org/licenses/>.
**/

#include "utility.h"

#include <UserEnv.h>
#pragma comment (lib, "userenv.lib")

#include <Shlobj.h>
#pragma comment (lib, "shell32.lib")

int
BMF_MessageBox (std::wstring caption, std::wstring title, uint32_t flags)
{
  return
    MessageBox (NULL, caption.c_str (), title.c_str (), 
                flags | MB_SYSTEMMODAL | MB_TOPMOST | MB_SETFOREGROUND);
}

std::wstring
BMF_GetDocumentsDir (void)
{
  HANDLE hToken;

  if (! OpenProcessToken (GetCurrentProcess (), TOKEN_READ, &hToken))
    return NULL;

  wchar_t* str;
  SHGetKnownFolderPath (FOLDERID_Documents, 0, hToken, &str);
  std::wstring ret = str;
  CoTaskMemFree (str);
  return ret;
}

BOOL
BMF_GetUserProfileDir (wchar_t* buf, DWORD* pdwLen)
{
  HANDLE hToken;

  if (!OpenProcessToken (GetCurrentProcess (), TOKEN_READ, &hToken))
    return FALSE;

  if (!GetUserProfileDirectory (hToken, buf, pdwLen))
    return FALSE;

  CloseHandle (hToken);
  return TRUE;
}

#include <string>

bool
BMF_IsTrue (const wchar_t* string)
{
  if (std::wstring (string).length () == 1 &&
    string [0] == L'1')
    return true;

  if (std::wstring (string).length () != 4)
    return false;

  if (towlower (string [0]) != L't')
    return false;
  if (towlower (string [1]) != L'r')
    return false;
  if (towlower (string [2]) != L'u')
    return false;
  if (towlower (string [3]) != L'e')
    return false;

  return true;
}

// Copies a file preserving file times
void
BMF_FullCopy (std::wstring from, std::wstring to)
{
  // Strip Read-Only
  BMF_SetNormalFileAttribs (to);
  DeleteFile (to.c_str ());
  CopyFile   (from.c_str (), to.c_str (), FALSE);

  WIN32_FIND_DATA FromFileData;
  HANDLE hFrom = FindFirstFile (from.c_str (), &FromFileData);

  OFSTRUCT ofTo;
  ofTo.cBytes = sizeof (OFSTRUCT);

  char     szFileTo [MAX_PATH];

  WideCharToMultiByte (CP_OEMCP, 0, to.c_str (), -1, szFileTo, MAX_PATH, NULL, NULL);
  HFILE hfTo = OpenFile (szFileTo, &ofTo, NULL);

  
  HANDLE hTo = HandleToHandle64 (&hfTo);
  CloseHandle (hTo);

  // Here's where the magic happens, apply the attributes from the original file to the new one!
  SetFileTime (hTo, &FromFileData.ftCreationTime, &FromFileData.ftLastAccessTime, &FromFileData.ftLastWriteTime);

  FindClose   (hFrom);
}

//BOOL TakeOwnership (LPTSTR lpszOwnFile);

void
BMF_SetNormalFileAttribs (std::wstring file)
{
  SetFileAttributes (file.c_str (), FILE_ATTRIBUTE_NORMAL);
}