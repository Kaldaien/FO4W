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

#ifndef __BMF__UTILITY_H__
#define __BMF__UTILITY_H__

#include <string>

std::wstring  BMF_GetDocumentsDir      (void);
bool          BMF_GetUserProfileDir    (wchar_t* buf, uint32_t* pdwLen);
bool          BMF_IsTrue               (const wchar_t* string);
int           BMF_MessageBox           (std::wstring caption,
                                        std::wstring title,
                                        uint32_t     flags);

void          BMF_SetNormalFileAttribs (std::wstring file);

#endif /* __BMF__UTILITY_H__ */