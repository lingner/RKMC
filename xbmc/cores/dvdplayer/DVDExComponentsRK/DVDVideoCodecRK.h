#pragma once
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

#include "cores/dvdplayer/DVDCodecs/Video/DVDVideoCodec.h"
#include "cores/dvdplayer/DVDStreamInfo.h"
#include "RKCoreComponent.h"

class CDVDVideoCodecRK : public CDVDVideoCodec
{
public:
  CDVDVideoCodecRK();
  virtual ~CDVDVideoCodecRK();

  // Required rockchip to overrides
  virtual bool Open(CDVDStreamInfo &hints, CDVDCodecOptions &options);
  virtual void Dispose(void);
  virtual int  Decode(uint8_t *pData, int iSize, double dts, double pts);
  virtual void Reset(void);
  virtual bool GetPicture(DVDVideoPicture *pDvdVideoPicture);
  virtual void SetSpeed(int iSpeed);
  virtual void SetDropState(bool bDrop);
  virtual int  GetDataSize(void);
  virtual double GetTimeSize(void);
  virtual const char* GetName(void) { return (const char*)m_pFormatName; }
  virtual void SetCodecControl(int flags);

  void SubmitEOS();
  bool SubmittedEOS();
  bool IsEOS();

protected:
  bool IsRkHwSupport(CDVDStreamInfo &hints);


protected:
  CRKCodec       *m_pCodec;
  const char     *m_pFormatName;
  DVDVideoPicture m_videobuffer;
  bool            m_bOpened;
  double          m_lfDecodePts;
  double          m_lfDisplayPts;
  CDVDStreamInfo  m_hints;

};

