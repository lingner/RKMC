/*
 *      Copyright (C) 2005-2013 Team XBMC
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

#include <math.h>

#include "DVDVideoCodecRK.h"
#include "cores/dvdplayer/DVDClock.h"
#include "cores/dvdplayer/DVDStreamInfo.h"
#include "utils/BitstreamConverter.h"
#include "utils/log.h"
#include "utils/StringUtils.h"

#define __MODULE_NAME__ "DVDVideoCodecRK"

CDVDVideoCodecRK::CDVDVideoCodecRK() :
  m_pFormatName("RKCodec"),
  m_bOpened(false),
  m_lfDecodePts(0.0),
  m_lfDisplayPts(0.0)
{
}

CDVDVideoCodecRK::~CDVDVideoCodecRK()
{
  Dispose();
}

bool CDVDVideoCodecRK::Open(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  /* check rkhw support */
  if (!IsRkHwSupport(hints))
    return false;
  
  m_hints = hints;

  // allocate a dummy DVDVideoPicture buffer.
  // first make sure all properties are reset.
  memset(&m_videobuffer, 0, sizeof(DVDVideoPicture));

  /* config xbmc getpicture buffer use bypass */
  m_videobuffer.dts = DVD_NOPTS_VALUE;
  m_videobuffer.pts = DVD_NOPTS_VALUE;
  m_videobuffer.format = RENDER_FMT_BYPASS;
  m_videobuffer.color_range  = 0;
  m_videobuffer.color_matrix = 4;
  m_videobuffer.iFlags  = DVP_FLAG_ALLOCATED;
  m_videobuffer.iWidth  = m_hints.width;
  m_videobuffer.iHeight = m_hints.height;
  m_videobuffer.iDisplayWidth  = m_videobuffer.iWidth;
  m_videobuffer.iDisplayHeight = m_videobuffer.iHeight;

  /* init rkcodec */
  m_pCodec = new CRKCodec();
  return true;
  
FAIL:
  if (m_pCodec)
  {
    delete m_pCodec;
    m_pCodec = NULL;
  }
  CLog::Log(LOGINFO, "%s: Opened Fail!", __MODULE_NAME__);  
  return false;
}


void CDVDVideoCodecRK::Dispose(void)
{
  CLog::Log(LOGINFO, "%s: Dispose!", __MODULE_NAME__);
  if (m_pCodec)
  {
    m_pCodec->CloseDecoder();
    delete m_pCodec;
    m_pCodec = NULL;
  }
}

int CDVDVideoCodecRK::Decode(uint8_t *pData, int iSize, double dts, double pts)
{
  int ret = VC_ERROR;
  
  if (!m_bOpened && m_pCodec)
  {
    CLog::Log(LOGINFO, "%s: Opened Success!", __MODULE_NAME__);
    m_pCodec->OpenDecoder(m_hints);
    m_bOpened = true;
  }

  if (m_bOpened)
  {
    if (m_hints.ptsinvalid)
      pts = DVD_NOPTS_VALUE;
    ret = m_pCodec->DecodeVideo(pData, iSize, dts, pts);
    m_lfDecodePts = pts;
  }
  return ret;
}

void CDVDVideoCodecRK::Reset(void)
{
  if (m_bOpened)
    m_pCodec->Reset();
}

bool CDVDVideoCodecRK::GetPicture(DVDVideoPicture* pDvdVideoPicture)
{
  *pDvdVideoPicture = m_videobuffer;
  if (m_bOpened)
  {
    /* when achieve the first picture, close ui */
    if (m_pCodec->GetDisplayInfo()->raw > 0)
    {
      CDVDClock *playerclock = CDVDClock::GetMasterClock();
      if (playerclock)
        pDvdVideoPicture->pts = playerclock->GetClock();
    }
    else
        pDvdVideoPicture->pts = 0;
  }
  return true;
}

void CDVDVideoCodecRK::SetDropState(bool bDrop)
{
}

