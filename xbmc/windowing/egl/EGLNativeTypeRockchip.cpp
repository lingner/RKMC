/*
 *      Copyright (C) 2011-2014 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <stdlib.h>

#include "system.h"
#include <EGL/egl.h>
#include "EGLNativeTypeRockchip.h"
#include "utils/log.h"
#include "guilib/gui3d.h"
#include "android/activity/XBMCApp.h"
#include "android/jni/Build.h"
#include "utils/StringUtils.h"
#include "utils/SysfsUtils.h"
#include "utils/RegExp.h"

bool CEGLNativeTypeRockchip::CheckCompatibility()
{
  if (StringUtils::StartsWithNoCase(CJNIBuild::HARDWARE, "rk3") && SysfsUtils::Has("/system/lib/librkffplayer.so"))  // Rockchip
  {
    if (SysfsUtils::HasRW("/sys/class/display/display0.HDMI/mode") || SysfsUtils::HasRW("/sys/class/display/HDMI/mode"))
    {
      CLog::Log(LOGDEBUG, "RKEGL: Detected");
      return true;
    }
    else
      CLog::Log(LOGERROR, "RKEGL: no rw on /sys/class/display/display0.HDMI/mode");
  }
  return false;
}

bool CEGLNativeTypeRockchip::SysModeToResolution(std::string mode, RESOLUTION_INFO *res) const
{
  if (!res)
    return false;

  res->iWidth = 0;
  res->iHeight= 0;

  if(mode.empty())
    return false;

  std::string fromMode = mode;
  if (!isdigit(mode[0]))
    fromMode = StringUtils::Mid(mode, 2);
  StringUtils::Trim(fromMode);

  CRegExp split(true);
  split.RegComp("([0-9]+)x([0-9]+)([pi])-([0-9]+)");
  if (split.RegFind(fromMode) < 0)
    return false;

  int w = atoi(split.GetMatch(1).c_str());
  int h = atoi(split.GetMatch(2).c_str());
  std::string p = split.GetMatch(3);
  int r = atoi(split.GetMatch(4).c_str());
  CLog::Log(LOGDEBUG,"SysModeToResolution w:%d h:%d r:%d",w,h,r);
  res->iWidth = w;
  res->iHeight= h;
  res->iScreenWidth = w;
  res->iScreenHeight= h;
  res->fRefreshRate = r;
  res->dwFlags = p[0] == 'p' ? D3DPRESENTFLAG_PROGRESSIVE : D3DPRESENTFLAG_INTERLACED;

  res->iScreen       = 0;
  res->bFullScreen   = true;
  res->iSubtitles    = (int)(0.965 * res->iHeight);
  res->fPixelRatio   = 1.0f;
  res->strMode       = StringUtils::Format("%dx%d @ %.2f%s - Full Screen", res->iScreenWidth, res->iScreenHeight, res->fRefreshRate,
                                           res->dwFlags & D3DPRESENTFLAG_INTERLACED ? "i" : "");
  res->strId         = mode;

  return res->iWidth > 0 && res->iHeight> 0;
}

bool CEGLNativeTypeRockchip::GetNativeResolution(RESOLUTION_INFO *res) const
{
  CEGLNativeTypeAndroid::GetNativeResolution(&m_fb_res);

  std::string mode;
  RESOLUTION_INFO hdmi_res;
  if ((SysfsUtils::GetString("/sys/class/display/display0.HDMI/mode", mode) == 0 
    || SysfsUtils::GetString("/sys/class/display/HDMI/mode", mode) == 0) && SysModeToResolution(mode, &hdmi_res))
  {
    m_curHdmiResolution = mode;
    *res = hdmi_res;
    res->iWidth = m_fb_res.iWidth;
    res->iHeight = m_fb_res.iHeight;
    res->iSubtitles = (int)(0.965 * res->iHeight);
  }
  else
    *res = m_fb_res;

  return true;
}

bool CEGLNativeTypeRockchip::SetNativeResolution(const RESOLUTION_INFO &res)
{
  switch((int)(res.fRefreshRate*10))
  {
    default:
    case 600:
      switch(res.iScreenWidth)
      {
        case 1280:
          return SetDisplayResolution("1280x720p-60");
          break;
        case 1920:
          if (res.dwFlags & D3DPRESENTFLAG_INTERLACED)
            return SetDisplayResolution("1920x1080i-60");
          else
            return SetDisplayResolution("1920x1080p-60");
          break;
        case 3840:
          if (CJNIBase::GetSDKVersion() >= 21)
            return SetDisplayResolution("3840x2160p-60(YCbCr420)");
          else
            return SetDisplayResolution("3840x2160p-60");
          break;
        case 4096:
          if (CJNIBase::GetSDKVersion() >= 21)
            return SetDisplayResolution("4096x2160p-60(YCbCr420)");
          else
            return SetDisplayResolution("4096x2160p-60");
          break;
        default:
          return SetDisplayResolution("1920x1080p-60");
          break;
      }
      break;
    case 500:
      switch(res.iScreenWidth)
      {
        case 1280:
          return SetDisplayResolution("1280x720p-50");
          break;
        case 1920:
          if (res.dwFlags & D3DPRESENTFLAG_INTERLACED)
            return SetDisplayResolution("1920x1080i-50");
          else
            return SetDisplayResolution("1920x1080p-50");
          break;
        case 3840:
          if (CJNIBase::GetSDKVersion() >= 21)
            return SetDisplayResolution("3840x2160p-50(YCbCr420)");
          else
            return SetDisplayResolution("3840x2160p-50");
          break;
        case 4096:
          if (CJNIBase::GetSDKVersion() >= 21)
            return SetDisplayResolution("4096x2160p-50(YCbCr420)");
          else
            return SetDisplayResolution("4096x2160p-50");
          break;
        default:
          return SetDisplayResolution("1920x1080p-60");
          break;
      }
      break;
    case 300:
      switch(res.iScreenWidth)
      {
        case 3840:
          return SetDisplayResolution("3840x2160p-30");
          break;
        default:
          return SetDisplayResolution("1920x1080p-30");
          break;
      }
      break;
    case 250:
      switch(res.iScreenWidth)
      {
        case 3840:
          return SetDisplayResolution("3840x2160p-25");
          break;
        default:
          return SetDisplayResolution("1920x1080p-25");
          break;
      }
      break;
    case 240:
      switch(res.iScreenWidth)
      {
        case 3840:
          return SetDisplayResolution("3840x2160p-24");
          break;
        case 4096:
          return SetDisplayResolution("4096x2160p-24");
          break;
        default:
          return SetDisplayResolution("1920x1080p-24");
          break;
      }
      break;
  }

  return false;
}

bool CEGLNativeTypeRockchip::ProbeResolutions(std::vector<RESOLUTION_INFO> &resolutions)
{
  CEGLNativeTypeAndroid::GetNativeResolution(&m_fb_res);

  std::string valstr;
  if (SysfsUtils::GetString("/sys/class/display/display0.HDMI/modes", valstr) < 0 
    && SysfsUtils::GetString("/sys/class/display/HDMI/modes", valstr) < 0)
    return false;
  std::vector<std::string> probe_str = StringUtils::Split(valstr, "\n");

  resolutions.clear();
  RESOLUTION_INFO res;
  
  for (size_t i = 0; i < probe_str.size(); i++)
  {
    if(SysModeToResolution(probe_str[i].c_str(), &res))
    {
      res.iWidth = m_fb_res.iWidth;
      res.iHeight = m_fb_res.iHeight;
      res.iSubtitles    = (int)(0.965 * res.iHeight);
      CLog::Log(LOGDEBUG,"ProbeResolutions width:%d height:%d fps:%f",m_fb_res.iWidth,m_fb_res.iHeight,res.fRefreshRate);
      resolutions.push_back(res);
    }
  }
  return resolutions.size() > 0;

}

bool CEGLNativeTypeRockchip::GetPreferredResolution(RESOLUTION_INFO *res) const
{
  return GetNativeResolution(res);
}

bool CEGLNativeTypeRockchip::SetDisplayResolution(const char *resolution)
{
  return false;
  CLog::Log(LOGDEBUG,"EGL SetDisplayResolution %s",resolution);
  // current 3d mode 
  if (Get3DMode() >= 0)
    return false;
  
  if (m_curHdmiResolution == resolution)
    return true;

  // switch display resolution
  std::string out = resolution;
  out += '\n';
  if (SysfsUtils::HasRW("/sys/class/display/display0.HDMI/mode"))
  {
    if (SysfsUtils::SetString("/sys/class/display/display0.HDMI/mode", out.c_str()) < 0)
      return false;
  }
  else if (SysfsUtils::HasRW("/sys/class/display/HDMI/mode"))
  {
    if (SysfsUtils::SetString("/sys/class/display/HDMI/mode", out.c_str()) < 0)
      return false;
  }
  else
    return false;

  m_curHdmiResolution = resolution;

  return true;
}

int CEGLNativeTypeRockchip::Get3DMode()
{
  std::string valstr;
  if (SysfsUtils::GetString("/sys/class/display/display0.HDMI/3dmode", valstr) < 0 
    && SysfsUtils::GetString("/sys/class/display/HDMI/3dmode", valstr) < 0)
    return -1;
  
  std::vector<std::string> probe_str = StringUtils::Split(valstr, "\n");
  for (size_t i = 0; i < probe_str.size(); i++)
  {
    if (StringUtils::StartsWith(probe_str[i], "cur3dmode="))
    {
      int mode;
      sscanf(probe_str[i].c_str(),"cur3dmode=%d",&mode);
      return mode;
    }
  }
  return -1;
}