void CDVDVideoCodecRK::SetSpeed(int speed)
{
  if (m_bOpened)
    m_pCodec->SetSpeed(speed);
}

int CDVDVideoCodecRK::GetDataSize(void)
{
  return 0;
}

double CDVDVideoCodecRK::GetTimeSize(void)
{
  if (m_bOpened)
  {
    m_lfDisplayPts = m_pCodec->GetDisplayInfo()->raw;
    double time_size = (m_lfDecodePts - m_lfDisplayPts) / DVD_TIME_BASE;
    time_size = 0;
    if (time_size < 0.0)
      time_size = 0.0;
    else if (time_size > 7.0)
      time_size = 7.0;
    return time_size;
  }
  return 0.0;
}

void CDVDVideoCodecRK::SetCodecControl(int flags)
{
    
}

void CDVDVideoCodecRK::SubmitEOS() 
{
  if (m_bOpened) 
    m_pCodec->SubmitEOS();
}

bool CDVDVideoCodecRK::SubmittedEOS()
{
  if (m_bOpened) 
    return m_pCodec->SubmittedEOS();
  return false;
}

bool CDVDVideoCodecRK::IsEOS()
{
  if (m_bOpened) 
    return m_pCodec->IsEOS();
  return false;
}

bool CDVDVideoCodecRK::IsRkHwSupport(CDVDStreamInfo & hints)
{
  AVCodecID codec_id = hints.codec;
  unsigned int codec_tag = hints.codec_tag;

  /*  here rk is not support divx */
  if ((codec_tag == MKTAG('3', 'I', 'V', 'D')) ||
      (codec_tag == MKTAG('D', 'I', 'V', 'X')) ||
      (codec_tag == MKTAG('X', 'V', 'I', 'D')) ||
      (codec_tag == MKTAG('3', 'I', 'V', '2')) )
  {
    CLog::Log(LOGDEBUG, "CDVDVideoCodecRK not support DIVX!");
    return false;
  }
  
  char name[5] = {0};
  if (name != NULL) 
  {
    name[0] = (codec_tag&0xff);
    name[1] = (codec_tag&0xff00)>>8;
    name[2] = (codec_tag&0x00ff0000)>>16;
    name[3] = (codec_tag&0xff000000)>>24;
  }
  
  std::string codecName(name);
  StringUtils::ToLower(codecName);
  if (strstr(codecName.c_str(),"div") != 0 || strstr(codecName.c_str(),"vid") != 0 ||
            strstr(codecName.c_str(),"dx50") != 0) 
  {
    CLog::Log(LOGDEBUG, "CDVDVideoCodecRK not support DIVX!");
    return false;
  }

  /* check video codec */
  switch (codec_id) 
  {
    case AV_CODEC_ID_RV10:
    case AV_CODEC_ID_RV20:
    case AV_CODEC_ID_RV30:
    case AV_CODEC_ID_RV40:
      return false;
    case AV_CODEC_ID_MPEG1VIDEO:
    case AV_CODEC_ID_MPEG2VIDEO:
      return true;
    case AV_CODEC_ID_MPEG4:
      if (hints.width >= 3840 || hints.height >= 2160)
        return false;
    case AV_CODEC_ID_FLV1:
    case AV_CODEC_ID_H264:
    case AV_CODEC_ID_VC1:
    case AV_CODEC_ID_WMV3:
    case AV_CODEC_ID_VP8:
    case AV_CODEC_ID_HEVC:
    case AV_CODEC_ID_VP6:
    case AV_CODEC_ID_VP6F:
    case AV_CODEC_ID_VP6A:
      CLog::Log(LOGDEBUG,"CDVDVideoCodecRK support codec %d", codec_id);
      return true;
    default:
      CLog::Log(LOGDEBUG,"CDVDVideoCodecRK not support codec %d", codec_id);
      return false;
  }
  
  return false;
}